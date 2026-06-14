#include "vmm.h"
#include "pmm.h"
#include "kheap.h"
#include "panic.h"
#include "string.h"
#include "timer.h"
#include "stddef.h"
#include "page_replace.h"
#include "swap.h"
#include "sched.h"
#include "klog.h"

typedef uint32_t page_table_entry_t;
typedef uint32_t page_dir_entry_t;

static page_directory_t *kernel_page_directory = NULL;
static page_directory_t *current_page_directory = NULL;

static page_frame_manager_t frame_manager;
static page_cache_t page_cache;
static uint64_t tick_count = 0;

static void invlpg(uint32_t addr) {
    __asm__ volatile("invlpg (%0)" : : "r"(addr) : "memory");
}

static void load_cr3(uint32_t cr3) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3));
}

static void enable_paging(void) {
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
}

static page_table_t *get_page_table(page_directory_t *dir, uint32_t virt) {
    uint32_t pde_index = virt >> 22;
    page_dir_entry_t pde = dir->entries[pde_index];

    if (!(pde & PTE_PRESENT)) {
        return NULL;
    }

    return (page_table_t *)(pde & 0xFFFFF000);
}

static page_table_t *create_page_table(page_directory_t *dir, uint32_t virt, uint32_t flags) {
    uint32_t pde_index = virt >> 22;

    if (dir->entries[pde_index] & PTE_PRESENT) {
        return (page_table_t *)(dir->entries[pde_index] & 0xFFFFF000);
    }

    void *phys = pmm_alloc_page();
    if (!phys) return NULL;

    page_table_t *table = (page_table_t *)phys;
    for (int i = 0; i < PAGE_ENTRIES; i++) {
        table->entries[i] = 0;
    }

    dir->entries[pde_index] = ((uint32_t)phys) | (flags & 0xFFF) | PTE_PRESENT | PTE_WRITABLE;

    return table;
}

void init_vmm(void) {
    frame_manager.frames = NULL;
    frame_manager.frame_count = 0;
    frame_manager.free_count = 0;
    spinlock_init(&frame_manager.lock);

    page_cache.pages = NULL;
    page_cache.count = 0;
    page_cache.size = 0;
    spinlock_init(&page_cache.lock);

    void *dir_phys = pmm_alloc_page();
    if (!dir_phys) KERNEL_PANIC("VMM: Failed to allocate page directory");

    kernel_page_directory = (page_directory_t *)dir_phys;
    current_page_directory = kernel_page_directory;

    for (int i = 0; i < PAGE_ENTRIES; i++) {
        kernel_page_directory->entries[i] = 0;
    }

    extern uint32_t _kernel_start;
    extern uint32_t _kernel_end;
    uint32_t kstart = (uint32_t)&_kernel_start;
    uint32_t kend = (uint32_t)&_kernel_end;

    /* Identity map and high-half alias of the first 128 MB of
     * physical memory (matches the default pmm_init() assumption).
     *
     * The identity alias is required because the kernel still
     * dereferences pointers using their *physical* address inside
     * code that runs after enable_paging() (notably
     * create_page_table() and the vmm_map_page() loop that calls
     * it).  If a freshly allocated page table or page directory
     * lives past the kernel image, dereferencing its physical
     * address would fault and trip the page-fault handler before
     * the heap is up, leaving the system wedged. */
    uint32_t hh_end = 128u * 1024u * 1024u;
    for (uint32_t phys = 0; phys < hh_end; phys += PAGE_SIZE) {
        vmm_map_page(kernel_page_directory, phys, phys,
                     PTE_PRESENT | PTE_WRITABLE);
        vmm_map_page(kernel_page_directory,
                     KERNEL_BASE + phys,
                     phys,
                     PTE_PRESENT | PTE_WRITABLE);
    }

    /* The page table for KERNEL_BASE was already wired in by the
     * vmm_map_page calls above.  Nothing more to do for the PDE:
     * entries[KERNEL_BASE >> 22] already points to the right
     * page table.  (Previous version wrote to entries[0], which
     * trashed the identity-mapped page table covering 0..0x400000
     * and caused nonsense translations for low addresses like
     * the VGA text buffer at 0xB8000.) */

    load_cr3((uint32_t)kernel_page_directory);
    enable_paging();

    kernel_page_directory = (page_directory_t *)((uint32_t)kernel_page_directory + KERNEL_BASE);
    current_page_directory = kernel_page_directory;
}

void vmm_map_page(page_directory_t *dir, uint32_t virt, uint32_t phys, uint32_t flags) {
    page_table_t *table = get_page_table(dir, virt);
    if (!table) {
        table = create_page_table(dir, virt, flags);
        if (!table) KERNEL_PANIC("VMM: Failed to create page table");
    }

    uint32_t pte_index = (virt >> 12) & 0x3FF;
    table->entries[pte_index] = (phys & 0xFFFFF000) | (flags & 0xFFF) | PTE_PRESENT;

    invlpg(virt);
}

void vmm_unmap_page(page_directory_t *dir, uint32_t virt) {
    page_table_t *table = get_page_table(dir, virt);
    if (!table) return;

    uint32_t pte_index = (virt >> 12) & 0x3FF;
    page_table_entry_t pte = table->entries[pte_index];

    if (pte & PTE_PRESENT) {
        uint32_t phys = pte & 0xFFFFF000;
        table->entries[pte_index] = 0;
        invlpg(virt);

        /* Do NOT free COW pages - they are shared with other address
         * spaces.  Only free pages that are exclusively owned. */
        if (!(pte & PTE_COW)) {
            pmm_free_page((void *)phys);
        }
    }
}

uint32_t vmm_get_physical(page_directory_t *dir, uint32_t virt) {
    page_table_t *table = get_page_table(dir, virt);
    if (!table) return 0;

    uint32_t pte_index = (virt >> 12) & 0x3FF;
    page_table_entry_t pte = table->entries[pte_index];

    if (!(pte & PTE_PRESENT)) return 0;

    return (pte & 0xFFFFF000) | (virt & 0xFFF);
}

page_directory_t *vmm_create_address_space(void) {
    void *dir_phys = pmm_alloc_page();
    if (!dir_phys) return NULL;

    page_directory_t *new_dir = (page_directory_t *)dir_phys;

    for (int i = 0; i < PAGE_ENTRIES; i++) {
        if (kernel_page_directory->entries[i] & PTE_PRESENT) {
            if (i >= (KERNEL_BASE >> 22)) {
                new_dir->entries[i] = kernel_page_directory->entries[i];
            } else {
                new_dir->entries[i] = 0;
            }
        } else {
            new_dir->entries[i] = 0;
        }
    }

    return (page_directory_t *)new_dir;
}

void vmm_destroy_address_space(page_directory_t *dir) {
    for (int i = 0; i < (KERNEL_BASE >> 22); i++) {
        if (dir->entries[i] & PTE_PRESENT) {
            page_table_t *table = (page_table_t *)(dir->entries[i] & 0xFFFFF000);
            for (int j = 0; j < PAGE_ENTRIES; j++) {
                if (table->entries[j] & PTE_PRESENT) {
                    uint32_t phys = table->entries[j] & 0xFFFFF000;
                    pmm_free_page((void *)phys);
                }
            }
            pmm_free_page(table);
            dir->entries[i] = 0;
        }
    }

    pmm_free_page(dir);
}

void *vmm_map_physical(uint32_t phys, uint32_t size) {
    static uint32_t next_virt = 0xFD000000;
    uint32_t start = next_virt;

    for (uint32_t offset = 0; offset < size; offset += PAGE_SIZE) {
        vmm_map_page(kernel_page_directory, next_virt, phys + offset, PTE_PRESENT | PTE_WRITABLE);
        next_virt += PAGE_SIZE;
    }

    return (void *)start;
}

void vmm_unmap_physical(void *virt, uint32_t size) {
    uint32_t addr = (uint32_t)virt;
    for (uint32_t offset = 0; offset < size; offset += PAGE_SIZE) {
        vmm_unmap_page(kernel_page_directory, addr + offset);
    }
}

void *vmm_alloc_pages(uint32_t count, uint32_t flags) {
    static uint32_t heap_virt = 0xE0000000;
    uint32_t start = heap_virt;

    for (uint32_t i = 0; i < count; i++) {
        void *phys = pmm_alloc_page();
        if (!phys) {
            /* Try to evict a page to free memory */
            if (page_replace_evict() == 0) {
                phys = pmm_alloc_page();
            }
            if (!phys) {
                for (uint32_t j = 0; j < i; j++) {
                    vmm_unmap_page(kernel_page_directory, start + j * PAGE_SIZE);
                }
                return NULL;
            }
        }
        vmm_map_page(kernel_page_directory, heap_virt, (uint32_t)phys, flags | PTE_PRESENT | PTE_WRITABLE);
        heap_virt += PAGE_SIZE;
    }

    return (void *)start;
}

void vmm_free_pages(void *addr, uint32_t count) {
    uint32_t virt = (uint32_t)addr;
    for (uint32_t i = 0; i < count; i++) {
        vmm_unmap_page(kernel_page_directory, virt + i * PAGE_SIZE);
    }
}

int vmm_handle_page_fault(uint32_t error_code, uint32_t fault_addr) {
    if (error_code & VMM_PAGE_FAULT_PRESENT) {
        if ((error_code & VMM_PAGE_FAULT_WRITE) &&
            (vmm_get_physical(current_page_directory, fault_addr) & PTE_COW)) {
            return vmm_copy_page_fault_cow(current_page_directory, fault_addr);
        }
        return -1;
    }

    /* Check if the page was swapped out */
    pcb_t *curr = sched_get_current();
    if (curr) {
        page_replace_access(fault_addr & ~0xFFF, (uint32_t)curr->pid);
        /* If page_replace_access brought the page back, it's now mapped */
        if (vmm_get_physical(current_page_directory, fault_addr) != 0) {
            return 0;
        }
    }

    uint32_t phys = (uint32_t)pmm_alloc_page();
    if (!phys) {
        /* Try to evict a page to free memory */
        if (page_replace_evict() == 0) {
            phys = (uint32_t)pmm_alloc_page();
        }
        if (!phys) return -1;
    }

    vmm_map_page(current_page_directory, fault_addr, phys,
                 VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE |
                 ((fault_addr < KERNEL_BASE) ? VMM_PAGE_USER : 0));
    return 0;
}

int vmm_copy_page_fault_cow(page_directory_t *dir, uint32_t addr) {
    addr &= ~0xFFF;
    uint32_t phys_old = vmm_get_physical(dir, addr) & 0xFFFFF000;

    uint32_t phys_new = (uint32_t)pmm_alloc_page();
    if (!phys_new) return -1;

    memcpy((void *)(phys_new + KERNEL_BASE), (void *)(phys_old + KERNEL_BASE), PAGE_SIZE);

    vmm_unmap_page(dir, addr);
    vmm_map_page(dir, addr, phys_new, VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE |
                 ((addr < KERNEL_BASE) ? VMM_PAGE_USER : 0));

    return 0;
}

void vmm_swap_out(void) {
    tick_count++;
}

page_frame_t *vmm_alloc_frame(void) {
    return NULL;
}

void vmm_free_frame(page_frame_t *frame) {
    (void)frame;
}

void vmm_page_ref_inc(page_frame_t *frame) {
    if (frame) {
        frame->ref_count++;
    }
}

void vmm_page_ref_dec(page_frame_t *frame) {
    if (frame && frame->ref_count > 0) {
        frame->ref_count--;
        if (frame->ref_count == 0) {
            vmm_free_frame(frame);
        }
    }
}

int vmm_clone_address_space(page_directory_t *dst, page_directory_t *src) {
    for (int i = 0; i < PAGE_ENTRIES; i++) {
        if (i >= (KERNEL_BASE >> 22)) {
            dst->entries[i] = src->entries[i];
        } else if (src->entries[i] & PTE_PRESENT) {
            page_table_t *src_table = (page_table_t *)(src->entries[i] & 0xFFFFF000);
            page_table_t *dst_table = (page_table_t *)pmm_alloc_page();
            if (!dst_table) return -1;

            for (int j = 0; j < PAGE_ENTRIES; j++) {
                if (src_table->entries[j] & PTE_PRESENT) {
                    uint32_t flags = src_table->entries[j] & 0xFFF;
                    /* Mark both copies as COW + read-only so that
                     * either process writing will trigger a fault
                     * and get a private copy. */
                    flags &= ~PTE_WRITABLE;
                    flags |= PTE_COW;
                    dst_table->entries[j] = (src_table->entries[j] & 0xFFFFF000) | flags;
                    src_table->entries[j] = (src_table->entries[j] & 0xFFFFF000) | flags;
                } else {
                    dst_table->entries[j] = 0;
                }
            }

            dst->entries[i] = ((uint32_t)dst_table) | (src->entries[i] & 0xFFF);
        }
    }
    return 0;
}

void vmm_invalidate_tlb(uint32_t addr) {
    invlpg(addr);
}

void vmm_flush_tlb(void) {
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3));
}

page_directory_t *vmm_get_current_dir(void) {
    return current_page_directory;
}

void vmm_set_current_dir(page_directory_t *dir) {
    current_page_directory = dir;
    load_cr3((uint32_t)dir);
}
