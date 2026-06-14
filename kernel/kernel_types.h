#ifndef KERNEL_TYPES_H
#define KERNEL_TYPES_H

#include "stdint.h"

typedef int32_t pid_t;

typedef void (*func_t)(void *);

typedef int32_t (*syscall_func_t)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

enum {
    PROCESS_UNUSED = 0,
    PROCESS_READY,
    PROCESS_RUNNING,
    PROCESS_BLOCKED,
    PROCESS_ZOMBIE
};

enum {
    PROCESS_KERNEL = 0,
    PROCESS_USER
};

typedef struct __attribute__((packed)) {
    /* Segment registers pushed by isr_common_stub / irq_common_stub
     * AFTER PUSHAD but BEFORE calling the C handler.
     * These MUST be first to match the stack layout. */
    uint32_t gs;
    uint32_t fs;
    uint32_t es;
    uint32_t ds;
    /* General-purpose registers pushed by PUSHAD */
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp_kernel;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    /* Interrupt number and error code pushed by ISR stub */
    uint32_t int_no;
    uint32_t err_code;
    /* Pushed by CPU during interrupt */
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    uint32_t useresp;
    uint32_t ss;
} regs_t;

typedef struct pcb_t pcb_t;

typedef struct {
    uint32_t entries[1024];
} page_directory_t;

typedef struct {
    uint32_t entries[1024];
} page_table_t;

#endif
