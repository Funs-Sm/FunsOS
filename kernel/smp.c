#include "smp.h"
#include "acpi.h"
#include "pmm.h"
#include "vmm.h"
#include "kheap.h"
#include "gdt.h"
#include "idt.h"
#include "string.h"
#include "sched.h"
#include "io.h"

static cpu_t cpus[SMP_MAX_CPUS];
static uint32_t cpu_count = 0;
static volatile uint8_t ap_startup_count = 0;

/* Shared data area for AP trampoline communication.
 * The trampoline code at 0x8000 will write to these. */
#define SMP_TRAMPOLINE_ADDR  0x8000
#define SMP_TRAMPOLINE_GDT    (SMP_TRAMPOLINE_ADDR + 0x1000)  /* 0x9000 */
#define SMP_TRAMPOLINE_ENTRY  (SMP_TRAMPOLINE_ADDR + 0x2008)  /* 0xA008 */
#define SMP_TRAMPOLINE_STACK  (SMP_TRAMPOLINE_ADDR + 0x2000)  /* 0xA000 */
#define SMP_TRAMPOLINE_READY  (SMP_TRAMPOLINE_ADDR + 0x2010)  /* 0xA010 */
#define SMP_TRAMPOLINE_CPUID  (SMP_TRAMPOLINE_ADDR + 0x2014)  /* 0xA014 */

/* GDT entries for trampoline */
static struct {
    uint32_t base;
    uint32_t limit;
} __attribute__((packed)) trampoline_gdt[3] = {
    { 0, 0 },            /* Null descriptor */
    { 0x00000000, 0xFFFFFFFF },  /* Code segment */
    { 0x00000000, 0xFFFFFFFF },  /* Data segment */
};

void smp_init(void) {
    cpu_count = 0;
    ap_startup_count = 0;

    uint32_t acpi_cpu_count = acpi_get_cpu_count();
    const uint8_t *ids = acpi_get_lapic_ids();
    for (uint32_t i = 0; i < acpi_cpu_count && i < SMP_MAX_CPUS; i++) {
        cpus[i].apic_id = ids[i];
        cpus[i].present = 1;
        cpus[i].started = 0;
        cpus[i].stack = 0;
        cpus[i].current_thread = 0;
        cpus[i].lapic_timer_ticks = 0;
        cpu_count++;
    }

    if (cpu_count > 0) {
        cpus[0].started = 1;
    }
}

static void smp_delay_ms(uint32_t ms) {
    for (volatile uint32_t i = 0; i < ms; i++) {
        for (volatile uint32_t j = 0; j < 100000; j++) {
        }
    }
}

static void smp_setup_trampoline(void) {
    /* Copy trampoline binary to 0x8000.
     * The trampoline is placed at a fixed low physical address so APs
     * can execute it in real mode before switching to protected mode.
     * We embed the trampoline as a flat binary blob. */
    static const uint8_t trampoline_code[] = {
        0xFA,                                   /* cli */
        0x31, 0xC0,                             /* xor ax, ax */
        0x8E, 0xD8,                             /* mov ds, ax */
        0x8E, 0xC0,                             /* mov es, ax */
        0x8E, 0xD0,                             /* mov ss, ax */
        /* Enable A20 via fast A20 gate (port 0x92) */
        0xE4, 0x92,                             /* in al, 0x92 */
        0x0C, 0x02,                             /* or al, 0x02 */
        0x24, 0xFE,                             /* and al, 0xFE */
        0xE6, 0x92,                             /* out 0x92, al */
        /* Load GDT at 0x9000 */
        0x0F, 0x01, 0x16, 0x00, 0x90,          /* lgdt [0x9000] */
        /* Set PE bit in CR0 */
        0x0F, 0x20, 0xC0,                       /* mov eax, cr0 */
        0x0C, 0x01,                             /* or al, 0x01 */
        0x0F, 0x22, 0xC0,                       /* mov cr0, eax */
        /* Far jump to 32-bit code (0x08:pm_entry) */
        0xEA, 0x25, 0x00, 0x00, 0x00, 0x08,    /* jmp 0x08:0x0025 */
        /* --- 32-bit protected mode --- */
        0x66, 0xB8, 0x10, 0x00,                 /* mov ax, 0x10 */
        0x66, 0x8E, 0xD8,                       /* mov ds, ax */
        0x66, 0x8E, 0xC0,                       /* mov es, ax */
        0x66, 0x8E, 0xE0,                       /* mov fs, ax */
        0x66, 0x8E, 0xE8,                       /* mov gs, ax */
        0x66, 0x8E, 0xD0,                       /* mov ss, ax */
        /* Load stack from 0xA000 */
        0x66, 0xA1, 0x00, 0xA0, 0x00, 0x00,    /* mov eax, [0xA000] */
        0x66, 0x89, 0xC4,                       /* mov esp, eax */
        /* Signal ready at 0xA010 */
        0x66, 0xC7, 0x05, 0x10, 0xA0, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00,                 /* mov dword [0xA010], 1 */
        /* Jump to C entry at 0xA008 */
        0x66, 0xA1, 0x08, 0xA0, 0x00, 0x00,    /* mov eax, [0xA008] */
        0x66, 0xFF, 0xD0,                       /* call eax */
        /* Halt */
        0xFA,                                   /* cli */
        0xF4,                                   /* hlt */
        0xEB, 0xFD,                             /* jmp $-1 */
    };
    memcpy((void *)SMP_TRAMPOLINE_ADDR, trampoline_code, sizeof(trampoline_code));

    /* Set up the GDT pointer in the trampoline data area */
    volatile uint32_t *gdt_ptr = (volatile uint32_t *)SMP_TRAMPOLINE_GDT;
    gdt_ptr[0] = (uint32_t)trampoline_gdt;       /* base  - will be set at runtime */
    gdt_ptr[1] = sizeof(trampoline_gdt) - 1;      /* limit */

    /* Set the C entry point */
    volatile uint32_t *entry_ptr = (volatile uint32_t *)SMP_TRAMPOLINE_ENTRY;
    *entry_ptr = (uint32_t)smp_ap_entry;

    /* Clear ready flag */
    volatile uint32_t *ready_ptr = (volatile uint32_t *)SMP_TRAMPOLINE_READY;
    *ready_ptr = 0;
}

void smp_start_aps(void) {
    if (cpu_count <= 1) return;

    /* Read BSP's LAPIC ID */
    uint8_t bsp_id = (uint8_t)(lapic_read(0x20) >> 24);

    /* Set up trampoline data area */
    smp_setup_trampoline();

    for (uint32_t i = 0; i < cpu_count; i++) {
        if (cpus[i].apic_id == bsp_id) continue;
        if (!cpus[i].present) continue;

        /* Allocate a stack for this AP */
        void *stack_page = pmm_alloc_page();
        if (!stack_page) continue;

        uint32_t stack_phys = (uint32_t)stack_page;
        uint32_t stack_virt = stack_phys + VMM_KERNEL_BASE;
        vmm_map_page(0, stack_virt, stack_phys, VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE);
        cpus[i].stack = stack_virt + PMM_PAGE_SIZE;

        /* Set the APIC ID in trampoline data so AP knows who it is */
        volatile uint32_t *cpuid_ptr = (volatile uint32_t *)SMP_TRAMPOLINE_CPUID;
        *cpuid_ptr = cpus[i].apic_id;

        /* Set the stack pointer in trampoline data */
        volatile uint32_t *stack_ptr = (volatile uint32_t *)SMP_TRAMPOLINE_STACK;
        *stack_ptr = cpus[i].stack;

        /* Clear ready flag */
        volatile uint32_t *ready_ptr = (volatile uint32_t *)SMP_TRAMPOLINE_READY;
        *ready_ptr = 0;

        /* Send INIT IPI to the AP */
        /* ICR low: Delivery mode=INIT(5), Assert, Edge trigger */
        lapic_send_ipi(cpus[i].apic_id, 0x00004500);
        smp_delay_ms(10);  /* Wait 10ms per Intel spec */

        /* Send STARTUP IPI with vector pointing to 0x8000 (page 8) */
        lapic_send_ipi(cpus[i].apic_id, 0x00004600 | (SMP_TRAMPOLINE_ADDR >> 12));
        smp_delay_ms(1);   /* Wait 200us per Intel spec, we use 1ms */

        /* Send second STARTUP IPI per Intel spec for old processors */
        lapic_send_ipi(cpus[i].apic_id, 0x00004600 | (SMP_TRAMPOLINE_ADDR >> 12));
        smp_delay_ms(1);

        /* Wait for AP to signal ready */
        uint32_t timeout = 1000;
        while (!(*ready_ptr) && timeout > 0) {
            smp_delay_ms(1);
            timeout--;
        }

        if (*ready_ptr) {
            cpus[i].started = 1;
            ap_startup_count++;
        }
    }
}

void smp_ap_entry(void) {
    /* Each AP runs this after the trampoline switches to protected mode */

    /* Re-initialize GDT and IDT for this AP */
    init_gdt();
    init_idt();

    /* Read our local APIC ID */
    uint32_t apic_id = lapic_read(0x20) >> 24;
    smp_set_current_cpu((uint8_t)apic_id);

    /* Enable LAPIC: set spurious interrupt vector with software enable */
    lapic_write(0xF0, lapic_read(0xF0) | 0x100 | 0xFF);

    /* Configure LAPIC timer (periodic mode, vector 0x20) */
    lapic_write(0x3E0, 0x03);       /* Divide by 16 */
    lapic_write(0x380, 100000);     /* Initial count */
    lapic_write(0x320, 0x20 | 0x20000);  /* Periodic, vector 0x20 */

    /* Mark this AP as started */
    uint8_t id = (uint8_t)apic_id;
    for (uint32_t i = 0; i < cpu_count; i++) {
        if (cpus[i].apic_id == id) {
            cpus[i].started = 1;
            break;
        }
    }

    ap_startup_count++;

    /* Signal ready to BSP */
    volatile uint32_t *ready_ptr = (volatile uint32_t *)SMP_TRAMPOLINE_READY;
    *ready_ptr = 1;

    /* Enter scheduler loop */
    sched_yield();

    /* If scheduler returns, halt */
    while (1) {
        __asm__ volatile("hlt");
    }
}

uint32_t smp_get_cpu_count(void) {
    return cpu_count;
}

uint8_t smp_get_cpu_id(void) {
    uint32_t apic_id = lapic_read(0x20) >> 24;
    for (uint32_t i = 0; i < cpu_count; i++) {
        if (cpus[i].apic_id == (uint8_t)apic_id) {
            return (uint8_t)i;
        }
    }
    return 0;
}

cpu_t *smp_get_current_cpu(void) {
    uint32_t apic_id = lapic_read(0x20) >> 24;
    for (uint32_t i = 0; i < cpu_count; i++) {
        if (cpus[i].apic_id == (uint8_t)apic_id) {
            return &cpus[i];
        }
    }
    return &cpus[0];
}

void smp_send_ipi(uint8_t cpu, uint32_t vector) {
    if (cpu < cpu_count) {
        lapic_send_ipi(cpus[cpu].apic_id, vector);
    }
}

void smp_broadcast_ipi(uint32_t vector) {
    lapic_send_ipi(0xFF, vector);
}

void smp_set_current_cpu(uint8_t id) {
    __asm__ volatile("mov %0, %%gs" : : "r"((uint16_t)id));
}
