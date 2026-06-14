#include "idt.h"
#include "kernel_types.h"
#include "kheap.h"
#include "../lib/string.h"

typedef struct {
    uint16_t base_low;
    uint16_t selector;
    uint8_t zero;
    uint8_t type_attr;
    uint16_t base_high;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_ptr_t;

static idt_entry_t idt[256];
idt_ptr_t idt_ptr;

extern void exception_handler(regs_t *regs);
extern void irq_handler(regs_t *regs);
extern void syscall_handler(regs_t *regs);

void idt_set_gate(uint8_t num, uint32_t handler, uint16_t selector, uint8_t type_attr) {
    idt[num].base_low = (uint16_t)(handler & 0xFFFF);
    idt[num].selector = selector;
    idt[num].zero = 0;
    idt[num].type_attr = type_attr;
    idt[num].base_high = (uint16_t)((handler >> 16) & 0xFFFF);
}

void init_idt(void) {
    memset(&idt, 0, sizeof(idt));

    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base = (uint32_t)&idt;

    for (int i = 0; i < 256; i++) {
        uint8_t type = 0x8E;
        if (i == 0x80) {
            type = 0xEE;
        }
        idt_set_gate((uint8_t)i, (uint32_t)interrupt_entry_table[i], 0x08, type);
    }

    asm volatile("lidt %0" : : "m"(idt_ptr));
}

void idt_flush(void) {
    asm volatile("lidt %0" : : "m"(idt_ptr));
}

void interrupt_handler(regs_t *regs) {
    if (regs->int_no < 32) {
        exception_handler(regs);
    } else if (regs->int_no < 48) {
        irq_handler(regs);
    } else if (regs->int_no == 0x80) {
        syscall_handler(regs);
    }
}
