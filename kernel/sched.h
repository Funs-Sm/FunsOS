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

#define BLOCK_REASON_NONE   0
#define BLOCK_REASON_IO     1
#define BLOCK_REASON_SLEEP  2
#define BLOCK_REASON_WAIT   3
#define BLOCK_REASON_MUTEX  4

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

#endif
