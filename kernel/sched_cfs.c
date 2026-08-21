/* sched_cfs.c - Completely Fair Scheduler (CFS) module.
 *
 * Implements a virtual-runtimed red-black tree-based fair scheduler.
 * - The leftmost entity (smallest vruntime) is selected each tick.
 * - On every update the entity's vruntime advances by
 *   delta_exec_ns * NICE_0_LOAD / weight, so nicer tasks run slower.
 * - min_vruntime is a per-rq monotonic clock that prevents
 *   underflow.
 */

#include "sched_cfs.h"
#include "string.h"

int cfs_rq_init(cfs_rq_t *rq) {
    if (!rq) return -1;
    memset(rq, 0, sizeof(*rq));
    rq->min_vruntime = 0;
    rq->target_latency = CFS_TARGET_LATENCY_NS;
    rq->min_granularity = CFS_MIN_GRANULARITY_NS;
    rq->inited = 1;
    return 0;
}

uint64_t cfs_calc_weight(int nice) {
    if (nice < SCHED_NICE_MIN) nice = SCHED_NICE_MIN;
    if (nice > SCHED_NICE_MAX) nice = SCHED_NICE_MAX;
    uint64_t w = NICE_TO_WEIGHT(nice);
    return w ? w : 1;
}

/* Timeslice = target_latency * (se->weight / rq->weight_sum).
 * If rq->weight_sum is zero (no tasks), timeslice = target_latency. */
uint64_t cfs_calc_timeslice(cfs_rq_t *rq) {
    if (!rq) return 0;
    if (rq->weight_sum == 0) return rq->target_latency;
    /* Pick the smallest weight entity by scanning. */
    sched_entity_t *e = rb_leftmost(rq);
    if (!e) return rq->target_latency;
    uint64_t s = (rq->target_latency * e->weight) / (uint64_t)rq->weight_sum;
    if (s < rq->min_granularity) s = rq->min_granularity;
    return s;
}

uint64_t cfs_rq_min_vruntime(cfs_rq_t *rq) {
    return rq ? rq->min_vruntime : 0;
}

/* ---- Red-black tree (Linux-style RB-tree, simplified) ---- */

#define RB_RED   0
#define RB_BLACK 1

static void rb_set_parent(sched_entity_t *n, sched_entity_t *p) {
    n->rb_parent = p;
}
static sched_entity_t *rb_parent(sched_entity_t *n) { return n->rb_parent; }
static int rb_color(sched_entity_t *n) { return n->rb_color; }
static void rb_set_color(sched_entity_t *n, int c) { n->rb_color = (uint32_t)c; }

static void rb_rotate_left(cfs_rq_t *rq, sched_entity_t *x) {
    sched_entity_t *y = x->rb_right;
    x->rb_right = y->rb_left;
    if (y->rb_left) rb_set_parent(y->rb_left, x);
    rb_set_parent(y, rb_parent(x));
    if (!rb_parent(x)) {
        rq->root = y;
    } else if (x == rb_parent(x)->rb_left) {
        rb_parent(x)->rb_left = y;
    } else {
        rb_parent(x)->rb_right = y;
    }
    y->rb_left = x;
    rb_set_parent(x, y);
}

static void rb_rotate_right(cfs_rq_t *rq, sched_entity_t *x) {
    sched_entity_t *y = x->rb_left;
    x->rb_left = y->rb_right;
    if (y->rb_right) rb_set_parent(y->rb_right, x);
    rb_set_parent(y, rb_parent(x));
    if (!rb_parent(x)) {
        rq->root = y;
    } else if (x == rb_parent(x)->rb_right) {
        rb_parent(x)->rb_right = y;
    } else {
        rb_parent(x)->rb_left = y;
    }
    y->rb_right = x;
    rb_set_parent(x, y);
}

int rb_insert(cfs_rq_t *rq, sched_entity_t *se) {
    if (!rq || !se) return -1;
    sched_entity_t *parent = (sched_entity_t *)0;
    sched_entity_t **link = &rq->root;
    while (*link) {
        parent = *link;
        if (se->vruntime < parent->vruntime) {
            link = &parent->rb_left;
        } else {
            link = &parent->rb_right;
        }
    }
    se->rb_parent = parent;
    se->rb_left = se->rb_right = (sched_entity_t *)0;
    se->rb_color = RB_RED;
    *link = se;
    /* Rebalance. */
    while (rb_parent(se) && rb_color(rb_parent(se)) == RB_RED) {
        sched_entity_t *g = rb_parent(rb_parent(se));
        if (!g) break;
        if (rb_parent(se) == g->rb_left) {
            sched_entity_t *u = g->rb_right;
            if (u && rb_color(u) == RB_RED) {
                rb_set_color(rb_parent(se), RB_BLACK);
                rb_set_color(u, RB_BLACK);
                rb_set_color(g, RB_RED);
                se = g;
                continue;
            }
            if (se == rb_parent(se)->rb_right) {
                se = rb_parent(se);
                rb_rotate_left(rq, se);
            }
            rb_set_color(rb_parent(se), RB_BLACK);
            rb_set_color(g, RB_RED);
            rb_rotate_right(rq, g);
        } else {
            sched_entity_t *u = g->rb_left;
            if (u && rb_color(u) == RB_RED) {
                rb_set_color(rb_parent(se), RB_BLACK);
                rb_set_color(u, RB_BLACK);
                rb_set_color(g, RB_RED);
                se = g;
                continue;
            }
            if (se == rb_parent(se)->rb_left) {
                se = rb_parent(se);
                rb_rotate_right(rq, se);
            }
            rb_set_color(rb_parent(se), RB_BLACK);
            rb_set_color(g, RB_RED);
            rb_rotate_left(rq, g);
        }
    }
    rb_set_color(rq->root, RB_BLACK);
    rq->rb_node_count++;
    return 0;
}

static sched_entity_t *rb_subtree_min(sched_entity_t *n) {
    if (!n) return (sched_entity_t *)0;
    while (n->rb_left) n = n->rb_left;
    return n;
}

void rb_erase(cfs_rq_t *rq, sched_entity_t *se) {
    if (!rq || !se) return;
    /* Simplified erase: replace with successor, then delete. */
    sched_entity_t *to_delete = se;
    sched_entity_t *replacement = (sched_entity_t *)0;
    if (to_delete->rb_left && to_delete->rb_right) {
        /* Two children: replace with successor. */
        sched_entity_t *succ = rb_subtree_min(to_delete->rb_right);
        to_delete->vruntime = succ->vruntime;
        to_delete = succ;
    }
    replacement = to_delete->rb_left ? to_delete->rb_left
                                     : to_delete->rb_right;
    if (replacement) rb_set_parent(replacement, rb_parent(to_delete));
    if (!rb_parent(to_delete)) {
        rq->root = replacement;
    } else if (to_delete == rb_parent(to_delete)->rb_left) {
        rb_parent(to_delete)->rb_left = replacement;
    } else {
        rb_parent(to_delete)->rb_right = replacement;
    }
    se->rb_left = se->rb_right = se->rb_parent = (sched_entity_t *)0;
    rq->rb_node_count--;
}

sched_entity_t *rb_leftmost(cfs_rq_t *rq) {
    if (!rq || !rq->root) return (sched_entity_t *)0;
    return rb_subtree_min(rq->root);
}

int cfs_rq_enqueue(cfs_rq_t *rq, sched_entity_t *se) {
    if (!rq || !se) return -1;
    if (se->on_rq) return -1;
    /* Adjust vruntime so it can't be less than min_vruntime - sys_latency. */
    if (se->vruntime < rq->min_vruntime) se->vruntime = rq->min_vruntime;
    rb_insert(rq, se);
    se->on_rq = 1;
    rq->nr_running++;
    rq->weight_sum += (int32_t)se->weight;
    return 0;
}

int cfs_rq_dequeue(cfs_rq_t *rq, sched_entity_t *se) {
    if (!rq || !se) return -1;
    if (!se->on_rq) return -1;
    rb_erase(rq, se);
    se->on_rq = 0;
    rq->nr_running--;
    if (se->weight && rq->weight_sum >= (int32_t)se->weight)
        rq->weight_sum -= (int32_t)se->weight;
    return 0;
}

sched_entity_t *cfs_rq_pick_next(cfs_rq_t *rq) {
    if (!rq) return (sched_entity_t *)0;
    return rb_leftmost(rq);
}

void cfs_rq_account(cfs_rq_t *rq, sched_entity_t *se, uint64_t delta_exec_ns)
{
    if (!rq || !se) return;
    uint64_t w = se->weight ? se->weight : 1;
    uint64_t delta = (delta_exec_ns * NICE_0_LOAD) / w;
    se->vruntime += delta;
    se->sum_exec_runtime += delta_exec_ns;
    if (se->vruntime > rq->min_vruntime) {
        rq->min_vruntime = se->vruntime;
    }
    rq->exec_clock += delta_exec_ns;
}