#include "sched.h"
#include "thread.h"
#include "process.h"
#include "timer.h"
#include "sync.h"
#include "spinlock.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"
#include "stddef.h"
#include "fpu.h"

/* Assembly context switch: saves callee-saved regs, switches ESP, restores */
extern void context_switch(uint32_t *old_esp, uint32_t new_esp);

/* Trampoline for newly created processes: enters user mode via iret */
void process_first_run(void) {
    pcb_t *proc = sched_get_current();
    if (!proc) {
        while (1) asm volatile("hlt");
    }

    asm volatile(
        "cli\n\t"
        "movw $0x23, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        "pushl $0x23\n\t"        /* ss  */
        "pushl %0\n\t"           /* useresp */
        "pushl $0x202\n\t"       /* eflags (IF=1) */
        "pushl $0x1B\n\t"        /* cs  */
        "pushl %1\n\t"           /* eip */
        "iret\n\t"
        : : "r"(proc->user_stack), "r"(proc->entry_point) : "eax", "memory"
    );
}

static sched_state_t sched;
static pcb_t *process_list = NULL;

static void queue_add(sched_queue_t *queue, pcb_t *proc) {
    proc->queue_next = NULL;
    proc->queue_prev = queue->tail;

    if (queue->tail) {
        queue->tail->queue_next = proc;
    } else {
        queue->head = proc;
    }
    queue->tail = proc;
    queue->count++;
}

static pcb_t *queue_remove(sched_queue_t *queue, pcb_t *proc) {
    if (!proc) return NULL;

    if (proc->queue_prev) {
        proc->queue_prev->queue_next = proc->queue_next;
    } else {
        queue->head = proc->queue_next;
    }

    if (proc->queue_next) {
        proc->queue_next->queue_prev = proc->queue_prev;
    } else {
        queue->tail = proc->queue_prev;
    }

    proc->queue_next = NULL;
    proc->queue_prev = NULL;
    queue->count--;

    return proc;
}

static pcb_t *queue_pop_front(sched_queue_t *queue) {
    if (!queue->head) return NULL;
    return queue_remove(queue, queue->head);
}

static uint32_t calculate_timeslice(uint32_t priority) {
    uint32_t max_slice = 100;
    uint32_t min_slice = 10;
    return min_slice + ((SCHED_PRIORITY_MAX - priority) * (max_slice - min_slice)) / SCHED_PRIORITY_MAX;
}

static void add_to_queue(pcb_t *proc) {
    if (proc->sched_policy & PROCESS_REAL_TIME) {
        int rt_level = proc->priority / 50;
        if (rt_level >= SCHED_RT_QUEUE_COUNT) {
            rt_level = SCHED_RT_QUEUE_COUNT - 1;
        }
        queue_add(&sched.rt_queues[rt_level], proc);
    } else {
        int mlfq_level = proc->queue_level;
        if (mlfq_level >= SCHED_QUEUE_COUNT) {
            mlfq_level = SCHED_QUEUE_COUNT - 1;
        }
        queue_add(&sched.mlfq_queues[mlfq_level], proc);
    }
}

static pcb_t *pick_next_process(void) {
    for (int i = 0; i < SCHED_RT_QUEUE_COUNT; i++) {
        pcb_t *proc = queue_pop_front(&sched.rt_queues[i]);
        if (proc) {
            return proc;
        }
    }

    for (int i = 0; i < SCHED_QUEUE_COUNT; i++) {
        pcb_t *proc = queue_pop_front(&sched.mlfq_queues[i]);
        if (proc) {
            return proc;
        }
    }

    return sched.idle_task;
}

void sched_init(void) {
    for (int i = 0; i < SCHED_RT_QUEUE_COUNT; i++) {
        sched.rt_queues[i].head = NULL;
        sched.rt_queues[i].tail = NULL;
        sched.rt_queues[i].count = 0;
        sched.rt_queues[i].priority = i;
    }

    for (int i = 0; i < SCHED_QUEUE_COUNT; i++) {
        sched.mlfq_queues[i].head = NULL;
        sched.mlfq_queues[i].tail = NULL;
        sched.mlfq_queues[i].count = 0;
        sched.mlfq_queues[i].priority = i;
    }

    spinlock_init(&sched.lock);
    sched.current = NULL;
    sched.idle_task = NULL;
    sched.tick_count = 0;
    sched.in_schedule = 0;
}

/* Idle task: runs when no other process is ready */
static void idle_task_func(void) {
    while (1) {
        asm volatile("hlt");
    }
}

void sched_create_idle_task(void) {
    /* Create a minimal kernel-thread as the idle task */
    extern pcb_t *process_create_kernel(const char *name, void (*entry)(void));
    pcb_t *idle = process_create_kernel("idle", idle_task_func);
    if (idle) {
        sched.idle_task = idle;
    }
}

void sched_set_current(pcb_t *proc) {
    sched.current = proc;
}

void sched_add(pcb_t *proc) {
    if (!proc) return;

    uint32_t flags = spinlock_irq_save(&sched.lock);

    proc->state = PROCESS_READY;
    proc->last_run_time = sched.tick_count;

    if (!process_list) {
        process_list = proc;
        proc->prev = NULL;
        proc->next = NULL;
    } else {
        proc->next = process_list;
        proc->prev = NULL;
        process_list->prev = proc;
        process_list = proc;
    }

    add_to_queue(proc);

    spinlock_irq_restore(&sched.lock, flags);
}

void sched_remove(pcb_t *proc) {
    if (!proc) return;

    uint32_t flags = spinlock_irq_save(&sched.lock);

    if (proc->sched_policy & PROCESS_REAL_TIME) {
        int rt_level = proc->priority / 50;
        if (rt_level >= SCHED_RT_QUEUE_COUNT) {
            rt_level = SCHED_RT_QUEUE_COUNT - 1;
        }
        queue_remove(&sched.rt_queues[rt_level], proc);
    } else {
        queue_remove(&sched.mlfq_queues[proc->queue_level], proc);
    }

    if (proc->prev) {
        proc->prev->next = proc->next;
    }
    if (proc->next) {
        proc->next->prev = proc->prev;
    }
    if (process_list == proc) {
        process_list = proc->next;
    }

    spinlock_irq_restore(&sched.lock, flags);
}

void sched_tick(void) {
    sched.tick_count++;

    sched_wakeup_sleepers();

    if (!sched.current) {
        /* No process currently running - try to schedule the first one */
        if (pick_next_process() || sched.idle_task) {
            sched.in_schedule = 0;
            schedule();
        }
        return;
    }

    sched.current->ticks_used++;
    sched.current->cpu_time++;
    sched.current->time_slice--;

    if (sched.current->sched_policy & PROCESS_NORMAL) {
        if (sched.current->time_slice <= 0) {
            if (sched.current->queue_level < SCHED_QUEUE_COUNT - 1) {
                sched.current->queue_level++;
            }
            schedule();
        }
    } else if (sched.current->sched_policy & PROCESS_REAL_TIME) {
        if (sched.current->time_slice <= 0) {
            schedule();
        }
    }
}

void schedule(void) {
    uint32_t flags = spinlock_irq_save(&sched.lock);

    if (sched.in_schedule) {
        spinlock_irq_restore(&sched.lock, flags);
        return;
    }
    sched.in_schedule = 1;

    pcb_t *prev = sched.current;

    if (prev && prev->state == PROCESS_RUNNING) {
        prev->state = PROCESS_READY;
        add_to_queue(prev);
    }

    pcb_t *next = pick_next_process();
    if (!next) {
        next = sched.idle_task;
    }

    if (!next) {
        /* No process to run - restore prev or spin in kernel */
        if (prev) {
            prev->state = PROCESS_RUNNING;
            sched.current = prev;
        }
        sched.in_schedule = 0;
        spinlock_irq_restore(&sched.lock, flags);
        return;
    }

    next->state = PROCESS_RUNNING;
    next->time_slice = calculate_timeslice(next->effective_priority);
    next->ticks_used = 0;
    sched.current = next;

    /* Save FPU state of previous process */
    if (prev && prev != next) {
        fpu_save(prev);
    }

    if (prev && prev->page_dir != next->page_dir) {
        asm volatile("mov %0, %%cr3" : : "r"(next->page_dir) : "memory");
    }

    /* Update TSS.esp0 so interrupts from user mode use the correct
     * kernel stack. */
    {
        extern void gdt_set_tss(uint32_t ss0, uint32_t esp0);
        if (next->kernel_stack) {
            gdt_set_tss(0x10, next->kernel_stack);
        }
    }

    sched.in_schedule = 0;
    spinlock_irq_restore(&sched.lock, flags);

    if (prev && prev != next) {
        /* Stack-based context switch: saves callee-saved registers
         * on prev's stack, switches to next's stack, restores
         * registers, and returns.  For a preempted process this
         * resumes exactly where it left off (returning through the
         * interrupt handler back to user mode).  For a new process
         * the stack was set up so that RET jumps to
         * process_first_run which enters user mode via iret. */
        context_switch(&prev->kernel_esp, next->kernel_esp);
    }
    /* If prev == NULL (kernel_main on boot stack), we don't context-switch.
     * The new process will be picked up on the next timer tick when
     * kernel_main is interrupted and the scheduler runs again.
     * Since sched.current is now set to 'next', the next tick will
     * properly save kernel_main's state and switch to the new process. */
}

void sched_block(pcb_t *proc, int reason) {
    if (!proc) return;

    uint32_t flags = spinlock_irq_save(&sched.lock);

    if (proc->state == PROCESS_RUNNING) {
        proc->state = PROCESS_BLOCKED;
        proc->blocked_reason = reason;
        spinlock_irq_restore(&sched.lock, flags);
        schedule();
    } else if (proc->state == PROCESS_READY) {
        if (proc->sched_policy & PROCESS_REAL_TIME) {
            int rt_level = proc->priority / 50;
            if (rt_level >= SCHED_RT_QUEUE_COUNT) {
                rt_level = SCHED_RT_QUEUE_COUNT - 1;
            }
            queue_remove(&sched.rt_queues[rt_level], proc);
        } else {
            queue_remove(&sched.mlfq_queues[proc->queue_level], proc);
        }
        proc->state = PROCESS_BLOCKED;
        proc->blocked_reason = reason;
        spinlock_irq_restore(&sched.lock, flags);
    } else {
        spinlock_irq_restore(&sched.lock, flags);
    }
}

void sched_unblock(pcb_t *proc) {
    if (!proc || proc->state != PROCESS_BLOCKED) return;

    uint32_t flags = spinlock_irq_save(&sched.lock);

    proc->state = PROCESS_READY;
    proc->blocked_reason = BLOCK_REASON_NONE;
    add_to_queue(proc);

    spinlock_irq_restore(&sched.lock, flags);
}

pcb_t *sched_get_current(void) {
    return sched.current;
}

int sched_set_priority(pcb_t *proc, uint32_t priority) {
    if (!proc) return -1;
    if (priority > SCHED_PRIORITY_MAX) {
        priority = SCHED_PRIORITY_MAX;
    }

    uint32_t flags = spinlock_irq_save(&sched.lock);

    if (proc->state == PROCESS_READY) {
        if (proc->sched_policy & PROCESS_REAL_TIME) {
            int rt_level = proc->priority / 50;
            if (rt_level >= SCHED_RT_QUEUE_COUNT) {
                rt_level = SCHED_RT_QUEUE_COUNT - 1;
            }
            queue_remove(&sched.rt_queues[rt_level], proc);
        } else {
            queue_remove(&sched.mlfq_queues[proc->queue_level], proc);
        }
    }

    proc->priority = priority;
    proc->original_priority = priority;
    proc->effective_priority = priority;

    if (proc->state == PROCESS_READY) {
        add_to_queue(proc);
    }

    spinlock_irq_restore(&sched.lock, flags);
    return 0;
}

void sched_yield(void) {
    pcb_t *current = sched_get_current();
    if (!current) return;  /* No current process - nothing to yield */
    if (current->sched_policy & PROCESS_NORMAL) {
        if (current->queue_level > 0 && current->ticks_used < current->time_slice / 2) {
            current->queue_level--;
        }
    }
    schedule();
}

void sched_sleep(uint32_t milliseconds) {
    pcb_t *current = sched_get_current();
    if (!current) return;

    current->wake_time = sched.tick_count + (milliseconds + 9) / 10;
    sched_block(current, BLOCK_REASON_SLEEP);
}

void sched_wakeup_sleepers(void) {
    uint32_t flags = spinlock_irq_save(&sched.lock);

    pcb_t *proc = process_list;
    while (proc) {
        if (proc->state == PROCESS_BLOCKED &&
            proc->blocked_reason == BLOCK_REASON_SLEEP &&
            sched.tick_count >= proc->wake_time) {
            /* Inline sched_unblock logic to avoid deadlock
             * (sched_unblock also tries to acquire sched.lock) */
            proc->state = PROCESS_READY;
            proc->blocked_reason = BLOCK_REASON_NONE;
            add_to_queue(proc);
        }
        proc = proc->next;
    }

    spinlock_irq_restore(&sched.lock, flags);
}

pcb_t *sched_find_process(pid_t pid) {
    uint32_t flags = spinlock_irq_save(&sched.lock);

    pcb_t *proc = process_list;
    while (proc) {
        if (proc->pid == pid) {
            spinlock_irq_restore(&sched.lock, flags);
        return proc;
        }
        proc = proc->next;
    }

    spinlock_irq_restore(&sched.lock, flags);
    return NULL;
}

int sched_set_policy(pcb_t *proc, uint32_t policy) {
    if (!proc) return -1;

    uint32_t flags = spinlock_irq_save(&sched.lock);

    if (proc->state == PROCESS_READY) {
        if (proc->sched_policy & PROCESS_REAL_TIME) {
            int rt_level = proc->priority / 50;
            if (rt_level >= SCHED_RT_QUEUE_COUNT) {
                rt_level = SCHED_RT_QUEUE_COUNT - 1;
            }
            queue_remove(&sched.rt_queues[rt_level], proc);
        } else {
            queue_remove(&sched.mlfq_queues[proc->queue_level], proc);
        }
    }

    proc->sched_policy = policy;
    proc->queue_level = 0;

    if (proc->state == PROCESS_READY) {
        add_to_queue(proc);
    }

    spinlock_irq_restore(&sched.lock, flags);
    return 0;
}

uint32_t sched_get_tick_count(void) {
    return sched.tick_count;
}

void sched_print_stats(void) {
    uint32_t flags = spinlock_irq_save(&sched.lock);

    printf("=== Scheduler Statistics ===\n");
    printf("Tick count: %llu\n", sched.tick_count);
    printf("\nReal-time queues:\n");
    for (int i = 0; i < SCHED_RT_QUEUE_COUNT; i++) {
        printf("  Queue %d: %d processes\n", i, sched.rt_queues[i].count);
    }
    printf("\nMLFQ queues:\n");
    for (int i = 0; i < SCHED_QUEUE_COUNT; i++) {
        printf("  Queue %d: %d processes\n", i, sched.mlfq_queues[i].count);
    }
    printf("\nProcess list:\n");
    pcb_t *proc = process_list;
    while (proc) {
        printf("  PID %d: %s, state=%d, priority=%d, CPU=%u\n",
               proc->pid, proc->name, proc->state, proc->priority, proc->cpu_time);
        proc = proc->next;
    }
    printf("============================\n");

    spinlock_irq_restore(&sched.lock, flags);
}

/* ============================================================
 * CFS (Completely Fair Scheduler) 完全公平调度器
 * ============================================================ */

static cfs_node_t cfs_runqueue[CFS_MAX_PROCS];
static cfs_rq_t    cfs_rq;
static uint64_t    cfs_tick_granularity;

uint32_t sched_slice(cfs_rq_t *rq) {
    uint32_t nr = rq->nr_running + 1;
    if (nr < 1) nr = 1;
    uint32_t slice = sched_latency / nr;
    if (slice < sched_min_granularity) {
        slice = sched_min_granularity;
    }
    return slice;
}

int wakeup_preempt(cfs_rq_t *rq, pcb_t *se) {
    if (rq->curr == NULL || rq->curr == se) {
        return 0;
    }
    uint64_t curr_vruntime = rq->curr->vruntime;
    uint64_t se_vruntime = se->vruntime;
    uint64_t gran = sched_wakeup_granularity;

    if (se_vruntime + gran < curr_vruntime) {
        return 1;
    }
    return 0;
}

void sched_cfs_init(void) {
    for (int i = 0; i < CFS_MAX_PROCS; i++) {
        cfs_runqueue[i].proc = NULL;
        cfs_runqueue[i].vruntime = 0;
        cfs_runqueue[i].next = NULL;
    }
    cfs_rq.head = NULL;
    cfs_rq.curr = NULL;
    cfs_rq.min_vruntime = 0;
    cfs_rq.nr_running = 0;
    cfs_tick_granularity = 1;
}

static cfs_node_t *cfs_alloc_node(void) {
    for (int i = 0; i < CFS_MAX_PROCS; i++) {
        if (cfs_runqueue[i].proc == NULL) {
            return &cfs_runqueue[i];
        }
    }
    return NULL;
}

static void cfs_free_node(cfs_node_t *node) {
    node->proc = NULL;
    node->vruntime = 0;
    node->next = NULL;
}

void enqueue_entity(cfs_rq_t *rq, pcb_t *se) {
    if (!rq || !se) return;

    cfs_node_t *node = cfs_alloc_node();
    if (!node) return;

    node->proc = se;
    node->vruntime = (se->vruntime > rq->min_vruntime) ? se->vruntime : rq->min_vruntime;
    se->exec_start = sched.tick_count;

    cfs_node_t *prev = NULL;
    cfs_node_t *curr = rq->head;

    while (curr && curr->vruntime < node->vruntime) {
        prev = curr;
        curr = curr->next;
    }

    if (prev == NULL) {
        node->next = rq->head;
        rq->head = node;
    } else {
        node->next = prev->next;
        prev->next = node;
    }

    rq->nr_running++;

    if (rq->curr != se) {
        uint32_t slice = sched_slice(rq);
        uint64_t curr_v = rq->curr ? rq->curr->vruntime : 0;
        if (wakeup_preempt(rq, se)) {
            if (rq->curr) {
                rq->curr->need_resched = 1;
            }
        } else {
            uint64_t gran = sched_wakeup_granularity;
            if (node->vruntime + gran < curr_v) {
                if (rq->curr) {
                    rq->curr->need_resched = 1;
                }
            }
        }
        (void)slice;
    }
}

pcb_t *pick_next_entity(cfs_rq_t *rq) {
    if (!rq || rq->nr_running == 0 || rq->head == NULL) {
        return NULL;
    }

    cfs_node_t *leftmost = rq->head;
    pcb_t *curr = rq->curr;

    if (curr != NULL && leftmost->proc != curr) {
        uint64_t left_vruntime = leftmost->vruntime;
        uint64_t curr_vruntime = curr->vruntime;

        if (left_vruntime < curr_vruntime - sched_min_granularity) {
            return leftmost->proc;
        }
    }

    return leftmost->proc;
}

void task_tick_fair(cfs_rq_t *rq, pcb_t *curr) {
    if (!rq || !curr) return;

    uint64_t delta_exec = sched.tick_count - curr->exec_start;
    uint64_t vruntime_delta = sched_calc_vruntime(curr, cfs_tick_granularity);
    curr->vruntime += vruntime_delta;

    uint32_t slice = sched_slice(rq);
    if (delta_exec >= slice) {
        curr->need_resched = 1;
    }
}

void sched_cfs_enqueue(pcb_t *proc) {
    enqueue_entity(&cfs_rq, proc);
}

static cfs_node_t *cfs_find_node(pcb_t *proc, cfs_node_t **pprev) {
    cfs_node_t *prev = NULL;
    cfs_node_t *curr = cfs_rq.head;
    while (curr) {
        if (curr->proc == proc) {
            if (pprev) *pprev = prev;
            return curr;
        }
        prev = curr;
        curr = curr->next;
    }
    return NULL;
}

pcb_t *sched_cfs_dequeue(void) {
    if (cfs_rq.nr_running == 0 || cfs_rq.head == NULL) return NULL;

    pcb_t *next = pick_next_entity(&cfs_rq);
    if (!next) return NULL;

    cfs_node_t *prev = NULL;
    cfs_node_t *node = cfs_find_node(next, &prev);
    if (!node) return NULL;

    if (prev) {
        prev->next = node->next;
    } else {
        cfs_rq.head = node->next;
    }

    if (node->vruntime > cfs_rq.min_vruntime) {
        cfs_rq.min_vruntime = node->vruntime;
    }

    pcb_t *proc = node->proc;
    cfs_free_node(node);
    cfs_rq.nr_running--;

    return proc;
}

uint64_t sched_calc_vruntime(pcb_t *proc, uint64_t delta_exec) {
    if (!proc) return 0;

    uint32_t priority = proc->effective_priority;
    uint64_t weight = 1024;

    if (priority > SCHED_DEFAULT_PRIORITY) {
        weight += (priority - SCHED_DEFAULT_PRIORITY) * 10;
    } else if (priority < SCHED_DEFAULT_PRIORITY) {
        weight -= (SCHED_DEFAULT_PRIORITY - priority) * 5;
        if (weight < 100) weight = 100;
    }

    return (delta_exec * 1024) / weight;
}

void sched_cfs_tick(pcb_t *proc) {
    if (!proc) return;

    task_tick_fair(&cfs_rq, proc);

    if (proc->need_resched && proc->state == PROCESS_RUNNING) {
        proc->need_resched = 0;
        proc->state = PROCESS_READY;
        enqueue_entity(&cfs_rq, proc);
        schedule();
    }
}

int sched_set_policy_cfs(pcb_t *proc) {
    if (!proc) return -1;

    uint32_t flags = spinlock_irq_save(&sched.lock);

    if (proc->state == PROCESS_READY) {
        if (proc->sched_policy & PROCESS_REAL_TIME) {
            int rt_level = proc->priority / 50;
            if (rt_level >= SCHED_RT_QUEUE_COUNT) {
                rt_level = SCHED_RT_QUEUE_COUNT - 1;
            }
            queue_remove(&sched.rt_queues[rt_level], proc);
        } else if (!(proc->sched_policy & PROCESS_CFS)) {
            queue_remove(&sched.mlfq_queues[proc->queue_level], proc);
        }
    }

    proc->sched_policy |= PROCESS_CFS;
    proc->vruntime = cfs_rq.min_vruntime;
    proc->need_resched = 0;
    proc->exec_start = sched.tick_count;

    if (proc->state == PROCESS_READY) {
        enqueue_entity(&cfs_rq, proc);
    } else if (proc->state == PROCESS_RUNNING) {
        cfs_rq.curr = proc;
    }

    spinlock_irq_restore(&sched.lock, flags);
    return 0;
}

/* ============================================================
 * 负载均衡模块
 * ============================================================ */

#define SCHED_LOAD_WINDOW 100 /* 采样窗口大小 */
typedef struct load_stat {
    uint32_t cpu_load[SCHED_LOAD_WINDOW]; /* CPU利用率0-100 */
    uint32_t window_pos;
    uint32_t avg_load;
} load_stat_t;

static load_stat_t load_stats;

/* 初始化负载统计 */
static void sched_load_init(void) {
    for (int i = 0; i < SCHED_LOAD_WINDOW; i++) {
        load_stats.cpu_load[i] = 0;
    }
    load_stats.window_pos = 0;
    load_stats.avg_load = 0;
}

/* 采样CPU负载 */
void sched_load_sample(uint32_t load) {
    if (load > 100) load = 100;

    /* 滑动窗口更新 */
    load_stats.cpu_load[load_stats.window_pos] = load;
    load_stats.window_pos = (load_stats.window_pos + 1) % SCHED_LOAD_WINDOW;

    /* 计算平均负载 */
    uint64_t sum = 0;
    for (int i = 0; i < SCHED_LOAD_WINDOW; i++) {
        sum += load_stats.cpu_load[i];
    }
    load_stats.avg_load = (uint32_t)(sum / SCHED_LOAD_WINDOW);

    /* 当平均负载>80%时建议降低优先级或迁移进程 */
    if (load_stats.avg_load > 80) {
        /* 高负载警告 - 可在此处触发负载均衡策略 */
        /* 例如：迁移部分进程到其他CPU、降低非关键进程优先级等 */
    }
}

/* 获取当前平均负载 */
uint32_t sched_get_avg_load(void) {
    return load_stats.avg_load;
}

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

static process_group_t pg_table[MAX_PROCESS_GROUPS];

/* 初始化进程组表 */
static void sched_pg_init(void) {
    for (int i = 0; i < MAX_PROCESS_GROUPS; i++) {
        pg_table[i].pgid = 0;
        pg_table[i].leader_pid = 0;
        pg_table[i].member_count = 0;
        pg_table[i].cpu_share = 1024; /* 默认配额 */
        pg_table[i].used = 0;
    }
}

/* 创建新的进程组 */
int sched_create_process_group(pid_t leader) {
    for (int i = 0; i < MAX_PROCESS_GROUPS; i++) {
        if (!pg_table[i].used) {
            pg_table[i].pgid = leader; /* 以leader PID作为PGID */
            pg_table[i].leader_pid = leader;
            pg_table[i].member_count = 1;
            pg_table[i].cpu_share = 1024;
            pg_table[i].used = 1;
            return (int)pg_table[i].pgid;
        }
    }
    return -1; /* 进程组表满 */
}

/* 将进程添加到指定进程组 */
int sched_add_to_group(pid_t pid, pid_t pgid) {
    for (int i = 0; i < MAX_PROCESS_GROUPS; i++) {
        if (pg_table[i].used && pg_table[i].pgid == pgid) {
            pg_table[i].member_count++;
            /* 更新进程的pgid信息 */
            pcb_t *proc = sched_find_process(pid);
            if (proc) {
                proc->pgid = pgid;
            }
            return 0;
        }
    }
    return -1; /* 进程组不存在 */
}

/* 从进程组中移除进程 */
int sched_remove_from_group(pid_t pid) {
    for (int i = 0; i < MAX_PROCESS_GROUPS; i++) {
        if (pg_table[i].used) {
            /* 查找并移除成员 */
            pcb_t *proc = sched_find_process(pid);
            if (proc && proc->pgid == pg_table[i].pgid) {
                pg_table[i].member_count--;
                proc->pgid = 0;

                /* 如果组成员为0且不是leader，可以回收该组 */
                if (pg_table[i].member_count == 0) {
                    pg_table[i].used = 0;
                    pg_table[i].pgid = 0;
                    pg_table[i].leader_pid = 0;
                }
                return 0;
            }
        }
    }
    return -1; /* 进程不在任何组中 */
}

/* 获取进程组信息 */
process_group_t *sched_get_group(pid_t pgid) {
    for (int i = 0; i < MAX_PROCESS_GROUPS; i++) {
        if (pg_table[i].used && pg_table[i].pgid == pgid) {
            return &pg_table[i];
        }
    }
    return NULL; /* 进程组不存在 */
}

/* ============================================================
 * 调度器调试接口
 * ============================================================ */

static int sched_debug_level = 0; /* 0=off, 1=basic, 2=verbose */

/* 打印指定队列内容 */
void sched_dump_queue(int queue_index) {
    uint32_t flags = spinlock_irq_save(&sched.lock);

    printf("=== Queue Dump ===\n");

    if (queue_index >= 0 && queue_index < SCHED_RT_QUEUE_COUNT) {
        printf("RT Queue %d (%d processes):\n",
               queue_index, sched.rt_queues[queue_index].count);
        pcb_t *proc = sched.rt_queues[queue_index].head;
        while (proc) {
            printf("  PID %d: %s, prio=%d\n",
                   proc->pid, proc->name, proc->priority);
            proc = proc->queue_next;
        }
    } else if (queue_index >= SCHED_RT_QUEUE_COUNT &&
               queue_index < SCHED_RT_QUEUE_COUNT + SCHED_QUEUE_COUNT) {
        int mlfq_idx = queue_index - SCHED_RT_QUEUE_COUNT;
        printf("MLFQ Queue %d (%d processes):\n",
               mlfq_idx, sched.mlfq_queues[mlfq_idx].count);
        pcb_t *proc = sched.mlfq_queues[mlfq_idx].head;
        while (proc) {
            printf("  PID %d: %s, prio=%d, level=%d\n",
                   proc->pid, proc->name, proc->priority, proc->queue_level);
            proc = proc->queue_next;
        }
    } else if (queue_index == -1) {
        /* 打印CFS队列 */
        printf("CFS Queue (%d processes):\n", cfs_rq.nr_running);
        cfs_node_t *node = cfs_rq.head;
        while (node) {
            printf("  PID %d: %s, vruntime=%llu\n",
                   node->proc->pid,
                   node->proc->name,
                   node->vruntime);
            node = node->next;
        }
    } else {
        printf("Invalid queue index: %d\n", queue_index);
    }

    printf("==================\n");
    spinlock_irq_restore(&sched.lock, flags);
}

/* 设置调试级别 */
void sched_set_debug_level(int level) {
    if (level >= 0 && level <= 2) {
        sched_debug_level = level;
    }
}

/* 获取调试级别 */
int sched_get_debug_level(void) {
    return sched_debug_level;
}

/* ============================================================
 * Deadline 调度器 (SCHED_DEADLINE) - 最早截止时间优先 (EDF)
 * ============================================================ */

static dl_node_t dl_nodes[SCHED_DL_MAX_PROCS];
static dl_rq_t   dl_rq;

void sched_dl_init(void) {
    for (int i = 0; i < SCHED_DL_MAX_PROCS; i++) {
        dl_nodes[i].proc = NULL;
        dl_nodes[i].next = NULL;
        dl_nodes[i].prev = NULL;
        memset(&dl_nodes[i].params, 0, sizeof(deadline_params_t));
    }
    dl_rq.head = NULL;
    dl_rq.tail = NULL;
    dl_rq.nr_running = 0;
    dl_rq.running_bw = 0;
}

static dl_node_t *dl_alloc_node(void) {
    for (int i = 0; i < SCHED_DL_MAX_PROCS; i++) {
        if (dl_nodes[i].proc == NULL) {
            return &dl_nodes[i];
        }
    }
    return NULL;
}

static void dl_free_node(dl_node_t *node) {
    if (node) {
        node->proc = NULL;
        node->next = NULL;
        node->prev = NULL;
    }
}

static dl_node_t *dl_find_node(pcb_t *proc) {
    dl_node_t *node = dl_rq.head;
    while (node) {
        if (node->proc == proc) return node;
        node = node->next;
    }
    return NULL;
}

int sched_set_policy_deadline(pcb_t *proc, uint64_t runtime,
                             uint64_t deadline, uint64_t period) {
    if (!proc || runtime == 0 || deadline == 0 || period == 0) return -1;
    if (deadline > period) return -1;
    if (runtime > deadline) return -1;

    uint32_t flags = spinlock_irq_save(&sched.lock);

    if (proc->state == PROCESS_READY) {
        if (proc->sched_policy & PROCESS_REAL_TIME) {
            int rt_level = proc->priority / 50;
            if (rt_level >= SCHED_RT_QUEUE_COUNT) rt_level = SCHED_RT_QUEUE_COUNT - 1;
            queue_remove(&sched.rt_queues[rt_level], proc);
        } else if (proc->sched_policy & PROCESS_CFS) {
            (void)sched_cfs_dequeue();
        } else {
            queue_remove(&sched.mlfq_queues[proc->queue_level], proc);
        }
    }

    proc->sched_policy = PROCESS_DEADLINE;

    dl_node_t *node = dl_alloc_node();
    if (!node) {
        spinlock_irq_restore(&sched.lock, flags);
        return -1;
    }
    node->proc = proc;
    node->params.runtime = runtime;
    node->params.deadline = deadline;
    node->params.period = period;
    node->params.remaining_runtime = runtime;
    node->params.current_deadline = sched.tick_count + deadline;
    node->params.active = 1;

    dl_rq.running_bw += (runtime * 1000) / period;

    if (proc->state == PROCESS_READY) {
        sched_dl_enqueue(proc);
    }

    spinlock_irq_restore(&sched.lock, flags);
    return 0;
}

static void dl_insert_sorted(dl_node_t *node) {
    dl_node_t *curr = dl_rq.head;
    dl_node_t *prev = NULL;

    while (curr && curr->params.current_deadline <= node->params.current_deadline) {
        prev = curr;
        curr = curr->next;
    }

    node->prev = prev;
    node->next = curr;

    if (prev) {
        prev->next = node;
    } else {
        dl_rq.head = node;
    }
    if (curr) {
        curr->prev = node;
    } else {
        dl_rq.tail = node;
    }
}

void sched_dl_enqueue(pcb_t *proc) {
    if (!proc) return;
    dl_node_t *node = dl_find_node(proc);
    if (!node) return;

    if (node->params.remaining_runtime == 0) {
        node->params.remaining_runtime = node->params.runtime;
        node->params.current_deadline = sched.tick_count + node->params.deadline;
    }
    node->params.active = 1;

    dl_insert_sorted(node);
    dl_rq.nr_running++;
}

pcb_t *sched_dl_dequeue(void) {
    if (!dl_rq.head) return NULL;

    dl_node_t *node = dl_rq.head;
    dl_rq.head = node->next;
    if (dl_rq.head) {
        dl_rq.head->prev = NULL;
    } else {
        dl_rq.tail = NULL;
    }
    node->next = NULL;
    node->prev = NULL;
    dl_rq.nr_running--;

    return node->proc;
}

void sched_dl_tick(pcb_t *proc) {
    if (!proc) return;
    dl_node_t *node = dl_find_node(proc);
    if (!node) return;

    node->params.remaining_runtime--;

    if (node->params.remaining_runtime == 0) {
        node->params.active = 0;
        proc->need_resched = 1;
    }
}

int sched_dl_check_preempt(pcb_t *curr, pcb_t *new_proc) {
    if (!curr || !new_proc) return 0;
    if (!(curr->sched_policy & PROCESS_DEADLINE)) return 1;
    if (!(new_proc->sched_policy & PROCESS_DEADLINE)) return 0;

    dl_node_t *curr_node = dl_find_node(curr);
    dl_node_t *new_node = dl_find_node(new_proc);
    if (!curr_node || !new_node) return 0;

    return (new_node->params.current_deadline < curr_node->params.current_deadline);
}

/* ============================================================
 * CPU 亲和性 (CPU Affinity)
 * ============================================================ */

static cpu_affinity_t proc_affinity[MAX_PROCESSES];

int sched_set_affinity(pcb_t *proc, uint32_t cpumask) {
    if (!proc || cpumask == 0) return -1;
    if (proc->pid >= MAX_PROCESSES) return -1;
    proc_affinity[proc->pid].cpumask = cpumask;
    proc_affinity[proc->pid].cpu_count = 0;
    for (int i = 0; i < 32; i++) {
        if (cpumask & (1 << i)) {
            proc_affinity[proc->pid].cpu_count++;
        }
    }
    return 0;
}

uint32_t sched_get_affinity(pcb_t *proc) {
    if (!proc || proc->pid >= MAX_PROCESSES) return 0;
    return proc_affinity[proc->pid].cpumask;
}

int sched_set_affinity_pid(pid_t pid, uint32_t cpumask) {
    pcb_t *proc = sched_find_process(pid);
    if (!proc) return -1;
    return sched_set_affinity(proc, cpumask);
}

uint32_t sched_get_affinity_pid(pid_t pid) {
    pcb_t *proc = sched_find_process(pid);
    if (!proc) return 0;
    return sched_get_affinity(proc);
}

/* ============================================================
 * 优先级继承 (Priority Inheritance)
 * ============================================================ */

static pi_info_t pi_table[MAX_PROCESSES];

void sched_pi_init(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        pi_table[i].owner = NULL;
        pi_table[i].original_priority = 0;
        pi_table[i].boosted_priority = 0;
        pi_table[i].boosted = 0;
    }
}

int sched_pi_boost(pcb_t *proc, uint32_t new_priority) {
    if (!proc) return -1;
    if (proc->pid >= MAX_PROCESSES) return -1;
    if (new_priority > SCHED_PRIORITY_MAX) new_priority = SCHED_PRIORITY_MAX;

    pi_info_t *pi = &pi_table[proc->pid];

    if (!pi->boosted) {
        pi->original_priority = proc->effective_priority;
        pi->boosted = 1;
    }

    if (new_priority > pi->boosted_priority) {
        pi->boosted_priority = new_priority;
        sched_set_priority(proc, new_priority);
    }

    return 0;
}

int sched_pi_unboost(pcb_t *proc) {
    if (!proc) return -1;
    if (proc->pid >= MAX_PROCESSES) return -1;

    pi_info_t *pi = &pi_table[proc->pid];
    if (!pi->boosted) return 0;

    sched_set_priority(proc, pi->original_priority);
    pi->boosted = 0;
    pi->boosted_priority = 0;
    pi->original_priority = 0;

    return 0;
}

int sched_pi_mutex_lock(pcb_t *holder, pcb_t *waiter) {
    if (!holder || !waiter) return -1;
    if (holder->pid >= MAX_PROCESSES || waiter->pid >= MAX_PROCESSES) return -1;

    if (waiter->effective_priority > holder->effective_priority) {
        sched_pi_boost(holder, waiter->effective_priority);
    }
    return 0;
}

int sched_pi_mutex_unlock(pcb_t *holder) {
    if (!holder) return -1;
    return sched_pi_unboost(holder);
}

/* ============================================================
 * 调度统计 (Scheduler Statistics)
 * ============================================================ */

static sched_stat_t proc_stats[MAX_PROCESSES];
static sched_global_stats_t global_stats;

void sched_stats_init(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        memset(&proc_stats[i], 0, sizeof(sched_stat_t));
        proc_stats[i].min_runtime = (uint64_t)-1;
    }
    memset(&global_stats, 0, sizeof(sched_global_stats_t));
}

void sched_stats_account(pcb_t *proc, int ticks_used, int voluntary) {
    if (!proc || proc->pid >= MAX_PROCESSES) return;

    sched_stat_t *st = &proc_stats[proc->pid];
    uint64_t ticks = (uint64_t)ticks_used;

    st->sched_count++;
    st->total_runtime += ticks;
    st->context_switches++;
    global_stats.total_context_switches++;

    if (ticks > st->max_runtime) st->max_runtime = ticks;
    if (ticks < st->min_runtime) st->min_runtime = ticks;

    if (st->sched_count > 0) {
        st->avg_runtime = st->total_runtime / st->sched_count;
    }

    if (voluntary) {
        st->voluntary_ctx++;
        global_stats.yield_count++;
    } else {
        st->involuntary_ctx++;
        global_stats.preempt_count++;
    }

    global_stats.total_ticks += ticks_used;
    if (proc->type == PROCESS_USER) {
        global_stats.user_ticks += ticks_used;
    } else {
        global_stats.kernel_ticks += ticks_used;
    }
    st->last_sched_time = sched.tick_count;
}

sched_stat_t *sched_get_proc_stats(pid_t pid) {
    if (pid >= MAX_PROCESSES) return NULL;
    return &proc_stats[pid];
}

const sched_global_stats_t *sched_get_global_stats(void) {
    return &global_stats;
}

void sched_stats_print(void) {
    printf("=== Global Scheduler Statistics ===\n");
    printf("Total context switches: %llu\n", global_stats.total_context_switches);
    printf("Total ticks: %llu\n", global_stats.total_ticks);
    printf("User ticks: %llu\n", global_stats.user_ticks);
    printf("Kernel ticks: %llu\n", global_stats.kernel_ticks);
    printf("Preempt count: %llu\n", global_stats.preempt_count);
    printf("Yield count: %llu\n", global_stats.yield_count);
    printf("\nPer-process stats:\n");
    pcb_t *proc = process_list;
    while (proc) {
        sched_stat_t *st = &proc_stats[proc->pid];
        printf("  PID %d (%s): sched=%llu runtime=%llu cs=%llu\n",
               proc->pid, proc->name, st->sched_count,
               st->total_runtime, st->context_switches);
        proc = proc->next;
    }
    printf("==================================\n");
}

void sched_stats_reset(void) {
    memset(&global_stats, 0, sizeof(sched_global_stats_t));
    for (int i = 0; i < MAX_PROCESSES; i++) {
        memset(&proc_stats[i], 0, sizeof(sched_stat_t));
        proc_stats[i].min_runtime = (uint64_t)-1;
    }
}

/* ============================================================
 * 批处理调度 (Batch Scheduling)
 * ============================================================ */

static batch_job_t batch_jobs[BATCH_MAX_JOBS];
static uint32_t batch_job_count = 0;

void sched_batch_init(void) {
    for (int i = 0; i < BATCH_MAX_JOBS; i++) {
        batch_jobs[i].pid = 0;
        batch_jobs[i].priority = 0;
        batch_jobs[i].est_runtime = 0;
        batch_jobs[i].used = 0;
    }
    batch_job_count = 0;
}

int sched_batch_add(pid_t pid, uint32_t prio, uint64_t est_runtime) {
    for (int i = 0; i < BATCH_MAX_JOBS; i++) {
        if (!batch_jobs[i].used) {
            batch_jobs[i].pid = pid;
            batch_jobs[i].priority = prio;
            batch_jobs[i].est_runtime = est_runtime;
            batch_jobs[i].used = 1;
            batch_job_count++;

            pcb_t *proc = sched_find_process(pid);
            if (proc) {
                proc->sched_policy |= PROCESS_BATCH;
                if (prio < SCHED_PRIORITY_MAX) {
                    sched_set_priority(proc, prio);
                }
            }
            return 0;
        }
    }
    return -1;
}

int sched_batch_remove(pid_t pid) {
    for (int i = 0; i < BATCH_MAX_JOBS; i++) {
        if (batch_jobs[i].used && batch_jobs[i].pid == pid) {
            batch_jobs[i].used = 0;
            batch_jobs[i].pid = 0;
            batch_job_count--;
            return 0;
        }
    }
    return -1;
}

void sched_batch_tick(void) {
    /* 批处理调度：低系统负载时运行批处理作业 */
    uint32_t load = sched_get_avg_load();
    if (load > 50) return; /* 负载超过50%时不调度批处理作业 */
    /* 负载低时，批处理作业可以正常参与调度 */
}

/* 在sched_init中初始化新增模块 */
__attribute__((constructor))
static void sched_extended_init(void) {
    sched_cfs_init();
    sched_load_init();
    sched_pg_init();
    sched_dl_init();
    sched_pi_init();
    sched_stats_init();
    sched_batch_init();
    for (int i = 0; i < MAX_PROCESSES; i++) {
        proc_affinity[i].cpumask = 0x1;
        proc_affinity[i].cpu_count = 1;
    }
}
