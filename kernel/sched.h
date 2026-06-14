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

#endif
