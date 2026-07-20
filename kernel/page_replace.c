#include "page_replace.h"
#include "swap.h"
#include "vmm.h"
#include "pmm.h"
#include "kheap.h"
#include "string.h"
#include "timer.h"
#include "sched.h"
#include "process.h"
#include "stdio.h"
#include "stdlib.h"

#define MAX_PAGE_REFS 4096

static page_ref_t page_refs[MAX_PAGE_REFS];
static uint32_t page_ref_count = 0;
static uint32_t current_algorithm = PAGE_REPLACE_LRU;
static uint32_t clock_hand = 0;
static uint32_t fifo_hand = 0;
static uint32_t aging_tick = 0;

static uint32_t page_hits = 0;
static uint32_t page_faults = 0;
static uint32_t page_evictions = 0;
static uint32_t page_swapins = 0;
static uint32_t total_accesses = 0;

static spinlock_t replace_lock;

static int evict_fifo(void);
static int evict_lru(void);
static int evict_clock(void);
static int evict_lfu(void);
static int evict_mru(void);
static int evict_workingset(void);
static int evict_secondchance(void);
static int evict_nfu(void);
static int evict_aging(void);
static int evict_random(void);

static void access_generic(uint32_t virt_addr, uint32_t pid);
static void access_lfu(uint32_t virt_addr, uint32_t pid);
static void access_workingset(uint32_t virt_addr, uint32_t pid);
static void access_aging(uint32_t virt_addr, uint32_t pid);

static page_replace_policy_t policy_table[] = {
    {"FIFO",         PAGE_REPLACE_FIFO,        evict_fifo,        access_generic,    0, "First-In-First-Out - simple queue"},
    {"LRU",          PAGE_REPLACE_LRU,         evict_lru,         access_generic,    1, "Least Recently Used - temporal locality"},
    {"Clock",        PAGE_REPLACE_CLOCK,       evict_clock,       access_generic,    1, "Clock/Second-Chance - LRU approximation"},
    {"LFU",          PAGE_REPLACE_LFU,         evict_lfu,         access_lfu,        0, "Least Frequently Used - frequency based"},
    {"MRU",          PAGE_REPLACE_MRU,         evict_mru,         access_generic,    1, "Most Recently Used - stack algorithms"},
    {"WorkingSet",   PAGE_REPLACE_WORKINGSET,  evict_workingset,  access_workingset, 1, "Working Set Model - Denning's algorithm"},
    {"2ndChance",    PAGE_REPLACE_SECONDCHANCE,evict_secondchance,access_generic,    1, "Enhanced Second-Chance with reference bits"},
    {"NFU",          PAGE_REPLACE_NFU,         evict_nfu,         access_lfu,        0, "Not Frequently Used - software counter"},
    {"Aging",        PAGE_REPLACE_AGING,       evict_aging,       access_aging,      1, "Aging algorithm - NFU with history decay"},
    {"Random",       PAGE_REPLACE_RANDOM,      evict_random,      access_generic,    0, "Random replacement - simple baseline"},
};

static void do_evict_page(page_ref_t *victim) {
    pcb_t *proc = process_get_pcb(victim->owner_pid);
    if (proc && proc->page_dir) {
        uint32_t phys = vmm_get_physical(proc->page_dir, victim->virtual_addr);
        if (phys != 0) {
            phys &= 0xFFFFF000;

            if (victim->dirty) {
                uint32_t slot = swap_alloc_slot();
                if (slot != (uint32_t)-1) {
                    swap_write_page(slot, (const void *)(phys + VMM_KERNEL_BASE));
                    victim->in_swap = 1;
                    victim->swap_slot = slot;
                }
            }

            page_table_t *table = (page_table_t *)(proc->page_dir->entries[victim->virtual_addr >> 22] & 0xFFFFF000);
            if (table) {
                uint32_t pte_index = (victim->virtual_addr >> 12) & 0x3FF;
                table->entries[pte_index] = 0;
                vmm_invalidate_tlb(victim->virtual_addr);
            }

            pmm_free_page((void *)phys);
        }
    }

    uint32_t idx = victim - page_refs;
    if (page_ref_count > 1 && idx < page_ref_count - 1) {
        page_refs[idx] = page_refs[page_ref_count - 1];
        if (clock_hand >= page_ref_count - 1 && clock_hand > 0) clock_hand--;
        if (fifo_hand >= page_ref_count - 1 && fifo_hand > 0) fifo_hand--;
    }
    page_ref_count--;
    page_evictions++;
}

void page_replace_init(uint32_t algorithm) {
    current_algorithm = algorithm;
    page_ref_count = 0;
    clock_hand = 0;
    fifo_hand = 0;
    aging_tick = 0;
    page_hits = 0;
    page_faults = 0;
    page_evictions = 0;
    page_swapins = 0;
    total_accesses = 0;
    spinlock_init(&replace_lock);

    for (uint32_t i = 0; i < MAX_PAGE_REFS; i++) {
        memset(&page_refs[i], 0, sizeof(page_ref_t));
        page_refs[i].virtual_addr = 0;
        page_refs[i].owner_pid = 0;
        page_refs[i].ref_count = 0;
        page_refs[i].last_access = 0;
        page_refs[i].access_count = 0;
        page_refs[i].accessed = 0;
        page_refs[i].dirty = 0;
        page_refs[i].in_swap = 0;
        page_refs[i].reference_byte = 0;
        page_refs[i].swap_slot = (uint32_t)-1;
        page_refs[i].age_counter = 0;
        page_refs[i].history_pos = 0;
        page_refs[i].frequency = 0;
        memset(page_refs[i].access_history, 0, WORKING_SET_WINDOW);
    }
}

static page_ref_t *find_page_ref(uint32_t virt_addr, uint32_t pid) {
    for (uint32_t i = 0; i < page_ref_count; i++) {
        if (page_refs[i].virtual_addr == virt_addr && page_refs[i].owner_pid == pid) {
            return &page_refs[i];
        }
    }
    return NULL;
}

static page_ref_t *alloc_page_ref(void) {
    if (page_ref_count < MAX_PAGE_REFS) {
        page_ref_t *ref = &page_refs[page_ref_count++];
        memset(ref, 0, sizeof(page_ref_t));
        ref->swap_slot = (uint32_t)-1;
        return ref;
    }
    return NULL;
}

static void access_generic(uint32_t virt_addr, uint32_t pid) {
    page_ref_t *ref = find_page_ref(virt_addr, pid);
    if (ref) {
        ref->accessed = 1;
        ref->last_access = timer_get_ticks();
        ref->reference_byte = 1;
    }
}

static void access_lfu(uint32_t virt_addr, uint32_t pid) {
    page_ref_t *ref = find_page_ref(virt_addr, pid);
    if (ref) {
        ref->accessed = 1;
        ref->last_access = timer_get_ticks();
        ref->access_count++;
        ref->frequency++;
    }
}

static void access_workingset(uint32_t virt_addr, uint32_t pid) {
    page_ref_t *ref = find_page_ref(virt_addr, pid);
    if (ref) {
        ref->accessed = 1;
        ref->last_access = timer_get_ticks();
        ref->access_history[ref->history_pos] = 1;
        ref->history_pos = (ref->history_pos + 1) % WORKING_SET_WINDOW;
    }
}

static void access_aging(uint32_t virt_addr, uint32_t pid) {
    page_ref_t *ref = find_page_ref(virt_addr, pid);
    if (ref) {
        ref->accessed = 1;
        ref->last_access = timer_get_ticks();
        ref->reference_byte = 1;
    }
}

void page_replace_access(uint32_t virt_addr, uint32_t pid) {
    spinlock_lock(&replace_lock);
    total_accesses++;

    page_ref_t *ref = find_page_ref(virt_addr, pid);
    if (ref) {
        page_hits++;
        if (ref->accessed == 0) {
            ref->accessed = 1;
        }
        ref->last_access = timer_get_ticks();

        if (ref->in_swap) {
            void *phys = pmm_alloc_page();
            if (phys) {
                if (swap_read_page(ref->swap_slot, (void *)((uint32_t)phys + VMM_KERNEL_BASE)) == 0) {
                    pcb_t *proc = process_get_pcb(pid);
                    if (proc && proc->page_dir) {
                        uint32_t flags = VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE;
                        if (virt_addr < KERNEL_BASE) flags |= VMM_PAGE_USER;
                        vmm_map_page(proc->page_dir, virt_addr, (uint32_t)phys, flags);
                    }
                    swap_free_slot(ref->swap_slot);
                    ref->in_swap = 0;
                    ref->swap_slot = (uint32_t)-1;
                    page_swapins++;
                } else {
                    pmm_free_page(phys);
                }
            }
        }

        if (policy_table[current_algorithm - 1].access) {
            policy_table[current_algorithm - 1].access(virt_addr, pid);
        }
    } else {
        page_faults++;
    }

    spinlock_unlock(&replace_lock);
}

void page_replace_mark_dirty(uint32_t virt_addr, uint32_t pid) {
    spinlock_lock(&replace_lock);
    page_ref_t *ref = find_page_ref(virt_addr, pid);
    if (ref) {
        ref->dirty = 1;
    }
    spinlock_unlock(&replace_lock);
}

static int evict_fifo(void) {
    if (page_ref_count == 0) return -1;

    uint32_t scanned = 0;
    while (scanned < page_ref_count) {
        page_ref_t *ref = &page_refs[fifo_hand % page_ref_count];
        fifo_hand = (fifo_hand + 1) % page_ref_count;
        scanned++;

        if (ref->virtual_addr >= KERNEL_BASE) continue;
        do_evict_page(ref);
        return 0;
    }
    return -1;
}

static int evict_lru(void) {
    if (page_ref_count == 0) return -1;

    page_ref_t *victim = NULL;
    uint32_t oldest_time = 0xFFFFFFFF;

    for (uint32_t i = 0; i < page_ref_count; i++) {
        if (page_refs[i].virtual_addr >= KERNEL_BASE) continue;
        if (page_refs[i].last_access < oldest_time) {
            oldest_time = page_refs[i].last_access;
            victim = &page_refs[i];
        }
    }

    if (victim) {
        do_evict_page(victim);
        return 0;
    }
    return -1;
}

static int evict_clock(void) {
    if (page_ref_count == 0) return -1;

    uint32_t scanned = 0;
    while (scanned < page_ref_count * 2) {
        page_ref_t *ref = &page_refs[clock_hand % page_ref_count];
        clock_hand = (clock_hand + 1) % page_ref_count;
        scanned++;

        if (ref->virtual_addr >= KERNEL_BASE) continue;

        if (ref->accessed) {
            ref->accessed = 0;
        } else {
            do_evict_page(ref);
            return 0;
        }
    }

    page_ref_t *ref = &page_refs[clock_hand % page_ref_count];
    if (ref->virtual_addr < KERNEL_BASE) {
        do_evict_page(ref);
        return 0;
    }
    return -1;
}

static int evict_lfu(void) {
    if (page_ref_count == 0) return -1;

    page_ref_t *victim = NULL;
    uint32_t min_freq = 0xFFFFFFFF;

    for (uint32_t i = 0; i < page_ref_count; i++) {
        if (page_refs[i].virtual_addr >= KERNEL_BASE) continue;
        if (page_refs[i].frequency < min_freq ||
            (page_refs[i].frequency == min_freq && page_refs[i].last_access < (victim ? victim->last_access : 0xFFFFFFFF))) {
            min_freq = page_refs[i].frequency;
            victim = &page_refs[i];
        }
    }

    if (victim) {
        do_evict_page(victim);
        return 0;
    }
    return -1;
}

static int evict_mru(void) {
    if (page_ref_count == 0) return -1;

    page_ref_t *victim = NULL;
    uint32_t newest_time = 0;

    for (uint32_t i = 0; i < page_ref_count; i++) {
        if (page_refs[i].virtual_addr >= KERNEL_BASE) continue;
        if (page_refs[i].last_access > newest_time) {
            newest_time = page_refs[i].last_access;
            victim = &page_refs[i];
        }
    }

    if (victim) {
        do_evict_page(victim);
        return 0;
    }
    return -1;
}

static int evict_workingset(void) {
    if (page_ref_count == 0) return -1;

    page_ref_t *victim = NULL;
    uint32_t oldest = 0xFFFFFFFF;

    for (uint32_t i = 0; i < page_ref_count; i++) {
        page_ref_t *ref = &page_refs[i];
        if (ref->virtual_addr >= KERNEL_BASE) continue;

        uint32_t refs_in_window = 0;
        for (int j = 0; j < WORKING_SET_WINDOW; j++) {
            if (ref->access_history[j]) refs_in_window++;
        }

        if (refs_in_window == 0) {
            if (ref->last_access < oldest) {
                oldest = ref->last_access;
                victim = ref;
            }
        }
    }

    if (!victim) {
        for (uint32_t i = 0; i < page_ref_count; i++) {
            if (page_refs[i].virtual_addr >= KERNEL_BASE) continue;
            if (page_refs[i].last_access < oldest) {
                oldest = page_refs[i].last_access;
                victim = &page_refs[i];
            }
        }
    }

    if (victim) {
        do_evict_page(victim);
        return 0;
    }
    return -1;
}

static int evict_secondchance(void) {
    if (page_ref_count == 0) return -1;

    uint32_t scanned = 0;
    while (scanned < page_ref_count * 3) {
        page_ref_t *ref = &page_refs[clock_hand % page_ref_count];
        clock_hand = (clock_hand + 1) % page_ref_count;
        scanned++;

        if (ref->virtual_addr >= KERNEL_BASE) continue;

        if (ref->accessed && ref->dirty) {
            ref->accessed = 0;
        } else if (ref->accessed) {
            ref->accessed = 0;
            ref->dirty = 0;
        } else if (!ref->dirty) {
            do_evict_page(ref);
            return 0;
        }
    }

    page_ref_t *ref = &page_refs[clock_hand % page_ref_count];
    if (ref->virtual_addr < KERNEL_BASE) {
        do_evict_page(ref);
        return 0;
    }
    return -1;
}

static int evict_nfu(void) {
    if (page_ref_count == 0) return -1;

    page_ref_t *victim = NULL;
    uint32_t min_count = 0xFFFFFFFF;

    for (uint32_t i = 0; i < page_ref_count; i++) {
        if (page_refs[i].virtual_addr >= KERNEL_BASE) continue;
        if (page_refs[i].access_count < min_count) {
            min_count = page_refs[i].access_count;
            victim = &page_refs[i];
        }
    }

    if (victim) {
        do_evict_page(victim);
        return 0;
    }
    return -1;
}

static int evict_aging(void) {
    if (page_ref_count == 0) return -1;

    aging_tick++;
    for (uint32_t i = 0; i < page_ref_count; i++) {
        page_refs[i].age_counter = (page_refs[i].age_counter >> 1);
        if (page_refs[i].accessed) {
            page_refs[i].age_counter |= 0x80;
            page_refs[i].accessed = 0;
        }
    }

    page_ref_t *victim = NULL;
    uint8_t min_age = 0xFF;

    for (uint32_t i = 0; i < page_ref_count; i++) {
        if (page_refs[i].virtual_addr >= KERNEL_BASE) continue;
        if (page_refs[i].age_counter < min_age) {
            min_age = page_refs[i].age_counter;
            victim = &page_refs[i];
        }
    }

    if (victim) {
        do_evict_page(victim);
        return 0;
    }
    return -1;
}

static int evict_random(void) {
    if (page_ref_count == 0) return -1;

    for (uint32_t tries = 0; tries < page_ref_count; tries++) {
        uint32_t idx = rand() % page_ref_count;
        page_ref_t *ref = &page_refs[idx];
        if (ref->virtual_addr < KERNEL_BASE) {
            do_evict_page(ref);
            return 0;
        }
    }
    return evict_lru();
}

int page_replace_evict(void) {
    spinlock_lock(&replace_lock);
    int ret;

    if (current_algorithm >= 1 && current_algorithm <= PAGE_REPLACE_ALGO_COUNT) {
        ret = policy_table[current_algorithm - 1].evict();
    } else {
        ret = evict_lru();
    }

    spinlock_unlock(&replace_lock);
    return ret;
}

int page_replace_register_page(uint32_t virt_addr, uint32_t pid) {
    spinlock_lock(&replace_lock);

    page_ref_t *ref = find_page_ref(virt_addr, pid);
    if (ref) {
        spinlock_unlock(&replace_lock);
        return 0;
    }

    ref = alloc_page_ref();
    if (!ref) {
        spinlock_unlock(&replace_lock);
        page_replace_evict();
        spinlock_lock(&replace_lock);
        ref = alloc_page_ref();
        if (!ref) {
            spinlock_unlock(&replace_lock);
            return -1;
        }
    }

    ref->virtual_addr = virt_addr;
    ref->owner_pid = pid;
    ref->ref_count = 1;
    ref->last_access = timer_get_ticks();
    ref->access_count = 1;
    ref->accessed = 1;
    ref->dirty = 0;
    ref->in_swap = 0;
    ref->reference_byte = 1;
    ref->swap_slot = (uint32_t)-1;
    ref->age_counter = 0x80;
    ref->history_pos = 0;
    ref->frequency = 1;
    memset(ref->access_history, 0, WORKING_SET_WINDOW);
    ref->access_history[0] = 1;

    spinlock_unlock(&replace_lock);
    return 0;
}

uint32_t page_replace_get_stats(uint32_t *faults, uint32_t *evictions, uint32_t *swapins) {
    if (faults) *faults = page_faults;
    if (evictions) *evictions = page_evictions;
    if (swapins) *swapins = page_swapins;
    return page_faults;
}

const page_replace_policy_t *page_replace_get_policy_table(void) {
    return policy_table;
}

int page_replace_set_policy(uint32_t policy_id) {
    if (policy_id < 1 || policy_id > PAGE_REPLACE_ALGO_COUNT) return -1;
    current_algorithm = policy_id;
    return 0;
}

const char *page_replace_get_current_policy_name(void) {
    if (current_algorithm >= 1 && current_algorithm <= PAGE_REPLACE_ALGO_COUNT) {
        return policy_table[current_algorithm - 1].name;
    }
    return "Unknown";
}

uint32_t page_replace_get_hit_ratio(void) {
    if (total_accesses == 0) return 0;
    return (page_hits * 100) / total_accesses;
}

void page_replace_reset_stats(void) {
    spinlock_lock(&replace_lock);
    page_hits = 0;
    page_faults = 0;
    page_evictions = 0;
    page_swapins = 0;
    total_accesses = 0;
    spinlock_unlock(&replace_lock);
}

void page_replace_print_stats(void) {
    printf("=== Page Replacement Statistics ===\n");
    printf("Algorithm: %s\n", page_replace_get_current_policy_name());
    printf("Total accesses: %u\n", total_accesses);
    printf("Page hits:      %u\n", page_hits);
    printf("Page faults:    %u\n", page_faults);
    printf("Evictions:      %u\n", page_evictions);
    printf("Swap-ins:       %u\n", page_swapins);
    printf("Hit ratio:      %u%%\n", page_replace_get_hit_ratio());
    printf("Tracked pages:  %u\n", page_ref_count);
    printf("===================================\n");
}
