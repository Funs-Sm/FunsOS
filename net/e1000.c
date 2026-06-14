#include "e1000.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "kheap.h"
#include "string.h"
#include "irq.h"

static uint32_t *mmio_base;

static e1000_rx_desc_t *rx_descs;
static e1000_tx_desc_t *tx_descs;

static void *rx_buffers[256];
static void *tx_buffers[256];

static uint32_t rx_tail;
static uint32_t tx_tail;

static net_interface_t e1000_iface;

static uint32_t e1000_read_reg(uint32_t offset) {
    return mmio_base[offset / 4];
}

static void e1000_write_reg(uint32_t offset, uint32_t val) {
    mmio_base[offset / 4] = val;
}

static void e1000_read_mac(void) {
    uint32_t low = e1000_read_reg(0x5400);
    uint32_t high = e1000_read_reg(0x5404);
    e1000_iface.mac.bytes[0] = low & 0xFF;
    e1000_iface.mac.bytes[1] = (low >> 8) & 0xFF;
    e1000_iface.mac.bytes[2] = (low >> 16) & 0xFF;
    e1000_iface.mac.bytes[3] = (low >> 24) & 0xFF;
    e1000_iface.mac.bytes[4] = high & 0xFF;
    e1000_iface.mac.bytes[5] = (high >> 8) & 0xFF;
}

void e1000_init(uint8_t bus, uint8_t dev, uint8_t func) {
    uint32_t bar0 = pci_read_config(bus, dev, func, 0x10);
    uint32_t phys_addr = bar0 & 0xFFFFFFF0;
    mmio_base = (uint32_t *)vmm_map_physical(phys_addr, 0x20000);

    e1000_write_reg(E1000_REG_CTRL, e1000_read_reg(E1000_REG_CTRL) | 0x04000000);

    e1000_read_mac();

    rx_descs = (e1000_rx_desc_t *)kmalloc(256 * sizeof(e1000_rx_desc_t));
    memset(rx_descs, 0, 256 * sizeof(e1000_rx_desc_t));
    tx_descs = (e1000_tx_desc_t *)kmalloc(256 * sizeof(e1000_tx_desc_t));
    memset(tx_descs, 0, 256 * sizeof(e1000_tx_desc_t));

    uint32_t i;
    for (i = 0; i < 256; i++) {
        rx_buffers[i] = kmalloc(2048);
        rx_descs[i].addr = (uint64_t)(uint32_t)rx_buffers[i];
        rx_descs[i].status = 0;
        tx_buffers[i] = kmalloc(2048);
        tx_descs[i].addr = (uint64_t)(uint32_t)tx_buffers[i];
        tx_descs[i].status = 0;
    }

    e1000_write_reg(0x2800, (uint32_t)rx_descs);
    e1000_write_reg(0x2808, 0);
    rx_tail = 255;
    e1000_write_reg(0x2810, 0);
    e1000_write_reg(0x2818, 256 - 1);

    e1000_write_reg(E1000_REG_RCTL,
        (1 << 1) |
        (1 << 2) |
        (0 << 4) |
        (1 << 15) |
        (0 << 16) |
        (1 << 26));

    e1000_write_reg(0x3800, (uint32_t)tx_descs);
    e1000_write_reg(0x3808, 0);
    tx_tail = 0;
    e1000_write_reg(0x3810, 0);
    e1000_write_reg(0x3818, 256 - 1);

    e1000_write_reg(E1000_REG_TCTL,
        (1 << 1) |
        (1 << 3) |
        (0x0F << 4) |
        (0x40 << 12));

    e1000_write_reg(0x00D0, 0x1F6DC);
    e1000_write_reg(E1000_REG_IMC, 0xFFFFFFFF);

    irq_register_handler(0x20 + (pci_read_config(bus, dev, func, 0x3C) & 0xFF), e1000_irq_handler);

    e1000_write_reg(0x00D0, 0x1F6DC);

    strcpy(e1000_iface.name, "eth0");
    e1000_iface.up = 1;
    e1000_iface.mtu = 1500;
    e1000_iface.send = e1000_send;
    e1000_iface.driver_data = (void *)0;
    net_register_interface(&e1000_iface);
}

int e1000_send(net_interface_t *iface, const void *data, uint32_t len) {
    if (len > 2048) return -1;
    memcpy(tx_buffers[tx_tail], data, len);
    tx_descs[tx_tail].length = len;
    tx_descs[tx_tail].cmd = (1 << 0) | (1 << 1) | (1 << 3);
    tx_descs[tx_tail].status = 0;
    uint32_t old_tail = tx_tail;
    tx_tail = (tx_tail + 1) % 256;
    e1000_write_reg(0x3818, tx_tail);
    (void)iface;
    return old_tail;
}

void e1000_poll(void) {
    uint32_t i;
    for (i = 0; i < 256; i++) {
        if (rx_descs[i].status & E1000_RXDESC_STATUS_DD) {
            net_buffer_t *buf = net_alloc_buffer();
            if (buf) {
                uint16_t pkt_len = rx_descs[i].length;
                memcpy(buf->data, rx_buffers[i], pkt_len);
                buf->len = pkt_len;
                buf->offset = 0;
                buf->iface = &e1000_iface;
                net_receive(buf);
            }
            rx_descs[i].status = 0;
            rx_tail = (rx_tail + 1) % 256;
            e1000_write_reg(0x2818, rx_tail);
        }
    }
}

void e1000_irq_handler(regs_t *regs) {
    uint32_t icr = e1000_read_reg(0x00C0);
    if (icr & 0x04) {
        e1000_poll();
    }
    if (icr & 0x01) {
    }
    e1000_write_reg(0x00C0, icr);
    (void)regs;
}
