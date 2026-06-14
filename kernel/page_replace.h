#ifndef PAGE_REPLACE_H
#define PAGE_REPLACE_H

#include "stdint.h"

/* Page replacement algorithms */
#define PAGE_REPLACE_FIFO     1
#define PAGE_REPLACE_LRU      2
#define PAGE_REPLACE_CLOCK    3

typedef struct page_ref {
    uint32_t virtual_addr;
    uint32_t owner_pid;
    uint32_t ref_count;
    uint32_t last_access;    /* Tick count of last access */
    uint8_t  accessed;       /* Accessed bit */
    uint8_t  dirty;          /* Dirty bit */
    uint8_t  in_swap;        /* Whether page is swapped out */
    uint32_t swap_slot;      /* Swap slot if swapped out */
} page_ref_t;

void page_replace_init(uint32_t algorithm);
int page_replace_evict(void);  /* Evict a page, return 0 on success */
void page_replace_access(uint32_t virt_addr, uint32_t pid);
void page_replace_mark_dirty(uint32_t virt_addr, uint32_t pid);
uint32_t page_replace_get_stats(uint32_t *faults, uint32_t *evictions, uint32_t *swapins);

#endif
