#include "readahead.h"
#include "page_cache.h"
#include "spinlock.h"
#include <string.h>

/* 每个 inode 的预读状态 */
typedef struct ra_state {
    inode_t *inode;
    uint32_t last_page;      /* 上一次读取的页 */
    uint32_t ra_start;       /* 预读窗口起始页 */
    uint32_t ra_len;         /* 预读窗口长度（页数）*/
    uint32_t sequential;     /* 连续顺序读取计数 */
    struct ra_state *next;
    struct ra_state *prev;
} ra_state_t;

#define MAX_RA_STATES  64

static spinlock_t g_ra_lock;
static ra_state_t g_ra_pool[MAX_RA_STATES];
static ra_state_t *g_ra_list = NULL;
static uint32_t g_ra_count = 0;

static readahead_stats_t g_stats;
static int g_initialized = 0;

/* ============================================================
 * 内部辅助
 * ============================================================ */

static ra_state_t *find_state(inode_t *inode) {
    ra_state_t *s = g_ra_list;
    while (s) {
        if (s->inode == inode) return s;
        s = s->next;
    }
    return NULL;
}

static ra_state_t *alloc_state(inode_t *inode) {
    /* 先找空槽 */
    for (uint32_t i = 0; i < MAX_RA_STATES; i++) {
        if (g_ra_pool[i].inode == NULL) {
            memset(&g_ra_pool[i], 0, sizeof(ra_state_t));
            g_ra_pool[i].inode = inode;

            /* 加到链表头 */
            g_ra_pool[i].next = g_ra_list;
            g_ra_pool[i].prev = NULL;
            if (g_ra_list) {
                g_ra_list->prev = &g_ra_pool[i];
            }
            g_ra_list = &g_ra_pool[i];
            g_ra_count++;

            return &g_ra_pool[i];
        }
    }

    /* 没有空槽，回收最老的（链表尾部）*/
    ra_state_t *old = g_ra_list;
    if (!old) return NULL;

    while (old->next) old = old->next;

    /* 从链表中移除 */
    if (old->prev) {
        old->prev->next = NULL;
    } else {
        g_ra_list = NULL;
    }

    memset(old, 0, sizeof(ra_state_t));
    old->inode = inode;

    /* 加到链表头 */
    old->next = g_ra_list;
    old->prev = NULL;
    if (g_ra_list) {
        g_ra_list->prev = old;
    }
    g_ra_list = old;

    return old;
}

/* ============================================================
 * 公共 API
 * ============================================================ */

void readahead_init(void) {
    if (g_initialized) return;

    spinlock_init(&g_ra_lock);
    memset(g_ra_pool, 0, sizeof(g_ra_pool));
    g_ra_list = NULL;
    g_ra_count = 0;
    memset(&g_stats, 0, sizeof(g_stats));
    g_initialized = 1;
}

void readahead_record_read(inode_t *inode, uint32_t page_index) {
    if (!g_initialized) readahead_init();
    if (!inode) return;

    spinlock_lock(&g_ra_lock);

    ra_state_t *state = find_state(inode);
    if (!state) {
        state = alloc_state(inode);
        if (!state) {
            spinlock_unlock(&g_ra_lock);
            return;
        }
        state->last_page = page_index;
        state->ra_len = READAHEAD_MIN_PAGES;
        state->sequential = 1;
        spinlock_unlock(&g_ra_lock);
        return;
    }

    /* 检查是否顺序读取（连续下一页） */
    if (page_index == state->last_page + 1 || page_index == state->last_page) {
        state->sequential++;
        if (state->sequential >= 2) {
            /* 检测到顺序读取，增加预读窗口 */
            if (state->ra_len < READAHEAD_MAX_PAGES) {
                state->ra_len *= 2;
                if (state->ra_len > READAHEAD_MAX_PAGES) {
                    state->ra_len = READAHEAD_MAX_PAGES;
                }
            }
            g_stats.seq_detections++;
        }
    } else {
        /* 非顺序读取，重置 */
        state->sequential = 1;
        state->ra_len = READAHEAD_MIN_PAGES;
    }

    state->last_page = page_index;

    spinlock_unlock(&g_ra_lock);
}

uint32_t readahead_trigger(inode_t *inode, uint32_t current_page) {
    if (!g_initialized) readahead_init();
    if (!inode) return 0;

    spinlock_lock(&g_ra_lock);

    ra_state_t *state = find_state(inode);
    if (!state) {
        state = alloc_state(inode);
        if (!state) {
            spinlock_unlock(&g_ra_lock);
            return 0;
        }
        state->ra_len = READAHEAD_MIN_PAGES;
    }

    uint32_t ra_len = state->ra_len;
    uint32_t start_page = current_page + 1;

    /* 更新预读窗口 */
    state->ra_start = start_page;
    state->last_page = current_page;

    g_stats.readahead_calls++;

    spinlock_unlock(&g_ra_lock);

    /* 执行预读：将页面加入页缓存（标记为预读）
     * 在真实系统中这是异步的，这里简化为同步预分配 */
    uint32_t actual = 0;
    for (uint32_t i = 0; i < ra_len; i++) {
        uint32_t pg = start_page + i;
        int is_new = 0;

        void *data = page_cache_get_page(inode, pg, &is_new);
        if (data) {
            if (is_new) {
                /* 新页，标记为预读（但内容无效，需要实际读取）
                 * 这里只是占位，实际读取由文件系统完成 */
                page_cache_put_page(inode, pg);
                actual++;
            } else {
                /* 已经在缓存中，也算命中 */
                page_cache_put_page(inode, pg);
                g_stats.cache_hits++;
                actual++;
            }
        }
    }

    g_stats.pages_readahead += actual;
    return actual;
}

void readahead_get_stats(readahead_stats_t *stats) {
    if (!g_initialized) readahead_init();
    if (!stats) return;

    spinlock_lock(&g_ra_lock);
    memcpy(stats, &g_stats, sizeof(g_stats));
    spinlock_unlock(&g_ra_lock);
}

void readahead_reset_stats(void) {
    if (!g_initialized) readahead_init();

    spinlock_lock(&g_ra_lock);
    memset(&g_stats, 0, sizeof(g_stats));
    spinlock_unlock(&g_ra_lock);
}
