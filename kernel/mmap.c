#include "mmap.h"
#include "kheap.h"
#include "string.h"
#include "sync.h"
#include "pmm.h"
#include "vmm.h"
#include "vfs.h"
#include "process.h"

/* Start of mmap region in virtual address space */
#define MMAP_REGION_START 0x40000000
#define MMAP_REGION_END   0xBFFFF000

static mmap_region_t *region_list = NULL;
static spinlock_t mmap_lock;
static uint32_t next_vaddr = MMAP_REGION_START;

void mmap_init(void) {
    region_list = NULL;
    next_vaddr = MMAP_REGION_START;
    spinlock_init(&mmap_lock);
}

/* Find a free virtual address range of the given size */
static uint32_t find_free_vaddr(uint32_t size) {
    /* Align to page boundary */
    uint32_t aligned_size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    spinlock_lock(&mmap_lock);

    /* Simple first-fit search using next_vaddr as a hint */
    uint32_t candidate = next_vaddr;

    /* Try starting from the hint */
    if (candidate + aligned_size <= MMAP_REGION_END) {
        int conflict = 0;
        mmap_region_t *cur = region_list;
        while (cur) {
            uint32_t reg_end = cur->start_vaddr + cur->size;
            if (!(candidate + aligned_size <= cur->start_vaddr ||
                  candidate >= reg_end)) {
                conflict = 1;
                break;
            }
            cur = cur->next;
        }
        if (!conflict) {
            next_vaddr = candidate + aligned_size;
            spinlock_unlock(&mmap_lock);
            return candidate;
        }
    }

    /* Linear scan from the beginning */
    candidate = MMAP_REGION_START;
    while (candidate + aligned_size <= MMAP_REGION_END) {
        int conflict = 0;
        mmap_region_t *cur = region_list;
        while (cur) {
            uint32_t reg_end = cur->start_vaddr + cur->size;
            if (!(candidate + aligned_size <= cur->start_vaddr ||
                  candidate >= reg_end)) {
                conflict = 1;
                candidate = reg_end;  /* Skip past this region */
                break;
            }
            cur = cur->next;
        }
        if (!conflict) {
            next_vaddr = candidate + aligned_size;
            spinlock_unlock(&mmap_lock);
            return candidate;
        }
        candidate += PAGE_SIZE;
    }

    spinlock_unlock(&mmap_lock);
    return 0;  /* No free range found */
}

void *mmap(void *addr, uint32_t length, uint32_t prot, uint32_t flags,
           uint32_t fd, uint32_t offset) {
    if (length == 0) return MAP_FAILED;

    uint32_t aligned_size = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint32_t vaddr;

    if (flags & MAP_FIXED) {
        if (!addr) return MAP_FAILED;
        vaddr = (uint32_t)addr;
        /* Unmap any existing regions at this address */
        munmap(addr, aligned_size);
    } else {
        if (addr) {
            vaddr = (uint32_t)addr;
        } else {
            vaddr = find_free_vaddr(aligned_size);
            if (vaddr == 0) return MAP_FAILED;
        }
    }

    /* Create the region descriptor */
    mmap_region_t *region = (mmap_region_t *)kmalloc(sizeof(mmap_region_t));
    if (!region) return MAP_FAILED;
    memset(region, 0, sizeof(mmap_region_t));

    region->start_vaddr = vaddr;
    region->size = aligned_size;
    region->prot = prot;
    region->flags = flags;
    region->file_offset = (uint64_t)offset;
    region->pid = 0;  /* Will be set by caller if needed */

    /* For file-backed mapping, store the inode number */
    if (!(flags & MAP_ANONYMOUS) && fd != 0xFFFFFFFF) {
        /* In a full implementation, we'd look up the inode from fd.
           For now, store fd as inode_num placeholder. */
        region->inode_num = fd;
    } else {
        region->inode_num = 0;
    }

    /* Pages are mapped lazily on page fault, so no page table changes here */

    spinlock_lock(&mmap_lock);
    region->next = region_list;
    region_list = region;
    spinlock_unlock(&mmap_lock);

    return (void *)vaddr;
}

int munmap(void *addr, uint32_t length) {
    if (!addr) return -1;

    uint32_t vaddr = (uint32_t)addr;
    uint32_t aligned_size = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    spinlock_lock(&mmap_lock);

    mmap_region_t **pp = &region_list;
    int found = 0;
    while (*pp) {
        mmap_region_t *reg = *pp;
        if (reg->start_vaddr == vaddr && reg->size == aligned_size) {
            /* Exact match - unmap all pages */
            uint32_t page;
            for (page = reg->start_vaddr; page < reg->start_vaddr + reg->size; page += PAGE_SIZE) {
                /* Get current page directory and unmap */
                page_directory_t *dir = vmm_get_current_dir();
                uint32_t phys = vmm_get_physical(dir, page);
                if (phys) {
                    vmm_unmap_page(dir, page);
                    /* Free the physical frame */
                    pmm_free_page((void *)phys);
                }
            }
            *pp = reg->next;
            kfree(reg);
            found = 1;
            break;
        } else if (reg->start_vaddr <= vaddr &&
                   vaddr + aligned_size <= reg->start_vaddr + reg->size) {
            /* Partial unmap - split the region */
            /* For simplicity, just unmap the pages but keep the region */
            uint32_t page;
            for (page = vaddr; page < vaddr + aligned_size; page += PAGE_SIZE) {
                page_directory_t *dir = vmm_get_current_dir();
                uint32_t phys = vmm_get_physical(dir, page);
                if (phys) {
                    vmm_unmap_page(dir, page);
                    pmm_free_page((void *)phys);
                }
            }
            found = 1;
            break;
        }
        pp = &(*pp)->next;
    }

    spinlock_unlock(&mmap_lock);
    return found ? 0 : -1;
}

int mprotect(void *addr, uint32_t length, uint32_t prot) {
    if (!addr) return -1;

    uint32_t vaddr = (uint32_t)addr;

    spinlock_lock(&mmap_lock);

    mmap_region_t *cur = region_list;
    while (cur) {
        if (cur->start_vaddr <= vaddr &&
            vaddr + length <= cur->start_vaddr + cur->size) {
            cur->prot = prot;

            /* Update page table flags for affected pages */
            uint32_t page;
            for (page = vaddr; page < vaddr + length; page += PAGE_SIZE) {
                page_directory_t *dir = vmm_get_current_dir();
                uint32_t phys = vmm_get_physical(dir, page);
                if (phys) {
                    uint32_t flags = PTE_PRESENT | PTE_USER;
                    if (prot & PROT_WRITE) flags |= PTE_WRITABLE;
                    if (!(prot & PROT_EXEC)) flags |= PTE_CACHE_DISABLE;
                    vmm_map_page(dir, page, phys, flags);
                    vmm_invalidate_tlb(page);
                }
            }

            spinlock_unlock(&mmap_lock);
            return 0;
        }
        cur = cur->next;
    }

    spinlock_unlock(&mmap_lock);
    return -1;
}

int msync(void *addr, uint32_t length, uint32_t flags) {
    if (!addr) return -1;

    uint32_t vaddr = (uint32_t)addr;

    spinlock_lock(&mmap_lock);

    mmap_region_t *cur = region_list;
    while (cur) {
        if (cur->start_vaddr <= vaddr &&
            vaddr + length <= cur->start_vaddr + cur->size) {
            /* Only sync MAP_SHARED file-backed mappings */
            if ((cur->flags & MAP_SHARED) && cur->inode_num != 0) {
                /* Write dirty pages back to file */
                uint32_t page;
                for (page = cur->start_vaddr;
                     page < cur->start_vaddr + cur->size;
                     page += PAGE_SIZE) {
                    page_directory_t *dir = vmm_get_current_dir();
                    uint32_t phys = vmm_get_physical(dir, page);
                    if (phys) {
                        /* Check if page is dirty (PTE_DIRTY flag) */
                        /* In a full implementation, we would write the page
                           back to the file via VFS. For now, this is a stub
                           that acknowledges the sync request. */
                    }
                }
            }
            spinlock_unlock(&mmap_lock);
            return 0;
        }
        cur = cur->next;
    }

    spinlock_unlock(&mmap_lock);
    return -1;
}

void mmap_handle_page_fault(uint32_t vaddr, uint32_t pid) {
    spinlock_lock(&mmap_lock);

    mmap_region_t *cur = region_list;
    while (cur) {
        if (vaddr >= cur->start_vaddr &&
            vaddr < cur->start_vaddr + cur->size) {
            /* Found the region containing the faulting address */

            /* Allocate a physical page */
            void *phys = pmm_alloc_page();
            if (!phys) {
                spinlock_unlock(&mmap_lock);
                return;
            }

            /* Zero the page first */
            memset((void *)((uint32_t)phys + KERNEL_BASE), 0, PAGE_SIZE);

            /* For file-backed mappings, read from file */
            if (!(cur->flags & MAP_ANONYMOUS) && cur->inode_num != 0) {
                uint32_t page_offset = vaddr - cur->start_vaddr;
                uint64_t file_off = cur->file_offset + page_offset;

                /* Open the file and read the relevant page */
                /* In a full implementation, we would use the inode to
                   locate and read the file data. For now, the page
                   remains zero-filled for anonymous or as a fallback. */
                (void)file_off;  /* Suppress unused warning */
            }

            /* Map the page into the process's address space */
            page_directory_t *dir = vmm_get_current_dir();
            uint32_t pte_flags = PTE_PRESENT | PTE_USER;
            if (cur->prot & PROT_WRITE) pte_flags |= PTE_WRITABLE;
            if (!(cur->prot & PROT_EXEC)) pte_flags |= PTE_CACHE_DISABLE;

            vmm_map_page(dir, vaddr, (uint32_t)phys, pte_flags);
            vmm_invalidate_tlb(vaddr);

            spinlock_unlock(&mmap_lock);
            return;
        }
        cur = cur->next;
    }

    spinlock_unlock(&mmap_lock);
    /* No mmap region found for this address - let the normal page fault
       handler deal with it */
}

void mmap_release_all(uint32_t pid) {
    spinlock_lock(&mmap_lock);

    mmap_region_t **pp = &region_list;
    while (*pp) {
        mmap_region_t *reg = *pp;
        if (reg->pid == pid) {
            /* Unmap all pages in this region */
            uint32_t page;
            for (page = reg->start_vaddr; page < reg->start_vaddr + reg->size; page += PAGE_SIZE) {
                page_directory_t *dir = vmm_get_current_dir();
                uint32_t phys = vmm_get_physical(dir, page);
                if (phys) {
                    vmm_unmap_page(dir, page);
                    pmm_free_page((void *)phys);
                }
            }
            *pp = reg->next;
            kfree(reg);
        } else {
            pp = &(*pp)->next;
        }
    }

    spinlock_unlock(&mmap_lock);
}
