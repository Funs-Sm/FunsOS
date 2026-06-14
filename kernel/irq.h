#ifndef IRQ_H
#define IRQ_H

#include "kernel_types.h"

void init_irq(void);
void pic_mask(uint8_t irq);
void pic_unmask(uint8_t irq);
void pic_eoi(uint8_t irq);
void irq_register_handler(uint8_t irq, void (*handler)(regs_t *));

#endif
