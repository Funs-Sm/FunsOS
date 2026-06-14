#include "gdt.h"
#include "kheap.h"
#include "../lib/string.h"

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) gdt_ptr_t;

typedef struct {
    uint32_t prev_task_link;
    uint32_t esp0;
    uint16_t ss0;
    uint16_t padding0;
    uint32_t esp1;
    uint16_t ss1;
    uint16_t padding1;
    uint32_t esp2;
    uint16_t ss2;
    uint16_t padding2;
    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;
    uint32_t eax;
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint16_t es;
    uint16_t padding_es;
    uint16_t cs;
    uint16_t padding_cs;
    uint16_t ss;
    uint16_t padding_ss;
    uint16_t ds;
    uint16_t padding_ds;
    uint16_t fs;
    uint16_t padding_fs;
    uint16_t gs;
    uint16_t padding_gs;
    uint16_t ldt;
    uint16_t padding_ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed)) tss_entry_t;

static gdt_entry_t gdt[16];
gdt_ptr_t gdt_ptr;
static tss_entry_t tss;

static void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].limit_low = (uint16_t)(limit & 0xFFFF);
    gdt[num].base_low = (uint16_t)(base & 0xFFFF);
    gdt[num].base_middle = (uint8_t)((base >> 16) & 0xFF);
    gdt[num].access = access;
    gdt[num].granularity = (uint8_t)((gran & 0xF0) | ((limit >> 16) & 0x0F));
    gdt[num].base_high = (uint8_t)((base >> 24) & 0xFF);
}

void init_gdt(void) {
    memset(&gdt, 0, sizeof(gdt));
    memset(&tss, 0, sizeof(tss));

    gdt_set_gate(0, 0, 0, 0, 0);
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);
    gdt_set_gate(5, (uint32_t)&tss, sizeof(tss_entry_t) - 1, 0xE9, 0x00);

    tss.ss0 = 0x10;
    tss.iomap_base = sizeof(tss_entry_t);

    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base = (uint32_t)&gdt;

    asm volatile(
        "lgdt %0\n\t"
        "ljmp $0x08, $1f\n\t"
        "1:\n\t"
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "movw %%ax, %%ss\n\t"
        : : "m"(gdt_ptr) : "ax", "memory"
    );

    flush_tss();
}

void gdt_set_tss(uint32_t ss0, uint32_t esp0) {
    tss.ss0 = (uint16_t)ss0;
    tss.esp0 = esp0;
    gdt_set_gate(5, (uint32_t)&tss, sizeof(tss_entry_t) - 1, 0xE9, 0x00);
}

void flush_tss(void) {
    uint16_t tr = 0x28;
    asm volatile("ltr %0" : : "r"(tr));
}
