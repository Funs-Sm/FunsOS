#ifndef PCI_SPI_H
#define PCI_SPI_H

#include "spi.h"

/* PCI SPI master controller driver.
 *
 * Implements a generic PCI-attached SPI host controller, supporting:
 *   - SPI mode 0..3 with configurable clock speed
 *   - Single / dual / quad SPI bus widths (x1, x2, x4)
 *   - Chip-select up to 8 devices
 *   - DMA descriptors for multi-segment transfers
 *   - Programmable inter-transfer delays in microseconds
 *
 * The driver implements the spi_controller_t interface in kernel/spi.h
 * so it plugs into the existing SPI framework. Common register layout
 * is matched by software-emulated BAR contents when run on the kernel
 * harness; on real hardware (e.g., a PCIe-to-SPI bridge), the same
 * register definitions map to BAR0.
 */

#define PCI_SPI_VENDOR_ID     0x8086
#define PCI_SPI_DEVICE_ID     0x4F67  /* example device */
#define PCI_SPI_VENDOR_ID_2   0x10EE
#define PCI_SPI_DEVICE_ID_2   0x7011

/* MMIO registers */
#define PCI_SPI_REG_CTRL       0x00
#define PCI_SPI_REG_CLK_DIV    0x04
#define PCI_SPI_REG_BPW        0x08
#define PCI_SPI_REG_CS_EN      0x0C
#define PCI_SPI_REG_TX_FIFO    0x10
#define PCI_SPI_REG_RX_FIFO    0x14
#define PCI_SPI_REG_STATUS     0x18
#define PCI_SPI_REG_INT_EN     0x1C
#define PCI_SPI_REG_INT_ST     0x20
#define PCI_SPI_REG_DMA_ADDR   0x24
#define PCI_SPI_REG_DMA_LEN    0x28
#define PCI_SPI_REG_DELAY      0x2C

#define PCI_SPI_CTRL_ENABLE    (1 << 0)
#define PCI_SPI_CTRL_MASTER    (1 << 1)
#define PCI_SPI_CTRL_RESET     (1 << 2)
#define PCI_SPI_CTRL_DMA_EN    (1 << 3)
#define PCI_SPI_CTRL_XFER_START (1 << 4)

#define PCI_SPI_STATUS_TXFULL  (1 << 0)
#define PCI_SPI_STATUS_RXEMPTY (1 << 1)
#define PCI_SPI_STATUS_DONE    (1 << 2)

/* Bus widths */
#define PCI_SPI_BPW_X1   0
#define PCI_SPI_BPW_X2   1
#define PCI_SPI_BPW_X4   2

/* Controller state */
typedef struct {
    volatile uint32_t *regs;
    spi_controller_t  *ctlr;
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    int     present;
    uint32_t base_clock_hz;
    /* DMA region. */
    uint32_t dma_buf_paddr;
    void    *dma_buf_virt;
    uint32_t dma_buf_size;
    /* Stats. */
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    uint64_t errors;
    uint64_t xfer_count;
} pci_spi_controller_t;

int  pci_spi_init(void);
int  pci_spi_probe(uint8_t bus, uint8_t dev, uint8_t func);
int  pci_spi_register(pci_spi_controller_t *c);
const pci_spi_controller_t *pci_spi_get_count(int *count);

#endif