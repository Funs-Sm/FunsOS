#ifndef KHEAP_H
#define KHEAP_H

#include "kernel_types.h"

void kheap_init(uint32_t start, uint32_t size);
void *kmalloc(uint32_t size);
void kfree(void *ptr);
void *kcalloc(uint32_t num, uint32_t size);
void *krealloc(void *ptr, uint32_t size);

/* Heap statistics for debugger/perf */
uint32_t kheap_get_start(void);
uint32_t kheap_get_current(void);
uint32_t kheap_get_used(void);

#endif
