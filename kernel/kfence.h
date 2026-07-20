#ifndef KFENCE_H
#define KFENCE_H

#include "stdint.h"

#define KFENCE_POOL_SIZE      (256 * 1024)
#define KFENCE_MAX_OBJECTS    64
#define KFENCE_GUARD_SIZE     4096
#define KFENCE_STACK_DEPTH    16
#define KFENCE_CANARY_VALUE   0xAA55AA55
#define KFENCE_MAGIC_ALLOC    0x4B464E41
#define KFENCE_MAGIC_FREE     0x4B464E46

enum kfence_obj_state {
    KFENCE_OBJECT_FREE = 0,
    KFENCE_OBJECT_ALLOCATED,
    KFENCE_OBJECT_QUARANTINE
};

struct kfence_object {
    void *addr;
    uint32_t size;
    uint32_t state;
    uint32_t alloc_stack[KFENCE_STACK_DEPTH];
    uint32_t free_stack[KFENCE_STACK_DEPTH];
    uint32_t alloc_count;
    uint32_t canary;
    uint32_t magic;
};

int kfence_init(void);

void *kfence_alloc_guard(uint32_t size);
void kfence_free_guard(void *addr);
int kfence_check(void *addr);
int kfence_check_all(void);

uint32_t kfence_get_uaf_detected(void);
uint32_t kfence_get_overflow_detected(void);

void kfence_print_stats(void);

#endif
