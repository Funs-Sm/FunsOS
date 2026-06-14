#include "page_replace.h"
#include "swap.h"
#include "vmm.h"
#include "pmm.h"
#include "kheap.h"
#include "string.h"
#include "timer.h"
#include "sched.h"
#include "process.h"

#define MAX_PAGE_REFS 4096

static page_ref_t page_refs[MAX_PAGE_REFS];
static uint32_t page_ref_count = 0;
static uint32_t current_algorithm = PAGE_REPLACE_LRU;
static uint32_t clock_hand = 0;

/* Statistics */
static uint32_t page_faults = 0;
static uint32_t page_evictions = 0;
static uint32_t page_swapins = 0;

static spinlock_t replace_lock;

void page_replace_init(uint32_t algorithm) {
    current_algorithm = algorithm;
    page_ref_count = 0;
    clock_hand = 0;
    page_faults = 0;
    page_evictions = 0;
    page_swapins = 0;
    spinlock_init(&replace_lock);

    for (uint32_t i = 0; i < MAX_PAGE_REFS; i++) {
        page_refs[i].virtual_addr = 0;
        page_refs[i].owner_pid = 0;
        page_refs[i].ref_count = 0;
        page_refs[i].last_access = 0;
        page_refs[i].accessed = 0;
        page_refs[i].dirty = 0;
        page_refs[i].in_swap = 0;
        page_refs[i].swap_slot = (uint32_t)-1;
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
        return &page_refs[page_ref_count++];
    }
    return NULL;
}

void page_replace_access(uint32_t virt_addr, uint32_t pid) {
    spinlock_lock(&replace_lock);

    page_ref_t *ref = find_page_ref(virt_addr, pid);
    if (ref) {
        ref->accessed = 1;
        ref->last_access = timer_get_ticks();

        /* If page was swapped out, bring it back */
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

int page_replace_evict(void) {
    spinlock_lock(&replace_lock);

    if (page_ref_count == 0) {
        spinlock_unlock(&replace_lock);
        return -1;
    }

    /* Clock algorithm (LRU approximation) */
    uint32_t scanned = 0;
    page_ref_t *victim = NULL;

    while (scanned < page_ref_count * 2) {
        page_ref_t *ref = &page_refs[clock_hand % page_ref_count];

        /* Skip kernel pages */
        if (ref->virtual_addr >= KERNEL_BASE) {
            clock_hand = (clock_hand + 1) % page_ref_count;
            scanned++;
            continue;
        }

        if (ref->accessed) {
            /* Give second chance: clear accessed bit */
            ref->accessed = 0;
            clock_hand = (clock_hand + 1) % page_ref_count;
            scanned++;
        } else {
            victim = ref;
            break;
        }
    }

    if (!victim) {
        spinlock_unlock(&replace_lock);
        return -1;
    }

    /* Get the physical address of the victim page */
    pcb_t *proc = process_get_pcb(victim->owner_pid);
    if (!proc || !proc->page_dir) {
        /* Process already gone, just clean up the ref */
        goto cleanup_ref;
    }

    uint32_t phys = vmm_get_physical(proc->page_dir, victim->virtual_addr);
    if (phys == 0) {
        goto cleanup_ref;
    }
    phys &= 0xFFFFF000;

    /* If dirty, write to swap */
    if (victim->dirty) {
        uint32_t slot = swap_alloc_slot();
        if (slot != (uint32_t)-1) {
            swap_write_page(slot, (const void *)(phys + VMM_KERNEL_BASE));
            victim->in_swap = 1;
            victim->swap_slot = slot;
        }
    }

    /* Unmap the page from the owner's page directory */
    page_table_t *table = (page_table_t *)(proc->page_dir->entries[victim->virtual_addr >> 22] & 0xFFFFF000);
    if (table) {
        uint32_t pte_index = (victim->virtual_addr >> 12) & 0x3FF;
        table->entries[pte_index] = 0;
        vmm_invalidate_tlb(victim->virtual_addr);
    }

    /* Free the physical frame */
    pmm_free_page((void *)phys);
    page_evictions++;

cleanup_ref:
    /* Remove victim from the page ref list by swapping with last */
    if (page_ref_count > 1) {
        uint32_t idx = victim - page_refs;
        page_refs[idx] = page_refs[page_ref_count - 1];
        page_ref_count--;
        /* Adjust clock hand if needed */
        if (clock_hand >= page_ref_count) {
            clock_hand = 0;
        }
    } else {
        page_ref_count = 0;
        clock_hand = 0;
    }

    spinlock_unlock(&replace_lock);
    return 0;
}

uint32_t page_replace_get_stats(uint32_t *faults, uint32_t *evictions, uint32_t *swapins) {
    if (faults) *faults = page_faults;
    if (evictions) *evictions = page_evictions;
    if (swapins) *swapins = page_swapins;
    return page_faults;
}
