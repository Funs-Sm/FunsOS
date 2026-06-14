#ifndef KERNEL_MEM_H
#define KERNEL_MEM_H

#include "kernel_types.h"

#define PMM_PAGE_SIZE 4096
#define PMM_BITMAP_SIZE 32768   /* 32768 * 32 = 1048576 页 = 4GB */
#define PMM_MAX_PAGES  1048576  /* 4GB / 4096 = 1048576 页 */
#define VMM_KERNEL_BASE 0xC0000000
#ifndef VMM_PAGE_PRESENT
#define VMM_PAGE_PRESENT 0x001
#endif
#ifndef VMM_PAGE_WRITABLE
#define VMM_PAGE_WRITABLE 0x002
#endif
#ifndef VMM_PAGE_USER
#define VMM_PAGE_USER 0x004
#endif
#define KHEAP_BLOCK_MAGIC 0xCAFE

typedef struct kheap_block_header {
    uint32_t magic;
    uint32_t size;
    uint8_t is_free;
    struct kheap_block_header *next;
} kheap_block_header_t;

#endif
