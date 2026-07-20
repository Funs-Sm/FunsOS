/* fim.c - 文件完整性监视子系统实现
 *
 * 监视列表和基线存储在 FunDB。
 *   - fim_watch 表：path(PK) - 监视路径列表
 *   - fim_baseline 表：path(PK), size, mtime, mode - 基线元数据
 * 扫描时 vfs_stat 每个路径，对比基线，变化写入 evlog。
 */
#include "fim.h"
#include "kheap.h"
#include "string.h"
#include "stdlib.h"
#include "stdio.h"
#include "spinlock.h"
#include "klog.h"
#include "fundb.h"
#include "evlog.h"
#include "../fs/vfs.h"
#include "timer.h"

/* ============================================================
 * 内部状态
 * ============================================================ */

typedef struct {
    spinlock_t  lock;
    fundb_handle_t db;
    uint8_t     initialized;
    fim_stats_t stats;
} fim_globals_t;

static fim_globals_t g_fim;

/* ============================================================
 * FunDB 表初始化
 * ============================================================ */

static void fim_db_init_tables(void) {
    if (!g_fim.db) return;
    fundb_column_t watch_cols[1];
    memset(watch_cols, 0, sizeof(watch_cols));
    strcpy(watch_cols[0].name, "path");
    watch_cols[0].type = FUNDB_TYPE_TEXT;
    watch_cols[0].size = FIM_MAX_PATH;
    watch_cols[0].not_null = 1;
    watch_cols[0].primary_key = 1;
    if (!fundb_table_exists(g_fim.db, FIM_TABLE_WATCH)) {
        fundb_create_table(g_fim.db, FIM_TABLE_WATCH, watch_cols, 1);
    }

    fundb_column_t base_cols[4];
    memset(base_cols, 0, sizeof(base_cols));
    strcpy(base_cols[0].name, "path");
    base_cols[0].type = FUNDB_TYPE_TEXT;
    base_cols[0].size = FIM_MAX_PATH;
    base_cols[0].not_null = 1;
    base_cols[0].primary_key = 1;
    strcpy(base_cols[1].name, "size");
    base_cols[1].type = FUNDB_TYPE_INT;
    base_cols[1].size = 4;
    strcpy(base_cols[2].name, "mtime");
    base_cols[2].type = FUNDB_TYPE_INT;
    base_cols[2].size = 4;
    strcpy(base_cols[3].name, "mode");
    base_cols[3].type = FUNDB_TYPE_INT;
    base_cols[3].size = 4;
    if (!fundb_table_exists(g_fim.db, FIM_TABLE_BASELINE)) {
        fundb_create_table(g_fim.db, FIM_TABLE_BASELINE, base_cols, 4);
    }
}

/* 加载所有监视路径到数组，返回数量 */
static uint32_t fim_db_load_watch_list(char paths[][FIM_MAX_PATH], uint32_t max_count) {
    if (!g_fim.db) return 0;
    fundb_result_t *r = fundb_select(g_fim.db, FIM_TABLE_WATCH, "*", NULL, NULL, 0);
    if (!r) return 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < r->row_count && n < max_count; i++) {
        if (r->rows[i].values[0]) {
            strncpy(paths[n], (const char *)r->rows[i].values[0], FIM_MAX_PATH - 1);
            paths[n][FIM_MAX_PATH - 1] = '\0';
            n++;
        }
    }
    fundb_free_result(r);
    return n;
}

/* 从基线表加载一条记录 */
static int fim_db_load_baseline(const char *path, uint32_t *size, uint32_t *mtime, uint32_t *mode) {
    if (!g_fim.db || !path) return -1;
    char where[FIM_MAX_PATH + 16];
    snprintf(where, sizeof(where), "path = '%s'", path);
    fundb_result_t *r = fundb_select(g_fim.db, FIM_TABLE_BASELINE, "*", where, NULL, 1);
    if (!r || r->row_count == 0) {
        if (r) fundb_free_result(r);
        return -1;
    }
    if (r->rows[0].values[1]) *size = *(uint32_t *)r->rows[0].values[1];
    if (r->rows[0].values[2]) *mtime = *(uint32_t *)r->rows[0].values[2];
    if (r->rows[0].values[3]) *mode = *(uint32_t *)r->rows[0].values[3];
    fundb_free_result(r);
    return 0;
}

/* 写入/更新基线记录（先删后插） */
static void fim_db_save_baseline(const char *path, uint32_t size, uint32_t mtime, uint32_t mode) {
    if (!g_fim.db || !path) return;
    char where[FIM_MAX_PATH + 16];
    snprintf(where, sizeof(where), "path = '%s'", path);
    fundb_delete(g_fim.db, FIM_TABLE_BASELINE, where);

    fundb_row_t row;
    void *vals[4];
    uint32_t sizes[4];
    uint32_t types[4];
    uint32_t sz = size, mt = mtime, md = mode;

    vals[0] = (void *)path;     sizes[0] = (uint32_t)strlen(path) + 1; types[0] = FUNDB_TYPE_TEXT;
    vals[1] = &sz;              sizes[1] = 4; types[1] = FUNDB_TYPE_INT;
    vals[2] = &mt;              sizes[2] = 4; types[2] = FUNDB_TYPE_INT;
    vals[3] = &md;              sizes[3] = 4; types[3] = FUNDB_TYPE_INT;

    row.values = vals;
    row.sizes = sizes;
    row.types = types;
    fundb_insert(g_fim.db, FIM_TABLE_BASELINE, &row);
}

/* 删除一条基线记录 */
static void fim_db_delete_baseline(const char *path) {
    if (!g_fim.db) return;
    char where[FIM_MAX_PATH + 16];
    snprintf(where, sizeof(where), "path = '%s'", path);
    fundb_delete(g_fim.db, FIM_TABLE_BASELINE, where);
}

/* ============================================================
 * 公共 API
 * ============================================================ */

void fim_init(void) {
    memset(&g_fim, 0, sizeof(g_fim));
    spinlock_init(&g_fim.lock);
    g_fim.db = fundb_open(FIM_DB_PATH);
    if (g_fim.db) {
        fim_db_init_tables();
    } else {
        klog_warn("fim: failed to open FunDB at %s", FIM_DB_PATH);
    }
    g_fim.initialized = 1;

    /* 加载监视计数到统计 */
    char paths[FIM_MAX_ENTRIES][FIM_MAX_PATH];
    g_fim.stats.watched = fim_db_load_watch_list(paths, FIM_MAX_ENTRIES);

    klog_info("fim: initialized, watching %u paths", g_fim.stats.watched);
}

void fim_shutdown(void) {
    spinlock_lock(&g_fim.lock);
    g_fim.initialized = 0;
    spinlock_unlock(&g_fim.lock);
}

int fim_add(const char *path) {
    if (!path || !*path) return -1;
    if (strlen(path) >= FIM_MAX_PATH) return -1;

    spinlock_lock(&g_fim.lock);
    if (!g_fim.db) {
        spinlock_unlock(&g_fim.lock);
        return -1;
    }

    /* 检查是否已存在 */
    char where[FIM_MAX_PATH + 16];
    snprintf(where, sizeof(where), "path = '%s'", path);
    fundb_result_t *r = fundb_select(g_fim.db, FIM_TABLE_WATCH, "*", where, NULL, 1);
    if (r && r->row_count > 0) {
        fundb_free_result(r);
        spinlock_unlock(&g_fim.lock);
        return 0;  /* 已存在 */
    }
    if (r) fundb_free_result(r);

    /* 插入监视列表 */
    fundb_row_t row;
    void *vals[1];
    uint32_t sizes[1];
    uint32_t types[1];
    vals[0] = (void *)path;
    sizes[0] = (uint32_t)strlen(path) + 1;
    types[0] = FUNDB_TYPE_TEXT;
    row.values = vals;
    row.sizes = sizes;
    row.types = types;
    fundb_insert(g_fim.db, FIM_TABLE_WATCH, &row);

    /* 尝试立即建立基线 */
    inode_t st;
    if (vfs_stat(path, &st) == 0) {
        fim_db_save_baseline(path, st.size, st.mtime, st.mode);
    }

    g_fim.stats.watched++;
    spinlock_unlock(&g_fim.lock);

    evlog_info("FileMon", 1, "fim: added watch on '%s'", path);
    return 0;
}

int fim_remove(const char *path) {
    if (!path) return -1;
    spinlock_lock(&g_fim.lock);
    if (!g_fim.db) {
        spinlock_unlock(&g_fim.lock);
        return -1;
    }
    char where[FIM_MAX_PATH + 16];
    snprintf(where, sizeof(where), "path = '%s'", path);
    fundb_delete(g_fim.db, FIM_TABLE_WATCH, where);
    fim_db_delete_baseline(path);
    if (g_fim.stats.watched > 0) g_fim.stats.watched--;
    spinlock_unlock(&g_fim.lock);
    return 0;
}

int fim_clear(void) {
    spinlock_lock(&g_fim.lock);
    if (!g_fim.db) {
        spinlock_unlock(&g_fim.lock);
        return -1;
    }
    fundb_delete(g_fim.db, FIM_TABLE_WATCH, NULL);
    fundb_delete(g_fim.db, FIM_TABLE_BASELINE, NULL);
    g_fim.stats.watched = 0;
    spinlock_unlock(&g_fim.lock);
    return 0;
}

uint32_t fim_count(void) {
    spinlock_lock(&g_fim.lock);
    uint32_t n = g_fim.stats.watched;
    spinlock_unlock(&g_fim.lock);
    return n;
}

int fim_list(fim_entry_t *out, uint32_t max_count) {
    if (!out) return 0;
    char paths[FIM_MAX_ENTRIES][FIM_MAX_PATH];
    uint32_t n = fim_db_load_watch_list(paths, max_count);
    if (n > max_count) n = max_count;

    for (uint32_t i = 0; i < n; i++) {
        memset(&out[i], 0, sizeof(out[i]));
        strncpy(out[i].path, paths[i], FIM_MAX_PATH - 1);

        /* 加载基线 */
        uint32_t bs = 0, bm = 0, bmd = 0;
        if (fim_db_load_baseline(paths[i], &bs, &bm, &bmd) == 0) {
            out[i].baseline_size = bs;
            out[i].baseline_mtime = bm;
            out[i].baseline_mode = bmd;
        }

        /* 加载当前 */
        inode_t st;
        if (vfs_stat(paths[i], &st) == 0) {
            out[i].current_size = st.size;
            out[i].current_mtime = st.mtime;
            out[i].current_mode = st.mode;
            out[i].last_state = (bs == 0 && bm == 0) ? FIM_STATE_NEW :
                                (st.size != bs || st.mtime != bm) ? FIM_STATE_MODIFIED :
                                FIM_STATE_UNCHANGED;
        } else {
            out[i].last_state = (bs != 0 || bm != 0) ? FIM_STATE_DELETED : FIM_STATE_ERROR;
        }
    }
    return (int)n;
}

void fim_scan(void) {
    if (!g_fim.initialized) return;

    spinlock_lock(&g_fim.lock);
    g_fim.stats.unchanged = 0;
    g_fim.stats.modified = 0;
    g_fim.stats.added = 0;
    g_fim.stats.deleted = 0;
    g_fim.stats.errors = 0;
    spinlock_unlock(&g_fim.lock);

    char paths[FIM_MAX_ENTRIES][FIM_MAX_PATH];
    uint32_t n = fim_db_load_watch_list(paths, FIM_MAX_ENTRIES);

    for (uint32_t i = 0; i < n; i++) {
        inode_t st;
        uint32_t bs = 0, bm = 0, bmd = 0;
        int has_baseline = (fim_db_load_baseline(paths[i], &bs, &bm, &bmd) == 0);
        int stat_ok = (vfs_stat(paths[i], &st) == 0);

        spinlock_lock(&g_fim.lock);

        if (stat_ok && !has_baseline) {
            /* 新增文件 */
            g_fim.stats.added++;
            spinlock_unlock(&g_fim.lock);
            fim_db_save_baseline(paths[i], st.size, st.mtime, st.mode);
            evlog_warn("FileMon", 2, "fim: ADDED '%s' size=%u", paths[i], st.size);
        } else if (stat_ok && has_baseline) {
            if (st.size != bs || st.mtime != bm) {
                /* 修改 */
                g_fim.stats.modified++;
                spinlock_unlock(&g_fim.lock);
                fim_db_save_baseline(paths[i], st.size, st.mtime, st.mode);
                evlog_warn("FileMon", 3, "fim: MODIFIED '%s' size %u->%u mtime %u->%u",
                           paths[i], bs, st.size, bm, st.mtime);
            } else {
                g_fim.stats.unchanged++;
                spinlock_unlock(&g_fim.lock);
            }
        } else if (!stat_ok && has_baseline) {
            /* 删除 */
            g_fim.stats.deleted++;
            spinlock_unlock(&g_fim.lock);
            fim_db_delete_baseline(paths[i]);
            evlog_err("FileMon", 4, "fim: DELETED '%s'", paths[i]);
        } else {
            /* 无基线且 stat 失败 */
            g_fim.stats.errors++;
            spinlock_unlock(&g_fim.lock);
            evlog_warn("FileMon", 5, "fim: ERROR '%s' (no baseline, stat failed)",
                       paths[i]);
        }
    }

    spinlock_lock(&g_fim.lock);
    g_fim.stats.last_scan_tick = timer_get_ticks();
    g_fim.stats.total_scans++;
    spinlock_unlock(&g_fim.lock);

    evlog_info("FileMon", 6,
               "fim: scan complete - %u unchanged, %u modified, %u added, %u deleted",
               g_fim.stats.unchanged, g_fim.stats.modified,
               g_fim.stats.added, g_fim.stats.deleted);
}

void fim_baseline(void) {
    if (!g_fim.initialized) return;
    char paths[FIM_MAX_ENTRIES][FIM_MAX_PATH];
    uint32_t n = fim_db_load_watch_list(paths, FIM_MAX_ENTRIES);

    /* 清空基线表 */
    fundb_delete(g_fim.db, FIM_TABLE_BASELINE, NULL);

    /* 以当前状态重建基线 */
    for (uint32_t i = 0; i < n; i++) {
        inode_t st;
        if (vfs_stat(paths[i], &st) == 0) {
            fim_db_save_baseline(paths[i], st.size, st.mtime, st.mode);
        }
    }

    spinlock_lock(&g_fim.lock);
    g_fim.stats.unchanged = n;
    g_fim.stats.modified = 0;
    g_fim.stats.added = 0;
    g_fim.stats.deleted = 0;
    g_fim.stats.last_scan_tick = timer_get_ticks();
    spinlock_unlock(&g_fim.lock);

    evlog_info("FileMon", 7, "fim: baseline reset for %u paths", n);
}

void fim_get_stats(fim_stats_t *stats) {
    if (!stats) return;
    spinlock_lock(&g_fim.lock);
    *stats = g_fim.stats;
    spinlock_unlock(&g_fim.lock);
}

void fim_reset_stats(void) {
    spinlock_lock(&g_fim.lock);
    g_fim.stats.total_scans = 0;
    memset(&g_fim.stats, 0, sizeof(g_fim.stats));
    /* 恢复 watched 计数 */
    char paths[FIM_MAX_ENTRIES][FIM_MAX_PATH];
    g_fim.stats.watched = fim_db_load_watch_list(paths, FIM_MAX_ENTRIES);
    spinlock_unlock(&g_fim.lock);
}
