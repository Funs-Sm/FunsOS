/* sched_smp.c - SMP scheduler with CFS + periodic load balance.
 *
 * Provides:
 *   - per-CPU runqueues
 *   - task create / wake / sleep / exit
 *   - CFS-based task selection
 *   - affinity masking
 *   - periodic load balance: pull tasks from busiest CPU
 *   - IPI scheduler kick (x86-specific via APIC; stubbed for harness)
 */

#include "sched_smp.h"
#include "string.h"
#include "klog.h"

int smp_sched_init(smp_scheduler_t *s, int nr_cpus) {
    if (!s) return -1;
    memset(s, 0, sizeof(*s));
    if (nr_cpus <= 0 || nr_cpus > SMP_MAX_CPUS) nr_cpus = 1;
    s->nr_cpus = nr_cpus;
    s->inited = 1;
    for (int i = 0; i < nr_cpus; i++) {
        s->rq[i].cpu_id = i;
        s->rq[i].active = 0;
        cfs_rq_init(&s->rq[i].cfs_rq);
    }
    return 0;
}

int smp_sched_register_cpu(smp_scheduler_t *s, int cpu) {
    if (!s || cpu < 0 || cpu >= s->nr_cpus) return -1;
    s->rq[cpu].active = 1;
    /* Create an idle task for the CPU. */
    if (!s->rq[cpu].idle_task) {
        uint32_t pid;
        if (smp_sched_create_task(s, "idle", 19, cpu, &pid) == 0) {
            smp_task_t *t = &s->tasks[pid - 1];
            t->state = TASK_IDLE;
            s->rq[cpu].idle_task = t;
        }
    }
    return 0;
}

int smp_sched_create_task(smp_scheduler_t *s, const char *name,
                           int nice, int cpu, uint32_t *pid_out)
{
    if (!s || !s->inited) return -1;
    if (s->nr_tasks >= SMP_MAX_TASKS) return -1;
    if (cpu < 0 || cpu >= s->nr_cpus) cpu = 0;

    int idx = -1;
    for (int i = 0; i < SMP_MAX_TASKS; i++) {
        if (s->tasks[i].pid == 0) { idx = i; break; }
    }
    if (idx < 0) return -1;
    smp_task_t *t = &s->tasks[idx];
    memset(t, 0, sizeof(*t));
    t->pid = (uint32_t)(idx + 1);
    t->cpu = cpu;
    t->last_cpu = cpu;
    t->state = TASK_READY;
    t->nice = nice;
    t->policy = 0;  /* CFS */
    t->cpumask = (1u << cpu) | 1u;
    if (name) {
        int n = 0;
        while (name[n] && n < 31) { t->name[n] = name[n]; n++; }
        t->name[n] = 0;
    }
    /* Initialize CFS sched_entity. */
    t->cfs_se.vruntime = s->rq[cpu].cfs_rq.min_vruntime;
    t->cfs_se.weight = (uint32_t)cfs_calc_weight(nice);
    t->cfs_se.time_slice_ns = (uint32_t)cfs_calc_timeslice(&s->rq[cpu].cfs_rq);
    t->cfs_se.nice = (int8_t)nice;
    /* Enqueue. */
    cfs_rq_enqueue(&s->rq[cpu].cfs_rq, &t->cfs_se);
    s->rq[cpu].nr_running++;
    s->rq[cpu].weight_sum += t->cfs_se.weight;
    /* Append to run list. */
    if (!s->rq[cpu].run_queue_tail) {
        s->rq[cpu].run_queue_head = t;
        s->rq[cpu].run_queue_tail = t;
    } else {
        t->prev = s->rq[cpu].run_queue_tail;
        s->rq[cpu].run_queue_tail->next = t;
        s->rq[cpu].run_queue_tail = t;
    }
    s->nr_tasks++;
    if (pid_out) *pid_out = t->pid;
    return 0;
}

int smp_sched_wakeup(smp_scheduler_t *s, uint32_t pid) {
    if (!s || pid == 0 || pid > SMP_MAX_TASKS) return -1;
    smp_task_t *t = &s->tasks[pid - 1];
    if (t->state == TASK_RUNNING || t->state == TASK_READY) return 0;
    /* Pick CPU by affinity. */
    int target = t->cpu;
    if (!(s->rq[target].active)) {
        for (int c = 0; c < s->nr_cpus; c++) {
            if (s->rq[c].active && (t->cpumask & (1u << c))) {
                target = c; break;
            }
        }
    }
    t->state = TASK_READY;
    t->cfs_se.vruntime = s->rq[target].cfs_rq.min_vruntime;
    cfs_rq_enqueue(&s->rq[target].cfs_rq, &t->cfs_se);
    s->rq[target].nr_running++;
    s->rq[target].weight_sum += t->cfs_se.weight;
    smp_sched_send_ipi(s, target, 1);
    return 0;
}

int smp_sched_sleep(smp_scheduler_t *s, uint32_t pid, uint64_t wakeup_time) {
    if (!s || pid == 0 || pid > SMP_MAX_TASKS) return -1;
    smp_task_t *t = &s->tasks[pid - 1];
    if (!t->cfs_se.on_rq) return 0;
    int cpu = t->cpu;
    cfs_rq_dequeue(&s->rq[cpu].cfs_rq, &t->cfs_se);
    if (s->rq[cpu].nr_running > 0) s->rq[cpu].nr_running--;
    if (s->rq[cpu].weight_sum >= t->cfs_se.weight)
        s->rq[cpu].weight_sum -= t->cfs_se.weight;
    t->state = (wakeup_time > 0) ? TASK_SLEEPING : TASK_BLOCKED;
    t->wakeup_time = wakeup_time;
    return 0;
}

int smp_sched_exit(smp_scheduler_t *s, uint32_t pid) {
    if (!s || pid == 0 || pid > SMP_MAX_TASKS) return -1;
    smp_task_t *t = &s->tasks[pid - 1];
    if (t->cfs_se.on_rq) {
        cfs_rq_dequeue(&s->rq[t->cpu].cfs_rq, &t->cfs_se);
        if (s->rq[t->cpu].weight_sum >= t->cfs_se.weight)
            s->rq[t->cpu].weight_sum -= t->cfs_se.weight;
    }
    if (s->rq[t->cpu].nr_running > 0) s->rq[t->cpu].nr_running--;
    t->state = TASK_ZOMBIE;
    t->pid = 0;
    s->nr_tasks--;
    return 0;
}

int smp_sched_set_affinity(smp_scheduler_t *s, uint32_t pid,
                            uint32_t cpumask)
{
    if (!s || pid == 0 || pid > SMP_MAX_TASKS) return -1;
    smp_task_t *t = &s->tasks[pid - 1];
    t->cpumask = cpumask ? cpumask : 1u;
    return 0;
}

int smp_sched_set_nice(smp_scheduler_t *s, uint32_t pid, int nice) {
    if (!s || pid == 0 || pid > SMP_MAX_TASKS) return -1;
    smp_task_t *t = &s->tasks[pid - 1];
    if (nice < SCHED_NICE_MIN) nice = SCHED_NICE_MIN;
    if (nice > SCHED_NICE_MAX) nice = SCHED_NICE_MAX;
    if (t->cfs_se.on_rq) {
        if (s->rq[t->cpu].weight_sum >= t->cfs_se.weight)
            s->rq[t->cpu].weight_sum -= t->cfs_se.weight;
    }
    t->cfs_se.weight = (uint32_t)cfs_calc_weight(nice);
    t->nice = nice;
    if (t->cfs_se.on_rq) {
        s->rq[t->cpu].weight_sum += t->cfs_se.weight;
    }
    return 0;
}

smp_task_t *smp_sched_pick_next(smp_scheduler_t *s, int cpu) {
    if (!s || cpu < 0 || cpu >= s->nr_cpus) return (smp_task_t *)0;
    smp_runqueue_t *rq = &s->rq[cpu];
    sched_entity_t *se = cfs_rq_pick_next(&rq->cfs_rq);
    if (!se) return rq->idle_task;
    /* Find the smp_task from its cfs_se. */
    for (int i = 0; i < SMP_MAX_TASKS; i++) {
        if (&s->tasks[i].cfs_se == se) return &s->tasks[i];
    }
    return rq->idle_task;
}

int smp_sched_run_tick(smp_scheduler_t *s, int cpu, uint64_t delta_ns) {
    if (!s || cpu < 0 || cpu >= s->nr_cpus) return -1;
    smp_runqueue_t *rq = &s->rq[cpu];
    if (rq->current_task) {
        cfs_rq_account(&rq->cfs_rq, &rq->current_task->cfs_se, delta_ns);
        rq->exec_time_ns += delta_ns;
        /* Time slice expired: pre-empt. */
        if (rq->current_task->cfs_se.time_slice_ns == 0) {
            rq->current_task->cfs_se.time_slice_ns =
                (uint32_t)cfs_calc_timeslice(&rq->cfs_rq);
            smp_task_t *next = smp_sched_pick_next(s, cpu);
            if (next && next != rq->current_task) {
                smp_sched_wakeup(s, rq->current_task->pid);
                rq->current_task->state = TASK_READY;
                rq->current_task = next;
                next->state = TASK_RUNNING;
                next->on_cpu = 1;
                s->ctx_switches++;
            }
        }
    } else {
        rq->idle_time_ns += delta_ns;
        s->idle_ticks++;
        rq->current_task = smp_sched_pick_next(s, cpu);
        if (rq->current_task) {
            rq->current_task->state = TASK_RUNNING;
            rq->current_task->on_cpu = 1;
        }
    }
    /* Periodic load balance. */
    rq->last_balance += delta_ns;
    if (rq->last_balance >= 1000000) {
        rq->last_balance = 0;
        smp_sched_load_balance(s);
    }
    return 0;
}

int smp_sched_load_balance(smp_scheduler_t *s) {
    if (!s || s->nr_cpus < 2) return 0;
    s->load_balance_runs++;
    int busiest = -1, lightest = -1;
    uint32_t max_w = 0, min_w = ~(uint32_t)0;
    for (int c = 0; c < s->nr_cpus; c++) {
        if (!s->rq[c].active) continue;
        if (s->rq[c].weight_sum > max_w) {
            max_w = s->rq[c].weight_sum;
            busiest = c;
        }
        if (s->rq[c].weight_sum < min_w) {
            min_w = s->rq[c].weight_sum;
            lightest = c;
        }
    }
    if (busiest < 0 || lightest < 0) return 0;
    if (busiest == lightest) return 0;
    if (max_w < min_w * 2) return 0;

    for (int i = 0; i < SMP_MAX_TASKS; i++) {
        smp_task_t *t = &s->tasks[i];
        if (t->pid == 0) continue;
        if (t->cpu != busiest) continue;
        if (t->state != TASK_READY) continue;
        if (!(t->cpumask & (1u << lightest))) continue;
        cfs_rq_dequeue(&s->rq[busiest].cfs_rq, &t->cfs_se);
        if (s->rq[busiest].weight_sum >= t->cfs_se.weight)
            s->rq[busiest].weight_sum -= t->cfs_se.weight;
        if (s->rq[busiest].nr_running > 0)
            s->rq[busiest].nr_running--;
        t->cpu = lightest;
        t->cfs_se.vruntime = s->rq[lightest].cfs_rq.min_vruntime;
        cfs_rq_enqueue(&s->rq[lightest].cfs_rq, &t->cfs_se);
        s->rq[lightest].nr_running++;
        s->rq[lightest].weight_sum += t->cfs_se.weight;
        smp_sched_send_ipi(s, lightest, 1);
        return 1;
    }
    return 0;
}

int smp_sched_send_ipi(smp_scheduler_t *s, int target_cpu, int reason) {
    if (!s || target_cpu < 0 || target_cpu >= s->nr_cpus) return -1;
    (void)reason;
    s->ipi_count++;
    return 0;
}

void smp_sched_print(smp_scheduler_t *s) {
    if (!s) return;
    klog_write(KLOG_INFO,
               "smp-sched: cpus=%d tasks=%d ctx=%llu bal=%llu ipi=%llu idle=%llu\n",
               s->nr_cpus, s->nr_tasks, s->ctx_switches,
               s->load_balance_runs, s->ipi_count, s->idle_ticks);
    for (int c = 0; c < s->nr_cpus; c++) {
        if (!s->rq[c].active) continue;
        klog_write(KLOG_INFO,
                   "  cpu=%d run=%u weight=%u min_vr=%llu idle_ns=%llu\n",
                   c, s->rq[c].nr_running, s->rq[c].weight_sum,
                   s->rq[c].cfs_rq.min_vruntime, s->rq[c].idle_time_ns);
    }
}