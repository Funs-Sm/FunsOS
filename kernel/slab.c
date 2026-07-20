#include "slab.h"
#include "kheap.h"
#include "pmm.h"
#include "string.h"
#include "klog.h"
#include "sync.h"
#include "stdio.h"

#define SLAB_PAGE_SIZE 4096
#define BITMAP_WORDS (SLAB_PAGE_SIZE / (8 * 4))

static struct kmem_cache cache_pool[MAX_SLAB_CACHES];
static uint8_t slab_initialized = 0;
static uint32_t cache_count;
static spinlock_t slab_lock;

struct kmem_cache *slab_cache_pcb;
struct kmem_cache *slab_cache_fd;
struct kmem_cache *slab_cache_inode;
struct kmem_cache *slab_cache_dentry;
struct kmem_cache *slab_cache_skb;

static uint32_t slab_align_size(uint32_t size, uint32_t align) {
    if (align == 0) align = sizeof(void *);
    return (size + align - 1) & ~(align - 1);
}

static int slab_grow(struct kmem_cache *cache) {
    uint32_t objs_per_slab = cache->objs_per_slab;
    uint32_t obj_size = cache->obj_size;
    uint32_t total_mem = SLAB_PAGE_SIZE;
    uint32_t bitmap_bytes = (objs_per_slab + 7) / 8;

    struct slab *s = (struct slab *)kmalloc(sizeof(struct slab) + bitmap_bytes);
    if (!s) return -1;
    memset(s, 0, sizeof(*s) + bitmap_bytes);
    s->bitmap = (uint8_t *)(s + 1);

    s->objects = kmalloc(total_mem);
    if (!s->objects) { kfree(s); return -1; }
    memset(s->objects, 0, total_mem);

    s->total = objs_per_slab;
    s->free_count = objs_per_slab;
    s->free_list = NULL;

    uint8_t *mem = (uint8_t *)s->objects;
    for (int i = objs_per_slab - 1; i >= 0; i--) {
        struct slab_bufctl *bufctl = (struct slab_bufctl *)(mem + i * obj_size);
        bufctl->next = s->free_list;
        s->free_list = bufctl;
    }

    s->next = cache->slabs_partial;
    cache->slabs_partial = s;
    cache->slab_count++;
    cache->total_objects += objs_per_slab;
    return 0;
}

int slab_init(void) {
    if (slab_initialized) return 0;
    spinlock_init(&slab_lock);
    memset(cache_pool, 0, sizeof(cache_pool));
    cache_count = 0;

    slab_cache_pcb = kmem_cache_create("pcb", 512, 32, SLAB_HWCACHE_ALIGN);
    slab_cache_fd = kmem_cache_create("filp", 64, sizeof(void*), 0);
    slab_cache_inode = kmem_cache_create("inode_cache", 128, sizeof(void*), 0);
    slab_cache_dentry = kmem_cache_create("dentry", 96, sizeof(void*), 0);
    slab_cache_skb = kmem_cache_create("sk_buff", 256, 32, SLAB_HWCACHE_ALIGN);

    slab_initialized = 1;
    klog_info("Slab allocator initialized (%u caches)", cache_count);
    return 0;
}

struct kmem_cache *kmem_cache_create(const char *name, uint32_t size,
                                     uint32_t align, uint32_t flags) {
    if (!name || size == 0 || cache_count >= MAX_SLAB_CACHES) return NULL;
    if (size < sizeof(struct slab_bufctl)) size = sizeof(struct slab_bufctl);

    struct kmem_cache *cache = &cache_pool[cache_count++];
    memset(cache, 0, sizeof(*cache));
    strncpy(cache->name, name, sizeof(cache->name) - 1);
    cache->obj_size = slab_align_size(size, align);
    cache->align = align ? align : sizeof(void *);
    cache->flags = flags;
    cache->objs_per_slab = (SLAB_PAGE_SIZE) / cache->obj_size;
    if (cache->objs_per_slab < 1) cache->objs_per_slab = 1;
    cache->slabs_full = NULL;
    cache->slabs_partial = NULL;
    cache->slabs_free = NULL;
    cache->slab_count = 0;
    cache->total_objects = 0;
    cache->inuse = 0;
    cache->alloc_count = 0;
    cache->free_count = 0;

    slab_grow(cache);
    return cache;
}

void kmem_cache_destroy(struct kmem_cache *cache) {
    if (!cache) return;
    struct slab *slabs[3] = { cache->slabs_full, cache->slabs_partial, cache->slabs_free };
    for (int i = 0; i < 3; i++) {
        struct slab *s = slabs[i];
        while (s) {
            struct slab *next = s->next;
            kfree(s->objects);
            kfree(s);
            s = next;
        }
    }
    cache->slabs_full = NULL;
    cache->slabs_partial = NULL;
    cache->slabs_free = NULL;
}

void *kmem_cache_alloc(struct kmem_cache *cache) {
    if (!cache) return NULL;
    spinlock_lock(&slab_lock);

    if (!cache->slabs_partial) {
        if (slab_grow(cache) != 0) {
            spinlock_unlock(&slab_lock);
            return NULL;
        }
    }

    struct slab *s = cache->slabs_partial;
    struct slab_bufctl *bufctl = s->free_list;
    s->free_list = bufctl->next;
    s->free_count--;

    void *obj = (void *)bufctl;
    cache->inuse++;
    cache->alloc_count++;

    if (s->free_count == 0) {
        cache->slabs_partial = s->next;
        s->next = cache->slabs_full;
        cache->slabs_full = s;
    }

    spinlock_unlock(&slab_lock);
    return obj;
}

void kmem_cache_free(struct kmem_cache *cache, void *obj) {
    if (!cache || !obj) return;
    spinlock_lock(&slab_lock);

    struct slab_bufctl *bufctl = (struct slab_bufctl *)obj;
    bufctl->next = NULL;

    struct slab *prev = NULL;
    struct slab *s = cache->slabs_full;
    int was_full = 1;
    while (s) {
        uintptr_t obj_addr = (uintptr_t)obj;
        uintptr_t slab_start = (uintptr_t)s->objects;
        uintptr_t slab_end = slab_start + SLAB_PAGE_SIZE;
        if (obj_addr >= slab_start && obj_addr < slab_end) {
            break;
        }
        prev = s;
        s = s->next;
    }

    if (!s) {
        was_full = 0;
        prev = NULL;
        s = cache->slabs_partial;
        while (s) {
            uintptr_t obj_addr = (uintptr_t)obj;
            uintptr_t slab_start = (uintptr_t)s->objects;
            uintptr_t slab_end = slab_start + SLAB_PAGE_SIZE;
            if (obj_addr >= slab_start && obj_addr < slab_end) break;
            prev = s;
            s = s->next;
        }
    }

    if (!s) {
        spinlock_unlock(&slab_lock);
        return;
    }

    bufctl->next = s->free_list;
    s->free_list = bufctl;
    s->free_count++;
    cache->inuse--;
    cache->free_count++;

    if (was_full) {
        if (prev) prev->next = s->next;
        else cache->slabs_full = s->next;
        s->next = cache->slabs_partial;
        cache->slabs_partial = s;
    }

    spinlock_unlock(&slab_lock);
}

void *kmem_cache_zalloc(struct kmem_cache *cache) {
    void *obj = kmem_cache_alloc(cache);
    if (obj) memset(obj, 0, cache->obj_size);
    return obj;
}

void *kmalloc_slab(uint32_t size) {
    (void)size;
    return kmalloc(size);
}

void kfree_slab(void *ptr) {
    if (ptr) kfree(ptr);
}

uint32_t kmalloc_slab_size(void *ptr) {
    (void)ptr;
    return 0;
}

void kmem_cache_shrink(struct kmem_cache *cache) {
    if (!cache) return;
    struct slab *s = cache->slabs_free;
    while (s) {
        struct slab *next = s->next;
        kfree(s->objects);
        kfree(s);
        s = next;
    }
    cache->slabs_free = NULL;
}

void kmem_cache_info(struct kmem_cache *cache) {
    if (!cache) return;
    klog_info("  [%s] obj=%u slab=%u total=%u inuse=%u alloc=%u free=%u",
             cache->name, cache->obj_size, cache->slab_count,
             cache->total_objects, cache->inuse, cache->alloc_count, cache->free_count);
}

void slab_print_stats(void) {
    klog_info("=== Slab Allocator Statistics ===");
    klog_info("Total caches: %u", cache_count);
    for (uint32_t i = 0; i < cache_count; i++) {
        kmem_cache_info(&cache_pool[i]);
    }
}
