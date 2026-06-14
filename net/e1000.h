#ifndef E1000_H
#define E1000_H

#include "net.h"
#include "stdint.h"
#include "kernel_types.h"

#define E1000_REG_CTRL   0x0000
#define E1000_REG_STATUS 0x0008
#define E1000_REG_EEC    0x0010
#define E1000_REG_IMC    0x00D8
#define E1000_REG_RCTL   0x0100
#define E1000_REG_TCTL   0x0400

#define E1000_RXDESC_STATUS_DD 0x01

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t status;
    uint8_t errors;
    uint16_t special;
} e1000_rx_desc_t;

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint8_t cso;
    uint8_t cmd;
    uint8_t status;
    uint8_t css;
    uint16_t special;
} e1000_tx_desc_t;

void e1000_init(uint8_t bus, uint8_t dev, uint8_t func);
int e1000_send(net_interface_t *iface, const void *data, uint32_t len);
void e1000_poll(void);
void e1000_irq_handler(regs_t *regs);

#endif
