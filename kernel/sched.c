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
