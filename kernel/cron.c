/* cron.c - 定时任务调度子系统实现
 *
 * 基于 kwork 周期触发 + FunDB 持久化。
 * 启动时加载 FunDB 中的任务定义，每秒检查到期任务并执行。
 */

#include "cron.h"
#include "kwork.h"
#include "timer.h"
#include "fundb.h"
#include "klog.h"
#include "kheap.h"
#include "string.h"
#include "stdlib.h"
#include "stdio.h"
#include "sync.h"
#include "evlog.h"

/* ============================================================
 * 内部状态
 * ============================================================ */

#define CRON_TABLE      "cron_jobs"
#define CRON_TICKS_PER_SEC  100   /* timer 运行在 100Hz */

typedef struct {
    cron_job_t   jobs[CRON_MAX_JOBS];
    uint32_t     job_count;        /* 已用槽位数 */
    uint32_t     next_id;           /* 下一个待分配的 ID（>=1） */
    spinlock_t   lock;              /* 保护 jobs 数组 */
    fundb_handle_t db;
    cron_executor_t executor;      /* 命令执行器回调（可 NULL） */
    kwork_t      tick_work;         /* 周期性 kwork */
    uint8_t      initialized;
    /* 统计 */
    uint64_t     total_runs;
    uint64_t     total_errors;
} cron_globals_t;

static cron_globals_t g_cron;

/* 把秒数换算为 ticks */
static inline uint64_t cron_sec_to_ticks(uint32_t sec) {
    return (uint64_t)sec * CRON_TICKS_PER_SEC;
}

static uint64_t cron_now_tick(void) {
    return (uint64_t)timer_get_ticks();
}

/* ============================================================
 * FunDB 持久化
 * ============================================================ */

static void cron_db_init_table(void) {
    if (!g_cron.db) return;
    fundb_column_t cols[11];
    memset(cols, 0, sizeof(cols));

    strcpy(cols[0].name, "id");
    cols[0].type = FUNDB_TYPE_INT;
    cols[0].size = 4;
    cols[0].not_null = 1;
    cols[0].primary_key = 1;

    strcpy(cols[1].name, "name");
    cols[1].type = FUNDB_TYPE_TEXT;
    cols[1].size = CRON_MAX_NAME;

    strcpy(cols[2].name, "command");
    cols[2].type = FUNDB_TYPE_TEXT;
    cols[2].size = CRON_MAX_CMD_LEN;

    strcpy(cols[3].name, "type");
    cols[3].type = FUNDB_TYPE_INT;
    cols[3].size = 4;

    strcpy(cols[4].name, "state");
    cols[4].type = FUNDB_TYPE_INT;
    cols[4].size = 4;

    strcpy(cols[5].name, "interval_sec");
    cols[5].type = FUNDB_TYPE_INT;
    cols[5].size = 4;

    strcpy(cols[6].name, "hour");
    cols[6].type = FUNDB_TYPE_INT;
    cols[6].size = 4;

    strcpy(cols[7].name, "minute");
    cols[7].type = FUNDB_TYPE_INT;
    cols[7].size = 4;

    strcpy(cols[8].name, "run_count");
    cols[8].type = FUNDB_TYPE_INT;
    cols[8].size = 4;

    strcpy(cols[9].name, "error_count");
    cols[9].type = FUNDB_TYPE_INT;
    cols[9].size = 4;

    strcpy(cols[10].name, "last_exit_code");
    cols[10].type = FUNDB_TYPE_INT;
    cols[10].size = 4;

    if (!fundb_table_exists(g_cron.db, CRON_TABLE)) {
        int rc = fundb_create_table(g_cron.db, CRON_TABLE, cols, 11);
        if (rc != FUNDB_OK) {
            klog_warn("cron: failed to create table (%d)", rc);
        }
    }
}

/* 把单个 job 写入 FunDB（先删后插） */
static void cron_db_save_job(const cron_job_t *job) {
    if (!g_cron.db || !job) return;

    char where[64];
    snprintf(where, sizeof(where), "id = %u", job->id);
    fundb_delete(g_cron.db, CRON_TABLE, where);

    fundb_row_t row;
    void *vals[11];
    uint32_t sizes[11];
    uint32_t types[11];

    vals[0] = (void *)&job->id;
    sizes[0] = sizeof(uint32_t);
    types[0] = FUNDB_TYPE_INT;

    vals[1] = (void *)job->name;
    sizes[1] = (uint32_t)strlen(job->name) + 1;
    types[1] = FUNDB_TYPE_TEXT;

    vals[2] = (void *)job->command;
    sizes[2] = (uint32_t)strlen(job->command) + 1;
    types[2] = FUNDB_TYPE_TEXT;

    uint32_t v;
    v = (uint32_t)job->type;    vals[3] = &v; sizes[3] = 4; types[3] = FUNDB_TYPE_INT;
    v = (uint32_t)job->state;  vals[4] = &v; sizes[4] = 4; types[4] = FUNDB_TYPE_INT;
    v = job->interval_sec;      vals[5] = &v; sizes[5] = 4; types[5] = FUNDB_TYPE_INT;
    v = (uint32_t)job->hour;    vals[6] = &v; sizes[6] = 4; types[6] = FUNDB_TYPE_INT;
    v = (uint32_t)job->minute;  vals[7] = &v; sizes[7] = 4; types[7] = FUNDB_TYPE_INT;
    v = job->run_count;         vals[8] = &v; sizes[8] = 4; types[8] = FUNDB_TYPE_INT;
    v = job->error_count;       vals[9] = &v; sizes[9] = 4; types[9] = FUNDB_TYPE_INT;
    v = (uint32_t)job->last_exit_code; vals[10] = &v; sizes[10] = 4; types[10] = FUNDB_TYPE_INT;

    row.values = vals;
    row.sizes = sizes;
    row.types = types;

    fundb_insert(g_cron.db, CRON_TABLE, &row);
}

static void cron_db_delete_job(uint32_t id) {
    if (!g_cron.db) return;
    char where[64];
    snprintf(where, sizeof(where), "id = %u", id);
    fundb_delete(g_cron.db, CRON_TABLE, where);
}

/* 启动时从 FunDB 加载所有任务 */
static void cron_db_load_all(void) {
    if (!g_cron.db) return;
    fundb_result_t *r = fundb_select(g_cron.db, CRON_TABLE, "*", NULL, NULL, 0);
    if (!r) return;

    for (uint32_t i = 0; i < r->row_count && g_cron.job_count < CRON_MAX_JOBS; i++) {
        cron_job_t *job = &g_cron.jobs[g_cron.job_count];
        memset(job, 0, sizeof(*job));

        if (!r->rows[i].values[0]) continue;
        job->id = *(uint32_t *)r->rows[i].values[0];

        if (r->rows[i].values[1]) {
            strncpy(job->name, (const char *)r->rows[i].values[1], CRON_MAX_NAME - 1);
        }
        if (r->rows[i].values[2]) {
            strncpy(job->command, (const char *)r->rows[i].values[2], CRON_MAX_CMD_LEN - 1);
        }
        if (r->rows[i].values[3]) job->type = *(cron_type_t *)r->rows[i].values[3];
        if (r->rows[i].values[4]) job->state = *(cron_state_t *)r->rows[i].values[4];
        if (r->rows[i].values[5]) job->interval_sec = *(uint32_t *)r->rows[i].values[5];
        if (r->rows[i].values[6]) job->hour = (uint8_t)*(uint32_t *)r->rows[i].values[6];
        if (r->rows[i].values[7]) job->minute = (uint8_t)*(uint32_t *)r->rows[i].values[7];
        if (r->rows[i].values[8]) job->run_count = *(uint32_t *)r->rows[i].values[8];
        if (r->rows[i].values[9]) job->error_count = *(uint32_t *)r->rows[i].values[9];
        if (r->rows[i].values[10]) job->last_exit_code = (int32_t)*(uint32_t *)r->rows[i].values[10];

        /* 更新 next_id */
        if (job->id >= g_cron.next_id) g_cron.next_id = job->id + 1;

        /* 重新计算 next_run_tick（基于当前时刻） */
        uint64_t now = cron_now_tick();
        if (job->state == CRON_STATE_ENABLED) {
            switch (job->type) {
                case CRON_TYPE_ONCE:
                    /* 一次性任务重启后直接作废（错过执行时刻） */
                    job->state = CRON_STATE_DISABLED;
                    break;
                case CRON_TYPE_INTERVAL:
                    job->next_run_tick = now + cron_sec_to_ticks(job->interval_sec);
                    break;
                case CRON_TYPE_DAILY:
                    /* 简化：安排到下一个整点的 hour:minute
                     * 这里仅按秒数粗略计算（不精确，但够用） */
                    job->next_run_tick = now + cron_sec_to_ticks(60);
                    break;
            }
        }
        job->last_run_tick = 0;

        g_cron.job_count++;
    }
    fundb_free_result(r);
    klog_info("cron: loaded %u jobs from FunDB", g_cron.job_count);
}

/* ============================================================
 * 内部辅助
 * ============================================================ */

/* 找空闲槽位，返回索引，-1 表示已满 */
static int cron_find_free_slot(void) {
    for (int i = 0; i < CRON_MAX_JOBS; i++) {
        if (g_cron.jobs[i].id == 0) return i;
    }
    return -1;
}

/* 按 ID 查找槽位索引，-1 表示不存在 */
static int cron_find_slot(uint32_t id) {
    if (id == CRON_INVALID_ID) return -1;
    for (int i = 0; i < CRON_MAX_JOBS; i++) {
        if (g_cron.jobs[i].id == id) return i;
    }
    return -1;
}

/* 计算任务下次执行时刻（基于当前时刻） */
static uint64_t cron_compute_next_run(const cron_job_t *job, uint64_t now) {
    switch (job->type) {
        case CRON_TYPE_ONCE:
        case CRON_TYPE_INTERVAL:
            return now + cron_sec_to_ticks(job->interval_sec);
        case CRON_TYPE_DAILY:
            /* 简化实现：6 小时后再次执行（避免复杂日期计算） */
            return now + cron_sec_to_ticks(6 * 3600);
        default:
            return now + cron_sec_to_ticks(3600);
    }
}

/* 执行一个任务（调用 executor） */
static void cron_execute_job(cron_job_t *job) {
    if (!job) return;
    job->state = CRON_STATE_RUNNING;
    job->last_run_tick = cron_now_tick();

    /* 审计：任务开始执行 */
    evlog_info("Cron", job->id,
               "job '%s' (id=%u) started: %s", job->name, job->id, job->command);

    int32_t rc = 0;
    if (g_cron.executor) {
        rc = g_cron.executor(job->command);
    } else {
        klog_info("cron: job %u '%s' would run: %s (no executor)",
                  job->id, job->name, job->command);
        rc = 0;
    }

    job->last_exit_code = rc;
    job->run_count++;
    g_cron.total_runs++;

    if (rc != 0) {
        job->error_count++;
        g_cron.total_errors++;
        klog_warn("cron: job %u '%s' failed (exit=%d)",
                  job->id, job->name, rc);
        /* 审计：任务失败 */
        evlog_err("Cron", job->id,
                  "job '%s' (id=%u) FAILED exit=%d errors=%u",
                  job->name, job->id, rc, job->error_count);
        if (job->error_count >= 5 && job->type != CRON_TYPE_ONCE) {
            job->state = CRON_STATE_ERROR;
            klog_err("cron: job %u disabled due to repeated errors", job->id);
            evlog_crit("Cron", job->id,
                       "job '%s' (id=%u) DISABLED after %u errors",
                       job->name, job->id, job->error_count);
        }
    } else {
        /* 审计：任务成功完成 */
        evlog_info("Cron", job->id,
                   "job '%s' (id=%u) completed exit=0 runs=%u",
                   job->name, job->id, job->run_count);
    }

    /* 持久化运行统计 */
    cron_db_save_job(job);
}

/* ============================================================
 * 公开 API
 * ============================================================ */

void cron_init(void) {
    if (g_cron.initialized) return;
    memset(&g_cron, 0, sizeof(g_cron));
    spinlock_init(&g_cron.lock);
    g_cron.next_id = 1;
    g_cron.executor = NULL;

    /* 打开 FunDB */
    g_cron.db = fundb_open(CRON_DB_PATH);
    if (g_cron.db) {
        cron_db_init_table();
        spinlock_lock(&g_cron.lock);
        cron_db_load_all();
        spinlock_unlock(&g_cron.lock);
    } else {
        klog_warn("cron: FunDB unavailable, running in-memory only");
    }

    /* 启动周期性 kwork（每秒检查一次） */
    kwork_init_work(&g_cron.tick_work, cron_tick, NULL);
    kwork_queue_periodic_work(&g_cron.tick_work, CRON_TICK_MS);

    g_cron.initialized = 1;
    klog_info("cron: scheduler initialized (tick=%dms)", CRON_TICK_MS);
}

void cron_shutdown(void) {
    if (!g_cron.initialized) return;
    kwork_cancel_work(&g_cron.tick_work);
    /* 刷盘 */
    spinlock_lock(&g_cron.lock);
    if (g_cron.db) {
        fundb_close(g_cron.db);
        g_cron.db = NULL;
    }
    spinlock_unlock(&g_cron.lock);
    g_cron.initialized = 0;
}

uint32_t cron_add_job(const cron_job_t *job) {
    if (!job || !job->command[0]) return CRON_INVALID_ID;

    spinlock_lock(&g_cron.lock);
    int slot = cron_find_free_slot();
    if (slot < 0) {
        spinlock_unlock(&g_cron.lock);
        klog_warn("cron: job table full (%u/%u)", g_cron.job_count, CRON_MAX_JOBS);
        return CRON_INVALID_ID;
    }

    cron_job_t *dst = &g_cron.jobs[slot];
    memset(dst, 0, sizeof(*dst));
    dst->id = g_cron.next_id++;
    dst->type = job->type;
    dst->state = job->state == CRON_STATE_DISABLED ?
                 CRON_STATE_DISABLED : CRON_STATE_ENABLED;
    dst->interval_sec = job->interval_sec;
    dst->hour = job->hour;
    dst->minute = job->minute;

    strncpy(dst->name, job->name, CRON_MAX_NAME - 1);
    strncpy(dst->command, job->command, CRON_MAX_CMD_LEN - 1);

    dst->next_run_tick = cron_compute_next_run(dst, cron_now_tick());
    dst->last_run_tick = 0;
    dst->run_count = 0;
    dst->error_count = 0;
    dst->last_exit_code = 0;

    g_cron.job_count++;
    cron_db_save_job(dst);
    spinlock_unlock(&g_cron.lock);

    klog_info("cron: added job %u '%s' (type=%d)", dst->id, dst->name, dst->type);
    return dst->id;
}

int cron_remove_job(uint32_t id) {
    spinlock_lock(&g_cron.lock);
    int slot = cron_find_slot(id);
    if (slot < 0) {
        spinlock_unlock(&g_cron.lock);
        return -1;
    }
    cron_db_delete_job(g_cron.jobs[slot].id);
    memset(&g_cron.jobs[slot], 0, sizeof(g_cron.jobs[slot]));
    g_cron.job_count--;
    spinlock_unlock(&g_cron.lock);
    klog_info("cron: removed job %u", id);
    return 0;
}

int cron_enable_job(uint32_t id) {
    spinlock_lock(&g_cron.lock);
    int slot = cron_find_slot(id);
    if (slot < 0) {
        spinlock_unlock(&g_cron.lock);
        return -1;
    }
    cron_job_t *job = &g_cron.jobs[slot];
    if (job->state == CRON_STATE_ERROR) {
        /* 错误状态下启用会重置错误计数 */
        job->error_count = 0;
    }
    job->state = CRON_STATE_ENABLED;
    job->next_run_tick = cron_compute_next_run(job, cron_now_tick());
    cron_db_save_job(job);
    spinlock_unlock(&g_cron.lock);
    return 0;
}

int cron_disable_job(uint32_t id) {
    spinlock_lock(&g_cron.lock);
    int slot = cron_find_slot(id);
    if (slot < 0) {
        spinlock_unlock(&g_cron.lock);
        return -1;
    }
    g_cron.jobs[slot].state = CRON_STATE_DISABLED;
    cron_db_save_job(&g_cron.jobs[slot]);
    spinlock_unlock(&g_cron.lock);
    return 0;
}

int cron_run_job_now(uint32_t id) {
    spinlock_lock(&g_cron.lock);
    int slot = cron_find_slot(id);
    if (slot < 0) {
        spinlock_unlock(&g_cron.lock);
        return -1;
    }
    if (!g_cron.executor) {
        spinlock_unlock(&g_cron.lock);
        return -2;
    }
    cron_job_t *job = &g_cron.jobs[slot];
    /* 手动触发：执行但不重排下次时刻（除非是 interval/daily 类型） */
    cron_execute_job(job);

    if (job->state == CRON_STATE_RUNNING) {
        /* 执行成功，恢复状态 */
        if (job->type == CRON_TYPE_ONCE) {
            job->state = CRON_STATE_DISABLED;
        } else {
            job->state = CRON_STATE_ENABLED;
            job->next_run_tick = cron_compute_next_run(job, cron_now_tick());
        }
        cron_db_save_job(job);
    }
    spinlock_unlock(&g_cron.lock);
    return job->last_exit_code;
}

cron_job_t *cron_find_job(uint32_t id) {
    spinlock_lock(&g_cron.lock);
    int slot = cron_find_slot(id);
    cron_job_t *job = (slot >= 0) ? &g_cron.jobs[slot] : NULL;
    spinlock_unlock(&g_cron.lock);
    return job;
}

int cron_list_jobs(cron_job_t *out_jobs, uint32_t max_count) {
    if (!out_jobs || max_count == 0) return 0;
    spinlock_lock(&g_cron.lock);
    uint32_t copied = 0;
    for (uint32_t i = 0; i < CRON_MAX_JOBS && copied < max_count; i++) {
        if (g_cron.jobs[i].id != 0) {
            out_jobs[copied++] = g_cron.jobs[i];
        }
    }
    spinlock_unlock(&g_cron.lock);
    return (int)copied;
}

void cron_get_stats(cron_stats_t *stats) {
    if (!stats) return;
    memset(stats, 0, sizeof(*stats));
    spinlock_lock(&g_cron.lock);
    stats->next_id = g_cron.next_id;
    stats->total_runs = (uint32_t)g_cron.total_runs;
    stats->total_errors = (uint32_t)g_cron.total_errors;
    for (uint32_t i = 0; i < CRON_MAX_JOBS; i++) {
        if (g_cron.jobs[i].id != 0) {
            stats->total_jobs++;
            if (g_cron.jobs[i].state == CRON_STATE_ENABLED ||
                g_cron.jobs[i].state == CRON_STATE_RUNNING) {
                stats->enabled_jobs++;
            }
        }
    }
    spinlock_unlock(&g_cron.lock);
}

void cron_reset_stats(void) {
    spinlock_lock(&g_cron.lock);
    g_cron.total_runs = 0;
    g_cron.total_errors = 0;
    spinlock_unlock(&g_cron.lock);
}

void cron_set_executor(cron_executor_t executor) {
    spinlock_lock(&g_cron.lock);
    g_cron.executor = executor;
    spinlock_unlock(&g_cron.lock);
    klog_info("cron: executor registered");
}

/* ============================================================
 * 调度入口（kwork 周期调用）
 * ============================================================ */

void cron_tick(void *unused) {
    (void)unused;
    if (!g_cron.initialized) return;

    uint64_t now = cron_now_tick();
    /* 收集到期任务（避免长时间持锁） */
    cron_job_t *due[CRON_MAX_JOBS];
    uint32_t due_count = 0;

    spinlock_lock(&g_cron.lock);
    for (uint32_t i = 0; i < CRON_MAX_JOBS; i++) {
        cron_job_t *job = &g_cron.jobs[i];
        if (job->id == 0) continue;
        if (job->state != CRON_STATE_ENABLED) continue;
        if (job->next_run_tick != 0 && now >= job->next_run_tick) {
            due[due_count++] = job;
            if (due_count >= CRON_MAX_JOBS) break;
        }
    }
    spinlock_unlock(&g_cron.lock);

    /* 执行到期任务（无锁，executor 自行保证线程安全） */
    for (uint32_t i = 0; i < due_count; i++) {
        cron_job_t *job = due[i];

        spinlock_lock(&g_cron.lock);
        /* 再次检查状态（可能已被 remove/disable） */
        if (job->id == 0 || job->state != CRON_STATE_ENABLED) {
            spinlock_unlock(&g_cron.lock);
            continue;
        }
        job->state = CRON_STATE_RUNNING;
        spinlock_unlock(&g_cron.lock);

        /* 执行 */
        int32_t rc = 0;
        if (g_cron.executor) {
            rc = g_cron.executor(job->command);
        } else {
            klog_debug("cron: job %u '%s' due (no executor): %s",
                       job->id, job->name, job->command);
        }

        spinlock_lock(&g_cron.lock);
        job->last_exit_code = rc;
        job->run_count++;
        job->last_run_tick = now;
        g_cron.total_runs++;

        if (rc != 0) {
            job->error_count++;
            g_cron.total_errors++;
            klog_warn("cron: job %u '%s' failed (exit=%d, errors=%u)",
                      job->id, job->name, rc, job->error_count);
            if (job->error_count >= 5 && job->type != CRON_TYPE_ONCE) {
                job->state = CRON_STATE_ERROR;
                klog_err("cron: job %u disabled (5+ errors)", job->id);
            } else {
                job->state = CRON_STATE_ENABLED;
            }
        } else {
            /* 成功：安排下次执行 */
            if (job->type == CRON_TYPE_ONCE) {
                job->state = CRON_STATE_DISABLED;
                klog_info("cron: one-shot job %u completed", job->id);
            } else {
                job->state = CRON_STATE_ENABLED;
                job->next_run_tick = cron_compute_next_run(job, now);
            }
        }
        cron_db_save_job(job);
        spinlock_unlock(&g_cron.lock);
    }
}

/* ============================================================
 * 调试
 * ============================================================ */

void cron_dump_all(void) {
    spinlock_lock(&g_cron.lock);
    klog_info("=== Cron Jobs Dump ===");
    klog_info("next_id=%u job_count=%u total_runs=%llu total_errors=%llu",
              g_cron.next_id, g_cron.job_count,
              (unsigned long long)g_cron.total_runs,
              (unsigned long long)g_cron.total_errors);
    for (uint32_t i = 0; i < CRON_MAX_JOBS; i++) {
        cron_job_t *job = &g_cron.jobs[i];
        if (job->id == 0) continue;
        klog_info("  [%u] %-12s type=%d state=%d runs=%u errs=%u cmd='%.60s'",
                  job->id, job->name, job->type, job->state,
                  job->run_count, job->error_count, job->command);
    }
    spinlock_unlock(&g_cron.lock);
}
