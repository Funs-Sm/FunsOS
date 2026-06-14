#ifndef PMM_H
#define PMM_H

#include "kernel_mem.h"
#include "../boot/boot_info.h"

/* 4GB 物理内存支持:
 * 4GB / 4096 = 1048576 页
 * 1048576 / 32 = 32768 个 uint32_t 位图项 */
#define PMM_MAX_PAGES      1048576
#define PMM_4GB_LIMIT      0x100000000ULL

void init_pmm(boot_info_t *info);
void *pmm_alloc_page(void);
void pmm_free_page(void *addr);
void *pmm_alloc_pages(uint32_t count);
void pmm_free_pages(void *addr, uint32_t count);
uint32_t pmm_get_total_pages(void);
uint32_t pmm_get_used_pages(void);
uint32_t pmm_get_free_pages(void);

#endif
