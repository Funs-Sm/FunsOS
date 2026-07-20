#ifndef SLAB_H
#define SLAB_H

#include "stdint.h"

#define SLAB_NAME_LEN 32
#define MAX_SLAB_CACHES 32
#define SLAB_MIN_SIZE   8
#define SLAB_MAX_SIZE   4096
#define SLAB_MAGIC      0x534C4142

struct slab;

struct slab_bufctl {
    struct slab_bufctl *next;
};

struct slab {
    struct slab *next;
    uint32_t free_count;
    uint32_t total;
    void *objects;
    struct slab_bufctl *free_list;
    uint8_t *bitmap;
};

struct kmem_cache {
    char name[SLAB_NAME_LEN];
    uint32_t obj_size;
    uint32_t align;
    uint32_t objs_per_slab;
    uint32_t slab_count;
    uint32_t total_objects;
    uint32_t inuse;
    uint32_t alloc_count;
    uint32_t free_count;
    struct slab *slabs_full;
    struct slab *slabs_partial;
    struct slab *slabs_free;
    uint32_t flags;
};

#define SLAB_HWCACHE_ALIGN  0x0001
#define SLAB_CACHE_DMA      0x0002
#define SLAB_PANIC          0x0004
#define SLAB_RECLAIM_ACCOUNT 0x0008

int slab_init(void);

struct kmem_cache *kmem_cache_create(const char *name, uint32_t size,
                                     uint32_t align, uint32_t flags);
void kmem_cache_destroy(struct kmem_cache *cache);
void *kmem_cache_alloc(struct kmem_cache *cache);
void kmem_cache_free(struct kmem_cache *cache, void *obj);
void *kmem_cache_zalloc(struct kmem_cache *cache);

void *kmalloc_slab(uint32_t size);
void kfree_slab(void *ptr);
uint32_t kmalloc_slab_size(void *ptr);

void kmem_cache_shrink(struct kmem_cache *cache);
void slab_print_stats(void);
void kmem_cache_info(struct kmem_cache *cache);

extern struct kmem_cache *slab_cache_pcb;
extern struct kmem_cache *slab_cache_fd;
extern struct kmem_cache *slab_cache_inode;
extern struct kmem_cache *slab_cache_dentry;
extern struct kmem_cache *slab_cache_skb;

#endif
