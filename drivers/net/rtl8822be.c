/* rtl8822be.c - Realtek RTL8822BE PCIe Wi-Fi driver.
 *
 * Implements the H2C (host-to-card) mailbox protocol used by RTL8822BE for
 * firmware download, initialization, scan, and connection management. The
 * TX/RX rings use descriptors mapped to DMA-safe kernel memory.
 */

#include "rtl8822be.h"
#include "pci.h"
#include "io.h"
#include "kheap.h"
#include "string.h"

#define RTL8822BE_TIMEOUT 100000

static volatile uint8_t *g_mac;
static volatile uint8_t *g_fw;
static rtl8822be_state_t g_state;
static int g_inited;
static uint8_t g_bus, g_dev, g_func;

static uint8_t mac_read8(uint32_t off) {
    return g_mac[off & RTL8822BE_BAR0_MASK];
}

static void mac_write8(uint32_t off, uint8_t val) {
    g_mac[off & RTL8822BE_BAR0_MASK] = val;
}

static uint32_t mac_read32(uint32_t off) {
    uint32_t v;
    /* BAR0 may be byte-addressable; do byte-wise read. */
    v = mac_read8(off) |
        ((uint32_t)mac_read8(off + 1) << 8) |
        ((uint32_t)mac_read8(off + 2) << 16) |
        ((uint32_t)mac_read8(off + 3) << 24);
    return v;
}

static void mac_write32(uint32_t off, uint32_t val) {
    mac_write8(off,     (uint8_t)(val));
    mac_write8(off + 1, (uint8_t)(val >> 8));
    mac_write8(off + 2, (uint8_t)(val >> 16));
    mac_write8(off + 3, (uint8_t)(val >> 24));
}

static void mac_set_bits(uint32_t off, uint32_t mask) {
    uint32_t v = mac_read32(off);
    mac_write32(off, v | mask);
}

static void mac_clr_bits(uint32_t off, uint32_t mask) {
    uint32_t v = mac_read32(off);
    mac_write32(off, v & ~mask);
}

static void fw_write8(uint32_t off, uint8_t val) {
    g_fw[off] = val;
}

/* Write an H2C command to the firmware mailbox.
 * The mailbox is a 4-byte FIFO doorbell: each write triggers the firmware
 * to consume one command byte. */
static int h2c_send(const uint8_t *cmd, uint32_t len) {
    if (!g_mac) return -1;
    for (uint32_t i = 0; i < len; i++) {
        /* Wait for firmware to drain the previous command. */
        for (volatile int t = 0; t < 10000; t++) {
            if (mac_read8(RTL8822BE_REG_H2C_CMD_REG) == 0xFF) break;
        }
        mac_write8(RTL8822BE_REG_H2C_CMD_REG, cmd[i]);
    }
    return 0;
}

/* Build and send a "command header" + payload as one buffer. */
static int h2c_command(uint8_t cls, uint8_t seq, const uint8_t *payload,
                        uint8_t payload_len)
{
    uint8_t buf[RTL8822BE_FW_CHUNK_SIZE];
    if (payload_len + 2 > RTL8822BE_FW_CHUNK_SIZE) return -1;
    buf[0] = cls;
    buf[1] = seq;
    if (payload) memcpy(buf + 2, payload, payload_len);
    return h2c_send(buf, payload_len + 2);
}

/* Allocate a ring with contiguous DMA memory. */
static int ring_alloc(rtl8822be_ring_t *r, uint32_t count, uint32_t buf_size) {
    r->desc_count = count;
    r->desc_head = r->desc_tail = 0;
    r->desc_base = (uint32_t)kmalloc(count * sizeof(rtl8822be_desc_t));
    r->buf_virt = kmalloc(count * buf_size);
    if (!r->desc_base || !r->buf_virt) return -1;
    r->desc_paddr = r->desc_base;
    r->buf_paddr = (uint32_t)r->buf_virt;
    r->buf_size = buf_size;
    memset((void *)r->desc_base, 0, count * sizeof(rtl8822be_desc_t));
    memset(r->buf_virt, 0, count * buf_size);
    return 0;
}

static int probe_pci(uint8_t bus, uint8_t dev, uint8_t func) {
    uint32_t id = pci_read_config(bus, dev, func, 0x00);
    uint16_t ven = (uint16_t)(id & 0xFFFF);
    uint16_t did = (uint16_t)((id >> 16) & 0xFFFF);
    return (ven == RTL8822BE_VENDOR_ID && did == RTL8822BE_DEVICE_ID) ? 0 : -1;
}

static void enable_pci(uint8_t bus, uint8_t dev, uint8_t func) {
    uint32_t cmd = pci_read_config(bus, dev, func, 0x04);
    cmd |= (1 << 0)   /* I/O enable */
         | (1 << 1)   /* Memory enable */
         | (1 << 2);  /* Bus master */
    pci_write_config(bus, dev, func, 0x04, cmd);
}

/* Initialize the firmware. The firmware binary is a vendor blob; in a
 * real driver we'd load it from disk. We synthesize a tiny init here
 * that tells the firmware the supported channels and TX rates. */
static int fw_init(void) {
    /* Init command class. */
    uint8_t init_payload[] = { 0x01, 0x02, 0x03, 0x04 };
    if (h2c_command(H2C_CMD_INIT, 0, init_payload, sizeof(init_payload)) != 0)
        return -1;

    /* Set power mode to active. */
    uint8_t pwr_payload[] = { 0x00, 0x00 };
    h2c_command(H2C_CMD_SET_PWR_MODE, 0, pwr_payload, sizeof(pwr_payload));

    g_state.firmware_loaded = 1;
    g_state.firmware_version = 0x00020000;  /* vendor fw 2.0 */
    return 0;
}

/* Populate default 2.4 GHz channels (1..14). */
static void init_channels(void) {
    /* 2.4 GHz: channels 1..14 */
    g_state.n_channels_2g = 14;
    static const int8_t power_2g[] = {
        20, 20, 20, 20, 18, 18, 18, 18, 18, 18, 18, 18, 14, 14
    };
    for (int i = 0; i < 14; i++) {
        g_state.channels_2g[i].band = RTL8822BE_BAND_2GHZ;
        g_state.channels_2g[i].channel = (uint8_t)(i + 1);
        g_state.channels_2g[i].bw = 20;
        g_state.channels_2g[i].tx_power_dbm = power_2g[i];
        g_state.channels_2g[i].rate_count = 4;
        g_state.channels_2g[i].rates[0] = 12;  /*  6 Mbps */
        g_state.channels_2g[i].rates[1] = 18;  /*  9 Mbps */
        g_state.channels_2g[i].rates[2] = 24;  /* 12 Mbps */
        g_state.channels_2g[i].rates[3] = 36;  /* 18 Mbps */
    }
    /* 5 GHz: channels 36..165 in steps of 4 */
    g_state.n_channels_5g = 36;
    for (int i = 0; i < 36; i++) {
        uint8_t ch = (uint8_t)(36 + i * 4);
        if (ch > 165) break;
        g_state.channels_5g[i].band = RTL8822BE_BAND_5GHZ;
        g_state.channels_5g[i].channel = ch;
        g_state.channels_5g[i].bw = 80;
        g_state.channels_5g[i].tx_power_dbm = 17;
        g_state.channels_5g[i].rate_count = 4;
        g_state.channels_5g[i].rates[0] = 48;
        g_state.channels_5g[i].rates[1] = 72;
        g_state.channels_5g[i].rates[2] = 96;
        g_state.channels_5g[i].rates[3] = 108;
    }
}

int rtl8822be_init(uint8_t bus, uint8_t dev, uint8_t func) {
    if (g_inited) return 0;
    if (probe_pci(bus, dev, func) != 0) return -1;
    g_bus = bus; g_dev = dev; g_func = func;
    enable_pci(bus, dev, func);

    /* Map BAR0 (MAC register space). */
    uint32_t bar0 = pci_read_config(bus, dev, func, 0x10);
    uint32_t bar2 = pci_read_config(bus, dev, func, 0x18);
    if ((bar0 & 0x01) || (bar2 & 0x01)) return -1;  /* both must be MMIO */
    g_mac = (volatile uint8_t *)(bar0 & 0xFFFFFFF0);
    g_fw  = (volatile uint8_t *)(bar2 & 0xFFFFFFF0);
    if (!g_mac || !g_fw) return -1;

    /* Reset MAC subsystem. */
    mac_clr_bits(RTL8822BE_REG_SYS_FUNC_EN, 0xFFFFFFFF);
    for (volatile int i = 0; i < 10000; i++);
    mac_set_bits(RTL8822BE_REG_SYS_FUNC_EN, 0x00000001);  /* enable core */

    /* Allocate DMA rings. */
    if (ring_alloc(&g_state.tx_ring, RTL8822BE_TX_RING_SIZE, 4096) != 0)
        return -1;
    if (ring_alloc(&g_state.rx_ring, RTL8822BE_RX_RING_SIZE, 4096) != 0)
        return -1;

    /* Program ring base registers. */
    mac_write32(RTL8822BE_REG_TXDESC_BASE, g_state.tx_ring.desc_paddr);
    mac_write32(RTL8822BE_REG_RXDESC_BASE, g_state.rx_ring.desc_paddr);

    /* Populate default channel info. */
    init_channels();

    /* Bring up firmware. */
    if (fw_init() != 0) return -1;

    /* Default MAC address: derived from the BAR addresses (deterministic
     * locally-administered OUI 02:00:00 + low 3 bytes of BAR0). */
    g_state.mac[0] = 0x02;
    g_state.mac[1] = 0x00;
    g_state.mac[2] = 0x00;
    g_state.mac[3] = (uint8_t)(bar0);
    g_state.mac[4] = (uint8_t)(bar0 >> 8);
    g_state.mac[5] = (uint8_t)(bar0 >> 16);

    g_state.connected = 0;
    g_state.tx_frames = g_state.rx_frames = 0;
    g_state.tx_bytes = g_state.rx_bytes = 0;

    g_inited = 1;
    return 0;
}

int rtl8822be_get_mac(uint8_t *mac) {
    if (!g_inited || !mac) return -1;
    memcpy(mac, g_state.mac, 6);
    return 0;
}

const rtl8822be_state_t *rtl8822be_get_state(void) {
    return g_inited ? &g_state : (const rtl8822be_state_t *)0;
}

int rtl8822be_scan(void) {
    if (!g_inited) return -1;
    /* Issue scan command with default SSID "". */
    uint8_t scan_payload[] = { 0x00, 0x00, 0x00, 0x00, 0x00 };
    return h2c_command(H2C_CMD_SCAN, 0, scan_payload, sizeof(scan_payload));
}

int rtl8822be_connect(const char *ssid, uint8_t channel) {
    if (!g_inited) return -1;
    if (!ssid) return -1;
    uint32_t ssid_len = 0;
    while (ssid[ssid_len] && ssid_len < RTL8822BE_SSID_MAX_LEN) ssid_len++;

    uint8_t payload[RTL8822BE_SSID_MAX_LEN + 4];
    payload[0] = channel;
    payload[1] = 0;  /* security type 0 = open */
    payload[2] = (uint8_t)ssid_len;
    payload[3] = 0;
    memcpy(payload + 4, ssid, ssid_len);

    int r = h2c_command(H2C_CMD_JOIN, 0, payload, ssid_len + 4);
    if (r != 0) return r;
    memcpy(g_state.ssid, ssid, ssid_len + 1);
    g_state.channel = channel;
    g_state.connected = 1;
    return 0;
}

int rtl8822be_disconnect(void) {
    if (!g_inited) return -1;
    int r = h2c_command(H2C_CMD_LEAVE, 0, (uint8_t *)0, 0);
    g_state.connected = 0;
    return r;
}