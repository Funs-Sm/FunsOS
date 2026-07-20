#ifndef VMALLOC_H
#define VMALLOC_H

#include "stdint.h"

#define VMALLOC_START        0xF0000000
#define VMALLOC_END          0xFF800000
#define VMALLOC_AREA_SIZE    (VMALLOC_END - VMALLOC_START)
#define VMALLOC_NAME_LEN     32
#define VMALLOC_MAX_AREAS    256
#define VMALLOC_MAGIC        0x564D414C

#define VM_IOREMAP           0x0001
#define VM_ALLOC             0x0002
#define VM_MAP               0x0004
#define VM_USERMAP           0x0008

struct vm_struct;

struct vm_struct {
    struct vm_struct *next;
    void *addr;
    uint32_t size;
    uint32_t flags;
    uint32_t nr_pages;
    uint32_t **pages;
    char name[VMALLOC_NAME_LEN];
};

int vmalloc_init(void);

void *vmalloc(uint32_t size);
void *vmalloc_user(uint32_t size);
void *vzalloc(uint32_t size);
void vfree(void *addr);

void *vmap(uint32_t **pages, uint32_t count, uint32_t flags, const char *name);
void vunmap(void *addr);

struct vm_struct *get_vm_area(uint32_t size, uint32_t flags);
struct vm_struct *find_vm_area(void *addr);
void free_vm_area(struct vm_struct *area);

void vmalloc_print_stats(void);

extern uint32_t vmap_stack_pfn;

#endif
