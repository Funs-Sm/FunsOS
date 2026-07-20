#include "dcache.h"
#include "spinlock.h"
#include "../kernel/klog.h"
#include <string.h>

/* 全局 dcache 锁 */
static spinlock_t g_dcache_lock;

/* LRU 链表头（双向循环链表）
 * head->cache_next = 最新使用的 (MRU)
 * head->cache_prev = 最久未使用的 (LRU) */
static dentry_t g_lru_head;

/* 统计信息 */
static dcache_stats_t g_stats;

/* 初始化标记 */
static int g_initialized = 0;

/* ============================================================
 * 内部辅助函数
 * ============================================================ */

/* 从 LRU 链表中移除 dentry */
static void lru_remove(dentry_t *dentry) {
    if (dentry->cache_prev && dentry->cache_next) {
        dentry->cache_prev->cache_next = dentry->cache_next;
        dentry->cache_next->cache_prev = dentry->cache_prev;
        dentry->cache_prev = NULL;
        dentry->cache_next = NULL;
    }
}

/* 添加到 LRU 头部（MRU 位置）*/
static void lru_add_head(dentry_t *dentry) {
    dentry->cache_next = g_lru_head.cache_next;
    dentry->cache_prev = &g_lru_head;
    g_lru_head.cache_next->cache_prev = dentry;
    g_lru_head.cache_next = dentry;
}

/* 移动到 LRU 头部（表示最近使用过）*/
static void lru_touch(dentry_t *dentry) {
    lru_remove(dentry);
    lru_add_head(dentry);
}

/* 检查 dentry 是否在 LRU 链表中 */
static int lru_is_linked(dentry_t *dentry) {
    return dentry->cache_prev != NULL && dentry->cache_next != NULL;
}

/* ============================================================
 * 公共 API
 * ============================================================ */

void dcache_init(void) {
    if (g_initialized) return;

    spinlock_init(&g_dcache_lock);

    /* 初始化 LRU 头节点（自循环）*/
    g_lru_head.cache_prev = &g_lru_head;
    g_lru_head.cache_next = &g_lru_head;

    memset(&g_stats, 0, sizeof(g_stats));
    g_initialized = 1;
}

dentry_t *dcache_lookup(dentry_t *parent, const char *name) {
    if (!g_initialized) dcache_init();
    if (!parent || !name || !*name) return NULL;

    spinlock_lock(&g_dcache_lock);

    g_stats.lookups++;

    /* 遍历父节点的子节点链表查找 */
    dentry_t *child = parent->child;
    while (child) {
        if (strcmp(child->name, name) == 0) {
            /* 找到！移动到 LRU 头部 */
            if (lru_is_linked(child)) {
                lru_touch(child);
            } else {
                lru_add_head(child);
                g_stats.total_entries++;
                if (g_stats.total_entries > g_stats.max_entries) {
                    g_stats.max_entries = g_stats.total_entries;
                }
            }

            g_stats.hits++;
            spinlock_unlock(&g_dcache_lock);
            return child;
        }
        child = child->next_sibling;
    }

    g_stats.misses++;
    spinlock_unlock(&g_dcache_lock);
    return NULL;
}

void dcache_add(dentry_t *dentry) {
    if (!g_initialized) dcache_init();
    if (!dentry) return;

    spinlock_lock(&g_dcache_lock);

    if (!lru_is_linked(dentry)) {
        lru_add_head(dentry);
        g_stats.total_entries++;
        if (g_stats.total_entries > g_stats.max_entries) {
            g_stats.max_entries = g_stats.total_entries;
        }
    } else {
        lru_touch(dentry);
    }

    /* 如果超过最大条目数，回收一些 */
    if (g_stats.total_entries > DCACHE_MAX_ENTRIES) {
        uint32_t to_reclaim = g_stats.total_entries - DCACHE_MAX_ENTRIES + DCACHE_RECLAIM_BATCH;
        spinlock_unlock(&g_dcache_lock);
        dcache_reclaim(to_reclaim);
        return;
    }

    spinlock_unlock(&g_dcache_lock);
}

void dcache_remove(dentry_t *dentry) {
    if (!g_initialized) dcache_init();
    if (!dentry) return;

    spinlock_lock(&g_dcache_lock);

    if (lru_is_linked(dentry)) {
        lru_remove(dentry);
        g_stats.total_entries--;
        g_stats.invalidations++;
    }

    spinlock_unlock(&g_dcache_lock);
}

void dcache_get(dentry_t *dentry) {
    if (!g_initialized) dcache_init();
    if (!dentry) return;

    spinlock_lock(&g_dcache_lock);

    if (lru_is_linked(dentry)) {
        lru_touch(dentry);
    } else {
        lru_add_head(dentry);
        g_stats.total_entries++;
        if (g_stats.total_entries > g_stats.max_entries) {
            g_stats.max_entries = g_stats.total_entries;
        }
    }

    dentry->d_refcount++;

    spinlock_unlock(&g_dcache_lock);
}

void dcache_put(dentry_t *dentry) {
    if (!g_initialized) dcache_init();
    if (!dentry) return;

    spinlock_lock(&g_dcache_lock);

    if (dentry->d_refcount > 0) {
        dentry->d_refcount--;
    }

    spinlock_unlock(&g_dcache_lock);
}

uint32_t dcache_reclaim(uint32_t count) {
    if (!g_initialized) dcache_init();
    if (count == 0) return 0;

    spinlock_lock(&g_dcache_lock);

    uint32_t reclaimed = 0;
    dentry_t *victim = g_lru_head.cache_prev;  /* LRU 端 */

    /* 从 LRU 端开始回收未被引用的 dentry */
    while (victim != &g_lru_head && reclaimed < count) {
        dentry_t *prev = victim->cache_prev;

        /* 只回收引用计数为 0 的 dentry */
        if (victim->d_refcount == 0) {
            lru_remove(victim);
            g_stats.total_entries--;
            reclaimed++;
        }

        victim = prev;
    }

    g_stats.reclaims += reclaimed;

    spinlock_unlock(&g_dcache_lock);
    return reclaimed;
}

void dcache_get_stats(dcache_stats_t *stats) {
    if (!g_initialized) dcache_init();
    if (!stats) return;

    spinlock_lock(&g_dcache_lock);
    memcpy(stats, &g_stats, sizeof(g_stats));
    spinlock_unlock(&g_dcache_lock);
}

void dcache_reset_stats(void) {
    if (!g_initialized) dcache_init();

    spinlock_lock(&g_dcache_lock);
    g_stats.lookups = 0;
    g_stats.hits = 0;
    g_stats.misses = 0;
    g_stats.reclaims = 0;
    g_stats.invalidations = 0;
    g_stats.max_entries = g_stats.total_entries;
    spinlock_unlock(&g_dcache_lock);
}

void dcache_dump(void) {
    if (!g_initialized) dcache_init();

    spinlock_lock(&g_dcache_lock);

    klog_write(1, "=== dcache dump ===\n");
    klog_write(1, "total entries: %u\n", g_stats.total_entries);
    klog_write(1, "max entries: %u\n", g_stats.max_entries);

    uint32_t count = 0;
    dentry_t *d = g_lru_head.cache_next;
    while (d != &g_lru_head && count < 20) {
        klog_write(1, "  [%u] %s (ref=%u)\n",
            count, d->name, d->d_refcount);
        d = d->cache_next;
        count++;
    }

    if (d != &g_lru_head) {
        klog_write(1, "  ... and more\n");
    }

    spinlock_unlock(&g_dcache_lock);
}
