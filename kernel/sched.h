#ifndef SCHED_H
#define SCHED_H

#include "kernel_proc.h"
#include "sync.h"

#define SCHED_QUEUE_COUNT 8
#define SCHED_RT_QUEUE_COUNT 2
#define SCHED_PRIORITY_MIN 0
#define SCHED_PRIORITY_MAX 100
#define SCHED_DEFAULT_PRIORITY 50

#define PROCESS_REAL_TIME 0x0001
#define PROCESS_NORMAL    0x0002
#define PROCESS_IDLE      0x0004
#define PROCESS_CFS       0x0008  /* CFS完全公平调度策略 */
#define PROCESS_DEADLINE  0x0010  /* Deadline调度策略 */
#define PROCESS_BATCH     0x0020  /* 批处理调度策略 */
#define PROCESS_IDLE_PRIO 0x0040  /* 空闲优先级 */

#define BLOCK_REASON_NONE   0
#define BLOCK_REASON_IO     1
#define BLOCK_REASON_SLEEP  2
#define BLOCK_REASON_WAIT   3
#define BLOCK_REASON_MUTEX  4
#define BLOCK_REASON_WAITPID 5
#define BLOCK_REASON_SIGNAL  6
#define BLOCK_REASON_FUTEX   7

/* CPU亲和性 */
#define MAX_CPU_AFFINITY 32
typedef struct cpu_affinity {
    uint32_t cpumask;
    uint32_t cpu_count;
} cpu_affinity_t;

/* Deadline调度参数 */
typedef struct deadline_params {
    uint64_t runtime;      /* 运行时间 (ns/ticks) */
    uint64_t deadline;     /* 相对截止时间 */
    uint64_t period;       /* 周期 */
    uint64_t remaining_runtime;
    uint64_t current_deadline;
    uint8_t  active;
} deadline_params_t;

/* 调度统计信息 */
typedef struct sched_stat {
    uint64_t sched_count;       /* 被调度次数 */
    uint64_t total_runtime;     /* 总运行时间(ticks) */
    uint64_t max_runtime;       /* 最长单次运行 */
    uint64_t min_runtime;       /* 最短单次运行 */
    uint64_t avg_runtime;       /* 平均运行时间 */
    uint64_t wait_time;         /* 总等待时间 */
    uint64_t context_switches;  /* 上下文切换次数 */
    uint64_t voluntary_ctx;     /* 主动让出次数 */
    uint64_t involuntary_ctx;   /* 被动抢占次数 */
    uint64_t last_sched_time;   /* 上次调度时间 */
} sched_stat_t;

typedef struct sched_queue {
    pcb_t *head;
    pcb_t *tail;
    uint32_t count;
    uint32_t priority;
} sched_queue_t;

typedef struct {
    sched_queue_t rt_queues[SCHED_RT_QUEUE_COUNT];
    sched_queue_t mlfq_queues[SCHED_QUEUE_COUNT];
    pcb_t *idle_task;
    pcb_t *current;
    spinlock_t lock;
    uint64_t tick_count;
    uint32_t in_schedule;
} sched_state_t;

void sched_init(void);
void sched_create_idle_task(void);
void sched_set_current(pcb_t *proc);
void sched_add(pcb_t *proc);
void sched_remove(pcb_t *proc);
void sched_tick(void);
void schedule(void);
void sched_block(pcb_t *proc, int reason);
void sched_unblock(pcb_t *proc);
pcb_t *sched_get_current(void);
int sched_set_priority(pcb_t *proc, uint32_t priority);
void sched_yield(void);
void sched_sleep(uint32_t milliseconds);
void sched_wakeup_sleepers(void);
pcb_t *sched_find_process(pid_t pid);
int sched_set_policy(pcb_t *proc, uint32_t policy);
uint32_t sched_get_tick_count(void);
void sched_print_stats(void);

/* Trampoline for newly created processes: enters user mode via iret.
 * Called from context_switch when a new process is first scheduled. */
void process_first_run(void);

/* Block the currently running process.  Used by I/O waiters (TCP/UDP,
 * sockets, pipes) that need to suspend until a peer event wakes them. */
static inline void sched_block_current(int reason) {
    pcb_t *p = sched_get_current();
    if (p) sched_block(p, reason);
}

/* ============================================================
 * CFS (Completely Fair Scheduler) 完全公平调度器
 * ============================================================ */

#define CFS_MAX_PROCS 256
#define sched_min_granularity  10   /* 最小调度粒度 (tick) */
#define sched_latency          20   /* 调度周期 (tick) */
#define sched_wakeup_granularity 4  /* 唤醒抢占粒度 (tick) */

typedef struct cfs_node {
    pcb_t          *proc;
    uint64_t        vruntime;   /* 虚拟运行时间 */
    struct cfs_node *next;
} cfs_node_t;

typedef struct cfs_rq {
    cfs_node_t *head;           /* vruntime最小的节点 (leftmost) */
    pcb_t      *curr;           /* 当前运行的进程 */
    uint64_t    min_vruntime;   /* CFS队列最小虚拟运行时间 */
    uint32_t    nr_running;     /* 队列中运行进程数 */
} cfs_rq_t;

void sched_cfs_init(void);
void sched_cfs_enqueue(pcb_t *proc);
pcb_t *sched_cfs_dequeue(void);
uint64_t sched_calc_vruntime(pcb_t *proc, uint64_t delta_exec);
void sched_cfs_tick(pcb_t *proc);
int sched_set_policy_cfs(pcb_t *proc);

/* CFS 标准接口 (enqueue_entity/pick_next_entity/task_tick_fair) */
void enqueue_entity(cfs_rq_t *rq, pcb_t *se);
pcb_t *pick_next_entity(cfs_rq_t *rq);
void task_tick_fair(cfs_rq_t *rq, pcb_t *curr);
uint32_t sched_slice(cfs_rq_t *rq);
int wakeup_preempt(cfs_rq_t *rq, pcb_t *se);

/* ============================================================
 * 负载均衡模块
 * ============================================================ */

#define SCHED_LOAD_WINDOW 100 /* 采样窗口大小 */

typedef struct load_stat {
    uint32_t cpu_load[SCHED_LOAD_WINDOW]; /* CPU利用率0-100 */
    uint32_t window_pos;
    uint32_t avg_load;
} load_stat_t;

void sched_load_sample(uint32_t load);
uint32_t sched_get_avg_load(void);

/* ============================================================
 * 进程组调度模块
 * ============================================================ */

#define MAX_PROCESS_GROUPS 16

typedef struct process_group {
    pid_t    pgid;
    pid_t    leader_pid;
    uint32_t member_count;
    uint32_t cpu_share;  /* 组CPU配额(1024基准) */
    uint8_t  used;
} process_group_t;

int  sched_create_process_group(pid_t leader);
int  sched_add_to_group(pid_t pid, pid_t pgid);
int  sched_remove_from_group(pid_t pid);
process_group_t *sched_get_group(pid_t pgid);

/* ============================================================
 * 调度器调试接口
 * ============================================================ */

void sched_dump_queue(int queue_index); /* 打印指定队列内容 */
void sched_set_debug_level(int level);  /* 0=off, 1=basic, 2=verbose */
int  sched_get_debug_level(void);

/* ============================================================
 * Deadline 调度器 (SCHED_DEADLINE)
 * ============================================================ */

#define SCHED_DL_MAX_PROCS 64
#define SCHED_DL_GRANULARITY 1

typedef struct dl_node {
    pcb_t              *proc;
    deadline_params_t   params;
    struct dl_node     *next;
    struct dl_node     *prev;
} dl_node_t;

typedef struct dl_rq {
    dl_node_t *head;
    dl_node_t *tail;
    uint32_t   nr_running;
    uint64_t   running_bw;
} dl_rq_t;

void sched_dl_init(void);
int  sched_set_policy_deadline(pcb_t *proc, uint64_t runtime,
                               uint64_t deadline, uint64_t period);
void sched_dl_enqueue(pcb_t *proc);
pcb_t *sched_dl_dequeue(void);
void sched_dl_tick(pcb_t *proc);
int  sched_dl_check_preempt(pcb_t *curr, pcb_t *new);

/* ============================================================
 * CPU 亲和性 (CPU Affinity)
 * ============================================================ */

int  sched_set_affinity(pcb_t *proc, uint32_t cpumask);
uint32_t sched_get_affinity(pcb_t *proc);
int  sched_set_affinity_pid(pid_t pid, uint32_t cpumask);
uint32_t sched_get_affinity_pid(pid_t pid);

/* ============================================================
 * 优先级继承 (Priority Inheritance)
 * ============================================================ */

#define PI_MAX_DEPTH 8

typedef struct pi_info {
    pcb_t   *owner;
    uint32_t original_priority;
    uint32_t boosted_priority;
    uint8_t   boosted;
} pi_info_t;

void sched_pi_init(void);
int  sched_pi_boost(pcb_t *proc, uint32_t new_priority);
int  sched_pi_unboost(pcb_t *proc);
int  sched_pi_mutex_lock(pcb_t *holder, pcb_t *waiter);
int  sched_pi_mutex_unlock(pcb_t *holder);

/* ============================================================
 * 调度统计 (Scheduler Statistics)
 * ============================================================ */

typedef struct sched_global_stats {
    uint64_t total_context_switches;
    uint64_t total_ticks;
    uint64_t idle_ticks;
    uint64_t user_ticks;
    uint64_t kernel_ticks;
    uint64_t preempt_count;
    uint64_t yield_count;
} sched_global_stats_t;

void sched_stats_init(void);
void sched_stats_account(pcb_t *proc, int ticks_used, int voluntary);
sched_stat_t *sched_get_proc_stats(pid_t pid);
const sched_global_stats_t *sched_get_global_stats(void);
void sched_stats_print(void);
void sched_stats_reset(void);

/* ============================================================
 * 批处理调度 (Batch Scheduling)
 * ============================================================ */

#define BATCH_MAX_JOBS 32

typedef struct batch_job {
    pid_t pid;
    uint32_t priority;
    uint64_t est_runtime;
    uint8_t  used;
} batch_job_t;

void sched_batch_init(void);
int  sched_batch_add(pid_t pid, uint32_t prio, uint64_t est_runtime);
int  sched_batch_remove(pid_t pid);
void sched_batch_tick(void);

#endif
