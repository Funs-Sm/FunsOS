/*
 * lib/rbtree.h - intrusive red-black tree (Julias-style).
 *
 * Used by:
 *   - kernel/sched_cfs: tasks ordered by virtual runtime (vruntime)
 *   - fs/page_cache  : pages ordered by access recency
 *   - kernel/mmu     : vma ranges ordered by address
 *
 * The tree is keyed on a uint64_t field embedded in the caller
 * structure (typically rb_key).  The user supplies the offset of the
 * key by calling rbtree_init(rb_root_t *, offsetof(struct, rb_key)).
 */
#ifndef LIB_RBTREE_H
#define LIB_RBTREE_H

#include "stdint.h"
#include "stdbool.h"
#include "stddef.h"

typedef struct rb_node {
    struct rb_node *parent;
    struct rb_node *left;
    struct rb_node *right;
    uint32_t        color;     /* 0 = red, 1 = black */
} rb_node_t;

typedef struct rb_root {
    rb_node_t *node;
} rb_root_t;

#define RB_RED   0u
#define RB_BLACK 1u

#define RBTREE_INIT(name)  { .node = NULL }
#define RB_NODE_INIT       { .parent = NULL, .left = NULL, .right = NULL, .color = RB_RED }

void rbtree_init(rb_root_t *root);
void rbtree_insert(rb_root_t *root, rb_node_t *node, uint64_t key,
                   size_t key_offset);
rb_node_t *rbtree_find(rb_root_t *root, uint64_t key, size_t key_offset);
rb_node_t *rbtree_first(rb_root_t *root);
rb_node_t *rbtree_last(rb_root_t *root);
rb_node_t *rbtree_next(rb_node_t *node);
rb_node_t *rbtree_prev(rb_node_t *node);
void rbtree_erase(rb_root_t *root, rb_node_t *node);
bool rbtree_empty(rb_root_t *root);

/* Stats for debugfs-style report. */
typedef struct rbtree_stats {
    uint64_t inserts;
    uint64_t erases;
    uint64_t finds;
    uint64_t rotations;
    uint32_t max_depth;
} rbtree_stats_t;

void rbtree_get_stats(rbtree_stats_t *out);
void rbtree_reset_stats(void);

#endif /* LIB_RBTREE_H */
