#include "buffer.h"
#include "kheap.h"
#include "pmm.h"
#include "string.h"
#include "sync.h"
#include "ide.h"
#include "stddef.h"

static cache_entry_t *cache_hash[CACHE_HASH_SIZE];
static cache_entry_t *lru_head;
static cache_entry_t *lru_tail;
static mutex_t cache_lock;
static uint32_t cache_hits;
static uint32_t cache_misses;
static uint32_t cache_time;

static uint32_t cache_hash_fn(uint32_t block_num) {
    return block_num % CACHE_HASH_SIZE;
}

void cache_init(void) {
    uint32_t i;
    for (i = 0; i < CACHE_HASH_SIZE; i++) {
        cache_hash[i] = NULL;
    }
    lru_head = NULL;
    lru_tail = NULL;
    mutex_init(&cache_lock);
    cache_hits = 0;
    cache_misses = 0;
    cache_time = 0;
}

cache_entry_t *cache_lookup(uint32_t block_num) {
    uint32_t idx = cache_hash_fn(block_num);
    cache_entry_t *entry = cache_hash[idx];

    while (entry) {
        if (entry->block_num == block_num && entry->valid) {
            return entry;
        }
        entry = entry->hash_next;
    }

    return NULL;
}

static void lru_remove(cache_entry_t *entry) {
    if (entry->lru_prev) {
        entry->lru_prev->lru_next = entry->lru_next;
    } else {
        lru_head = entry->lru_next;
    }

    if (entry->lru_next) {
        entry->lru_next->lru_prev = entry->lru_prev;
    } else {
        lru_tail = entry->lru_prev;
    }

    entry->lru_next = NULL;
    entry->lru_prev = NULL;
}

static void lru_add_front(cache_entry_t *entry) {
    entry->lru_next = lru_head;
    entry->lru_prev = NULL;

    if (lru_head) {
        lru_head->lru_prev = entry;
    }
    lru_head = entry;

    if (!lru_tail) {
        lru_tail = entry;
    }
}

int32_t cache_insert(uint32_t block_num, void *data) {
    uint32_t idx = cache_hash_fn(block_num);

    cache_entry_t *entry = (cache_entry_t *)kmalloc(sizeof(cache_entry_t));
    if (!entry) return -1;

    entry->data = kmalloc(4096);
    if (!entry->data) {
        kfree(entry);
        return -1;
    }

    entry->block_num = block_num;
    memcpy(entry->data, data, 4096);
    entry->dirty = 0;
    entry->valid = 1;
    entry->ref_count = 0;
    entry->access_time = cache_time++;

    entry->hash_next = cache_hash[idx];
    entry->hash_prev = NULL;
    if (cache_hash[idx]) {
        cache_hash[idx]->hash_prev = entry;
    }
    cache_hash[idx] = entry;

    lru_add_front(entry);

    return 0;
}

int32_t cache_read(uint32_t block_num, void *buf) {
    mutex_lock(&cache_lock);

    cache_entry_t *entry = cache_lookup(block_num);
    if (entry) {
        memcpy(buf, entry->data, 4096);
        entry->ref_count++;
        entry->access_time = cache_time++;
        cache_hits++;

        lru_remove(entry);
        lru_add_front(entry);

        mutex_unlock(&cache_lock);
        return 0;
    }

    cache_misses++;

    void *disk_buf = kmalloc(4096);
    if (!disk_buf) {
        mutex_unlock(&cache_lock);
        return -1;
    }

    uint32_t lba = block_num * (4096 / 512);
    if (ide_read_sectors(0, 8, lba, disk_buf) != 0) {
        kfree(disk_buf);
        mutex_unlock(&cache_lock);
        return -1;
    }

    cache_insert(block_num, disk_buf);
    kfree(disk_buf);

    entry = cache_lookup(block_num);
    if (entry) {
        memcpy(buf, entry->data, 4096);
        entry->ref_count++;
        entry->access_time = cache_time++;
    }

    mutex_unlock(&cache_lock);
    return entry ? 0 : -1;
}

int32_t cache_write(uint32_t block_num, const void *buf) {
    mutex_lock(&cache_lock);

    cache_entry_t *entry = cache_lookup(block_num);
    if (entry) {
        memcpy(entry->data, buf, 4096);
        entry->dirty = 1;
        entry->access_time = cache_time++;

        lru_remove(entry);
        lru_add_front(entry);

        mutex_unlock(&cache_lock);
        return 0;
    }

    cache_insert(block_num, (void *)buf);

    entry = cache_lookup(block_num);
    if (entry) {
        entry->dirty = 1;
        entry->access_time = cache_time++;
    }

    mutex_unlock(&cache_lock);
    return entry ? 0 : -1;
}

int32_t cache_flush(uint32_t block_num) {
    mutex_lock(&cache_lock);

    cache_entry_t *entry = cache_lookup(block_num);
    if (!entry || !entry->dirty) {
        mutex_unlock(&cache_lock);
        return 0;
    }

    uint32_t lba = block_num * (4096 / 512);
    if (ide_write_sectors(0, 8, lba, entry->data) != 0) {
        mutex_unlock(&cache_lock);
        return -1;
    }

    entry->dirty = 0;

    mutex_unlock(&cache_lock);
    return 0;
}

int32_t cache_flush_all(void) {
    mutex_lock(&cache_lock);

    uint32_t i;
    for (i = 0; i < CACHE_HASH_SIZE; i++) {
        cache_entry_t *entry = cache_hash[i];
        while (entry) {
            if (entry->dirty && entry->valid) {
                uint32_t lba = entry->block_num * (4096 / 512);
                ide_write_sectors(0, 8, lba, entry->data);
                entry->dirty = 0;
            }
            entry = entry->hash_next;
        }
    }

    mutex_unlock(&cache_lock);
    return 0;
}

void cache_evict(void) {
    mutex_lock(&cache_lock);

    cache_entry_t *entry = lru_tail;
    while (entry) {
        if (entry->ref_count == 0) {
            if (entry->dirty) {
                uint32_t lba = entry->block_num * (4096 / 512);
                ide_write_sectors(0, 8, lba, entry->data);
            }

            lru_remove(entry);

            uint32_t idx = cache_hash_fn(entry->block_num);
            if (cache_hash[idx] == entry) {
                cache_hash[idx] = entry->hash_next;
            }
            if (entry->hash_prev) {
                entry->hash_prev->hash_next = entry->hash_next;
            }
            if (entry->hash_next) {
                entry->hash_next->hash_prev = entry->hash_prev;
            }

            kfree(entry->data);
            kfree(entry);

            mutex_unlock(&cache_lock);
            return;
        }
        entry = entry->lru_prev;
    }

    mutex_unlock(&cache_lock);
}
