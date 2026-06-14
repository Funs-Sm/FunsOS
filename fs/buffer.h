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

#endif
