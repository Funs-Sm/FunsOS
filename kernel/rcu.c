#include "rcu.h"
#include "kheap.h"
#include "string.h"
#include "timer.h"
#include "klog.h"
#include "sync.h"
#include "stdio.h"

static struct rcu_state rcu_state_obj;
static volatile int32_t rcu_read_lock_nesting = 0;
static uint8_t rcu_initialized = 0;
static spinlock_t rcu_lock;

void rcu_init(void) {
    if (rcu_initialized) return;
    spinlock_init(&rcu_lock);
    memset(&rcu_state_obj, 0, sizeof(rcu_state_obj));
    rcu_state_obj.gp_number = 1;
    rcu_state_obj.ncpus = 1;
    rcu_state_obj.cpu_idle[0] = 0;
    for (int i = 0; i < RCU_NEXT_BATCHES; i++) {
        rcu_state_obj.nextlist[i] = NULL;
        rcu_state_obj.waitedlist[i] = NULL;
        rcu_state_obj.next_pending[i] = 0;
    }
    rcu_state_obj.donelist = NULL;
    rcu_initialized = 1;
    klog_info("RCU (Read-Copy-Update) subsystem initialized");
}

void rcu_read_lock(void) {
    __sync_fetch_and_add(&rcu_read_lock_nesting, 1);
}

void rcu_read_unlock(void) {
    __sync_fetch_and_sub(&rcu_read_lock_nesting, 1);
}

int rcu_read_lock_held(void) {
    return rcu_read_lock_nesting > 0;
}

static void rcu_enqueue_callback(struct rcu_head *head, rcu_callback_t func) {
    head->func = func;
    head->next = NULL;
    head->enqueue_time = timer_get_ticks();

    spinlock_lock(&rcu_lock);
    int batch = 0;
    if (rcu_state_obj.nextlist[batch] == NULL) {
        rcu_state_obj.nextlist[batch] = head;
    } else {
        struct rcu_head *tail = rcu_state_obj.nextlist[batch];
        while (tail->next) tail = tail->next;
        tail->next = head;
    }
    rcu_state_obj.next_pending[batch]++;
    rcu_state_obj.callbacks_pending++;
    spinlock_unlock(&rcu_lock);
}

void call_rcu(struct rcu_head *head, rcu_callback_t func) {
    if (!head || !func) return;
    rcu_enqueue_callback(head, func);
}

static void rcu_kfree_callback(struct rcu_head *head) {
    void *ptr = (void *)((uintptr_t)head - (uintptr_t)&((struct rcu_head*)0)->func - sizeof(rcu_callback_t));
    kfree(ptr);
}

void kfree_call_rcu(struct rcu_head *head, rcu_callback_t func) {
    if (!head) return;
    if (func) {
        call_rcu(head, func);
    } else {
        call_rcu(head, rcu_kfree_callback);
    }
}

void synchronize_rcu(void) {
    if (!rcu_initialized) return;
    uint64_t start = timer_get_ticks();
    while (rcu_read_lock_nesting > 0) {
        for (volatile int i = 0; i < 1000; i++) { asm volatile("pause"); }
        if ((uint64_t)(timer_get_ticks() - start) > 1000) break;
    }
    rcu_state_obj.gp_number++;
    rcu_state_obj.gp_start = timer_get_ticks();
}

static void rcu_advance_callbacks(void) {
    spinlock_lock(&rcu_lock);
    for (int i = RCU_NEXT_BATCHES - 1; i > 0; i--) {
        if (rcu_state_obj.waitedlist[i]) {
            if (!rcu_state_obj.donelist) {
                rcu_state_obj.donelist = rcu_state_obj.waitedlist[i];
            } else {
                struct rcu_head *tail = rcu_state_obj.donelist;
                while (tail->next) tail = tail->next;
                tail->next = rcu_state_obj.waitedlist[i];
            }
            rcu_state_obj.waitedlist[i] = NULL;
        }
        rcu_state_obj.waitedlist[i] = rcu_state_obj.nextlist[i - 1];
        rcu_state_obj.nextlist[i - 1] = NULL;
    }

    if (rcu_read_lock_nesting == 0) {
        if (rcu_state_obj.waitedlist[0]) {
            if (!rcu_state_obj.donelist) {
                rcu_state_obj.donelist = rcu_state_obj.waitedlist[0];
            } else {
                struct rcu_head *tail = rcu_state_obj.donelist;
                while (tail->next) tail = tail->next;
                tail->next = rcu_state_obj.waitedlist[0];
            }
            rcu_state_obj.waitedlist[0] = NULL;
        }
        rcu_state_obj.waitedlist[0] = rcu_state_obj.nextlist[0];
        rcu_state_obj.nextlist[0] = NULL;
    }
    spinlock_unlock(&rcu_lock);
}

void rcu_process_callbacks(void) {
    if (!rcu_initialized) return;
    rcu_advance_callbacks();

    spinlock_lock(&rcu_lock);
    struct rcu_head *list = rcu_state_obj.donelist;
    rcu_state_obj.donelist = NULL;
    spinlock_unlock(&rcu_lock);

    while (list) {
        struct rcu_head *next = list->next;
        if (list->func) list->func(list);
        rcu_state_obj.callbacks_processed++;
        rcu_state_obj.callbacks_pending--;
        list = next;
    }
}

uint32_t rcu_pending(void) {
    return rcu_state_obj.callbacks_pending;
}

void rcu_check_callbacks(void) {
    if (rcu_pending() > 0) {
        rcu_process_callbacks();
    }
}

void rcu_barrier(void) {
    while (rcu_pending() > 0) {
        rcu_process_callbacks();
        for (volatile int i = 0; i < 1000; i++) { asm volatile("pause"); }
    }
}

void rcu_print_stats(void) {
    klog_info("=== RCU Statistics ===");
    klog_info("  Grace period:      %llu", (unsigned long long)rcu_state_obj.gp_number);
    klog_info("  GP in progress:    %s", rcu_state_obj.gp_in_progress ? "yes" : "no");
    klog_info("  Callbacks pending: %u", rcu_state_obj.callbacks_pending);
    klog_info("  Callbacks done:    %u", rcu_state_obj.callbacks_processed);
    klog_info("  Read lock nesting: %d", rcu_read_lock_nesting);
    uint32_t total_next = 0;
    for (int i = 0; i < RCU_NEXT_BATCHES; i++) {
        total_next += rcu_state_obj.next_pending[i];
    }
    klog_info("  In next lists:     %u", total_next);
}
