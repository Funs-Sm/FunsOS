#ifndef SCHED_CFS_H
#define SCHED_CFS_H

#include "stdint.h"

/* CFS (Completely Fair Scheduler) module.
 *
 * Implements the Linux CFS virtual runtime rbtree model:
 *   - Each task tracks a virtual runtime (vruntime).
 *   - Tasks are placed in a red-black tree keyed by vruntime.
 *   - On each tick the leftmost (smallest vruntime) task is picked.
 *   - vruntime advances with delta_exec / weight, where weight is
 *     derived from nice (NICE_TO_WEIGHT).
 *
 * The SMP layer (sched_smp.h) builds on CFS by giving each CPU its
 * own rq->cfs_rq and providing load balancing.
 */

#define NICE_TO_WEIGHT(n)  ((n) >= 0 ? (1024 >> ((n) > 19 ? 19 : (n))) \
                                   : (1024 << ((-(n)) > 19 ? 19 : (-(n)))))
#define NICE_0_LOAD         1024

/* CFS weight table (subset of Linux). */
#define SCHED_NICE_MIN    -20
#define SCHED_NICE_MAX     19
#define SCHED_NICE_WIDTH   40

/* CFS tunables */
#define CFS_TARGET_LATENCY_NS  (20 * 1000 * 1000)    /* 20 ms */
#define CFS_MIN_GRANULARITY_NS (1 * 1000 * 1000)     /* 1 ms */

/* Fair scheduling class. */
typedef struct sched_entity {
    uint64_t vruntime;
    uint64_t sum_exec_runtime;
    uint64_t prev_sum_exec_runtime;
    uint32_t weight;
    int8_t   nice;
    uint32_t time_slice_ns;
    /* rb-tree node. */
    uint64_t rb_key;        /* = vruntime */
    uint32_t rb_color;      /* 0 = red, 1 = black */
    struct sched_entity *rb_left;
    struct sched_entity *rb_right;
    struct sched_entity *rb_parent;
    uint8_t  on_rq;         /* 1 if on runqueue */
    uint8_t  is_leftmost;   /* 1 if currently the leftmost */
} sched_entity_t;

/* Per-CPU runqueue's CFS portion. */
typedef struct {
    sched_entity_t *root;
    uint32_t        rb_node_count;
    uint64_t        min_vruntime;       /* monotonic clock for this rq */
    uint64_t        exec_clock;
    int32_t         nr_running;
    int32_t         weight_sum;
    /* Tunables. */
    uint64_t        target_latency;
    uint64_t        min_granularity;
    int             inited;
} cfs_rq_t;

int  cfs_rq_init(cfs_rq_t *rq);
int  cfs_rq_enqueue(cfs_rq_t *rq, sched_entity_t *se);
int  cfs_rq_dequeue(cfs_rq_t *rq, sched_entity_t *se);
sched_entity_t *cfs_rq_pick_next(cfs_rq_t *rq);
void cfs_rq_account(cfs_rq_t *rq, sched_entity_t *se, uint64_t delta_exec_ns);
uint64_t cfs_rq_min_vruntime(cfs_rq_t *rq);
uint64_t cfs_calc_weight(int nice);
uint64_t cfs_calc_timeslice(cfs_rq_t *rq);

/* RB-tree helpers (used by cfs_rq). */
int  rb_insert(cfs_rq_t *rq, sched_entity_t *se);
void rb_erase(cfs_rq_t *rq, sched_entity_t *se);
sched_entity_t *rb_leftmost(cfs_rq_t *rq);

#endif