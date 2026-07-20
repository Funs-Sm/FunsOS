#ifndef RCU_H
#define RCU_H

#include "stdint.h"

#define RCU_MAX_CALLBACKS 1024
#define RCU_NEXT_BATCHES  4

struct rcu_head;
typedef void (*rcu_callback_t)(struct rcu_head *head);

struct rcu_head {
    struct rcu_head *next;
    rcu_callback_t func;
    uint64_t enqueue_time;
};

struct rcu_state {
    uint8_t initialized;
    uint64_t gp_number;
    uint64_t gp_start;
    uint32_t gp_in_progress;
    uint32_t callbacks_pending;
    uint32_t callbacks_processed;
    struct rcu_head *nextlist[RCU_NEXT_BATCHES];
    struct rcu_head *waitedlist[RCU_NEXT_BATCHES];
    struct rcu_head *donelist;
    uint32_t next_pending[RCU_NEXT_BATCHES];
    uint8_t  cpu_idle[256];
    uint32_t ncpus;
};

void rcu_init(void);

void rcu_read_lock(void);
void rcu_read_unlock(void);
int rcu_read_lock_held(void);

void synchronize_rcu(void);
void call_rcu(struct rcu_head *head, rcu_callback_t func);
void kfree_call_rcu(struct rcu_head *head, rcu_callback_t func);
void rcu_barrier(void);

void rcu_check_callbacks(void);
uint32_t rcu_pending(void);

void rcu_process_callbacks(void);
void rcu_print_stats(void);

#define rcu_dereference(p) (p)
#define rcu_assign_pointer(p, v) ((p) = (v))
#define rcu_access_pointer(p) (p)

#endif
