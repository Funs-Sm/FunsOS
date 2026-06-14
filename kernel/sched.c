#include "sched.h"
#include "thread.h"
#include "process.h"
#include "timer.h"
#include "sync.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"
#include "stddef.h"
#include "fpu.h"

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

void sched_add(pcb_t *proc) {
    if (!proc) return;

    spinlock_lock(&sched.lock);

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

    spinlock_unlock(&sched.lock);
}

void sched_remove(pcb_t *proc) {
    if (!proc) return;

    spinlock_lock(&sched.lock);

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

    spinlock_unlock(&sched.lock);
}

void sched_tick(void) {
    sched.tick_count++;

    sched_wakeup_sleepers();

    if (!sched.current) {
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
    spinlock_lock(&sched.lock);

    if (sched.in_schedule) {
        spinlock_unlock(&sched.lock);
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
        sched.in_schedule = 0;
        spinlock_unlock(&sched.lock);
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

    sched.in_schedule = 0;
    spinlock_unlock(&sched.lock);

    if (prev != next) {
        asm volatile(
            "mov %0, %%esp\n"
            "pop %%edi\n"
            "pop %%esi\n"
            "pop %%ebp\n"
            "add $4, %%esp\n"
            "pop %%ebx\n"
            "pop %%edx\n"
            "pop %%ecx\n"
            "pop %%eax\n"
            "add $8, %%esp\n"
            "iret\n"
            : : "r"(&next->context) : "memory"
        );
    }
}

void sched_block(pcb_t *proc, int reason) {
    if (!proc) return;

    spinlock_lock(&sched.lock);

    if (proc->state == PROCESS_RUNNING) {
        proc->state = PROCESS_BLOCKED;
        proc->blocked_reason = reason;
        spinlock_unlock(&sched.lock);
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
        spinlock_unlock(&sched.lock);
    } else {
        spinlock_unlock(&sched.lock);
    }
}

void sched_unblock(pcb_t *proc) {
    if (!proc || proc->state != PROCESS_BLOCKED) return;

    spinlock_lock(&sched.lock);

    proc->state = PROCESS_READY;
    proc->blocked_reason = BLOCK_REASON_NONE;
    add_to_queue(proc);

    spinlock_unlock(&sched.lock);
}

pcb_t *sched_get_current(void) {
    return sched.current;
}

int sched_set_priority(pcb_t *proc, uint32_t priority) {
    if (!proc) return -1;
    if (priority > SCHED_PRIORITY_MAX) {
        priority = SCHED_PRIORITY_MAX;
    }

    spinlock_lock(&sched.lock);

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

    spinlock_unlock(&sched.lock);
    return 0;
}

void sched_yield(void) {
    pcb_t *current = sched_get_current();
    if (current && current->sched_policy & PROCESS_NORMAL) {
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
    spinlock_lock(&sched.lock);

    pcb_t *proc = process_list;
    while (proc) {
        if (proc->state == PROCESS_BLOCKED &&
            proc->blocked_reason == BLOCK_REASON_SLEEP &&
            sched.tick_count >= proc->wake_time) {
            sched_unblock(proc);
        }
        proc = proc->next;
    }

    spinlock_unlock(&sched.lock);
}

pcb_t *sched_find_process(pid_t pid) {
    spinlock_lock(&sched.lock);

    pcb_t *proc = process_list;
    while (proc) {
        if (proc->pid == pid) {
            spinlock_unlock(&sched.lock);
            return proc;
        }
        proc = proc->next;
    }

    spinlock_unlock(&sched.lock);
    return NULL;
}

int sched_set_policy(pcb_t *proc, uint32_t policy) {
    if (!proc) return -1;

    spinlock_lock(&sched.lock);

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

    spinlock_unlock(&sched.lock);
    return 0;
}

uint32_t sched_get_tick_count(void) {
    return sched.tick_count;
}

void sched_print_stats(void) {
    spinlock_lock(&sched.lock);

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

    spinlock_unlock(&sched.lock);
}
