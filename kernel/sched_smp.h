#ifndef SCHED_SMP_H
#define SCHED_SMP_H

#include "stdint.h"
#include "sched_cfs.h"

/* SMP (Symmetric Multi-Processing) scheduler.
 *
 * Each CPU maintains its own runqueue. Tasks are bound by affinity
 * masks. A periodic load balancer selects a CPU from which to pull
 * tasks if its load drops below the global average.
 *
 * Functions exported:
 *   - per-CPU init / shutdown
 *   - task create / fork / wake / sleep
 *   - schedule_next: pick the next entity to run on a given CPU
 *   - load_balance: periodic balancer that pulls tasks across CPUs
 *   - IPI scheduler kick (x86-specific)
 */

#define SMP_MAX_CPUS         32
#define SMP_MAX_TASKS        1024

typedef enum {
    TASK_RUNNING = 0,
    TASK_READY,
    TASK_BLOCKED,
    TASK_SLEEPING,
    TASK_ZOMBIE,
    TASK_IDLE,
} task_state_t;

typedef struct smp_task {
    sched_entity_t   cfs_se;
    uint32_t         pid;
    int              cpu;
    int              on_cpu;
    int              last_cpu;
    task_state_t     state;
    uint64_t         wakeup_time;       /* absolute timestamp */
    uint32_t         cpumask;           /* bitmask */
    uint32_t         policy;            /* CFS / RT / DEADLINE */
    int              nice;
    char             name[32];
    /* Tree linkage. */
    struct smp_task *next;
    struct smp_task *prev;
} smp_task_t;

typedef struct {
    int             cpu_id;
    int             active;
    cfs_rq_t        cfs_rq;
    smp_task_t     *idle_task;
    smp_task_t     *current_task;
    smp_task_t     *run_queue_head;
    smp_task_t     *run_queue_tail;
    uint32_t        nr_running;
    uint64_t        last_balance;
    uint64_t        idle_time_ns;
    uint64_t        exec_time_ns;
    uint32_t        weight_sum;
} smp_runqueue_t;

typedef struct {
    smp_runqueue_t  rq[SMP_MAX_CPUS];
    smp_task_t      tasks[SMP_MAX_TASKS];
    int             nr_cpus;
    int             nr_tasks;
    int             inited;
    /* Global stats. */
    uint64_t        ctx_switches;
    uint64_t        load_balance_runs;
    uint64_t        ipi_count;
    uint64_t        idle_ticks;
} smp_scheduler_t;

int  smp_sched_init(smp_scheduler_t *s, int nr_cpus);
int  smp_sched_register_cpu(smp_scheduler_t *s, int cpu);
int  smp_sched_create_task(smp_scheduler_t *s, const char *name,
                            int nice, int cpu, uint32_t *pid_out);
int  smp_sched_wakeup(smp_scheduler_t *s, uint32_t pid);
int  smp_sched_sleep(smp_scheduler_t *s, uint32_t pid, uint64_t wakeup_time);
int  smp_sched_exit(smp_scheduler_t *s, uint32_t pid);
int  smp_sched_set_affinity(smp_scheduler_t *s, uint32_t pid,
                              uint32_t cpumask);
int  smp_sched_set_nice(smp_scheduler_t *s, uint32_t pid, int nice);

smp_task_t *smp_sched_pick_next(smp_scheduler_t *s, int cpu);
int  smp_sched_run_tick(smp_scheduler_t *s, int cpu, uint64_t delta_ns);
int  smp_sched_load_balance(smp_scheduler_t *s);

int  smp_sched_send_ipi(smp_scheduler_t *s, int target_cpu, int reason);
void smp_sched_print(smp_scheduler_t *s);

#endif