#include "virtio_net.h"
#include "pci.h"
#include "vmm.h"
#include "kheap.h"
#include "string.h"
#include "irq.h"
#include "io.h"

static uint32_t io_base;

static virtq_desc_t *rx_descs;
static virtq_desc_t *tx_descs;

static void *rx_buffers[VIRTIO_NET_RX_RING_SIZE];
static void *tx_buffers[VIRTIO_NET_TX_RING_SIZE];

static uint32_t rx_free_count;
static uint32_t rx_last_used;
static uint32_t tx_free_count;
static uint32_t tx_last_used;
static uint32_t tx_next_avail;

static net_interface_t virtio_iface;

static uint32_t virtio_read32(uint16_t offset) {
    return inl(io_base + offset);
}

static void virtio_write32(uint16_t offset, uint32_t val) {
    outl(io_base + offset, val);
}

static uint16_t virtio_read16(uint16_t offset) {
    return inw(io_base + offset);
}

static void virtio_write16(uint16_t offset, uint16_t val) {
    outw(io_base + offset, val);
}

static uint8_t virtio_read8(uint16_t offset) {
    return inb(io_base + offset);
}

static void virtio_write8(uint16_t offset, uint8_t val) {
    outb(io_base + offset, val);
}

void virtio_net_init(uint8_t bus, uint8_t dev, uint8_t func) {
    /* Get I/O base from BAR0 */
    uint32_t bar0 = pci_read_config(bus, dev, func, 0x10);
    io_base = bar0 & 0xFFFFFFFC;
    if (io_base == 0) {
        bar0 = pci_read_config(bus, dev, func, 0x14);
        io_base = bar0 & 0xFFFFFFFC;
    }
    if (io_base == 0) return;

    /* Enable bus master and I/O access */
    uint32_t cmd = pci_read_config(bus, dev, func, 0x04) & 0xFFFF;
    cmd |= (1 << 2) | (1 << 0);
    pci_write_config(bus, dev, func, 0x04, cmd);

    /* Reset device */
    virtio_write8(VIRTIO_PCI_STATUS, 0);

    /* Acknowledge the device */
    virtio_write8(VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    virtio_write8(VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    /* Negotiate features */
    uint32_t host_features = virtio_read32(VIRTIO_PCI_HOST_FEATURES);
    uint32_t guest_features = 0;
    if (host_features & (1 << VIRTIO_NET_F_MAC)) {
        guest_features |= (1 << VIRTIO_NET_F_MAC);
    }
    virtio_write32(VIRTIO_PCI_GUEST_FEATURES, guest_features);

    /* Read MAC address from config space */
    uint32_t i;
    for (i = 0; i < 6; i++) {
        virtio_iface.mac.bytes[i] = virtio_read8(VIRTIO_PCI_CONFIG + i);
    }

    /* Set up receive queue (queue 0) */
    virtio_write16(VIRTIO_PCI_QUEUE_SEL, 0);
    uint16_t rx_queue_size = virtio_read16(VIRTIO_PCI_QUEUE_NUM);
    if (rx_queue_size == 0) rx_queue_size = VIRTIO_NET_RX_RING_SIZE;

    rx_descs = (virtq_desc_t *)kmalloc(rx_queue_size * sizeof(virtq_desc_t));
    memset(rx_descs, 0, rx_queue_size * sizeof(virtq_desc_t));

    for (i = 0; i < rx_queue_size && i < VIRTIO_NET_RX_RING_SIZE; i++) {
        rx_buffers[i] = kmalloc(2048 + sizeof(virtio_net_hdr_t));
        rx_descs[i].addr = (uint64_t)(uint32_t)rx_buffers[i];
        rx_descs[i].len = 2048 + sizeof(virtio_net_hdr_t);
        rx_descs[i].flags = 0x02; /* write-only */
        rx_descs[i].next = 0;
    }

    virtio_write32(VIRTIO_PCI_QUEUE_PFN, (uint32_t)rx_descs >> 12);
    rx_free_count = rx_queue_size;
    rx_last_used = 0;

    /* Set up transmit queue (queue 1) */
    virtio_write16(VIRTIO_PCI_QUEUE_SEL, 1);
    uint16_t tx_queue_size = virtio_read16(VIRTIO_PCI_QUEUE_NUM);
    if (tx_queue_size == 0) tx_queue_size = VIRTIO_NET_TX_RING_SIZE;

    tx_descs = (virtq_desc_t *)kmalloc(tx_queue_size * sizeof(virtq_desc_t));
    memset(tx_descs, 0, tx_queue_size * sizeof(virtq_desc_t));

    for (i = 0; i < tx_queue_size && i < VIRTIO_NET_TX_RING_SIZE; i++) {
        tx_buffers[i] = kmalloc(2048 + sizeof(virtio_net_hdr_t));
        tx_descs[i].addr = (uint64_t)(uint32_t)tx_buffers[i];
        tx_descs[i].len = 2048 + sizeof(virtio_net_hdr_t);
        tx_descs[i].flags = 0;
        tx_descs[i].next = 0;
    }

    virtio_write32(VIRTIO_PCI_QUEUE_PFN, (uint32_t)tx_descs >> 12);
    tx_free_count = tx_queue_size;
    tx_last_used = 0;
    tx_next_avail = 0;

    /* Driver OK */
    virtio_write8(VIRTIO_PCI_STATUS,
        VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);

    /* Register IRQ handler */
    irq_register_handler(0x20 + (pci_read_config(bus, dev, func, 0x3C) & 0xFF), virtio_net_irq_handler);

    /* Register network interface */
    strcpy(virtio_iface.name, "vio0");
    virtio_iface.up = 1;
    virtio_iface.mtu = 1500;
    virtio_iface.send = virtio_net_send;
    virtio_iface.driver_data = (void *)0;
    net_register_interface(&virtio_iface);
}

int virtio_net_send(net_interface_t *iface, const void *data, uint32_t len) {
    if (len > 2048) return -1;
    if (tx_free_count == 0) return -1;

    /* Prepare virtio net header + data */
    virtio_net_hdr_t *hdr = (virtio_net_hdr_t *)tx_buffers[tx_next_avail];
    memset(hdr, 0, sizeof(virtio_net_hdr_t));
    memcpy((uint8_t *)tx_buffers[tx_next_avail] + sizeof(virtio_net_hdr_t), data, len);

    tx_descs[tx_next_avail].len = sizeof(virtio_net_hdr_t) + len;
    tx_descs[tx_next_avail].flags = 0;

    /* Notify the queue */
    virtio_write16(VIRTIO_PCI_QUEUE_NOTIFY, 1);

    tx_next_avail = (tx_next_avail + 1) % VIRTIO_NET_TX_RING_SIZE;
    tx_free_count--;
    (void)iface;
    return 0;
}

void virtio_net_irq_handler(regs_t *regs) {
    uint8_t isr = virtio_read8(VIRTIO_PCI_ISR);

    if (isr & 0x01) {
        /* Queue interrupt - stub: check receive queue */
        uint32_t i;
        for (i = 0; i < VIRTIO_NET_RX_RING_SIZE; i++) {
            if (rx_descs[i].len > 0 && !(rx_descs[i].flags & 0x02)) {
                uint32_t pkt_len = rx_descs[i].len - sizeof(virtio_net_hdr_t);
                net_buffer_t *buf = net_alloc_buffer();
                if (buf) {
                    memcpy(buf->data,
                        (uint8_t *)rx_buffers[i] + sizeof(virtio_net_hdr_t),
                        pkt_len);
                    buf->len = pkt_len;
                    buf->offset = 0;
                    buf->iface = &virtio_iface;
                    net_receive(buf);
                }
                /* Re-queue the descriptor */
                rx_descs[i].len = 2048 + sizeof(virtio_net_hdr_t);
                rx_descs[i].flags = 0x02;
            }
        }
    }

    (void)regs;
}
