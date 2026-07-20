#include "vmalloc.h"
#include "klog.h"
#include "string.h"
#include "kheap.h"

static struct vm_struct *vm_list;
static uint8_t vmalloc_initialized;
static uint32_t total_vmalloced;
static uint32_t total_vfreed;
static uint32_t vmalloc_count;
static uint32_t vfree_count;
static uint32_t vmap_count;
static uint32_t vunmap_count;

uint32_t vmap_stack_pfn;

static uintptr_t next_vmalloc_addr;

static struct vm_struct *vm_area_alloc(void) {
    struct vm_struct *area = (struct vm_struct *)kmalloc(sizeof(struct vm_struct));
    if (!area) return NULL;
    memset(area, 0, sizeof(*area));
    return area;
}

static void vm_area_free(struct vm_struct *area) {
    if (area) kfree(area);
}

static int overlaps(uintptr_t start, uintptr_t end) {
    struct vm_struct *tmp = vm_list;
    while (tmp) {
        uintptr_t a_start = (uintptr_t)tmp->addr;
        uintptr_t a_end = a_start + tmp->size;
        if (!(end <= a_start || start >= a_end)) return 1;
        tmp = tmp->next;
    }
    return 0;
}

int vmalloc_init(void) {
    if (vmalloc_initialized) return 0;

    vm_list = NULL;
    next_vmalloc_addr = VMALLOC_START;
    total_vmalloced = 0;
    total_vfreed = 0;
    vmalloc_count = 0;
    vfree_count = 0;
    vmap_count = 0;
    vunmap_count = 0;

    vmap_stack_pfn = 0;

    vmalloc_initialized = 1;
    klog_info("Vmalloc allocator initialized (0x%x-0x%x)", VMALLOC_START, VMALLOC_END);
    return 0;
}

struct vm_struct *get_vm_area(uint32_t size, uint32_t flags) {
    if (!vmalloc_initialized || size == 0) return NULL;

    size = (size + 4095) & ~4095;

    struct vm_struct *area = vm_area_alloc();
    if (!area) return NULL;

    uintptr_t addr = next_vmalloc_addr;
    int tries = 0;
    while (addr + size <= VMALLOC_END && tries < 1024) {
        if (!overlaps(addr, addr + size)) break;
        addr += 4096;
        tries++;
    }

    if (addr + size > VMALLOC_END || tries >= 1024) {
        vm_area_free(area);
        return NULL;
    }

    area->addr = (void *)addr;
    area->size = size;
    area->flags = flags;
    area->nr_pages = 0;
    area->pages = NULL;
    memset(area->name, 0, VMALLOC_NAME_LEN);
    strncpy(area->name, "vmalloc", VMALLOC_NAME_LEN - 1);

    area->next = vm_list;
    vm_list = area;

    if (addr + size > next_vmalloc_addr) {
        next_vmalloc_addr = addr + size;
    }

    return area;
}

struct vm_struct *find_vm_area(void *addr) {
    if (!vmalloc_initialized || !addr) return NULL;

    struct vm_struct *tmp = vm_list;
    while (tmp) {
        if (tmp->addr == addr) return tmp;
        tmp = tmp->next;
    }
    return NULL;
}

void free_vm_area(struct vm_struct *area) {
    if (!area) return;

    struct vm_struct *prev = NULL;
    struct vm_struct *tmp = vm_list;
    while (tmp) {
        if (tmp == area) {
            if (prev) prev->next = tmp->next;
            else vm_list = tmp->next;
            if (area->pages) kfree(area->pages);
            vm_area_free(area);
            return;
        }
        prev = tmp;
        tmp = tmp->next;
    }
}

void *vmalloc(uint32_t size) {
    if (!vmalloc_initialized || size == 0) return NULL;

    struct vm_struct *area = get_vm_area(size, VM_ALLOC);
    if (!area) return NULL;

    void *mem = kmalloc(size);
    if (!mem) {
        free_vm_area(area);
        return NULL;
    }
    memset(mem, 0, size);

    area->pages = NULL;
    area->nr_pages = 0;

    total_vmalloced += size;
    vmalloc_count++;
    return area->addr;
}

void *vmalloc_user(uint32_t size) {
    void *addr = vmalloc(size);
    if (addr) {
        struct vm_struct *area = find_vm_area(addr);
        if (area) {
            area->flags |= VM_USERMAP;
            strncpy(area->name, "vmalloc_user", VMALLOC_NAME_LEN - 1);
        }
    }
    return addr;
}

void *vzalloc(uint32_t size) {
    return vmalloc(size);
}

void vfree(void *addr) {
    if (!vmalloc_initialized || !addr) return;

    struct vm_struct *area = find_vm_area(addr);
    if (!area) return;

    total_vfreed += area->size;
    vfree_count++;

    free_vm_area(area);
}

void *vmap(uint32_t **pages, uint32_t count, uint32_t flags, const char *name) {
    if (!vmalloc_initialized || !pages || count == 0) return NULL;

    struct vm_struct *area = get_vm_area(count * 4096, flags | VM_MAP);
    if (!area) return NULL;

    area->pages = (uint32_t **)kmalloc(count * sizeof(uint32_t *));
    if (!area->pages) {
        free_vm_area(area);
        return NULL;
    }
    memcpy(area->pages, pages, count * sizeof(uint32_t *));
    area->nr_pages = count;

    if (name) strncpy(area->name, name, VMALLOC_NAME_LEN - 1);

    vmap_count++;
    return area->addr;
}

void vunmap(void *addr) {
    if (!vmalloc_initialized || !addr) return;

    struct vm_struct *area = find_vm_area(addr);
    if (!area) return;

    vunmap_count++;
    free_vm_area(area);
}

void vmalloc_print_stats(void) {
    klog_info("=== Vmalloc Virtual Memory Allocator Statistics ===");
    klog_info("Vmalloc range: 0x%x - 0x%x (%u MB)", VMALLOC_START, VMALLOC_END, VMALLOC_AREA_SIZE / 1048576);
    klog_info("Next addr: 0x%x", next_vmalloc_addr);
    klog_info("Total allocations: %u", vmalloc_count);
    klog_info("Total frees: %u", vfree_count);
    klog_info("Total vmalloced: %u bytes (%u KB)", total_vmalloced, total_vmalloced / 1024);
    klog_info("Total vfreed: %u bytes (%u KB)", total_vfreed, total_vfreed / 1024);
    klog_info("vmap calls: %u", vmap_count);
    klog_info("vunmap calls: %u", vunmap_count);

    klog_info("Active vmalloc areas:");
    struct vm_struct *tmp = vm_list;
    int i = 0;
    while (tmp) {
        klog_info("  [%d] addr=0x%x size=%u flags=0x%x name=%s",
                 i++, tmp->addr, tmp->size, tmp->flags, tmp->name);
        tmp = tmp->next;
    }
}
