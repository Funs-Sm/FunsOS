#ifndef NE2000_H
#define NE2000_H

#include "stdint.h"
#include "net.h"

/* NE2000-compatible NIC (RTL8029 / DP8390) driver.
 *
 * The driver supports the common ISA variants that QEMU exposes with
 *   -device ne2k_isa
 * and the PCI variant of the same chip (-device ne2k_pci).
 *
 * Only a small subset of features is implemented:
 *   - programmed-I/O data path (no DMA)
 *   - 8-bit data bus (matches the default jumper settings)
 *   - single 256-byte receive buffer window
 *   - one 64-byte transmit buffer
 *
 * Those choices keep the driver small enough to be reviewed in one
 * sitting while still being functional in QEMU.
 */

#define NE2K_PAGE_SIZE      256
#define NE2K_TX_PAGES       2        /* 1 transmit buffer = 2 * 256B */
#define NE2K_TX_BUF_START   0x40
#define NE2K_TX_BUF_END     (NE2K_TX_BUF_START + NE2K_TX_PAGES)  /* 0x42 */
#define NE2K_RX_BUF_START   0x42
#define NE2K_RX_BUF_END     0x80     /* wraps to 0x42 -- 14 KiB */
#define NE2K_RX_BUF_BYTES   ((NE2K_RX_BUF_END - NE2K_RX_BUF_START) * NE2K_PAGE_SIZE)

/* PCI vendor/device IDs */
#define NE2K_PCI_VENDOR     0x10EC   /* Realtek                 */
#define NE2K_PCI_DEVICE     0x8029   /* RTL8029 (PCI NE2000)    */

/* Typical ISA I/O bases the BIOS / QEMU may use */
#define NE2K_ISA_BASE_DEFAULT  0x300
#define NE2K_ISA_BASES_NUM     5
extern const uint16_t ne2k_isa_bases[NE2K_ISA_BASES_NUM];

/* Driver entry points -- follow the same shape as the other
 * drivers/net modules. */
int  ne2k_probe(void);
int  ne2k_send(net_interface_t *iface, const void *data, uint32_t len);
int  ne2k_poll(void);                  /* pump incoming frames */
int  ne2k_is_up(void);
const uint8_t *ne2k_mac(void);

#endif
