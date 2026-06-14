#include "vmm_cow.h"
#include "vmm.h"
#include "pmm.h"
#include "kheap.h"
#include "string.h"

static uint32_t *page_ref_counts = 0;
static uint32_t ref_count_total_entries = 0;

void vmm_cow_init(void) {
    uint32_t total = pmm_get_total_pages();
    ref_count_total_entries = total;
    page_ref_counts = (uint32_t *)kcalloc(total, sizeof(uint32_t));
}

static void cow_ref_inc(uint32_t phys) {
    uint32_t idx = phys / PMM_PAGE_SIZE;
    if (idx < ref_count_total_entries) {
        page_ref_counts[idx]++;
    }
}

static void cow_ref_dec(uint32_t phys) {
    uint32_t idx = phys / PMM_PAGE_SIZE;
    if (idx < ref_count_total_entries && page_ref_counts[idx] > 0) {
        page_ref_counts[idx]--;
    }
}

static uint32_t cow_get_ref(uint32_t phys) {
    uint32_t idx = phys / PMM_PAGE_SIZE;
    if (idx < ref_count_total_entries) {
        return page_ref_counts[idx];
    }
    return 0;
}

void vmm_cow_fork(page_directory_t *parent_dir, page_directory_t *child_dir) {
    for (uint32_t pd_idx = 0; pd_idx < 1024; pd_idx++) {
        uint32_t pde = parent_dir->entries[pd_idx];
        if (!(pde & VMM_PAGE_PRESENT)) continue;

        page_table_t *parent_pt = (page_table_t *)(pde & 0xFFFFF000);
        if ((uint32_t)parent_pt < VMM_KERNEL_BASE) {
            parent_pt = (page_table_t *)((uint32_t)parent_pt + VMM_KERNEL_BASE);
        }

        for (uint32_t pt_idx = 0; pt_idx < 1024; pt_idx++) {
            uint32_t pte = parent_pt->entries[pt_idx];
            if (!(pte & VMM_PAGE_PRESENT)) continue;

            uint32_t virt_addr = (pd_idx << 22) | (pt_idx << 12);
            uint32_t phys = pte & 0xFFFFF000;

            parent_pt->entries[pt_idx] = pte & ~VMM_PAGE_WRITABLE;

            uint32_t child_flags = VMM_PAGE_PRESENT | VMM_PAGE_USER;
            if (pte & VMM_PAGE_USER) child_flags |= VMM_PAGE_USER;

            vmm_map_page(child_dir, virt_addr, phys, child_flags);

            cow_ref_inc(phys);
        }
    }

    __asm__ volatile("mov %cr3, %eax; mov %eax, %cr3");
}

void vmm_cow_page_fault(uint32_t fault_addr, page_directory_t *dir) {
    uint32_t phys = vmm_get_physical(dir, fault_addr);
    if (!phys) return;

    uint32_t page_addr = fault_addr & ~(PMM_PAGE_SIZE - 1);
    uint32_t ref = cow_get_ref(phys);

    if (ref > 1) {
        void *new_phys = pmm_alloc_page();
        if (!new_phys) return;

        uint32_t src_virt = phys + VMM_KERNEL_BASE;
        uint32_t dst_virt = (uint32_t)new_phys + VMM_KERNEL_BASE;

        vmm_map_page(dir, dst_virt, (uint32_t)new_phys, VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE);

        memcpy((void *)dst_virt, (void *)src_virt, PMM_PAGE_SIZE);

        cow_ref_dec(phys);

        uint32_t flags = VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE | VMM_PAGE_USER;
        vmm_map_page(dir, page_addr, (uint32_t)new_phys, flags);

        cow_ref_inc((uint32_t)new_phys);
    } else {
        uint32_t flags = VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE | VMM_PAGE_USER;
        vmm_map_page(dir, page_addr, phys, flags);
    }
}
