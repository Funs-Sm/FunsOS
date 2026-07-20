#ifndef PERCPU_H
#define PERCPU_H

#include "stdint.h"

#define PERCPU_AREA_SIZE     65536
#define PERCPU_MAX_ALLOCS    128
#define PERCPU_NAME_LEN      32
#define PERCPU_MAX_CPUS      8
#define PERCPU_MAGIC         0x50435055

struct percpu_area {
    struct percpu_area *next;
    uint32_t size;
    uint32_t align;
    uint32_t cpu_offset[PERCPU_MAX_CPUS];
    void *cpu_ptr[PERCPU_MAX_CPUS];
    char name[PERCPU_NAME_LEN];
    uint32_t in_use;
};

#define DEFINE_PER_CPU(type, name) \
    static type __percpu_##name[PERCPU_MAX_CPUS] __attribute__((aligned(64))); \
    static struct percpu_area __percpu_area_##name

int percpu_init(void);

void *percpu_alloc(uint32_t size, uint32_t align, const char *name);
void percpu_free(void *ptr);
void *percpu_ptr(void *base, uint32_t cpu);

#define this_cpu_ptr(ptr) percpu_ptr((ptr), percpu_get_cpu_id())
#define get_cpu_var(var) (*(typeof(&__percpu_##var[0]))percpu_ptr(__percpu_##var, percpu_get_cpu_id()))

uint32_t percpu_get_cpu_id(void);
void percpu_set_cpu_id(uint32_t cpu);

void percpu_print_stats(void);

#endif
