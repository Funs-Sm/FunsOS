#include "pcnet.h"
#include "pci.h"
#include "vmm.h"
#include "kheap.h"
#include "string.h"
#include "irq.h"
#include "io.h"

static uint32_t io_base;

static pcnet_desc_t *rx_descs;
static pcnet_desc_t *tx_descs;

static void *rx_buffers[PCNET_RX_RING_SIZE];
static void *tx_buffers[PCNET_TX_RING_SIZE];

static uint32_t rx_current;
static uint32_t tx_current;

static net_interface_t pcnet_iface;

static void pcnet_write_csr(uint16_t csr, uint16_t val) {
    outw(io_base + PCNET_REG_RAP, csr);
    outw(io_base + PCNET_REG_RDP, val);
}

static uint16_t pcnet_read_csr(uint16_t csr) {
    outw(io_base + PCNET_REG_RAP, csr);
    return inw(io_base + PCNET_REG_RDP);
}

void pcnet_init(uint8_t bus, uint8_t dev, uint8_t func) {
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

    /* Reset the chip by reading the reset register */
    inw(io_base + PCNET_REG_RESET);
    uint32_t timeout = 10000;
    while (timeout--) {}

    /* Set 32-bit mode (DWIO) by writing to RDP */
    outl(io_base + PCNET_REG_RDP, 0);

    /* Stop the controller */
    pcnet_write_csr(PCNET_CSR0, PCNET_CSR0_STOP);
    timeout = 10000;
    while (timeout--) {}

    /* Read MAC address from PROM */
    uint32_t i;
    for (i = 0; i < 6; i++) {
        pcnet_iface.mac.bytes[i] = inb(io_base + i);
    }

    /* Allocate RX and TX descriptor rings */
    rx_descs = (pcnet_desc_t *)kmalloc(PCNET_RX_RING_SIZE * sizeof(pcnet_desc_t));
    memset(rx_descs, 0, PCNET_RX_RING_SIZE * sizeof(pcnet_desc_t));
    tx_descs = (pcnet_desc_t *)kmalloc(PCNET_TX_RING_SIZE * sizeof(pcnet_desc_t));
    memset(tx_descs, 0, PCNET_TX_RING_SIZE * sizeof(pcnet_desc_t));

    for (i = 0; i < PCNET_RX_RING_SIZE; i++) {
        rx_buffers[i] = kmalloc(2048);
        rx_descs[i].base = (uint32_t)rx_buffers[i];
        rx_descs[i].buf_length = (uint16_t)(-2048);
        rx_descs[i].status = PCNET_RXDESC_OWN;
    }

    for (i = 0; i < PCNET_TX_RING_SIZE; i++) {
        tx_buffers[i] = kmalloc(2048);
        tx_descs[i].base = (uint32_t)tx_buffers[i];
        tx_descs[i].status = 0;
    }

    rx_current = 0;
    tx_current = 0;

    /* Set up initialization block (simplified) */
    /* CSR1/CSR2 point to the init block - for now use software style init */
    pcnet_write_csr(PCNET_CSR1, 0);
    pcnet_write_csr(PCNET_CSR2, 0);

    /* Set CSR3: disable interrupts during init, enable BSWP */
    pcnet_write_csr(PCNET_CSR3, 0x0002);

    /* Set CSR4: default */
    pcnet_write_csr(PCNET_CSR4, 0x0118);

    /* Initialize the controller */
    pcnet_write_csr(PCNET_CSR0, PCNET_CSR0_INIT);

    /* Wait for initialization to complete */
    timeout = 100000;
    while (timeout--) {
        uint16_t csr0 = pcnet_read_csr(PCNET_CSR0);
        if (csr0 & PCNET_CSR0_IDON) break;
    }

    /* Start the controller and enable interrupts */
    pcnet_write_csr(PCNET_CSR0, PCNET_CSR0_IENA | PCNET_CSR0_STRT);

    /* Register IRQ handler */
    irq_register_handler(0x20 + (pci_read_config(bus, dev, func, 0x3C) & 0xFF), pcnet_irq_handler);

    /* Register network interface */
    strcpy(pcnet_iface.name, "pcn0");
    pcnet_iface.up = 1;
    pcnet_iface.mtu = 1500;
    pcnet_iface.send = pcnet_send;
    pcnet_iface.driver_data = (void *)0;
    net_register_interface(&pcnet_iface);
}

int pcnet_send(net_interface_t *iface, const void *data, uint32_t len) {
    if (len > 2048) return -1;

    memcpy(tx_buffers[tx_current], data, len);
    tx_descs[tx_current].buf_length = (uint16_t)(-len);
    tx_descs[tx_current].msg_length = 0;
    tx_descs[tx_current].status = PCNET_TXDESC_OWN | PCNET_TXDESC_STP | PCNET_TXDESC_ENP;

    /* Trigger transmission */
    pcnet_write_csr(PCNET_CSR0, pcnet_read_csr(PCNET_CSR0) | PCNET_CSR0_TDMD);

    uint32_t old = tx_current;
    tx_current = (tx_current + 1) % PCNET_TX_RING_SIZE;
    (void)iface;
    return old;
}

void pcnet_irq_handler(regs_t *regs) {
    uint16_t csr0 = pcnet_read_csr(PCNET_CSR0);

    if (csr0 & PCNET_CSR0_RINT) {
        /* Receive interrupt - stub: poll for received packets */
        uint32_t i;
        for (i = 0; i < PCNET_RX_RING_SIZE; i++) {
            if (!(rx_descs[rx_current].status & PCNET_RXDESC_OWN)) {
                uint16_t pkt_len = rx_descs[rx_current].msg_length & 0x0FFF;
                net_buffer_t *buf = net_alloc_buffer();
                if (buf) {
                    memcpy(buf->data, rx_buffers[rx_current], pkt_len);
                    buf->len = pkt_len;
                    buf->offset = 0;
                    buf->iface = &pcnet_iface;
                    net_receive(buf);
                }
                rx_descs[rx_current].status = PCNET_RXDESC_OWN;
                rx_current = (rx_current + 1) % PCNET_RX_RING_SIZE;
            }
        }
    }

    /* Acknowledge interrupt */
    pcnet_write_csr(PCNET_CSR0, csr0 & ~PCNET_CSR0_INTR);
    (void)regs;
}
