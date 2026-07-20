#include "icache.h"
#include "spinlock.h"
#include "../kernel/klog.h"
#include <string.h>

/* inode 缓存条目 */
typedef struct icache_entry {
    uint32_t dev;
    uint32_t ino;
    inode_t *inode;
    uint32_t ref_count;
    /* LRU 链表指针 */
    struct icache_entry *lru_prev;
    struct icache_entry *lru_next;
    /* 哈希链表指针 */
    struct icache_entry *hash_next;
} icache_entry_t;

/* 全局锁 */
static spinlock_t g_icache_lock;

/* LRU 链表头（双向循环链表）
 * head->lru_next = 最新使用的 (MRU)
 * head->lru_prev = 最久未使用的 (LRU) */
static icache_entry_t g_lru_head;

/* 哈希表 */
static icache_entry_t *g_hash_table[ICACHE_HASH_BUCKETS];

/* 统计信息 */
static icache_stats_t g_stats;

/* 条目池 */
static icache_entry_t g_entry_pool[ICACHE_MAX_ENTRIES];
static uint32_t g_pool_used = 0;

/* 初始化标记 */
static int g_initialized = 0;

/* ============================================================
 * 内部辅助函数
 * ============================================================ */

/* 哈希函数 */
static uint32_t icache_hash(uint32_t dev, uint32_t ino) {
    uint32_t h = dev ^ (ino * 2654435761u);
    return h % ICACHE_HASH_BUCKETS;
}

/* 从池中分配一个条目 */
static icache_entry_t *alloc_entry(void) {
    if (g_pool_used < ICACHE_MAX_ENTRIES) {
        icache_entry_t *entry = &g_entry_pool[g_pool_used++];
        memset(entry, 0, sizeof(icache_entry_t));
        return entry;
    }
    return NULL;
}

/* 从 LRU 链表中移除条目 */
static void lru_remove(icache_entry_t *entry) {
    if (entry->lru_prev && entry->lru_next) {
        entry->lru_prev->lru_next = entry->lru_next;
        entry->lru_next->lru_prev = entry->lru_prev;
        entry->lru_prev = NULL;
        entry->lru_next = NULL;
    }
}

/* 添加到 LRU 头部（MRU 位置）*/
static void lru_add_head(icache_entry_t *entry) {
    entry->lru_next = g_lru_head.lru_next;
    entry->lru_prev = &g_lru_head;
    g_lru_head.lru_next->lru_prev = entry;
    g_lru_head.lru_next = entry;
}

/* 移动到 LRU 头部（表示最近使用过）*/
static void lru_touch(icache_entry_t *entry) {
    lru_remove(entry);
    lru_add_head(entry);
}

/* 从哈希表中查找 */
static icache_entry_t *hash_find(uint32_t dev, uint32_t ino) {
    uint32_t h = icache_hash(dev, ino);
    icache_entry_t *entry = g_hash_table[h];

    while (entry) {
        if (entry->dev == dev && entry->ino == ino) {
            return entry;
        }
        entry = entry->hash_next;
    }
    return NULL;
}

/* 添加到哈希表 */
static void hash_add(icache_entry_t *entry) {
    uint32_t h = icache_hash(entry->dev, entry->ino);
    entry->hash_next = g_hash_table[h];
    g_hash_table[h] = entry;
}

/* 从哈希表中移除 */
static void hash_remove(icache_entry_t *entry) {
    uint32_t h = icache_hash(entry->dev, entry->ino);
    icache_entry_t *prev = NULL;
    icache_entry_t *cur = g_hash_table[h];

    while (cur) {
        if (cur == entry) {
            if (prev) {
                prev->hash_next = cur->hash_next;
            } else {
                g_hash_table[h] = cur->hash_next;
            }
            entry->hash_next = NULL;
            return;
        }
        prev = cur;
        cur = cur->hash_next;
    }
}

/* 回收一个 LRU 条目（从缓存中移除但不释放内存）*/
static void reclaim_entry(icache_entry_t *entry) {
    lru_remove(entry);
    hash_remove(entry);
    g_stats.total_entries--;
    g_stats.reclaims++;
}

/* ============================================================
 * 公共 API
 * ============================================================ */

void icache_init(void) {
    if (g_initialized) return;

    spinlock_init(&g_icache_lock);

    /* 初始化 LRU 头节点 */
    g_lru_head.lru_prev = &g_lru_head;
    g_lru_head.lru_next = &g_lru_head;

    /* 初始化哈希表 */
    memset(g_hash_table, 0, sizeof(g_hash_table));

    memset(&g_stats, 0, sizeof(g_stats));
    g_pool_used = 0;
    g_initialized = 1;
}

inode_t *icache_lookup(uint32_t dev, uint32_t ino) {
    if (!g_initialized) icache_init();

    spinlock_lock(&g_icache_lock);

    g_stats.lookups++;

    icache_entry_t *entry = hash_find(dev, ino);
    if (entry) {
        /* 找到！移动到 LRU 头部 */
        lru_touch(entry);
        entry->ref_count++;
        g_stats.hits++;
        spinlock_unlock(&g_icache_lock);
        return entry->inode;
    }

    g_stats.misses++;
    spinlock_unlock(&g_icache_lock);
    return NULL;
}

void icache_add(uint32_t dev, uint32_t ino, inode_t *inode) {
    if (!g_initialized) icache_init();
    if (!inode) return;

    spinlock_lock(&g_icache_lock);

    /* 检查是否已存在 */
    icache_entry_t *entry = hash_find(dev, ino);
    if (entry) {
        /* 更新并移动到头部 */
        entry->inode = inode;
        lru_touch(entry);
        spinlock_unlock(&g_icache_lock);
        return;
    }

    /* 如果超过最大条目数，先回收一些 */
    if (g_stats.total_entries >= ICACHE_MAX_ENTRIES) {
        /* 回收 LRU 端未被引用的条目 */
        icache_entry_t *victim = g_lru_head.lru_prev;
        uint32_t reclaimed = 0;

        while (victim != &g_lru_head && reclaimed < ICACHE_RECLAIM_BATCH) {
            icache_entry_t *prev = victim->lru_prev;
            if (victim->ref_count == 0) {
                reclaim_entry(victim);
                reclaimed++;
            }
            victim = prev;
        }

        /* 如果还是满的，就不添加了 */
        if (g_stats.total_entries >= ICACHE_MAX_ENTRIES) {
            spinlock_unlock(&g_icache_lock);
            return;
        }
    }

    /* 分配新条目 */
    entry = alloc_entry();
    if (!entry) {
        spinlock_unlock(&g_icache_lock);
        return;
    }

    entry->dev = dev;
    entry->ino = ino;
    entry->inode = inode;
    entry->ref_count = 0;

    hash_add(entry);
    lru_add_head(entry);

    g_stats.total_entries++;
    if (g_stats.total_entries > g_stats.max_entries) {
        g_stats.max_entries = g_stats.total_entries;
    }

    spinlock_unlock(&g_icache_lock);
}

void icache_remove(uint32_t dev, uint32_t ino) {
    if (!g_initialized) icache_init();

    spinlock_lock(&g_icache_lock);

    icache_entry_t *entry = hash_find(dev, ino);
    if (entry) {
        reclaim_entry(entry);
        g_stats.invalidations++;
    }

    spinlock_unlock(&g_icache_lock);
}

void icache_get(inode_t *inode) {
    if (!g_initialized) icache_init();
    if (!inode) return;

    spinlock_lock(&g_icache_lock);

    /* 遍历查找（不高效但简单）*/
    for (uint32_t i = 0; i < ICACHE_HASH_BUCKETS; i++) {
        icache_entry_t *entry = g_hash_table[i];
        while (entry) {
            if (entry->inode == inode) {
                lru_touch(entry);
                entry->ref_count++;
                spinlock_unlock(&g_icache_lock);
                return;
            }
            entry = entry->hash_next;
        }
    }

    spinlock_unlock(&g_icache_lock);
}

void icache_put(inode_t *inode) {
    if (!g_initialized) icache_init();
    if (!inode) return;

    spinlock_lock(&g_icache_lock);

    for (uint32_t i = 0; i < ICACHE_HASH_BUCKETS; i++) {
        icache_entry_t *entry = g_hash_table[i];
        while (entry) {
            if (entry->inode == inode) {
                if (entry->ref_count > 0) {
                    entry->ref_count--;
                }
                spinlock_unlock(&g_icache_lock);
                return;
            }
            entry = entry->hash_next;
        }
    }

    spinlock_unlock(&g_icache_lock);
}

uint32_t icache_reclaim(uint32_t count) {
    if (!g_initialized) icache_init();
    if (count == 0) return 0;

    spinlock_lock(&g_icache_lock);

    uint32_t reclaimed = 0;
    icache_entry_t *victim = g_lru_head.lru_prev;

    while (victim != &g_lru_head && reclaimed < count) {
        icache_entry_t *prev = victim->lru_prev;

        if (victim->ref_count == 0) {
            reclaim_entry(victim);
            reclaimed++;
        }

        victim = prev;
    }

    spinlock_unlock(&g_icache_lock);
    return reclaimed;
}

uint32_t icache_reclaim_dev(uint32_t dev) {
    if (!g_initialized) icache_init();

    spinlock_lock(&g_icache_lock);

    uint32_t reclaimed = 0;

    for (uint32_t i = 0; i < ICACHE_HASH_BUCKETS; i++) {
        icache_entry_t *entry = g_hash_table[i];
        while (entry) {
            icache_entry_t *next = entry->hash_next;
            if (entry->dev == dev && entry->ref_count == 0) {
                reclaim_entry(entry);
                reclaimed++;
            }
            entry = next;
        }
    }

    spinlock_unlock(&g_icache_lock);
    return reclaimed;
}

void icache_get_stats(icache_stats_t *stats) {
    if (!g_initialized) icache_init();
    if (!stats) return;

    spinlock_lock(&g_icache_lock);
    memcpy(stats, &g_stats, sizeof(g_stats));
    spinlock_unlock(&g_icache_lock);
}

void icache_reset_stats(void) {
    if (!g_initialized) icache_init();

    spinlock_lock(&g_icache_lock);
    g_stats.lookups = 0;
    g_stats.hits = 0;
    g_stats.misses = 0;
    g_stats.reclaims = 0;
    g_stats.invalidations = 0;
    g_stats.max_entries = g_stats.total_entries;
    spinlock_unlock(&g_icache_lock);
}
