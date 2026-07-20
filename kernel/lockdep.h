#ifndef LOCKDEP_H
#define LOCKDEP_H

#include "stdint.h"

#define MAX_LOCK_CLASSES         128
#define MAX_LOCK_DEPTH           32
#define MAX_LOCK_CHAINS          512
#define LOCKDEP_NAME_LEN         64
#define LOCKDEP_MAGIC            0x4C434B44

#define LOCK_USED                0x0001
#define LOCK_HELD                0x0002
#define LOCK_RECURSIVE           0x0004
#define LOCK_ENABLED             0x0008

struct lock_class {
    char name[LOCKDEP_NAME_LEN];
    uint32_t usage_mask;
    uint32_t acquire_count;
    uint32_t release_count;
    uint32_t contention_count;
    uint8_t  irq_safe;
};

struct lockdep_map {
    uint32_t class_idx;
    const char *name;
    uint32_t cpu;
    uint32_t ip;
};

struct lock_chain {
    uint16_t class_before;
    uint16_t class_after;
    uint32_t count;
};

struct lock_stack {
    uint32_t depth;
    uint16_t held_locks[MAX_LOCK_DEPTH];
};

int lockdep_init(void);

void lockdep_register_key(struct lockdep_map *lock, const char *name);
void lockdep_unregister_key(struct lockdep_map *lock);

void lockdep_acquire(struct lockdep_map *lock);
void lockdep_release(struct lockdep_map *lock);

int lockdep_check_deadlock(void);
int lockdep_try_lock(struct lockdep_map *lock);

void lockdep_reset(void);
void lockdep_print_stats(void);

#define DEFINE_LOCKDEP_MAP(lockname) \
    static struct lockdep_map __lockdep_map_##lockname = { .name = #lockname, .class_idx = 0xFFFFFFFF }

#define spin_lock_acquire(lock)   lockdep_acquire((struct lockdep_map *)lock)
#define spin_lock_release(lock)   lockdep_release((struct lockdep_map *)lock)

#endif
