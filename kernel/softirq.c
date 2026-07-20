#include "softirq.h"
#include "string.h"
#include "kheap.h"
#include "klog.h"
#include "sync.h"
#include "stdio.h"

struct softirq_action {
    softirq_handler_t handler;
    uint64_t count;
    const char *name;
};

static struct softirq_action softirq_vec[NR_SOFTIRQS];
static volatile uint32_t softirq_pending;
static uint32_t softirq_processing;
static uint8_t softirq_initialized = 0;
static uint64_t softirq_invocation_count;

static spinlock_t tasklet_lock;
static struct tasklet_struct *tasklet_list;
static struct tasklet_struct *tasklet_hi_list;

static void tasklet_action(void) {
    struct tasklet_struct *t;
    struct tasklet_struct *prev = NULL;

    spinlock_lock(&tasklet_lock);
    t = tasklet_list;
    tasklet_list = NULL;
    spinlock_unlock(&tasklet_lock);

    while (t) {
        struct tasklet_struct *next = t->next;
        t->state &= ~(1U << TASKLET_STATE_SCHED);

        if (t->count == 0 && t->func) {
            t->func(t->data);
        } else {
            spinlock_lock(&tasklet_lock);
            t->next = tasklet_list;
            tasklet_list = t;
            spinlock_unlock(&tasklet_lock);
        }
        t = next;
        (void)prev;
    }
}

static void tasklet_hi_action(void) {
    struct tasklet_struct *t;

    spinlock_lock(&tasklet_lock);
    t = tasklet_hi_list;
    tasklet_hi_list = NULL;
    spinlock_unlock(&tasklet_lock);

    while (t) {
        struct tasklet_struct *next = t->next;
        t->state &= ~(1U << TASKLET_STATE_SCHED);

        if (t->count == 0 && t->func) {
            t->func(t->data);
        }
        t = next;
    }
}

static const char *softirq_names[NR_SOFTIRQS] = {
    "HI", "TIMER", "NET_TX", "NET_RX", "BLOCK",
    "TASKLET", "SCHED", "HRTIMER", "RCU"
};

void softirq_init(void) {
    if (softirq_initialized) return;

    memset(softirq_vec, 0, sizeof(softirq_vec));
    softirq_pending = 0;
    softirq_processing = 0;
    softirq_invocation_count = 0;
    tasklet_list = NULL;
    tasklet_hi_list = NULL;

    spinlock_init(&tasklet_lock);

    for (int i = 0; i < NR_SOFTIRQS; i++) {
        softirq_vec[i].name = softirq_names[i];
    }

    open_softirq(TASKLET_SOFTIRQ, tasklet_action);
    open_softirq(HI_SOFTIRQ, tasklet_hi_action);

    softirq_initialized = 1;
    klog_info("SoftIRQ subsystem initialized (%u vectors)", NR_SOFTIRQS);
}

void open_softirq(uint32_t nr, softirq_handler_t handler) {
    if (nr >= NR_SOFTIRQS || !handler) return;
    softirq_vec[nr].handler = handler;
}

void raise_softirq(uint32_t nr) {
    if (nr >= NR_SOFTIRQS) return;
    softirq_pending |= (1U << nr);
}

void do_softirq(void) {
    uint32_t pending;
    uint32_t max_restart = 10;

    if (softirq_processing) return;
    softirq_processing = 1;

restart:
    pending = softirq_pending;
    softirq_pending = 0;

    for (uint32_t i = 0; i < NR_SOFTIRQS && pending; i++) {
        if (pending & 1) {
            if (softirq_vec[i].handler) {
                softirq_vec[i].handler();
                softirq_vec[i].count++;
            }
        }
        pending >>= 1;
    }

    softirq_invocation_count++;

    if (softirq_pending && --max_restart) {
        goto restart;
    }

    softirq_processing = 0;
}

void softirq_check_pending(void) {
    if (softirq_pending && !softirq_processing) {
        do_softirq();
    }
}

uint32_t softirq_pending_count(void) {
    uint32_t pending = softirq_pending;
    uint32_t count = 0;
    while (pending) {
        count += pending & 1;
        pending >>= 1;
    }
    return count;
}

void softirq_print_stats(void) {
    klog_info("=== SoftIRQ Statistics ===");
    klog_info("Invocations: %lu", (unsigned long)softirq_invocation_count);
    klog_info("Pending: %u", softirq_pending_count());
    klog_info("Per-vector counts:");
    for (int i = 0; i < NR_SOFTIRQS; i++) {
        klog_info("  [%2u] %-12s %lu", i, softirq_vec[i].name,
                 (unsigned long)softirq_vec[i].count);
    }
}

void tasklet_init(struct tasklet_struct *t, tasklet_handler_t func, unsigned long data) {
    if (!t) return;
    t->next = NULL;
    t->func = func;
    t->data = data;
    t->count = 0;
    t->state = 0;
}

void tasklet_schedule(struct tasklet_struct *t) {
    if (!t || !t->func) return;
    if (t->state & (1U << TASKLET_STATE_SCHED)) return;

    t->state |= (1U << TASKLET_STATE_SCHED);

    spinlock_lock(&tasklet_lock);
    t->next = tasklet_list;
    tasklet_list = t;
    spinlock_unlock(&tasklet_lock);

    raise_softirq(TASKLET_SOFTIRQ);
}

void tasklet_hi_schedule(struct tasklet_struct *t) {
    if (!t || !t->func) return;
    if (t->state & (1U << TASKLET_STATE_SCHED)) return;

    t->state |= (1U << TASKLET_STATE_SCHED);

    spinlock_lock(&tasklet_lock);
    t->next = tasklet_hi_list;
    tasklet_hi_list = t;
    spinlock_unlock(&tasklet_lock);

    raise_softirq(HI_SOFTIRQ);
}

void tasklet_kill(struct tasklet_struct *t) {
    if (!t) return;
    while (t->state & (1U << TASKLET_STATE_SCHED)) {
        asm volatile("pause");
    }
    t->func = NULL;
}

void tasklet_disable(struct tasklet_struct *t) {
    if (t) t->count++;
}

void tasklet_enable(struct tasklet_struct *t) {
    if (t && t->count > 0) t->count--;
}
