/* taskmgr.h - 任务管理器 (Task Manager)
 *
 * 提供进程资源统计、Top-N CPU 占用、审计 kill、汇总概览。
 *
 * 数据来源：
 *   - process_get_pcb(pid) 遍历进程表 (0..MAX_PROCESSES-1)
 *   - pcb_t 字段：name, state, priority, ticks_used, cpu_time, parent_pid
 *   - sched_get_global_stats() 全局调度统计
 *   - pmm_get_total_pages()/pmm_get_used_pages() 物理内存使用
 *
 * 与既有命令的关系：
 *   - ps/top/kill 是简单 shell 命令，没有审计、没有 top-N 排序
 *   - taskmgr 提供聚合视图 + 审计 + 持久化统计快照（可选）
 */
#ifndef TASKMGR_H
#define TASKMGR_H

#include "stdint.h"
#include "kernel_proc.h"

#define TASKMGR_MAX_PROCS    256
#define TASKMGR_SNAPSHOT_SIZE 32
#define TASKMGR_DB_PATH       "/var/db/taskmgr.db"
#define TASKMGR_TABLE_STATS   "taskmgr_stats"

/* 进程信息快照（从 pcb_t 复制，便于排序与持久化） */
typedef struct taskmgr_proc {
    pid_t    pid;
    pid_t    parent_pid;
    char     name[32];
    uint32_t state;
    uint32_t priority;
    uint32_t ticks_used;
    uint32_t cpu_time;
    uint32_t nice;
    uint32_t queue_level;
    int32_t  blocked_reason;
    uint8_t  is_kernel;
} taskmgr_proc_t;

/* 系统汇总 */
typedef struct taskmgr_summary {
    uint32_t total_procs;
    uint32_t running;
    uint32_t ready;
    uint32_t blocked;
    uint32_t zombie;
    uint32_t kernel_procs;
    uint32_t user_procs;
    uint32_t total_threads;     /* 与 total_procs 相同（暂无线程独立计数） */
    uint64_t total_ticks;
    uint64_t idle_ticks;
    uint64_t user_ticks;
    uint64_t kernel_ticks;
    uint32_t context_switches;
    uint32_t total_memory_kb;
    uint32_t used_memory_kb;
    uint32_t free_memory_kb;
    uint32_t cpu_load_percent;
} taskmgr_summary_t;

/* kill 审计记录 */
typedef struct taskmgr_kill_record {
    pid_t    pid;
    char     name[32];
    char     reason[64];
    uint32_t timestamp;
    uint8_t  success;
} taskmgr_kill_record_t;

#define TASKMGR_KILL_HISTORY 32

/* ---- 初始化 ---- */
void taskmgr_init(void);
void taskmgr_shutdown(void);

/* ---- 进程枚举 ---- */
uint32_t taskmgr_list_procs(taskmgr_proc_t *out, uint32_t max_count);
taskmgr_proc_t *taskmgr_get_proc(pid_t pid, taskmgr_proc_t *out);

/* ---- Top-N CPU 占用（按 ticks_used 降序） ---- */
uint32_t taskmgr_get_top_cpu(taskmgr_proc_t *out, uint32_t max_count);

/* ---- 汇总 ---- */
void taskmgr_get_summary(taskmgr_summary_t *summary);

/* ---- Kill with audit ---- */
int taskmgr_kill(pid_t pid, const char *reason);
int taskmgr_kill_by_name(const char *name, const char *reason);

/* ---- Kill 历史 ---- */
uint32_t taskmgr_kill_history(taskmgr_kill_record_t *out, uint32_t max_count);

/* ---- 状态名称 ---- */
const char *taskmgr_state_name(uint32_t state);

/* ---- 快照持久化（写入 FunDB，便于历史查询） ---- */
int taskmgr_save_snapshot(void);

#endif /* TASKMGR_H */
