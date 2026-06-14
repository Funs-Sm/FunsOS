#ifndef PCNET_H
#define PCNET_H

#include "net.h"
#include "stdint.h"
#include "kernel_types.h"

/* AMD PCnet PCI IDs */
#define PCNET_VENDOR_ID 0x1022
#define PCNET_DEVICE_ID 0x2000

/* PCnet register offsets (via I/O space) */
#define PCNET_REG_RDP      0x10
#define PCNET_REG_RAP      0x12
#define PCNET_REG_RESET    0x14
#define PCNET_REG_BDP      0x16

/* CSR registers */
#define PCNET_CSR0         0x0000
#define PCNET_CSR1         0x0001
#define PCNET_CSR2         0x0002
#define PCNET_CSR3         0x0003
#define PCNET_CSR4         0x0004
#define PCNET_CSR15        0x000F

/* CSR0 bits */
#define PCNET_CSR0_INIT    0x0001
#define PCNET_CSR0_STRT    0x0002
#define PCNET_CSR0_STOP    0x0004
#define PCNET_CSR0_TDMD    0x0008
#define PCNET_CSR0_RXON    0x0010
#define PCNET_CSR0_TXON    0x0020
#define PCNET_CSR0_IENA    0x0040
#define PCNET_CSR0_INTR    0x0080
#define PCNET_CSR0_IDON    0x0100
#define PCNET_CSR0_TINT    0x0200
#define PCNET_CSR0_RINT    0x0400
#define PCNET_CSR0_MERR    0x0800
#define PCNET_CSR0_MISS    0x1000
#define PCNET_CSR0_CERR    0x2000
#define PCNET_CSR0_BABL    0x4000
#define PCNET_CSR0_ERR     0x8000

/* Descriptor flags - status is 16-bit, OWN is bit 15 */
#define PCNET_TXDESC_STP   0x0200
#define PCNET_TXDESC_ENP   0x0100
#define PCNET_TXDESC_OWN   0x8000

#define PCNET_RXDESC_OWN   0x8000

#define PCNET_RX_RING_SIZE 32
#define PCNET_TX_RING_SIZE 32

typedef struct {
    uint32_t base;
    uint16_t buf_length;
    uint16_t status;
    uint32_t msg_length;
    uint32_t misc;
} pcnet_desc_t;

void pcnet_init(uint8_t bus, uint8_t dev, uint8_t func);
int pcnet_send(net_interface_t *iface, const void *data, uint32_t len);
void pcnet_irq_handler(regs_t *regs);

#endif
