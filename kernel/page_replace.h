#ifndef PAGE_REPLACE_H
#define PAGE_REPLACE_H

#include "stdint.h"

#define PAGE_REPLACE_FIFO        1
#define PAGE_REPLACE_LRU         2
#define PAGE_REPLACE_CLOCK       3
#define PAGE_REPLACE_LFU         4
#define PAGE_REPLACE_MRU         5
#define PAGE_REPLACE_WORKINGSET  6
#define PAGE_REPLACE_SECONDCHANCE 7
#define PAGE_REPLACE_NFU         8
#define PAGE_REPLACE_AGING       9
#define PAGE_REPLACE_RANDOM      10
#define PAGE_REPLACE_OPTIMAL     11

#define PAGE_REPLACE_ALGO_COUNT  11

#define PAGE_REPLACE_POLICY_NORMAL    0
#define PAGE_REPLACE_POLICY_LOWMEM    1
#define PAGE_REPLACE_POLICY_PERF      2
#define PAGE_REPLACE_POLICY_POWERSAVE 3
#define PAGE_REPLACE_POLICY_ADAPTIVE  4

#define PAGE_ACCESS_READ   1
#define PAGE_ACCESS_WRITE  2
#define PAGE_ACCESS_EXEC   4

#define WORKING_SET_WINDOW 50

typedef struct page_ref {
    uint32_t virtual_addr;
    uint32_t owner_pid;
    uint32_t ref_count;
    uint32_t last_access;
    uint32_t access_count;
    uint8_t  accessed;
    uint8_t  dirty;
    uint8_t  in_swap;
    uint8_t  reference_byte;
    uint32_t swap_slot;
    uint8_t  age_counter;
    uint8_t  access_history[WORKING_SET_WINDOW];
    uint32_t history_pos;
    uint32_t frequency;
} page_ref_t;

typedef int (*page_evict_func_t)(void);
typedef void (*page_access_func_t)(uint32_t, uint32_t);

typedef struct page_replace_policy {
    const char *name;
    uint32_t algo_id;
    page_evict_func_t evict;
    page_access_func_t access;
    uint32_t ref_bit_support;
    const char *description;
} page_replace_policy_t;

void page_replace_init(uint32_t algorithm);
int page_replace_evict(void);
void page_replace_access(uint32_t virt_addr, uint32_t pid);
void page_replace_mark_dirty(uint32_t virt_addr, uint32_t pid);
uint32_t page_replace_get_stats(uint32_t *faults, uint32_t *evictions, uint32_t *swapins);

const page_replace_policy_t *page_replace_get_policy_table(void);
int page_replace_set_policy(uint32_t policy_id);
const char *page_replace_get_current_policy_name(void);
uint32_t page_replace_get_hit_ratio(void);
void page_replace_reset_stats(void);
void page_replace_print_stats(void);

#endif
