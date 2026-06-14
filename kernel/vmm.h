#ifndef VMM_H
#define VMM_H

#include "kernel_mem.h"
#include "sync.h"

#define PAGE_SIZE 4096
#define PAGE_ENTRIES 1024

#define PTE_PRESENT    0x001
#define PTE_WRITABLE 0x002
#define PTE_USER     0x004
#define PTE_WRITETHROUGH 0x008
#define PTE_CACHE_DISABLE 0x010
#define PTE_ACCESSED 0x020
#define PTE_DIRTY    0x040
#define PTE_PAGESIZE 0x080
#define PTE_GLOBAL 0x100
#define PTE_COW     0x200

#ifndef VMM_PAGE_PRESENT
#define VMM_PAGE_PRESENT  PTE_PRESENT
#endif
#ifndef VMM_PAGE_WRITABLE
#define VMM_PAGE_WRITABLE PTE_WRITABLE
#endif
#ifndef VMM_PAGE_USER
#define VMM_PAGE_USER    PTE_USER
#endif
#define VMM_PAGE_COW     PTE_COW

#define KERNEL_BASE 0xC0000000

#define VMM_PAGE_FAULT_PRESENT  0x01
#define VMM_PAGE_FAULT_WRITE    0x02
#define VMM_PAGE_FAULT_USER    0x04
#define VMM_PAGE_FAULT_RESERVED 0x08
#define VMM_PAGE_FAULT_INSTRUCTION 0x10

typedef struct page_frame {
    uint32_t phys_addr;
    uint32_t ref_count;
    uint32_t flags;
    struct page_frame *next;
    struct page_frame *prev;
    uint64_t last_access;
} page_frame_t;

typedef struct {
    page_frame_t *frames;
    uint32_t frame_count;
    uint32_t free_count;
    spinlock_t lock;
} page_frame_manager_t;

typedef struct {
    page_frame_t **pages;
    uint32_t count;
    uint32_t size;
    spinlock_t lock;
} page_cache_t;

typedef struct {
    uint32_t virt_addr;
    uint32_t length;
    uint32_t flags;
    struct vma_struct *next;
    struct vma_struct *prev;
} vma_struct;

vma_struct *vma_create(uint32_t start, uint32_t length, uint32_t flags);
void vma_destroy(vma_struct *vma);
vma_struct *vma_find(page_directory_t *dir, uint32_t addr);

void init_vmm(void);
void vmm_map_page(page_directory_t *dir, uint32_t virt, uint32_t phys, uint32_t flags);
void vmm_unmap_page(page_directory_t *dir, uint32_t virt);
uint32_t vmm_get_physical(page_directory_t *dir, uint32_t virt);
page_directory_t *vmm_create_address_space(void);
void vmm_destroy_address_space(page_directory_t *dir);
void *vmm_map_physical(uint32_t phys, uint32_t size);
void vmm_unmap_physical(void *virt, uint32_t size);
void *vmm_alloc_pages(uint32_t count, uint32_t flags);
void vmm_free_pages(void *addr, uint32_t count);
int vmm_handle_page_fault(uint32_t error_code, uint32_t fault_addr);
int vmm_copy_page_fault_cow(page_directory_t *dir, uint32_t addr);
void vmm_swap_out(void);
page_frame_t *vmm_alloc_frame(void);
void vmm_free_frame(page_frame_t *frame);
void vmm_page_ref_inc(page_frame_t *frame);
void vmm_page_ref_dec(page_frame_t *frame);
int vmm_clone_address_space(page_directory_t *dst, page_directory_t *src);
void vmm_invalidate_tlb(uint32_t addr);
void vmm_flush_tlb(void);
page_directory_t *vmm_get_current_dir(void);
void vmm_set_current_dir(page_directory_t *dir);

#endif
