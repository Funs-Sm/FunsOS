#ifndef SMP_H
#define SMP_H

#include "stdint.h"
#include "acpi.h"
#include "kernel_proc.h"

#define SMP_MAX_CPUS 256

typedef struct {
    uint8_t apic_id;
    uint8_t present;
    uint8_t started;
    uint32_t stack;
    pcb_t *current_thread;
    uint32_t lapic_timer_ticks;
} cpu_t;

void smp_init(void);
void smp_start_aps(void);
void smp_ap_entry(void);
uint32_t smp_get_cpu_count(void);
cpu_t *smp_get_current_cpu(void);
void smp_send_ipi(uint8_t cpu, uint32_t vector);
void smp_broadcast_ipi(uint32_t vector);
void smp_set_current_cpu(uint8_t id);
uint8_t smp_get_cpu_id(void);

#endif
