/* evlog.c - 系统事件日志子系统实现
 *
 * 持久化结构化事件日志（FunDB）。
 *   - 启动时从 /var/db/evlog.db 加载历史统计
 *   - 写操作同步落盘到 FunDB
 *   - 读操作从 FunDB select 后在内存过滤
 *   - 超过 retention 时自动剪裁最旧记录
 *   - 未知源首次写入时自动注册（默认启用，min_severity=INFO）
 */
#include "evlog.h"
#include "kheap.h"
#include "string.h"
#include "stdlib.h"
#include "stdio.h"
#include "spinlock.h"
#include "klog.h"
#include "fundb.h"
#include "timer.h"
#include "sched.h"
#include "../fs/vfs.h"

/* ============================================================
 * 内部状态
 * ============================================================ */

#define EVLOG_TABLE           "evlog_events"   /* EVLOG_TICKS_PER_SEC 定义在 evlog.h */

typedef struct {
    evlog_source_t  sources[EVLOG_MAX_SOURCES];
    uint32_t        source_count;
    evlog_watch_t   watches[EVLOG_MAX_WATCHES];
    uint32_t        watch_count;
    uint32_t        next_watch_id;
    uint32_t        next_id;          /* 下一个记录 ID（>=1） */
    uint32_t        retention_limit;
    spinlock_t      lock;
    fundb_handle_t  db;
    uint8_t         initialized;
    /* 统计 */
    uint32_t        total_records;
    uint32_t        per_severity[EVLOG_SEV_COUNT];
    uint32_t        pruned;
    uint32_t        dropped;
} evlog_globals_t;

static evlog_globals_t g_evlog;

/* ============================================================
 * 辅助
 * ============================================================ */

static uint64_t evlog_now_tick(void) {
    return (uint64_t)timer_get_ticks();
}

static uint16_t evlog_current_pid(void) {
    pcb_t *cur = sched_get_current();
    return cur ? (uint16_t)cur->pid : (uint16_t)0;
}

/* 在 sources 数组中查找，返回索引，-1 表示不存在 */
static int evlog_find_source_locked(const char *name) {
    if (!name) return -1;
    for (uint32_t i = 0; i < g_evlog.source_count; i++) {
        if (strcmp(g_evlog.sources[i].name, name) == 0) return (int)i;
    }
    return -1;
}

/* 找空闲槽位，没有则返回 -1 */
static int evlog_find_free_slot_locked(void) {
    for (int i = 0; i < EVLOG_MAX_SOURCES; i++) {
        if (g_evlog.sources[i].name[0] == '\0') return i;
    }
    return -1;
}

/* ============================================================
 * FunDB 持久化
 * ============================================================ */

static void evlog_db_init_table(void) {
    if (!g_evlog.db) return;
    fundb_column_t cols[8];
    memset(cols, 0, sizeof(cols));

    strcpy(cols[0].name, "id");
    cols[0].type = FUNDB_TYPE_INT;
    cols[0].size = 4;
    cols[0].not_null = 1;
    cols[0].primary_key = 1;
    /* 不使用 auto_increment，由 evlog 自行维护 next_id */

    strcpy(cols[1].name, "timestamp");
    cols[1].type = FUNDB_TYPE_INT;
    cols[1].size = 4;   /* 存储低 32 位 ticks（100Hz 下 ~497 天） */

    strcpy(cols[2].name, "event_id");
    cols[2].type = FUNDB_TYPE_INT;
    cols[2].size = 4;

    strcpy(cols[3].name, "source");
    cols[3].type = FUNDB_TYPE_TEXT;
    cols[3].size = EVLOG_MAX_SOURCE_NAME;

    strcpy(cols[4].name, "severity");
    cols[4].type = FUNDB_TYPE_INT;
    cols[4].size = 4;

    strcpy(cols[5].name, "category");
    cols[5].type = FUNDB_TYPE_INT;
    cols[5].size = 4;

    strcpy(cols[6].name, "pid");
    cols[6].type = FUNDB_TYPE_INT;
    cols[6].size = 4;

    strcpy(cols[7].name, "message");
    cols[7].type = FUNDB_TYPE_TEXT;
    cols[7].size = EVLOG_MAX_MSG;

    if (!fundb_table_exists(g_evlog.db, EVLOG_TABLE)) {
        int rc = fundb_create_table(g_evlog.db, EVLOG_TABLE, cols, 8);
        if (rc != FUNDB_OK) {
            klog_warn("evlog: failed to create table (%s)",
                      fundb_error_string(rc));
        }
    }
}

/* 把一条记录写入 FunDB */
static void evlog_db_insert(const evlog_record_t *rec) {
    if (!g_evlog.db || !rec) return;

    fundb_row_t row;
    void *vals[8];
    uint32_t sizes[8];
    uint32_t types[8];

    uint32_t id = rec->id;
    uint32_t ts = (uint32_t)rec->timestamp;   /* 低 32 位 */
    uint32_t eid = rec->event_id;
    uint32_t sev = rec->severity;
    uint32_t cat = rec->category;
    uint32_t pid = rec->pid;

    vals[0] = &id;     sizes[0] = 4; types[0] = FUNDB_TYPE_INT;
    vals[1] = &ts;     sizes[1] = 4; types[1] = FUNDB_TYPE_INT;
    vals[2] = &eid;    sizes[2] = 4; types[2] = FUNDB_TYPE_INT;
    vals[3] = (void *)rec->source; sizes[3] = (uint32_t)strlen(rec->source) + 1;
    types[3] = FUNDB_TYPE_TEXT;
    vals[4] = &sev;    sizes[4] = 4; types[4] = FUNDB_TYPE_INT;
    vals[5] = &cat;    sizes[5] = 4; types[5] = FUNDB_TYPE_INT;
    vals[6] = &pid;    sizes[6] = 4; types[6] = FUNDB_TYPE_INT;
    vals[7] = (void *)rec->message; sizes[7] = (uint32_t)strlen(rec->message) + 1;
    types[7] = FUNDB_TYPE_TEXT;

    row.values = vals;
    row.sizes = sizes;
    row.types = types;

    fundb_insert(g_evlog.db, EVLOG_TABLE, &row);
}

/* 从结果集行解析为 evlog_record_t */
static void evlog_db_row_to_record(const fundb_row_t *row, evlog_record_t *rec) {
    memset(rec, 0, sizeof(*rec));
    if (!row) return;
    if (row->values[0]) rec->id = *(uint32_t *)row->values[0];
    if (row->values[1]) {
        rec->timestamp = (uint64_t)(*(uint32_t *)row->values[1]);
    }
    if (row->values[2]) rec->event_id = *(uint32_t *)row->values[2];
    if (row->values[3]) {
        strncpy(rec->source, (const char *)row->values[3], EVLOG_MAX_SOURCE_NAME - 1);
    }
    if (row->values[4]) rec->severity = (uint8_t)*(uint32_t *)row->values[4];
    if (row->values[5]) rec->category = (uint8_t)*(uint32_t *)row->values[5];
    if (row->values[6]) rec->pid = (uint16_t)*(uint32_t *)row->values[6];
    if (row->values[7]) {
        strncpy(rec->message, (const char *)row->values[7], EVLOG_MAX_MSG - 1);
    }
}

/* 删除指定 id 的记录 */
static void evlog_db_delete_id(uint32_t id) {
    if (!g_evlog.db) return;
    char where[64];
    snprintf(where, sizeof(where), "id = %u", id);
    fundb_delete(g_evlog.db, EVLOG_TABLE, where);
}

/* 加载全部记录到调用者提供的缓冲区，返回加载数（按 id 升序） */
static uint32_t evlog_db_load_all(evlog_record_t *out, uint32_t max_count) {
    if (!g_evlog.db || !out || max_count == 0) return 0;
    fundb_result_t *r = fundb_select(g_evlog.db, EVLOG_TABLE, "*",
                                    NULL, NULL, 0);
    if (!r) return 0;

    uint32_t n = 0;
    for (uint32_t i = 0; i < r->row_count && n < max_count; i++) {
        evlog_db_row_to_record(&r->rows[i], &out[n]);
        if (out[n].id != 0) {
            n++;
        }
    }
    fundb_free_result(r);

    /* 简单插入排序按 id 升序（FunDB select 不保证顺序） */
    for (uint32_t i = 1; i < n; i++) {
        evlog_record_t key = out[i];
        int j = (int)i - 1;
        while (j >= 0 && out[j].id > key.id) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }
    return n;
}

/* 启动时重建统计与 next_id */
static void evlog_db_load_stats(void) {
    if (!g_evlog.db) return;
    fundb_result_t *r = fundb_select(g_evlog.db, EVLOG_TABLE, "*",
                                    NULL, NULL, 0);
    if (!r) return;

    for (uint32_t i = 0; i < r->row_count; i++) {
        evlog_record_t rec;
        evlog_db_row_to_record(&r->rows[i], &rec);
        if (rec.id == 0) continue;
        g_evlog.total_records++;
        if (rec.severity < EVLOG_SEV_COUNT) {
            g_evlog.per_severity[rec.severity]++;
        }
        /* 重建 next_id */
        if (rec.id >= g_evlog.next_id) {
            g_evlog.next_id = rec.id + 1;
        }
        /* 重建源 event_count（源本身在 init 时已预注册） */
        spinlock_lock(&g_evlog.lock);
        int idx = evlog_find_source_locked(rec.source);
        if (idx >= 0) {
            g_evlog.sources[idx].event_count++;
        }
        spinlock_unlock(&g_evlog.lock);
    }
    fundb_free_result(r);
    klog_info("evlog: loaded %u records, next_id=%u",
              g_evlog.total_records, g_evlog.next_id);
}

/* ============================================================
 * 自动剪裁
 * ============================================================ */

/* 删除最旧的 (total - keep) 条记录。返回删除条数 */
static uint32_t evlog_prune_locked(uint32_t keep) {
    if (!g_evlog.db) return 0;
    uint32_t total = g_evlog.total_records;
    if (total <= keep) return 0;
    uint32_t to_delete = total - keep;
    if (to_delete == 0) return 0;

    /* 加载全部并按 id 排序（升序），删除前 to_delete 条 */
    evlog_record_t *all = (evlog_record_t *)kmalloc(sizeof(evlog_record_t) * total);
    if (!all) return 0;

    uint32_t n = evlog_db_load_all(all, total);
    if (n < to_delete) to_delete = n;

    for (uint32_t i = 0; i < to_delete; i++) {
        evlog_db_delete_id(all[i].id);
    }
    kfree(all);

    g_evlog.total_records -= to_delete;
    g_evlog.pruned += to_delete;
    return to_delete;
}

/* ============================================================
 * 默认事件源
 * ============================================================ */

static void evlog_register_defaults(void) {
    static const char *defaults[] = {
        "Kernel", "FileSystem", "Memory", "Scheduler",
        "Cron", "Shell", "Network", "System",
        "Security", "Driver", NULL
    };
    for (int i = 0; defaults[i]; i++) {
        /* 直接填充，避免递归锁 */
        if (g_evlog.source_count >= EVLOG_MAX_SOURCES) break;
        int slot = evlog_find_free_slot_locked();
        if (slot < 0) break;
        evlog_source_t *s = &g_evlog.sources[slot];
        memset(s, 0, sizeof(*s));
        strncpy(s->name, defaults[i], EVLOG_MAX_SOURCE_NAME - 1);
        s->min_severity = EVLOG_SEV_INFO;
        s->enabled = 1;
        s->event_count = 0;
        g_evlog.source_count++;
    }
}

/* ============================================================
 * 公共 API
 * ============================================================ */

void evlog_init(void) {
    memset(&g_evlog, 0, sizeof(g_evlog));
    spinlock_init(&g_evlog.lock);
    g_evlog.retention_limit = EVLOG_DEFAULT_RETENTION;
    g_evlog.next_id = 1;
    g_evlog.next_watch_id = 1;

    /* 打开 FunDB（与 registry/cron 共享同一文件目录） */
    g_evlog.db = fundb_open(EVLOG_DB_PATH);
    if (g_evlog.db) {
        evlog_db_init_table();
    } else {
        klog_warn("evlog: failed to open FunDB at %s", EVLOG_DB_PATH);
    }

    /* 预注册默认源 */
    spinlock_lock(&g_evlog.lock);
    evlog_register_defaults();
    spinlock_unlock(&g_evlog.lock);

    /* 从 FunDB 加载历史统计与 next_id */
    evlog_db_load_stats();

    g_evlog.initialized = 1;
    klog_info("evlog: initialized with %u sources, %u historical records",
              g_evlog.source_count, g_evlog.total_records);
}

void evlog_shutdown(void) {
    spinlock_lock(&g_evlog.lock);
    g_evlog.initialized = 0;
    spinlock_unlock(&g_evlog.lock);
}

/* ---- 事件源管理 ---- */

int evlog_register_source(const char *name) {
    if (!name || !*name) return -1;
    spinlock_lock(&g_evlog.lock);
    if (evlog_find_source_locked(name) >= 0) {
        spinlock_unlock(&g_evlog.lock);
        return 0;  /* 已存在 */
    }
    int slot = evlog_find_free_slot_locked();
    if (slot < 0) {
        spinlock_unlock(&g_evlog.lock);
        return -1;
    }
    evlog_source_t *s = &g_evlog.sources[slot];
    memset(s, 0, sizeof(*s));
    strncpy(s->name, name, EVLOG_MAX_SOURCE_NAME - 1);
    s->min_severity = EVLOG_SEV_INFO;
    s->enabled = 1;
    g_evlog.source_count++;
    spinlock_unlock(&g_evlog.lock);
    return 0;
}

int evlog_unregister_source(const char *name) {
    if (!name) return -1;
    spinlock_lock(&g_evlog.lock);
    int idx = evlog_find_source_locked(name);
    if (idx < 0) {
        spinlock_unlock(&g_evlog.lock);
        return -1;
    }
    memset(&g_evlog.sources[idx], 0, sizeof(g_evlog.sources[idx]));
    if (g_evlog.source_count > 0) g_evlog.source_count--;
    spinlock_unlock(&g_evlog.lock);
    return 0;
}

int evlog_source_set_min_severity(const char *name, uint8_t min_sev) {
    if (min_sev >= EVLOG_SEV_COUNT) return -1;
    spinlock_lock(&g_evlog.lock);
    int idx = evlog_find_source_locked(name);
    if (idx < 0) {
        spinlock_unlock(&g_evlog.lock);
        return -1;
    }
    g_evlog.sources[idx].min_severity = min_sev;
    spinlock_unlock(&g_evlog.lock);
    return 0;
}

int evlog_source_enable(const char *name, int enabled) {
    spinlock_lock(&g_evlog.lock);
    int idx = evlog_find_source_locked(name);
    if (idx < 0) {
        spinlock_unlock(&g_evlog.lock);
        return -1;
    }
    g_evlog.sources[idx].enabled = enabled ? 1 : 0;
    spinlock_unlock(&g_evlog.lock);
    return 0;
}

int evlog_source_count(void) {
    spinlock_lock(&g_evlog.lock);
    int n = (int)g_evlog.source_count;
    spinlock_unlock(&g_evlog.lock);
    return n;
}

int evlog_list_sources(evlog_source_t *out, uint32_t max_count) {
    if (!out) return 0;
    spinlock_lock(&g_evlog.lock);
    uint32_t n = g_evlog.source_count;
    if (n > max_count) n = max_count;
    uint32_t wrote = 0;
    for (uint32_t i = 0; i < EVLOG_MAX_SOURCES && wrote < n; i++) {
        if (g_evlog.sources[i].name[0] != '\0') {
            out[wrote++] = g_evlog.sources[i];
        }
    }
    spinlock_unlock(&g_evlog.lock);
    return (int)wrote;
}

/* ---- 写入 ---- */

void evlog_write_va(const char *source, uint8_t severity, uint8_t category,
                    uint32_t event_id, const char *fmt, va_list args) {
    if (!g_evlog.initialized || !source || !*source) return;
    if (severity >= EVLOG_SEV_COUNT) severity = EVLOG_SEV_ERROR;

    /* 格式化消息 */
    char msgbuf[EVLOG_MAX_MSG];
    msgbuf[0] = '\0';
    if (fmt) {
        vsnprintf(msgbuf, sizeof(msgbuf), fmt, args);
    }

    /* 同时输出到 klog 以便 dmesg 可见 */
    switch (severity) {
        case EVLOG_SEV_CRITICAL: klog_err("[evlog/%s] %s", source, msgbuf); break;
        case EVLOG_SEV_ERROR:    klog_err("[evlog/%s] %s", source, msgbuf); break;
        case EVLOG_SEV_WARNING:  klog_warn("[evlog/%s] %s", source, msgbuf); break;
        default:                  klog_info("[evlog/%s] %s", source, msgbuf); break;
    }

    /* 检查源是否启用 */
    spinlock_lock(&g_evlog.lock);

    int idx = evlog_find_source_locked(source);
    if (idx < 0) {
        /* 未知源 - 自动注册 */
        int slot = evlog_find_free_slot_locked();
        if (slot < 0) {
            g_evlog.dropped++;
            spinlock_unlock(&g_evlog.lock);
            return;
        }
        evlog_source_t *s = &g_evlog.sources[slot];
        memset(s, 0, sizeof(*s));
        strncpy(s->name, source, EVLOG_MAX_SOURCE_NAME - 1);
        s->min_severity = EVLOG_SEV_INFO;
        s->enabled = 1;
        g_evlog.source_count++;
        idx = slot;
    }

    if (!g_evlog.sources[idx].enabled ||
        severity < g_evlog.sources[idx].min_severity) {
        g_evlog.dropped++;
        spinlock_unlock(&g_evlog.lock);
        return;
    }

    /* 分配新 ID */
    uint32_t id = g_evlog.next_id++;
    if (g_evlog.next_id == 0) g_evlog.next_id = 1;  /* 回绕保护 */

    /* 更新统计 */
    g_evlog.total_records++;
    g_evlog.per_severity[severity]++;
    g_evlog.sources[idx].event_count++;

    /* 需要剪裁的数量 */
    uint32_t over = 0;
    if (g_evlog.total_records > g_evlog.retention_limit) {
        over = g_evlog.total_records - g_evlog.retention_limit;
    }
    spinlock_unlock(&g_evlog.lock);

    /* 写入 FunDB（在锁外执行，避免长时间持锁） */
    evlog_record_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.id = id;
    rec.timestamp = evlog_now_tick();
    rec.event_id = event_id;
    strncpy(rec.source, source, EVLOG_MAX_SOURCE_NAME - 1);
    rec.severity = severity;
    rec.category = category;
    rec.pid = evlog_current_pid();
    strncpy(rec.message, msgbuf, EVLOG_MAX_MSG - 1);
    evlog_db_insert(&rec);

    /* 自动剪裁最旧记录 */
    if (over > 0) {
        spinlock_lock(&g_evlog.lock);
        uint32_t deleted = evlog_prune_locked(g_evlog.retention_limit);
        (void)deleted;
        spinlock_unlock(&g_evlog.lock);
    }

    /* 检查监视规则匹配（更新统计，不执行回调） */
    spinlock_lock(&g_evlog.lock);
    for (uint32_t i = 0; i < EVLOG_MAX_WATCHES; i++) {
        evlog_watch_t *w = &g_evlog.watches[i];
        if (w->id == 0 || !w->enabled) continue;
        if (w->source[0] != '\0' && strcmp(w->source, rec.source) != 0) continue;
        if (rec.severity < w->min_severity) continue;
        if (w->event_id != 0 && w->event_id != rec.event_id) continue;
        /* 匹配成功 */
        w->match_count++;
        w->last_match_tick = rec.timestamp;
        w->last_match_id = rec.id;
        strncpy(w->last_match_msg, rec.message, sizeof(w->last_match_msg) - 1);
        w->last_match_msg[sizeof(w->last_match_msg) - 1] = '\0';
    }
    spinlock_unlock(&g_evlog.lock);
}

void evlog_write(const char *source, uint8_t severity, uint8_t category,
                 uint32_t event_id, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    evlog_write_va(source, severity, category, event_id, fmt, args);
    va_end(args);
}

/* ---- 查询 ---- */

uint32_t evlog_query(const evlog_filter_t *filter,
                     evlog_record_t *out, uint32_t max_count) {
    if (!out || max_count == 0) return 0;

    uint32_t limit = (filter && filter->limit) ? filter->limit : 100;
    if (limit > max_count) limit = max_count;

    /* 加载全部记录到临时缓冲 */
    spinlock_lock(&g_evlog.lock);
    uint32_t total = g_evlog.total_records;
    spinlock_unlock(&g_evlog.lock);
    if (total == 0) return 0;

    evlog_record_t *all = (evlog_record_t *)kmalloc(sizeof(evlog_record_t) * total);
    if (!all) return 0;

    uint32_t n = evlog_db_load_all(all, total);
    /* evlog_db_load_all 已按 id 升序排序；倒序遍历得到最新在前 */

    uint32_t wrote = 0;
    for (int i = (int)n - 1; i >= 0 && wrote < limit; i--) {
        const evlog_record_t *r = &all[i];
        if (filter) {
            if (filter->source && *filter->source &&
                strcmp(r->source, filter->source) != 0) continue;
            if (filter->min_severity > 0 &&
                r->severity < filter->min_severity) continue;
            if (filter->event_id != 0 && r->event_id != filter->event_id) continue;
        }
        out[wrote++] = *r;
    }
    kfree(all);
    return wrote;
}

int evlog_get_by_id(uint32_t id, evlog_record_t *out) {
    if (!out || id == 0) return -1;
    if (!g_evlog.db) return -1;

    char where[64];
    snprintf(where, sizeof(where), "id = %u", id);
    fundb_result_t *r = fundb_select(g_evlog.db, EVLOG_TABLE, "*",
                                     where, NULL, 1);
    if (!r || r->row_count == 0) {
        if (r) fundb_free_result(r);
        return -1;
    }
    evlog_db_row_to_record(&r->rows[0], out);
    fundb_free_result(r);
    return (out->id == id) ? 0 : -1;
}

/* ---- 管理 ---- */

void evlog_clear(void) {
    if (!g_evlog.db) return;
    /* fundb_delete 用 NULL where 删除全部 */
    fundb_delete(g_evlog.db, EVLOG_TABLE, NULL);
    spinlock_lock(&g_evlog.lock);
    g_evlog.total_records = 0;
    memset(g_evlog.per_severity, 0, sizeof(g_evlog.per_severity));
    for (uint32_t i = 0; i < EVLOG_MAX_SOURCES; i++) {
        g_evlog.sources[i].event_count = 0;
    }
    g_evlog.next_id = 1;
    spinlock_unlock(&g_evlog.lock);
    klog_info("evlog: all records cleared");
}

int evlog_set_retention(uint32_t limit) {
    if (limit == 0 || limit > EVLOG_MAX_RETENTION) return -1;
    spinlock_lock(&g_evlog.lock);
    g_evlog.retention_limit = limit;
    spinlock_unlock(&g_evlog.lock);

    /* 若当前记录数超过新上限，立即剪裁 */
    if (g_evlog.total_records > limit) {
        spinlock_lock(&g_evlog.lock);
        evlog_prune_locked(limit);
        spinlock_unlock(&g_evlog.lock);
    }
    return 0;
}

void evlog_get_stats(evlog_stats_t *stats) {
    if (!stats) return;
    spinlock_lock(&g_evlog.lock);
    stats->total_records = g_evlog.total_records;
    stats->total_sources = g_evlog.source_count;
    memcpy(stats->per_severity, g_evlog.per_severity, sizeof(g_evlog.per_severity));
    stats->retention_limit = g_evlog.retention_limit;
    stats->pruned = g_evlog.pruned;
    stats->dropped = g_evlog.dropped;
    spinlock_unlock(&g_evlog.lock);
}

void evlog_reset_stats(void) {
    spinlock_lock(&g_evlog.lock);
    g_evlog.pruned = 0;
    g_evlog.dropped = 0;
    spinlock_unlock(&g_evlog.lock);
}

int evlog_prune(uint32_t keep_recent) {
    spinlock_lock(&g_evlog.lock);
    uint32_t deleted = evlog_prune_locked(keep_recent);
    spinlock_unlock(&g_evlog.lock);
    return (int)deleted;
}

/* ---- 辅助 ---- */

const char *evlog_severity_name(uint8_t sev) {
    switch (sev) {
        case EVLOG_SEV_INFO:     return "INFO";
        case EVLOG_SEV_WARNING:  return "WARN";
        case EVLOG_SEV_ERROR:    return "ERROR";
        case EVLOG_SEV_CRITICAL: return "CRIT";
        default:                 return "????";
    }
}

const char *evlog_category_name(uint8_t cat) {
    switch (cat) {
        case EVLOG_CAT_NONE:     return "none";
        case EVLOG_CAT_SYSTEM:   return "system";
        case EVLOG_CAT_SECURITY: return "security";
        case EVLOG_CAT_APP:      return "app";
        case EVLOG_CAT_SYSADMIN: return "sysadmin";
        case EVLOG_CAT_DRIVER:   return "driver";
        case EVLOG_CAT_NETWORK:  return "network";
        default:                 return "unknown";
    }
}

/* ============================================================
 * 导出
 * ============================================================ */

int evlog_export(const char *path, const evlog_filter_t *filter) {
    if (!path || !*path) return -1;

    /* 先创建/截断目标文件 */
    if (vfs_creat(path, FILE_MODE_WRITE | FILE_MODE_READ) != 0) {
        klog_warn("evlog: export failed to create '%s'", path);
        return -1;
    }
    file_t *f = NULL;
    if (vfs_open(path, FILE_MODE_WRITE, &f) != 0 || !f) {
        klog_warn("evlog: export failed to open '%s'", path);
        return -1;
    }

    /* 写文件头 */
    static const char header[] =
        "# FUNSOS Event Log Export\n"
        "# id | timestamp(ticks) | source | severity | event_id | pid | message\n";
    vfs_write(f, header, (uint32_t)sizeof(header) - 1);

    /* 查询匹配记录 */
    uint32_t limit = (filter && filter->limit) ? filter->limit : 1000;
    if (limit > 5000) limit = 5000;

    evlog_record_t *recs = (evlog_record_t *)kmalloc(sizeof(evlog_record_t) * limit);
    if (!recs) {
        vfs_close(f);
        return -1;
    }
    uint32_t n = evlog_query(filter, recs, limit);

    /* 按 id 升序写入文件（从数组末尾向前遍历，因为 query 返回最新在前） */
    int written = 0;
    for (int i = (int)n - 1; i >= 0; i--) {
        char line[EVLOG_MAX_MSG + 160];
        int len = snprintf(line, sizeof(line),
                           "%u | %llu | %s | %s | %u | %u | %s\n",
                           recs[i].id,
                           (unsigned long long)recs[i].timestamp,
                           recs[i].source,
                           evlog_severity_name(recs[i].severity),
                           recs[i].event_id,
                           recs[i].pid,
                           recs[i].message);
        if (len > 0) {
            vfs_write(f, line, (uint32_t)len);
            written++;
        }
    }
    kfree(recs);
    vfs_close(f);

    klog_info("evlog: exported %d records to %s", written, path);
    return written;
}

/* ============================================================
 * 监视规则
 * ============================================================ */

static int evlog_find_watch_slot_locked(uint32_t id) {
    if (id == 0) return -1;
    for (int i = 0; i < EVLOG_MAX_WATCHES; i++) {
        if (g_evlog.watches[i].id == id) return i;
    }
    return -1;
}

static int evlog_find_free_watch_slot_locked(void) {
    for (int i = 0; i < EVLOG_MAX_WATCHES; i++) {
        if (g_evlog.watches[i].id == 0) return i;
    }
    return -1;
}

int evlog_watch_add(const char *name, const char *source,
                    uint8_t min_severity, uint32_t event_id) {
    if (!name || !*name) return -1;
    if (min_severity >= EVLOG_SEV_COUNT) return -1;

    spinlock_lock(&g_evlog.lock);
    int slot = evlog_find_free_watch_slot_locked();
    if (slot < 0) {
        spinlock_unlock(&g_evlog.lock);
        return -1;
    }
    evlog_watch_t *w = &g_evlog.watches[slot];
    memset(w, 0, sizeof(*w));
    w->id = g_evlog.next_watch_id++;
    if (g_evlog.next_watch_id == 0) g_evlog.next_watch_id = 1;
    strncpy(w->name, name, EVLOG_WATCH_NAME - 1);
    if (source && *source) {
        strncpy(w->source, source, EVLOG_MAX_SOURCE_NAME - 1);
    }
    w->min_severity = min_severity;
    w->event_id = event_id;
    w->enabled = 1;
    g_evlog.watch_count++;
    uint32_t assigned = w->id;
    spinlock_unlock(&g_evlog.lock);
    return (int)assigned;
}

int evlog_watch_remove(uint32_t id) {
    spinlock_lock(&g_evlog.lock);
    int slot = evlog_find_watch_slot_locked(id);
    if (slot < 0) {
        spinlock_unlock(&g_evlog.lock);
        return -1;
    }
    memset(&g_evlog.watches[slot], 0, sizeof(g_evlog.watches[slot]));
    if (g_evlog.watch_count > 0) g_evlog.watch_count--;
    spinlock_unlock(&g_evlog.lock);
    return 0;
}

int evlog_watch_enable(uint32_t id, int enabled) {
    spinlock_lock(&g_evlog.lock);
    int slot = evlog_find_watch_slot_locked(id);
    if (slot < 0) {
        spinlock_unlock(&g_evlog.lock);
        return -1;
    }
    g_evlog.watches[slot].enabled = enabled ? 1 : 0;
    spinlock_unlock(&g_evlog.lock);
    return 0;
}

int evlog_watch_count(void) {
    spinlock_lock(&g_evlog.lock);
    int n = (int)g_evlog.watch_count;
    spinlock_unlock(&g_evlog.lock);
    return n;
}

int evlog_watch_list(evlog_watch_t *out, uint32_t max_count) {
    if (!out) return 0;
    spinlock_lock(&g_evlog.lock);
    uint32_t wrote = 0;
    for (uint32_t i = 0; i < EVLOG_MAX_WATCHES && wrote < max_count; i++) {
        if (g_evlog.watches[i].id != 0) {
            out[wrote++] = g_evlog.watches[i];
        }
    }
    spinlock_unlock(&g_evlog.lock);
    return (int)wrote;
}

int evlog_watch_reset_stats(uint32_t id) {
    spinlock_lock(&g_evlog.lock);
    int slot = evlog_find_watch_slot_locked(id);
    if (slot < 0) {
        spinlock_unlock(&g_evlog.lock);
        return -1;
    }
    evlog_watch_t *w = &g_evlog.watches[slot];
    w->match_count = 0;
    w->last_match_tick = 0;
    w->last_match_id = 0;
    w->last_match_msg[0] = '\0';
    spinlock_unlock(&g_evlog.lock);
    return 0;
}
