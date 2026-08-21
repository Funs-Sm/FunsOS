/* pci_spi.c - PCI SPI master controller driver.
 *
 * Implements the kernel/spi.h spi_controller_t interface for a generic
 * PCI-attached SPI master. The controller exposes MMIO registers for
 * clock divisor, chip-select enable, FIFO access, and a DMA descriptor
 * pointer. DMA-safe memory is allocated via kmalloc() and the resulting
 * physical address is written to the DMA_ADDR register.
 */

#include "pci_spi.h"
#include "pci.h"
#include "kheap.h"
#include "klog.h"
#include "string.h"

#define MAX_PCI_SPI_CONTROLLERS 4
static pci_spi_controller_t g_ctrls[MAX_PCI_SPI_CONTROLLERS];
static int g_n_ctrls;
static int g_inited;

static int my_transfer_one(spi_controller_t *ctlr, spi_device_t *spi,
                            struct spi_transfer *xfer)
{
    pci_spi_controller_t *c = (pci_spi_controller_t *)ctlr->data;
    if (!c || !c->regs || !xfer) return -1;

    /* Compute clock divider. */
    uint32_t hz = xfer->speed_hz ? xfer->speed_hz : spi->max_speed_hz;
    if (hz == 0) hz = 1000000;
    uint32_t div = c->base_clock_hz / hz;
    if (div == 0) div = 1;
    c->regs[PCI_SPI_REG_CLK_DIV / 4] = div;

    /* Bits-per-word / bus width */
    uint8_t bpw = xfer->bits_per_word ? xfer->bits_per_word
                                       : spi->bits_per_word;
    if (bpw == 0) bpw = 8;
    c->regs[PCI_SPI_REG_BPW / 4] = bpw;

    /* Select chip. */
    c->regs[PCI_SPI_REG_CS_EN / 4] = (uint32_t)(spi->chip_select & 0x7);

    /* Program optional inter-transfer delay. */
    if (xfer->delay_usecs) c->regs[PCI_SPI_REG_DELAY / 4] = xfer->delay_usecs;

    /* Choose DMA path for larger transfers, polling for short ones. */
    if (xfer->len >= 64 && c->dma_buf_virt) {
        uint32_t len = xfer->len;
        if (len > c->dma_buf_size) len = c->dma_buf_size;
        if (xfer->tx_buf) {
            memcpy(c->dma_buf_virt, xfer->tx_buf, len);
        }
        c->regs[PCI_SPI_REG_DMA_ADDR / 4] = c->dma_buf_paddr;
        c->regs[PCI_SPI_REG_DMA_LEN / 4] = len;
        c->regs[PCI_SPI_REG_CTRL / 4] |= PCI_SPI_CTRL_DMA_EN | PCI_SPI_CTRL_XFER_START;
        /* Wait for completion. */
        for (volatile int t = 0; t < 100000; t++) {
            if (c->regs[PCI_SPI_REG_STATUS / 4] & PCI_SPI_STATUS_DONE) break;
        }
        if (xfer->rx_buf) {
            memcpy(xfer->rx_buf, c->dma_buf_virt, len);
        }
        c->tx_bytes += len;
        c->xfer_count++;
    } else {
        /* Byte-level FIFO transfer. */
        const uint8_t *tx = (const uint8_t *)xfer->tx_buf;
        uint8_t *rx = (uint8_t *)xfer->rx_buf;
        for (uint32_t i = 0; i < xfer->len; i++) {
            while (c->regs[PCI_SPI_REG_STATUS / 4] & PCI_SPI_STATUS_TXFULL) {}
            c->regs[PCI_SPI_REG_TX_FIFO / 4] = tx ? tx[i] : 0xFF;
            while (c->regs[PCI_SPI_REG_STATUS / 4] & PCI_SPI_STATUS_RXEMPTY) {}
            uint32_t v = c->regs[PCI_SPI_REG_RX_FIFO / 4];
            if (rx) rx[i] = (uint8_t)(v & 0xFF);
        }
        c->tx_bytes += xfer->len;
        c->xfer_count++;
    }
    return 0;
}

static int my_setup(spi_device_t *spi) {
    if (!spi) return -1;
    if (spi->max_speed_hz == 0) spi->max_speed_hz = 1000000;
    if (spi->bits_per_word == 0) spi->bits_per_word = 8;
    return 0;
}

int pci_spi_probe(uint8_t bus, uint8_t dev, uint8_t func) {
    uint32_t id = pci_read_config(bus, dev, func, 0x00);
    uint16_t ven = (uint16_t)(id & 0xFFFF);
    uint16_t did = (uint16_t)((id >> 16) & 0xFFFF);
    int match = (ven == PCI_SPI_VENDOR_ID  && did == PCI_SPI_DEVICE_ID) ||
                (ven == PCI_SPI_VENDOR_ID_2 && did == PCI_SPI_DEVICE_ID_2);
    if (!match) return -1;
    if (g_n_ctrls >= MAX_PCI_SPI_CONTROLLERS) return -1;

    /* Enable bus master + memory. */
    uint32_t cmd = pci_read_config(bus, dev, func, 0x04);
    cmd |= 0x06;
    pci_write_config(bus, dev, func, 0x04, cmd);

    uint32_t bar = pci_read_config(bus, dev, func, 0x10);
    if (bar & 0x01) return -1;

    pci_spi_controller_t *c = &g_ctrls[g_n_ctrls];
    memset(c, 0, sizeof(*c));
    c->bus = bus; c->dev = dev; c->func = func;
    c->regs = (volatile uint32_t *)(bar & 0xFFFFFFF0);
    c->base_clock_hz = 100000000;  /* 100 MHz reference */
    c->dma_buf_size = 4096;
    c->dma_buf_virt = kmalloc(c->dma_buf_size);
    if (!c->dma_buf_virt) return -1;
    c->dma_buf_paddr = (uint32_t)c->dma_buf_virt;

    /* Soft-reset. */
    c->regs[PCI_SPI_REG_CTRL / 4] = PCI_SPI_CTRL_RESET;
    for (volatile int i = 0; i < 1000; i++);
    c->regs[PCI_SPI_REG_CTRL / 4] = PCI_SPI_CTRL_ENABLE | PCI_SPI_CTRL_MASTER;

    g_n_ctrls++;
    return 0;
}

int pci_spi_register(pci_spi_controller_t *c) {
    if (!c) return -1;
    spi_controller_t ctlr;
    memset(&ctlr, 0, sizeof(ctlr));
    strcpy(ctlr.name, "pci-spi");
    ctlr.bus_num = 0;
    ctlr.num_chipselect = 8;
    ctlr.min_speed_hz = 1000;
    ctlr.max_speed_hz = c->base_clock_hz / 2;
    ctlr.transfer_one = my_transfer_one;
    ctlr.setup = my_setup;
    ctlr.data = c;
    int r = spi_register_controller(&ctlr);
    if (r == 0) {
        c->ctlr = &ctlr;
        klog_write(KLOG_INFO, "pci-spi: registered %s bus=%u cs=%u\n",
                   ctlr.name, ctlr.bus_num, ctlr.num_chipselect);
    }
    return r;
}

int pci_spi_init(void) {
    if (g_inited) return 0;
    g_inited = 1;
    g_n_ctrls = 0;

    /* Probe all PCI devices for matches. */
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t id = pci_read_config((uint8_t)bus, dev, func, 0x00);
                if ((uint16_t)(id & 0xFFFF) == 0xFFFF) continue;
                pci_spi_probe((uint8_t)bus, dev, func);
            }
        }
    }
    for (int i = 0; i < g_n_ctrls; i++) {
        pci_spi_register(&g_ctrls[i]);
    }
    return 0;
}

const pci_spi_controller_t *pci_spi_get_count(int *count) {
    if (count) *count = g_n_ctrls;
    return g_ctrls;
}