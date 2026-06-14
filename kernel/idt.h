#ifndef IDT_H
#define IDT_H

#include "kernel_types.h"

void init_idt(void);
void idt_set_gate(uint8_t num, uint32_t handler, uint16_t selector, uint8_t type_attr);

extern uint32_t interrupt_entry_table[];

#endif
