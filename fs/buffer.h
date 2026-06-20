#ifndef BUFFER_H
#define BUFFER_H

#include "stdint.h"

#define CACHE_HASH_SIZE 1024

typedef struct cache_entry_t {
    uint32_t block_num;
    void *data;
    uint8_t dirty;
    uint8_t valid;
    uint32_t ref_count;
    uint32_t access_time;
    struct cache_entry_t *hash_next;
    struct cache_entry_t *hash_prev;
    struct cache_entry_t *lru_next;
    struct cache_entry_t *lru_prev;
} cache_entry_t;

void cache_init(void);
cache_entry_t *cache_lookup(uint32_t block);
int32_t cache_insert(uint32_t block, void *data);
int32_t cache_read(uint32_t block, void *buf);
int32_t cache_write(uint32_t block, const void *buf);
int32_t cache_flush(uint32_t block);
int32_t cache_flush_all(void);
void cache_evict(void);

/* 缓存统计结构体 */
typedef struct cache_stats {
    uint32_t hits;            /* 缓存命中次数 */
    uint32_t misses;          /* 缓存未命中次数 */
    uint32_t entries;         /* 当前缓存条目数 */
    uint32_t hit_rate;        /* 命中率（百分比） */
} cache_stats_t;

/* 缓存高级操作 */
cache_stats_t *cache_get_stats(void);
int32_t cache_prefetch(uint32_t block_num);
void cache_invalidate(uint32_t block_num);
void cache_set_max_entries(uint32_t max);

#endif
