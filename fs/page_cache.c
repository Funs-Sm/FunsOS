#include "page_cache.h"
#include "spinlock.h"
#include "kheap.h"
#include "../kernel/klog.h"
#include <string.h>

/* 页缓存条目 */
typedef struct pc_page {
    inode_t *inode;
    uint32_t page_index;
    uint32_t flags;
    uint32_t ref_count;
    void *data;  /* 页数据（PAGE_CACHE_SIZE 字节）*/

    /* LRU 链表 */
    struct pc_page *lru_prev;
    struct pc_page *lru_next;

    /* 哈希链表 */
    struct pc_page *hash_next;
} pc_page_t;

/* 全局锁 */
static spinlock_t g_pc_lock;

/* LRU 链表头 */
static pc_page_t g_lru_head;

/* 哈希表 */
static pc_page_t *g_hash_table[PAGE_CACHE_HASH_BUCKETS];

/* 统计信息 */
static page_cache_stats_t g_stats;

/* 条目池（预分配，避免运行时分配）*/
static pc_page_t g_page_pool[PAGE_CACHE_MAX_PAGES];
static uint32_t g_pool_used = 0;

/* 初始化标记 */
static int g_initialized = 0;

/* ============================================================
 * 内部辅助函数
 * ============================================================ */

static uint32_t pc_hash(inode_t *inode, uint32_t page_index) {
    uint32_t h = (uint32_t)(uintptr_t)inode ^ (page_index * 2654435761u);
    return h % PAGE_CACHE_HASH_BUCKETS;
}

static pc_page_t *alloc_page_entry(void) {
    if (g_pool_used < PAGE_CACHE_MAX_PAGES) {
        pc_page_t *page = &g_page_pool[g_pool_used++];
        memset(page, 0, sizeof(pc_page_t));
        return page;
    }
    return NULL;
}

static pc_page_t *hash_find(inode_t *inode, uint32_t page_index) {
    uint32_t h = pc_hash(inode, page_index);
    pc_page_t *page = g_hash_table[h];

    while (page) {
        if (page->inode == inode && page->page_index == page_index) {
            return page;
        }
        page = page->hash_next;
    }
    return NULL;
}

static void hash_add(pc_page_t *page) {
    uint32_t h = pc_hash(page->inode, page->page_index);
    page->hash_next = g_hash_table[h];
    g_hash_table[h] = page;
}

static void hash_remove(pc_page_t *page) {
    uint32_t h = pc_hash(page->inode, page->page_index);
    pc_page_t *prev = NULL;
    pc_page_t *cur = g_hash_table[h];

    while (cur) {
        if (cur == page) {
            if (prev) {
                prev->hash_next = cur->hash_next;
            } else {
                g_hash_table[h] = cur->hash_next;
            }
            page->hash_next = NULL;
            return;
        }
        prev = cur;
        cur = cur->hash_next;
    }
}

static void lru_remove(pc_page_t *page) {
    if (page->lru_prev && page->lru_next) {
        page->lru_prev->lru_next = page->lru_next;
        page->lru_next->lru_prev = page->lru_prev;
        page->lru_prev = NULL;
        page->lru_next = NULL;
    }
}

static void lru_add_head(pc_page_t *page) {
    page->lru_next = g_lru_head.lru_next;
    page->lru_prev = &g_lru_head;
    g_lru_head.lru_next->lru_prev = page;
    g_lru_head.lru_next = page;
}

static void lru_touch(pc_page_t *page) {
    lru_remove(page);
    lru_add_head(page);
}

/* 回收一页（必须确保 ref_count == 0 且不是脏页，或者脏页已写回）*/
static void reclaim_page(pc_page_t *page) {
    lru_remove(page);
    hash_remove(page);

    if (page->data) {
        kfree(page->data);
        page->data = NULL;
    }

    g_stats.total_pages--;
    g_stats.reclaims++;

    if (page->flags & PC_PAGE_DIRTY) {
        g_stats.dirty_pages--;
    }
}

/* ============================================================
 * 公共 API
 * ============================================================ */

void page_cache_init(void) {
    if (g_initialized) return;

    spinlock_init(&g_pc_lock);

    g_lru_head.lru_prev = &g_lru_head;
    g_lru_head.lru_next = &g_lru_head;

    memset(g_hash_table, 0, sizeof(g_hash_table));
    memset(&g_stats, 0, sizeof(g_stats));
    g_pool_used = 0;
    g_initialized = 1;
}

void *page_cache_lookup(inode_t *inode, uint32_t page_index) {
    if (!g_initialized) page_cache_init();
    if (!inode) return NULL;

    spinlock_lock(&g_pc_lock);

    g_stats.lookups++;

    pc_page_t *page = hash_find(inode, page_index);
    if (page && (page->flags & PC_PAGE_UPTODATE)) {
        lru_touch(page);
        g_stats.hits++;
        spinlock_unlock(&g_pc_lock);
        return page->data;
    }

    g_stats.misses++;
    spinlock_unlock(&g_pc_lock);
    return NULL;
}

void *page_cache_get_page(inode_t *inode, uint32_t page_index, int *is_new) {
    if (!g_initialized) page_cache_init();
    if (!inode) return NULL;

    spinlock_lock(&g_pc_lock);

    g_stats.lookups++;

    pc_page_t *page = hash_find(inode, page_index);
    if (page) {
        lru_touch(page);
        page->ref_count++;
        g_stats.hits++;
        if (is_new) *is_new = 0;
        spinlock_unlock(&g_pc_lock);
        return page->data;
    }

    g_stats.misses++;

    /* 需要创建新页，先检查是否需要回收 */
    if (g_stats.total_pages >= PAGE_CACHE_MAX_PAGES) {
        /* 回收一些未引用的干净页 */
        pc_page_t *victim = g_lru_head.lru_prev;
        uint32_t reclaimed = 0;

        while (victim != &g_lru_head && reclaimed < PAGE_CACHE_RECLAIM_BATCH) {
            pc_page_t *prev = victim->lru_prev;
            if (victim->ref_count == 0 && !(victim->flags & PC_PAGE_DIRTY)) {
                reclaim_page(victim);
                reclaimed++;
            }
            victim = prev;
        }

        if (g_stats.total_pages >= PAGE_CACHE_MAX_PAGES) {
            spinlock_unlock(&g_pc_lock);
            return NULL;
        }
    }

    /* 分配新页 */
    page = alloc_page_entry();
    if (!page) {
        spinlock_unlock(&g_pc_lock);
        return NULL;
    }

    page->data = kmalloc(PAGE_CACHE_SIZE);
    if (!page->data) {
        spinlock_unlock(&g_pc_lock);
        return NULL;
    }
    memset(page->data, 0, PAGE_CACHE_SIZE);

    page->inode = inode;
    page->page_index = page_index;
    page->flags = 0;
    page->ref_count = 1;

    hash_add(page);
    lru_add_head(page);

    g_stats.total_pages++;
    if (g_stats.total_pages > g_stats.max_pages) {
        g_stats.max_pages = g_stats.total_pages;
    }

    if (is_new) *is_new = 1;

    spinlock_unlock(&g_pc_lock);
    return page->data;
}

void *page_cache_add_page(inode_t *inode, uint32_t page_index, const void *data) {
    if (!g_initialized) page_cache_init();
    if (!inode) return NULL;

    int is_new = 0;
    void *page_data = page_cache_get_page(inode, page_index, &is_new);
    if (!page_data) return NULL;

    if (data && is_new) {
        memcpy(page_data, data, PAGE_CACHE_SIZE);
    }

    spinlock_lock(&g_pc_lock);
    pc_page_t *page = hash_find(inode, page_index);
    if (page) {
        page->flags |= PC_PAGE_UPTODATE;
        page->ref_count--;  /* 抵消 get_page 中的引用 */
    }
    spinlock_unlock(&g_pc_lock);

    return page_data;
}

void page_cache_put_page(inode_t *inode, uint32_t page_index) {
    if (!g_initialized) page_cache_init();
    if (!inode) return;

    spinlock_lock(&g_pc_lock);

    pc_page_t *page = hash_find(inode, page_index);
    if (page && page->ref_count > 0) {
        page->ref_count--;
    }

    spinlock_unlock(&g_pc_lock);
}

void page_cache_mark_dirty(inode_t *inode, uint32_t page_index) {
    if (!g_initialized) page_cache_init();
    if (!inode) return;

    spinlock_lock(&g_pc_lock);

    pc_page_t *page = hash_find(inode, page_index);
    if (page) {
        if (!(page->flags & PC_PAGE_DIRTY)) {
            page->flags |= PC_PAGE_DIRTY;
            g_stats.dirty_pages++;
        }
    }

    spinlock_unlock(&g_pc_lock);
}

void page_cache_mark_clean(inode_t *inode, uint32_t page_index) {
    if (!g_initialized) page_cache_init();
    if (!inode) return;

    spinlock_lock(&g_pc_lock);

    pc_page_t *page = hash_find(inode, page_index);
    if (page) {
        if (page->flags & PC_PAGE_DIRTY) {
            page->flags &= ~PC_PAGE_DIRTY;
            g_stats.dirty_pages--;
        }
    }

    spinlock_unlock(&g_pc_lock);
}

int page_cache_is_dirty(inode_t *inode, uint32_t page_index) {
    if (!g_initialized) page_cache_init();
    if (!inode) return 0;

    spinlock_lock(&g_pc_lock);

    pc_page_t *page = hash_find(inode, page_index);
    int dirty = page && (page->flags & PC_PAGE_DIRTY);

    spinlock_unlock(&g_pc_lock);
    return dirty;
}

uint32_t page_cache_writeback_inode(inode_t *inode) {
    if (!g_initialized) page_cache_init();
    if (!inode) return 0;

    spinlock_lock(&g_pc_lock);

    uint32_t written = 0;

    /* 遍历该 inode 的所有页 */
    for (uint32_t i = 0; i < PAGE_CACHE_HASH_BUCKETS; i++) {
        pc_page_t *page = g_hash_table[i];
        while (page) {
            pc_page_t *next = page->hash_next;
            if (page->inode == inode && (page->flags & PC_PAGE_DIRTY)) {
                /* 标记为干净（模拟写回）
                 * 实际系统中这里会调用块设备写入 */
                page->flags &= ~PC_PAGE_DIRTY;
                g_stats.dirty_pages--;
                g_stats.writebacks++;
                written++;
            }
            page = next;
        }
    }

    spinlock_unlock(&g_pc_lock);
    return written;
}

uint32_t page_cache_writeback_all(void) {
    if (!g_initialized) page_cache_init();

    spinlock_lock(&g_pc_lock);

    uint32_t written = 0;
    pc_page_t *page = g_lru_head.lru_next;

    while (page != &g_lru_head) {
        pc_page_t *next = page->lru_next;
        if (page->flags & PC_PAGE_DIRTY) {
            page->flags &= ~PC_PAGE_DIRTY;
            g_stats.dirty_pages--;
            g_stats.writebacks++;
            written++;
        }
        page = next;
    }

    spinlock_unlock(&g_pc_lock);
    return written;
}

uint32_t page_cache_invalidate_inode(inode_t *inode) {
    if (!g_initialized) page_cache_init();
    if (!inode) return 0;

    spinlock_lock(&g_pc_lock);

    uint32_t count = 0;

    for (uint32_t i = 0; i < PAGE_CACHE_HASH_BUCKETS; i++) {
        pc_page_t *page = g_hash_table[i];
        while (page) {
            pc_page_t *next = page->hash_next;
            if (page->inode == inode && page->ref_count == 0) {
                reclaim_page(page);
                count++;
            }
            page = next;
        }
    }

    spinlock_unlock(&g_pc_lock);
    return count;
}

uint32_t page_cache_invalidate_range(inode_t *inode,
                                     uint32_t start_page,
                                     uint32_t end_page) {
    if (!g_initialized) page_cache_init();
    if (!inode) return 0;

    spinlock_lock(&g_pc_lock);

    uint32_t count = 0;

    for (uint32_t i = 0; i < PAGE_CACHE_HASH_BUCKETS; i++) {
        pc_page_t *page = g_hash_table[i];
        while (page) {
            pc_page_t *next = page->hash_next;
            if (page->inode == inode &&
                page->page_index >= start_page &&
                page->page_index <= end_page &&
                page->ref_count == 0) {
                reclaim_page(page);
                count++;
            }
            page = next;
        }
    }

    spinlock_unlock(&g_pc_lock);
    return count;
}

uint32_t page_cache_reclaim(uint32_t count) {
    if (!g_initialized) page_cache_init();
    if (count == 0) return 0;

    spinlock_lock(&g_pc_lock);

    uint32_t reclaimed = 0;
    pc_page_t *victim = g_lru_head.lru_prev;

    while (victim != &g_lru_head && reclaimed < count) {
        pc_page_t *prev = victim->lru_prev;

        if (victim->ref_count == 0 && !(victim->flags & PC_PAGE_DIRTY)) {
            reclaim_page(victim);
            reclaimed++;
        }

        victim = prev;
    }

    spinlock_unlock(&g_pc_lock);
    return reclaimed;
}

void page_cache_get_stats(page_cache_stats_t *stats) {
    if (!g_initialized) page_cache_init();
    if (!stats) return;

    spinlock_lock(&g_pc_lock);
    memcpy(stats, &g_stats, sizeof(g_stats));
    spinlock_unlock(&g_pc_lock);
}

void page_cache_reset_stats(void) {
    if (!g_initialized) page_cache_init();

    spinlock_lock(&g_pc_lock);
    g_stats.lookups = 0;
    g_stats.hits = 0;
    g_stats.misses = 0;
    g_stats.reclaims = 0;
    g_stats.writebacks = 0;
    g_stats.reads = 0;
    g_stats.writes = 0;
    g_stats.max_pages = g_stats.total_pages;
    spinlock_unlock(&g_pc_lock);
}
