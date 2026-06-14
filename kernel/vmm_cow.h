#ifndef VMM_COW_H
#define VMM_COW_H

#include "kernel_mem.h"
#include "kernel_types.h"

void vmm_cow_init(void);
void vmm_cow_fork(page_directory_t *parent, page_directory_t *child);
void vmm_cow_page_fault(uint32_t addr, page_directory_t *dir);

#endif
