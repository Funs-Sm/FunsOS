#include "kheap.h"
#include "vmm.h"
#include "pmm.h"
#include "sync.h"
#include "stddef.h"
#include "klog.h"

#define HEAP_BLOCK_MAGIC 0xCAFE
#define ALIGNMENT 8
#define PAGE_SIZE 4096
#define MIN_BLOCK_SIZE sizeof(heap_header_t)

typedef struct heap_header {
    uint16_t magic;
    uint16_t padding;
    uint32_t size;
    uint8_t is_free;
    uint8_t pad[3];
    struct heap_header *next;
} heap_header_t;

static uint32_t heap_start = 0;
static uint32_t heap_end = 0;
static uint32_t heap_current = 0;
static heap_header_t *heap_first = NULL;
static spinlock_t heap_lock;

static uint32_t align_up(uint32_t value, uint32_t align) {
    return (value + align - 1) & ~(align - 1);
}

static void expand_heap(uint32_t size) {
    uint32_t needed = align_up(size + sizeof(heap_header_t), PAGE_SIZE);
    uint32_t pages = needed / PAGE_SIZE;

    for (uint32_t i = 0; i < pages; i++) {
        void *phys = pmm_alloc_page();
        if (!phys) return;

        vmm_map_page(vmm_get_current_dir(), heap_current, (uint32_t)phys, 0x003);

        uint8_t *ptr = (uint8_t *)heap_current;
        for (uint32_t j = 0; j < PAGE_SIZE; j++) {
            ptr[j] = 0;
        }

        heap_current += PAGE_SIZE;
    }

    heap_end = heap_current;
}

void kheap_init(uint32_t start, uint32_t size) {
    spinlock_init(&heap_lock);
    heap_start = start;
    heap_current = start;
    heap_end = start + size;

    uint32_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    klog_info("kheap: init start pages=%u", pages);
    for (uint32_t i = 0; i < pages; i++) {
        void *phys = pmm_alloc_page();
        if (!phys) {
            klog_info("kheap: pmm_alloc_page returned NULL at i=%u", i);
            return;
        }
        vmm_map_page(vmm_get_current_dir(), heap_current, (uint32_t)phys, 0x003);
        heap_current += PAGE_SIZE;
        if ((i % 1024) == 0) {
            klog_info("kheap: mapped %u/%u", i, pages);
        }
    }

    heap_first = (heap_header_t *)heap_start;
    heap_first->magic = HEAP_BLOCK_MAGIC;
    heap_first->size = size - sizeof(heap_header_t);
    heap_first->is_free = 1;
    heap_first->next = NULL;
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;

    size = align_up(size, ALIGNMENT);

    spinlock_lock(&heap_lock);

    heap_header_t *current = heap_first;
    while (current) {
        if (current->magic != HEAP_BLOCK_MAGIC) {
            spinlock_unlock(&heap_lock);
            return NULL;
        }

        if (current->is_free && current->size >= size) {
            if (current->size > size + sizeof(heap_header_t) + ALIGNMENT) {
                heap_header_t *new_block = (heap_header_t *)((uint32_t)current + sizeof(heap_header_t) + size);
                new_block->magic = HEAP_BLOCK_MAGIC;
                new_block->size = current->size - size - sizeof(heap_header_t);
                new_block->is_free = 1;
                new_block->next = current->next;

                current->size = size;
                current->next = new_block;
            }

            current->is_free = 0;
            spinlock_unlock(&heap_lock);

            void *ptr = (void *)((uint32_t)current + sizeof(heap_header_t));
            return ptr;
        }

        current = current->next;
    }

    expand_heap(size);

    heap_header_t *new_block = (heap_header_t *)heap_current;
    new_block->magic = HEAP_BLOCK_MAGIC;
    new_block->size = size;
    new_block->is_free = 0;
    new_block->next = NULL;

    current = heap_first;
    while (current->next) {
        current = current->next;
    }
    current->next = new_block;

    spinlock_unlock(&heap_lock);

    void *ptr = (void *)((uint32_t)new_block + sizeof(heap_header_t));
    return ptr;
}

void kfree(void *ptr) {
    if (!ptr) return;

    heap_header_t *header = (heap_header_t *)((uint32_t)ptr - sizeof(heap_header_t));

    if (header->magic != HEAP_BLOCK_MAGIC) return;

    spinlock_lock(&heap_lock);

    header->is_free = 1;

    if (header->next && header->next->magic == HEAP_BLOCK_MAGIC && header->next->is_free) {
        header->size += sizeof(heap_header_t) + header->next->size;
        header->next = header->next->next;
    }

    heap_header_t *prev = NULL;
    heap_header_t *current = heap_first;

    while (current && current != header) {
        prev = current;
        current = current->next;
    }

    if (prev && prev->magic == HEAP_BLOCK_MAGIC && prev->is_free) {
        prev->size += sizeof(heap_header_t) + header->size;
        prev->next = header->next;
    }

    spinlock_unlock(&heap_lock);
}

void *kcalloc(size_t num, size_t size) {
    size_t total = num * size;
    void *ptr = kmalloc(total);
    if (!ptr) return NULL;

    uint8_t *buf = (uint8_t *)ptr;
    for (size_t i = 0; i < total; i++) {
        buf[i] = 0;
    }

    return ptr;
}

void *krealloc(void *ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    if (size == 0) {
        kfree(ptr);
        return NULL;
    }

    heap_header_t *header = (heap_header_t *)((uint32_t)ptr - sizeof(heap_header_t));
    if (header->magic != HEAP_BLOCK_MAGIC) return NULL;

    if (header->size >= size) return ptr;

    void *new_ptr = kmalloc(size);
    if (!new_ptr) return NULL;

    uint8_t *src = (uint8_t *)ptr;
    uint8_t *dst = (uint8_t *)new_ptr;
    for (size_t i = 0; i < header->size; i++) {
        dst[i] = src[i];
    }

    kfree(ptr);
    return new_ptr;
}

uint32_t kheap_get_start(void) {
    return heap_start;
}

uint32_t kheap_get_current(void) {
    return heap_current;
}

uint32_t kheap_get_used(void) {
    uint32_t used = 0;
    heap_header_t *cur = heap_first;

    spinlock_lock(&heap_lock);
    while (cur) {
        if (cur->magic == HEAP_BLOCK_MAGIC && !cur->is_free)
            used += cur->size;
        cur = cur->next;
    }
    spinlock_unlock(&heap_lock);

    return used;
}
