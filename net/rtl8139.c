#include "rtl8139.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "kheap.h"
#include "string.h"
#include "irq.h"

static uint32_t io_base;

static uint8_t *rx_buffer;

static uint32_t rx_offset;

static net_interface_t rtl_iface;

static void rtl8139_outb(uint16_t reg, uint8_t val) {
    *((volatile uint8_t *)(io_base + reg)) = val;
}

static void rtl8139_outw(uint16_t reg, uint16_t val) {
    *((volatile uint16_t *)(io_base + reg)) = val;
}

static void rtl8139_outl(uint16_t reg, uint32_t val) {
    *((volatile uint32_t *)(io_base + reg)) = val;
}

static uint8_t rtl8139_inb(uint16_t reg) {
    return *((volatile uint8_t *)(io_base + reg));
}

static uint16_t rtl8139_inw(uint16_t reg) {
    return *((volatile uint16_t *)(io_base + reg));
}

static uint32_t rtl8139_inl(uint16_t reg) {
    return *((volatile uint32_t *)(io_base + reg));
}

void rtl8139_init(uint8_t bus, uint8_t dev, uint8_t func) {
    uint32_t bar1 = pci_read_config(bus, dev, func, 0x14);
    io_base = bar1 & 0xFFFFFFFC;

    uint16_t cmd = pci_read_config(bus, dev, func, 0x04) & 0xFFFF;
    cmd |= (1 << 2) | (1 << 0);
    pci_write_config(bus, dev, func, 0x04, cmd);

    rtl8139_outb(RTL8139_REG_CONFIG1, 0x00);

    rtl8139_outb(RTL8139_REG_CMD, 0x10);
    while ((rtl8139_inb(RTL8139_REG_CMD) & 0x10) != 0)
        ;

    uint32_t i;
    for (i = 0; i < 6; i++) {
        rtl_iface.mac.bytes[i] = rtl8139_inb(RTL8139_REG_MAC0 + i);
    }

    rx_buffer = (uint8_t *)kmalloc(8192 + 16);
    memset(rx_buffer, 0, 8192 + 16);
    rtl8139_outl(RTL8139_REG_RBSTART, (uint32_t)rx_buffer);

    rx_offset = 0;

    rtl8139_outl(RTL8139_REG_RXCFG,
        (1 << 1) |
        (1 << 2) |
        (1 << 3) |
        (1 << 4) |
        (1 << 5) |
        (1 << 7) |
        (0 << 11) |
        (1 << 13));

    rtl8139_outl(RTL8139_REG_TXCFG,
        (1 << 1) |
        (1 << 2) |
        (0 << 8));

    rtl8139_outw(RTL8139_REG_IMR, 0x0005);

    rtl8139_outb(RTL8139_REG_CMD, 0x0C);

    irq_register_handler(0x20 + (pci_read_config(bus, dev, func, 0x3C) & 0xFF), rtl8139_irq_handler);

    strcpy(rtl_iface.name, "rtl0");
    rtl_iface.up = 1;
    rtl_iface.mtu = 1500;
    rtl_iface.send = rtl8139_send;
    rtl_iface.driver_data = (void *)0;
    net_register_interface(&rtl_iface);
}

int rtl8139_send(net_interface_t *iface, const void *data, uint32_t len) {
    static uint32_t tx_slot = 0;
    if (len > 1792) return -1;

    uint32_t tx_addr_reg = 0x20 + (tx_slot * 4);
    rtl8139_outl(tx_addr_reg, (uint32_t)data);

    uint32_t tx_status_reg = 0x10 + (tx_slot * 4);
    uint32_t tx_cmd = len | (1 << 13) | (1 << 15) | (1 << 16);
    rtl8139_outl(tx_status_reg, tx_cmd);

    tx_slot = (tx_slot + 1) % 4;
    (void)iface;
    return 0;
}

void rtl8139_irq_handler(regs_t *regs) {
    uint16_t isr = rtl8139_inw(RTL8139_REG_ISR);

    if (isr & 0x01) {
        while ((rtl8139_inb(RTL8139_REG_CMD) & 0x01) == 0) {
            uint16_t pkt_len = *((volatile uint16_t *)(rx_buffer + rx_offset));
            rx_offset = (rx_offset + 2) % 8192;

            uint8_t *pkt_data = rx_buffer + rx_offset;

            net_buffer_t *buf = net_alloc_buffer();
            if (buf) {
                memcpy(buf->data, pkt_data, pkt_len - 4);
                buf->len = pkt_len - 4;
                buf->offset = 0;
                buf->iface = &rtl_iface;
                net_receive(buf);
            }

            rx_offset = (rx_offset + pkt_len + 3) & ~3;
            rx_offset %= 8192;

            rtl8139_outw(RTL8139_REG_CAPR, rx_offset - 16);
        }
    }

    rtl8139_outw(RTL8139_REG_ISR, isr);
    (void)regs;
}
