#ifndef BLOCK_CACHE_H
#define BLOCK_CACHE_H

#include "stdint.h"

#define BLOCK_CACHE_SIZE 1024

typedef struct block_cache_entry_t {
    uint32_t block_num;
    void *data;
    uint8_t dirty;
    uint8_t valid;
    struct block_cache_entry_t *next;
    struct block_cache_entry_t *prev;
} block_cache_entry_t;

void block_cache_init(void);
int32_t block_cache_read(uint32_t block, void *buf);
int32_t block_cache_write(uint32_t block, const void *buf);
int32_t block_cache_flush(uint32_t block);
int32_t block_cache_flush_all(void);
void block_cache_invalidate(uint32_t block);

#endif
