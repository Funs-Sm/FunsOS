#include "pmm.h"
#include "kheap.h"
#include "stddef.h"

#define PAGE_SIZE 4096
#define BITS_PER_UINT32 32

/* 位图大小: 32768 个 uint32_t 可覆盖 4GB (32768*32*4096 = 4GB) */
static uint32_t bitmap[PMM_MAX_PAGES / BITS_PER_UINT32];
static uint32_t total_pages = 0;
static uint32_t used_pages = 0;
static uint32_t bitmap_size = 0;

static void bitmap_set(uint32_t index) {
    bitmap[index / BITS_PER_UINT32] |= (1 << (index % BITS_PER_UINT32));
}

static void bitmap_clear(uint32_t index) {
    bitmap[index / BITS_PER_UINT32] &= ~(1 << (index % BITS_PER_UINT32));
}

static int bitmap_test(uint32_t index) {
    return (bitmap[index / BITS_PER_UINT32] >> (index % BITS_PER_UINT32)) & 1;
}

void init_pmm(boot_info_t *info) {
    /* 默认支持 4GB 物理内存 */
    uint32_t max_addr = 0xFFFFFFFF;  /* 4GB-1 */

    if (info) {
        /* Use mem_upper (in KB) from multiboot info */
        max_addr = info->mem_upper * 1024;
        if (max_addr == 0) max_addr = 0xFFFFFFFF;
    }

    total_pages = max_addr / PAGE_SIZE;

    /* 限制最大页数为 PMM_MAX_PAGES (4GB) */
    if (total_pages > PMM_MAX_PAGES) {
        total_pages = PMM_MAX_PAGES;
    }

    bitmap_size = (total_pages + BITS_PER_UINT32 - 1) / BITS_PER_UINT32;

    if (bitmap_size > PMM_MAX_PAGES / BITS_PER_UINT32) {
        bitmap_size = PMM_MAX_PAGES / BITS_PER_UINT32;
    }

    /* Mark all pages as used initially */
    for (uint32_t i = 0; i < bitmap_size; i++) {
        bitmap[i] = 0xFFFFFFFF;
    }
    used_pages = total_pages;

    /* Free pages in the usable memory range (1MB..max_addr) */
    uint32_t start_page = 0x100000 / PAGE_SIZE; /* skip first 1MB */
    for (uint32_t p = start_page; p < total_pages; p++) {
        if (bitmap_test(p)) {
            bitmap_clear(p);
            used_pages--;
        }
    }

    /* Mark page 0 as used */
    bitmap_set(0);
    used_pages++;

    /* Mark bitmap itself as used */
    uint32_t bitmap_bytes = bitmap_size * sizeof(uint32_t);
    uint32_t bitmap_start_page = (uint32_t)bitmap / PAGE_SIZE;
    uint32_t bitmap_end_page = ((uint32_t)bitmap + bitmap_bytes + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint32_t p = bitmap_start_page; p < bitmap_end_page && p < total_pages; p++) {
        if (!bitmap_test(p)) {
            bitmap_set(p);
            used_pages++;
        }
    }

    /* Reserve every page the kernel image currently occupies
     * (text/rodata/data/bss).  Without this the very first
     * pmm_alloc_page() inside init_vmm() returns the physical
     * address 0x100000, and the subsequent memset that zeroes the
     * freshly-allocated page directory overwrites the first page
     * of the kernel .text section.  After that the kernel can't
     * run any more code, so init_vmm() appears to hang silently.
     *
     * The kernel range comes from the linker symbols _kernel_start
     * / _kernel_end.  The C compiler on PE/COFF prepends an
     * underscore to C identifiers, so the C name "_kernel_start"
     * resolves to the linker symbol "__kernel_start". */
    extern uint32_t _kernel_start;
    extern uint32_t _kernel_end;
    uint32_t kstart = (uint32_t)&_kernel_start;
    uint32_t kend = (uint32_t)&_kernel_end;
    if (kend < kstart) kend = kstart;
    uint32_t kstart_page = kstart / PAGE_SIZE;
    uint32_t kend_page = (kend + PAGE_SIZE - 1) / PAGE_SIZE;
    for (uint32_t p = kstart_page; p < kend_page && p < total_pages; p++) {
        if (!bitmap_test(p)) {
            bitmap_set(p);
            used_pages++;
        }
    }
}

void *pmm_alloc_page(void) {
    for (uint32_t i = 0; i < bitmap_size; i++) {
        if (bitmap[i] != 0xFFFFFFFF) {
            for (uint32_t j = 0; j < BITS_PER_UINT32; j++) {
                uint32_t index = i * BITS_PER_UINT32 + j;
                if (index >= total_pages) return NULL;
                if (!(bitmap[i] & (1 << j))) {
                    bitmap_set(index);
                    used_pages++;
                    return (void *)(index * PAGE_SIZE);
                }
            }
        }
    }
    return NULL;
}

void pmm_free_page(void *addr) {
    uint32_t phys = (uint32_t)addr;
    if (phys % PAGE_SIZE != 0) return;
    uint32_t index = phys / PAGE_SIZE;
    if (index >= total_pages) return;
    if (bitmap_test(index)) {
        bitmap_clear(index);
        used_pages--;
    }
}

void *pmm_alloc_pages(uint32_t count) {
    if (count == 0) return NULL;
    if (count == 1) return pmm_alloc_page();

    uint32_t consecutive = 0;
    uint32_t start_index = 0;

    for (uint32_t i = 0; i < total_pages; i++) {
        if (!bitmap_test(i)) {
            if (consecutive == 0) start_index = i;
            consecutive++;
            if (consecutive == count) {
                for (uint32_t p = start_index; p < start_index + count; p++) {
                    bitmap_set(p);
                    used_pages++;
                }
                return (void *)(start_index * PAGE_SIZE);
            }
        } else {
            consecutive = 0;
        }
    }
    return NULL;
}

void pmm_free_pages(void *addr, uint32_t count) {
    uint32_t phys = (uint32_t)addr;
    if (phys % PAGE_SIZE != 0) return;
    uint32_t index = phys / PAGE_SIZE;
    for (uint32_t i = 0; i < count; i++) {
        if (index + i >= total_pages) break;
        if (bitmap_test(index + i)) {
            bitmap_clear(index + i);
            used_pages--;
        }
    }
}

uint32_t pmm_get_total_pages(void) {
    return total_pages;
}

uint32_t pmm_get_used_pages(void) {
    return used_pages;
}

uint32_t pmm_get_free_pages(void) {
    return total_pages - used_pages;
}
