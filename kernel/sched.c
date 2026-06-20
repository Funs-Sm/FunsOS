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

/* CFS红黑树节点 - 简化为有序链表实现 */
typedef struct cfs_node {
    pcb_t          *proc;
    uint64_t        vruntime;   /* 虚拟运行时间 */
    struct cfs_node *next;
} cfs_node_t;

#define CFS_MAX_PROCS 256
static cfs_node_t cfs_runqueue[CFS_MAX_PROCS];
static uint32_t   cfs_count;
static uint64_t   min_vruntime;
static uint64_t   cfs_tick_granularity; /* 默认1ms */

/* 初始化CFS调度器 */
void sched_cfs_init(void) {
    for (int i = 0; i < CFS_MAX_PROCS; i++) {
        cfs_runqueue[i].proc = NULL;
        cfs_runqueue[i].vruntime = 0;
        cfs_runqueue[i].next = NULL;
    }
    cfs_count = 0;
    min_vruntime = 0;
    cfs_tick_granularity = 1; /* 默认1ms粒度 */
}

/* 将进程加入CFS运行队列（按vruntime排序插入） */
void sched_cfs_enqueue(pcb_t *proc) {
    if (!proc || cfs_count >= CFS_MAX_PROCS) return;

    /* 找到空闲槽位 */
    int slot = -1;
    for (int i = 0; i < CFS_MAX_PROCS; i++) {
        if (cfs_runqueue[i].proc == NULL) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return; /* 队列满 */

    /* 计算初始vruntime：继承min_vruntime或使用进程权重调整 */
    cfs_node_t *node = &cfs_runqueue[slot];
    node->proc = proc;
    node->vruntime = (proc->vruntime > min_vruntime) ? proc->vruntime : min_vruntime;
    node->next = NULL;

    /* 有序链表插入：按vruntime升序排列 */
    if (cfs_count == 0) {
        /* 队列为空，直接作为头节点 */
        node->next = NULL;
    } else {
        /* 找到合适的插入位置 */
        cfs_node_t *prev = NULL;
        cfs_node_t *curr = &cfs_runqueue[0]; /* 假设第一个节点是链表头 */

        /* 查找实际的链表头 */
        int head_idx = -1;
        for (int i = 0; i < CFS_MAX_PROCS; i++) {
            if (cfs_runqueue[i].proc != NULL) {
                head_idx = i;
                break;
            }
        }
        if (head_idx < 0) return;

        curr = &cfs_runqueue[head_idx];

        while (curr && curr->vruntime < node->vruntime) {
            prev = curr;
            curr = curr->next;
        }

        if (prev == NULL) {
            /* 插入到链表头部 */
            node->next = &cfs_runqueue[head_idx];
        } else {
            node->next = prev->next;
            prev->next = node;
        }
    }

    cfs_count++;
}

/* 从CFS队列中取出vruntime最小的进程 */
pcb_t *sched_cfs_dequeue(void) {
    if (cfs_count == 0) return NULL;

    /* 找到vruntime最小的节点（链表头） */
    int min_idx = -1;
    uint64_t min_val = UINT64_MAX;

    for (int i = 0; i < CFS_MAX_PROCS; i++) {
        if (cfs_runqueue[i].proc != NULL && cfs_runqueue[i].vruntime < min_val) {
            min_val = cfs_runqueue[i].vruntime;
            min_idx = i;
        }
    }

    if (min_idx < 0) return NULL;

    cfs_node_t *node = &cfs_runqueue[min_idx];
    pcb_t *proc = node->proc;

    /* 更新min_vruntime */
    if (node->vruntime > min_vruntime) {
        min_vruntime = node->vruntime;
    }

    /* 清除节点 */
    node->proc = NULL;
    node->vruntime = 0;
    node->next = NULL;
    cfs_count--;

    return proc;
}

/* 计算虚拟运行时间增量 */
uint64_t sched_calc_vruntime(pcb_t *proc, uint64_t delta_exec) {
    if (!proc) return 0;

    /* 根据进程优先级/权重计算vruntime增量
     * 权重越高，vruntime增长越慢，获得更多CPU时间
     * 公式: vruntime += delta_exec * 1024 / weight
     * 这里简化为根据优先级线性调整 */
    uint32_t priority = proc->effective_priority;
    uint64_t weight = 1024; /* 基准权重 */

    /* 优先级越高，权重越大 */
    if (priority > SCHED_DEFAULT_PRIORITY) {
        weight += (priority - SCHED_DEFAULT_PRIORITY) * 10;
    } else if (priority < SCHED_DEFAULT_PRIORITY) {
        weight -= (SCHED_DEFAULT_PRIORITY - priority) * 5;
        if (weight < 100) weight = 100; /* 最小权重 */
    }

    return (delta_exec * 1024) / weight;
}

/* CFS时钟滴答处理 */
void sched_cfs_tick(pcb_t *proc) {
    if (!proc) return;

    /* 更新进程的vruntime */
    uint64_t delta = cfs_tick_granularity;
    uint64_t vruntime_delta = sched_calc_vruntime(proc, delta);
    proc->vruntime += vruntime_delta;

    /* 如果时间片用完，重新入队 */
    proc->time_slice--;
    if (proc->time_slice <= 0 && proc->state == PROCESS_RUNNING) {
        /* 重新加入CFS队列 */
        proc->state = PROCESS_READY;
        sched_cfs_enqueue(proc);
    }
}

/* 设置进程的调度策略为CFS */
int sched_set_policy_cfs(pcb_t *proc) {
    if (!proc) return -1;

    uint32_t flags = spinlock_irq_save(&sched.lock);

    /* 从当前队列移除 */
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

    /* 设置为CFS策略 */
    proc->sched_policy |= PROCESS_CFS;
    proc->vruntime = min_vruntime;

    /* 加入CFS队列 */
    if (proc->state == PROCESS_READY || proc->state == PROCESS_RUNNING) {
        sched_cfs_enqueue(proc);
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
        printf("CFS Queue (%d processes):\n", cfs_count);
        for (int i = 0; i < CFS_MAX_PROCS && cfs_count > 0; i++) {
            if (cfs_runqueue[i].proc) {
                printf("  PID %d: %s, vruntime=%llu\n",
                       cfs_runqueue[i].proc->pid,
                       cfs_runqueue[i].proc->name,
                       cfs_runqueue[i].vruntime);
            }
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

/* 在sched_init中初始化新增模块 */
__attribute__((constructor))
static void sched_extended_init(void) {
    sched_cfs_init();
    sched_load_init();
    sched_pg_init();
}
