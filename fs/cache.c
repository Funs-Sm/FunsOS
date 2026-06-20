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
static uint32_t max_cache_entries = 0;  /* 0表示无限制 */

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

/* ==================== 缓存统计和高级操作 ==================== */

/* 获取缓存统计信息 */
cache_stats_t *cache_get_stats(void) {
    static cache_stats_t stats;
    mutex_lock(&cache_lock);

    /* 统计当前缓存条目数 */
    uint32_t entry_count = 0;
    for (uint32_t i = 0; i < CACHE_HASH_SIZE; i++) {
        cache_entry_t *entry = cache_hash[i];
        while (entry) {
            if (entry->valid) {
                entry_count++;
            }
            entry = entry->hash_next;
        }
    }

    stats.hits = cache_hits;
    stats.misses = cache_misses;
    stats.entries = entry_count;

    /* 计算命中率（百分比） */
    uint32_t total_accesses = cache_hits + cache_misses;
    if (total_accesses > 0) {
        stats.hit_rate = (cache_hits * 100) / total_accesses;
    } else {
        stats.hit_rate = 0;
    }

    mutex_unlock(&cache_lock);
    return &stats;
}

/* 预读取指定块到缓存（不返回数据） */
int32_t cache_prefetch(uint32_t block_num) {
    mutex_lock(&cache_lock);

    /* 检查是否已在缓存中 */
    cache_entry_t *entry = cache_lookup(block_num);
    if (entry) {
        /* 已在缓存中，更新访问时间 */
        entry->access_time = cache_time++;
        lru_remove(entry);
        lru_add_front(entry);
        mutex_unlock(&cache_lock);
        return 0;
    }

    /* 检查是否超过最大条目限制 */
    if (max_cache_entries > 0) {
        uint32_t current_entries = 0;
        for (uint32_t i = 0; i < CACHE_HASH_SIZE; i++) {
            cache_entry_t *e = cache_hash[i];
            while (e) {
                if (e->valid) current_entries++;
                e = e->hash_next;
            }
        }
        if (current_entries >= max_cache_entries) {
            mutex_unlock(&cache_lock);
            return -1; /* 缓存已满 */
        }
    }

    /* 从磁盘读取数据 */
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

    /* 插入到缓存 */
    int32_t ret = cache_insert(block_num, disk_buf);
    kfree(disk_buf);

    mutex_unlock(&cache_lock);
    return ret;
}

/* 使指定块缓存失效 */
void cache_invalidate(uint32_t block_num) {
    mutex_lock(&cache_lock);

    cache_entry_t *entry = cache_lookup(block_num);
    if (!entry || !entry->valid) {
        mutex_unlock(&cache_lock);
        return;
    }

    /* 如果脏数据，先写回磁盘 */
    if (entry->dirty) {
        uint32_t lba = block_num * (4096 / 512);
        ide_write_sectors(0, 8, lba, entry->data);
    }

    /* 从哈希表中移除 */
    uint32_t idx = cache_hash_fn(block_num);
    if (cache_hash[idx] == entry) {
        cache_hash[idx] = entry->hash_next;
    }
    if (entry->hash_prev) {
        entry->hash_prev->hash_next = entry->hash_next;
    }
    if (entry->hash_next) {
        entry->hash_next->hash_prev = entry->hash_prev;
    }

    /* 从LRU链表移除 */
    lru_remove(entry);

    /* 释放内存 */
    kfree(entry->data);
    kfree(entry);

    mutex_unlock(&cache_lock);
}

/* 设置最大缓存条目数限制 */
void cache_set_max_entries(uint32_t max) {
    mutex_lock(&cache_lock);
    max_cache_entries = max;

    /* 如果新限制小于当前条目数，驱逐多余条目 */
    if (max > 0) {
        uint32_t current_entries = 0;
        for (uint32_t i = 0; i < CACHE_HASH_SIZE; i++) {
            cache_entry_t *e = cache_hash[i];
            while (e) {
                if (e->valid) current_entries++;
                e = e->hash_next;
            }
        }

        while (current_entries > max) {
            cache_evict();
            current_entries--;
        }
    }

    mutex_unlock(&cache_lock);
}
