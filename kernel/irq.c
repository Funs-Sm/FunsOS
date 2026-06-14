#include "irq.h"
#include "kernel_types.h"
#include "idt.h"
#include "sync.h"
#include "kheap.h"
#include "acpi.h"
#include "../lib/string.h"
#include "stddef.h"
#include "io.h"

#define PIC1_CMD   0x20
#define PIC1_DATA  0x21
#define PIC2_CMD   0xA0
#define PIC2_DATA  0xA1

#define ICW1       0x11
#define ICW4_8086  0x01

void ioapic_set_routing(uint8_t irq, uint8_t vector, uint8_t cpu);

static void (*irq_handlers[16])(regs_t *regs);
static void *irq_handler_data[16];
static uint32_t irq_count[16];
static spinlock_t irq_lock;

static void pic_remap(uint8_t offset1, uint8_t offset2) {
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    outb(PIC1_CMD, ICW1);
    io_wait();
    outb(PIC2_CMD, ICW1);
    io_wait();

    outb(PIC1_DATA, offset1);
    io_wait();
    outb(PIC2_DATA, offset2);
    io_wait();

    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();

    outb(PIC1_DATA, ICW4_8086);
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

void pic_mask(uint8_t irq) {
    uint16_t port;
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    uint8_t value = inb(port) | (1 << irq);
    outb(port, value);
}

void pic_unmask(uint8_t irq) {
    uint16_t port;
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    uint8_t value = inb(port) & ~(1 << irq);
    outb(port, value);
}

void pic_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_CMD, 0x20);
    }
    outb(PIC1_CMD, 0x20);
}

void irq_handler(regs_t *regs) {
    uint8_t irq = regs->int_no - 32;

    if (irq < 16) {
        irq_count[irq]++;
        spinlock_lock(&irq_lock);
        void (*handler)(regs_t *) = irq_handlers[irq];
        void *data = irq_handler_data[irq];
        spinlock_unlock(&irq_lock);

        if (handler) {
            handler(regs);
        }
        (void)data;
    }

    pic_eoi(irq);
}

void irq_register_handler(uint8_t irq, void (*handler)(regs_t *)) {
    if (irq >= 16) return;

    spinlock_lock(&irq_lock);
    irq_handlers[irq] = handler;
    spinlock_unlock(&irq_lock);

    pic_unmask(irq);
}

void irq_unregister_handler(uint8_t irq) {
    if (irq >= 16) return;

    pic_mask(irq);

    spinlock_lock(&irq_lock);
    irq_handlers[irq] = NULL;
    irq_handler_data[irq] = NULL;
    spinlock_unlock(&irq_lock);
}

static void ioapic_init(void) {
    uint32_t ioapic_base = acpi_get_ioapic_base();
    if (ioapic_base == 0) return;

    /* Route all IRQs through IOAPIC with vectors 32+irq */
    for (int i = 0; i < 24; i++) {
        ioapic_set_routing((uint8_t)i, (uint8_t)(32 + i), 0);
    }

    /* Mask PICs so IRQs go through IOAPIC only */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

void ioapic_set_routing(uint8_t irq, uint8_t vector, uint8_t cpu) {
    uint32_t ioapic_base = acpi_get_ioapic_base();
    if (ioapic_base == 0) return;

    uint32_t low = (uint32_t)vector
                 | (0 << 8)
                 | (0 << 11)
                 | (0 << 13)
                 | (0 << 15)
                 | (0 << 16);

    uint32_t high = (uint32_t)(cpu & 0xFF) << 24;

    ioapic_write(0, 0x10 + irq * 2, low);
    ioapic_write(0, 0x11 + irq * 2, high);
}

void init_irq(void) {
    spinlock_init(&irq_lock);

    memset(irq_handlers, 0, sizeof(irq_handlers));
    memset(irq_handler_data, 0, sizeof(irq_handler_data));
    memset(irq_count, 0, sizeof(irq_count));

    pic_remap(32, 40);

    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);

    pic_unmask(2);

    for (int i = 0; i < 16; i++) {
        idt_set_gate((uint8_t)(32 + i), (uint32_t)interrupt_entry_table[32 + i], 0x08, 0x8E);
    }

    /* Initialize IOAPIC (if present) - routes IRQs through APIC/IOAPIC.
     * PICs remain active as fallback. */
    ioapic_init();
}
