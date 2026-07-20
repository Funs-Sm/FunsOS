/* taskmgr.c - 任务管理器实现
 *
 * 通过 process_get_pcb() 遍历进程表，复制到 taskmgr_proc_t 中。
 * kill 操作：设置 pcb->state = PROCESS_ZOMBIE（与现有 cmd_kill 一致），
 *          并记录审计到 evlog (source="SysAdmin") + 内存环形缓冲。
 */
#include "taskmgr.h"
#include "evlog.h"
#include "fundb.h"
#include "klog.h"
#include "spinlock.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "timer.h"
#include "process.h"
#include "sched.h"
#include "kernel_types.h"

#define TASKMGR_EV_SOURCE "SysAdmin"

/* pmm 接口（外部） */
extern uint32_t pmm_get_total_pages(void);
extern uint32_t pmm_get_used_pages(void);

/* 全局状态 */
static struct {
    spinlock_t             lock;
    fundb_handle_t         db;
    int                    initialized;
    taskmgr_kill_record_t  kill_history[TASKMGR_KILL_HISTORY];
    uint32_t               kill_history_head;
    taskmgr_proc_t         proc_cache[TASKMGR_MAX_PROCS];  /* 临时缓存 */
} g_tm;

/* ---- 内部辅助：从 pcb_t 复制到 taskmgr_proc_t ---- */

static void taskmgr_copy_proc(taskmgr_proc_t *out, const pcb_t *p) {
    if (!out || !p) return;
    memset(out, 0, sizeof(*out));
    out->pid            = p->pid;
    out->parent_pid     = p->parent_pid;
    strncpy(out->name, p->name, sizeof(out->name) - 1);
    out->name[sizeof(out->name) - 1] = '\0';
    out->state          = p->state;
    out->priority       = p->priority;
    out->ticks_used     = p->ticks_used;
    out->cpu_time       = p->cpu_time;
    out->nice           = p->nice;
    out->queue_level    = p->queue_level;
    out->blocked_reason = p->blocked_reason;
    out->is_kernel      = (p->type == PROCESS_KERNEL) ? 1 : 0;
}

/* 添加 kill 记录到环形缓冲 */
static void taskmgr_kill_record_add(pid_t pid, const char *name,
                                     const char *reason, int success) {
    taskmgr_kill_record_t *r = &g_tm.kill_history[g_tm.kill_history_head];
    memset(r, 0, sizeof(*r));
    r->pid = pid;
    if (name) {
        strncpy(r->name, name, sizeof(r->name) - 1);
    }
    if (reason) {
        strncpy(r->reason, reason, sizeof(r->reason) - 1);
    }
    r->timestamp = (uint32_t)timer_get_ticks();
    r->success = (uint8_t)success;
    g_tm.kill_history_head = (g_tm.kill_history_head + 1) % TASKMGR_KILL_HISTORY;
}

/* ---- FunDB 表初始化 ---- */

static void taskmgr_db_init_tables(void) {
    if (!g_tm.db) return;

    if (!fundb_table_exists(g_tm.db, TASKMGR_TABLE_STATS)) {
        fundb_column_t cols[6];
        memset(cols, 0, sizeof(cols));

        strcpy(cols[0].name, "snapshot_tick");
        cols[0].type = FUNDB_TYPE_INT; cols[0].size = 4;
        cols[0].not_null = 1; cols[0].primary_key = 1;

        strcpy(cols[1].name, "total_procs");
        cols[1].type = FUNDB_TYPE_INT; cols[1].size = 4;

        strcpy(cols[2].name, "running");
        cols[2].type = FUNDB_TYPE_INT; cols[2].size = 4;

        strcpy(cols[3].name, "blocked");
        cols[3].type = FUNDB_TYPE_INT; cols[3].size = 4;

        strcpy(cols[4].name, "memory_used_kb");
        cols[4].type = FUNDB_TYPE_INT; cols[4].size = 4;

        strcpy(cols[5].name, "cpu_load_percent");
        cols[5].type = FUNDB_TYPE_INT; cols[5].size = 4;

        int rc = fundb_create_table(g_tm.db, TASKMGR_TABLE_STATS, cols, 6);
        if (rc != FUNDB_OK) {
            klog_warn("taskmgr: failed to create %s (%s)",
                      TASKMGR_TABLE_STATS, fundb_error_string(rc));
        }
    }
}

/* ---- 公共 API ---- */

void taskmgr_init(void) {
    memset(&g_tm, 0, sizeof(g_tm));
    spinlock_init(&g_tm.lock);

    g_tm.db = fundb_open(TASKMGR_DB_PATH);
    if (!g_tm.db) {
        klog_warn("taskmgr: failed to open %s (snapshots disabled)", TASKMGR_DB_PATH);
    }
    taskmgr_db_init_tables();

    evlog_register_source(TASKMGR_EV_SOURCE);

    g_tm.initialized = 1;
    klog_info("taskmgr: task manager initialized");
}

void taskmgr_shutdown(void) {
    if (!g_tm.initialized) return;
    if (g_tm.db) {
        fundb_close(g_tm.db);
        g_tm.db = NULL;
    }
    g_tm.initialized = 0;
}

/* ---- 进程枚举 ---- */

uint32_t taskmgr_list_procs(taskmgr_proc_t *out, uint32_t max_count) {
    if (!out) return 0;
    uint32_t count = 0;
    for (pid_t pid = 0; pid < MAX_PROCESSES && count < max_count; pid++) {
        pcb_t *p = process_get_pcb(pid);
        if (!p || p->state == PROCESS_UNUSED) continue;
        taskmgr_copy_proc(&out[count], p);
        count++;
    }
    return count;
}

taskmgr_proc_t *taskmgr_get_proc(pid_t pid, taskmgr_proc_t *out) {
    if (!out || pid < 0 || pid >= MAX_PROCESSES) return NULL;
    pcb_t *p = process_get_pcb(pid);
    if (!p || p->state == PROCESS_UNUSED) return NULL;
    taskmgr_copy_proc(out, p);
    return out;
}

/* ---- Top-N CPU ---- */

uint32_t taskmgr_get_top_cpu(taskmgr_proc_t *out, uint32_t max_count) {
    if (!out || max_count == 0) return 0;

    /* 复制到缓存，然后排序 */
    uint32_t count = taskmgr_list_procs(g_tm.proc_cache, TASKMGR_MAX_PROCS);

    /* 简单选择排序：按 ticks_used 降序 */
    uint32_t n = count < max_count ? count : max_count;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t max_idx = i;
        for (uint32_t j = i + 1; j < count; j++) {
            if (g_tm.proc_cache[j].ticks_used > g_tm.proc_cache[max_idx].ticks_used) {
                max_idx = j;
            }
        }
        if (max_idx != i) {
            taskmgr_proc_t tmp = g_tm.proc_cache[i];
            g_tm.proc_cache[i] = g_tm.proc_cache[max_idx];
            g_tm.proc_cache[max_idx] = tmp;
        }
        out[i] = g_tm.proc_cache[i];
    }
    return n;
}

/* ---- 汇总 ---- */

void taskmgr_get_summary(taskmgr_summary_t *summary) {
    if (!summary) return;
    memset(summary, 0, sizeof(*summary));

    summary->total_ticks = sched_get_tick_count();

    /* 遍历进程表统计 */
    for (pid_t pid = 0; pid < MAX_PROCESSES; pid++) {
        pcb_t *p = process_get_pcb(pid);
        if (!p || p->state == PROCESS_UNUSED) continue;
        summary->total_procs++;
        switch (p->state) {
            case PROCESS_RUNNING: summary->running++; break;
            case PROCESS_READY:   summary->ready++; break;
            case PROCESS_BLOCKED: summary->blocked++; break;
            case PROCESS_ZOMBIE:  summary->zombie++; break;
            default: break;
        }
        if (p->type == PROCESS_KERNEL) summary->kernel_procs++;
        else summary->user_procs++;
    }
    summary->total_threads = summary->total_procs;

    /* 全局调度统计（如果可用） */
    const sched_global_stats_t *gstats = sched_get_global_stats();
    if (gstats) {
        summary->context_switches = (uint32_t)gstats->total_context_switches;
        summary->user_ticks = gstats->user_ticks;
        summary->kernel_ticks = gstats->kernel_ticks;
        summary->idle_ticks = gstats->idle_ticks;
    }

    /* 内存使用 */
    uint32_t total_pages = pmm_get_total_pages();
    uint32_t used_pages = pmm_get_used_pages();
    summary->total_memory_kb = total_pages * 4;  /* 4KB per page */
    summary->used_memory_kb = used_pages * 4;
    summary->free_memory_kb = (total_pages > used_pages) ?
                              (total_pages - used_pages) * 4 : 0;

    /* CPU 负载估算：基于过去 100 tick 的 idle 占比 */
    summary->cpu_load_percent = sched_get_avg_load();
}

/* ---- Kill with audit ---- */

int taskmgr_kill(pid_t pid, const char *reason) {
    if (pid < 0 || pid >= MAX_PROCESSES) return -1;

    pcb_t *p = process_get_pcb(pid);
    if (!p || p->state == PROCESS_UNUSED) return -2;
    if (p->state == PROCESS_ZOMBIE) return -3;

    /* 记录 kill */
    spinlock_lock(&g_tm.lock);
    taskmgr_kill_record_add(pid, p->name, reason ? reason : "unspecified", 1);
    spinlock_unlock(&g_tm.lock);

    /* 设置为 ZOMBIE（与现有 cmd_kill 一致） */
    p->state = PROCESS_ZOMBIE;
    p->exit_status = 9; /* SIGKILL */

    /* 审计到 evlog */
    evlog_warn(TASKMGR_EV_SOURCE, 10,
               "process killed pid=%d name='%s' reason='%s' caller_pid=%d",
               pid, p->name, reason ? reason : "unspecified",
               (int)(sched_get_current() ? sched_get_current()->pid : 0));

    return 0;
}

int taskmgr_kill_by_name(const char *name, const char *reason) {
    if (!name) return -1;
    int killed = 0;
    for (pid_t pid = 0; pid < MAX_PROCESSES; pid++) {
        pcb_t *p = process_get_pcb(pid);
        if (!p || p->state == PROCESS_UNUSED) continue;
        if (strcmp(p->name, name) != 0) continue;
        if (p->state == PROCESS_ZOMBIE) continue;
        if (taskmgr_kill(pid, reason) == 0) killed++;
    }
    if (killed == 0) return -1;
    return killed;
}

/* ---- Kill 历史 ---- */

uint32_t taskmgr_kill_history(taskmgr_kill_record_t *out, uint32_t max_count) {
    if (!out) return 0;
    spinlock_lock(&g_tm.lock);
    uint32_t n = 0;
    /* 从最新到最旧 */
    for (uint32_t i = 0; i < TASKMGR_KILL_HISTORY && n < max_count; i++) {
        uint32_t idx = (g_tm.kill_history_head + TASKMGR_KILL_HISTORY - 1 - i)
                       % TASKMGR_KILL_HISTORY;
        taskmgr_kill_record_t *r = &g_tm.kill_history[idx];
        if (!r->name[0] && !r->reason[0] && r->pid == 0) continue;
        out[n++] = *r;
    }
    spinlock_unlock(&g_tm.lock);
    return n;
}

/* ---- 状态名称 ---- */

const char *taskmgr_state_name(uint32_t state) {
    switch (state) {
        case PROCESS_UNUSED:  return "UNUSED";
        case PROCESS_READY:   return "READY";
        case PROCESS_RUNNING: return "RUNNING";
        case PROCESS_BLOCKED: return "BLOCKED";
        case PROCESS_ZOMBIE:  return "ZOMBIE";
        default: return "UNKNOWN";
    }
}

/* ---- 快照持久化 ---- */

int taskmgr_save_snapshot(void) {
    if (!g_tm.db) return -1;

    taskmgr_summary_t s;
    taskmgr_get_summary(&s);

    uint32_t snapshot_tick = (uint32_t)timer_get_ticks();
    uint32_t total_procs = s.total_procs;
    uint32_t running = s.running;
    uint32_t blocked = s.blocked;
    uint32_t mem_used = s.used_memory_kb;
    uint32_t cpu_load = s.cpu_load_percent;

    /* 删除旧快照（保留最近 100 条） */
    /* 简化：直接插入，依赖人工清理 */

    void *vals[6];
    uint32_t sizes[6];
    uint32_t types[6];

    vals[0] = &snapshot_tick; sizes[0] = 4; types[0] = FUNDB_TYPE_INT;
    vals[1] = &total_procs;    sizes[1] = 4; types[1] = FUNDB_TYPE_INT;
    vals[2] = &running;         sizes[2] = 4; types[2] = FUNDB_TYPE_INT;
    vals[3] = &blocked;         sizes[3] = 4; types[3] = FUNDB_TYPE_INT;
    vals[4] = &mem_used;        sizes[4] = 4; types[4] = FUNDB_TYPE_INT;
    vals[5] = &cpu_load;        sizes[5] = 4; types[5] = FUNDB_TYPE_INT;

    fundb_row_t row;
    row.values = vals; row.sizes = sizes; row.types = types;
    int rc = fundb_insert(g_tm.db, TASKMGR_TABLE_STATS, &row);
    if (rc != FUNDB_OK) {
        klog_warn("taskmgr: snapshot insert failed (%s)", fundb_error_string(rc));
        return -2;
    }
    return 0;
}
