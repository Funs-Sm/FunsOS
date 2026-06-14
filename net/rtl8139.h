#ifndef RTL8139_H
#define RTL8139_H

#include "net.h"
#include "stdint.h"
#include "kernel_types.h"

#define RTL8139_REG_MAC0   0x00
#define RTL8139_REG_RBSTART 0x30
#define RTL8139_REG_CMD    0x37
#define RTL8139_REG_CAPR   0x38
#define RTL8139_REG_CBR    0x3A
#define RTL8139_REG_IMR    0x3C
#define RTL8139_REG_ISR    0x3E
#define RTL8139_REG_TXCFG  0x40
#define RTL8139_REG_RXCFG  0x44
#define RTL8139_REG_CONFIG1 0x52

void rtl8139_init(uint8_t bus, uint8_t dev, uint8_t func);
int rtl8139_send(net_interface_t *iface, const void *data, uint32_t len);
void rtl8139_irq_handler(regs_t *regs);

#endif
