#include "lockdep.h"
#include "klog.h"
#include "string.h"

static struct lock_class lock_classes[MAX_LOCK_CLASSES];
static struct lock_chain lock_chains[MAX_LOCK_CHAINS];
static struct lock_stack lock_stacks[8];
static uint8_t lockdep_initialized;
static uint32_t num_classes;
static uint32_t num_chains;
static uint32_t total_acquires;
static uint32_t total_releases;
static uint32_t abba_deadlocks;
static uint32_t recursive_locks;
static uint32_t current_cpu;

static uint32_t find_or_create_class(const char *name) {
    for (uint32_t i = 0; i < num_classes; i++) {
        if (strcmp(lock_classes[i].name, name) == 0) {
            return i;
        }
    }

    if (num_classes >= MAX_LOCK_CLASSES) return 0xFFFFFFFF;

    uint32_t idx = num_classes++;
    memset(&lock_classes[idx], 0, sizeof(struct lock_class));
    strncpy(lock_classes[idx].name, name, LOCKDEP_NAME_LEN - 1);
    lock_classes[idx].usage_mask = LOCK_USED | LOCK_ENABLED;
    return idx;
}

static int add_chain(uint16_t before, uint16_t after) {
    for (uint32_t i = 0; i < num_chains; i++) {
        if (lock_chains[i].class_before == before && lock_chains[i].class_after == after) {
            lock_chains[i].count++;
            return 0;
        }
    }

    if (num_chains >= MAX_LOCK_CHAINS) return -1;

    uint32_t idx = num_chains++;
    lock_chains[idx].class_before = before;
    lock_chains[idx].class_after = after;
    lock_chains[idx].count = 1;
    return 0;
}

static int check_abba(uint16_t held, uint16_t new_lock) {
    for (uint32_t i = 0; i < num_chains; i++) {
        if (lock_chains[i].class_before == new_lock &&
            lock_chains[i].class_after == held) {
            return -1;
        }
    }
    return 0;
}

int lockdep_init(void) {
    if (lockdep_initialized) return 0;

    memset(lock_classes, 0, sizeof(lock_classes));
    memset(lock_chains, 0, sizeof(lock_chains));
    memset(lock_stacks, 0, sizeof(lock_stacks));

    num_classes = 0;
    num_chains = 0;
    total_acquires = 0;
    total_releases = 0;
    abba_deadlocks = 0;
    recursive_locks = 0;
    current_cpu = 0;

    lockdep_initialized = 1;
    klog_info("Lock dependency validator initialized (%u classes max)", MAX_LOCK_CLASSES);
    return 0;
}

void lockdep_register_key(struct lockdep_map *lock, const char *name) {
    if (!lockdep_initialized || !lock) return;

    if (name) {
        lock->name = name;
    } else if (!lock->name) {
        lock->name = "unknown";
    }

    lock->class_idx = find_or_create_class(lock->name);
    lock->cpu = 0;
    lock->ip = 0;
}

void lockdep_unregister_key(struct lockdep_map *lock) {
    if (!lockdep_initialized || !lock) return;
    lock->class_idx = 0xFFFFFFFF;
}

void lockdep_acquire(struct lockdep_map *lock) {
    if (!lockdep_initialized || !lock) return;

    if (lock->class_idx == 0xFFFFFFFF) {
        lockdep_register_key(lock, lock->name ? lock->name : "unknown");
    }

    uint32_t class_idx = lock->class_idx;
    if (class_idx >= MAX_LOCK_CLASSES) return;

    struct lock_stack *stack = &lock_stacks[current_cpu];
    uint16_t new_class = (uint16_t)class_idx;

    for (uint32_t i = 0; i < stack->depth; i++) {
        if (stack->held_locks[i] == new_class) {
            recursive_locks++;
            klog_info("lockdep: RECURSIVE lock detected: %s", lock_classes[class_idx].name);
            return;
        }
    }

    for (uint32_t i = 0; i < stack->depth; i++) {
        uint16_t held = stack->held_locks[i];
        if (check_abba(held, new_class) < 0) {
            abba_deadlocks++;
            klog_info("lockdep: ABBA DEADLOCK detected!");
            klog_info("  Lock order violation:");
            klog_info("    Existing order: %s -> %s",
                     lock_classes[new_class].name, lock_classes[held].name);
            klog_info("    Current acquire: %s while holding %s",
                     lock_classes[new_class].name, lock_classes[held].name);
        }
        add_chain(held, new_class);
    }

    if (stack->depth < MAX_LOCK_DEPTH) {
        stack->held_locks[stack->depth++] = new_class;
    }

    lock_classes[class_idx].acquire_count++;
    lock_classes[class_idx].usage_mask |= LOCK_HELD;
    total_acquires++;
}

void lockdep_release(struct lockdep_map *lock) {
    if (!lockdep_initialized || !lock) return;

    uint32_t class_idx = lock->class_idx;
    if (class_idx >= MAX_LOCK_CLASSES || class_idx == 0xFFFFFFFF) return;

    struct lock_stack *stack = &lock_stacks[current_cpu];

    if (stack->depth == 0) {
        klog_info("lockdep: releasing lock %s with empty stack", lock_classes[class_idx].name);
        return;
    }

    if (stack->held_locks[stack->depth - 1] != class_idx) {
        klog_info("lockdep: warning: unlocking %s out of order", lock_classes[class_idx].name);
    }

    for (int i = (int)stack->depth - 1; i >= 0; i--) {
        if (stack->held_locks[i] == (uint16_t)class_idx) {
            for (uint32_t j = i; j < stack->depth - 1; j++) {
                stack->held_locks[j] = stack->held_locks[j + 1];
            }
            stack->depth--;
            break;
        }
    }

    lock_classes[class_idx].release_count++;
    total_releases++;
}

int lockdep_try_lock(struct lockdep_map *lock) {
    if (!lockdep_initialized || !lock) return 0;
    lockdep_acquire(lock);
    return 1;
}

int lockdep_check_deadlock(void) {
    return abba_deadlocks > 0 ? -1 : 0;
}

void lockdep_reset(void) {
    if (!lockdep_initialized) return;

    for (uint32_t i = 0; i < 8; i++) {
        lock_stacks[i].depth = 0;
        memset(lock_stacks[i].held_locks, 0, sizeof(lock_stacks[i].held_locks));
    }

    klog_info("lockdep: lock stacks reset");
}

void lockdep_print_stats(void) {
    klog_info("=== Lock Dependency Validator (Lockdep) Statistics ===");
    klog_info("Registered lock classes: %u / %u", num_classes, MAX_LOCK_CLASSES);
    klog_info("Lock chains tracked: %u / %u", num_chains, MAX_LOCK_CHAINS);
    klog_info("Total lock acquires: %u", total_acquires);
    klog_info("Total lock releases: %u", total_releases);
    klog_info("ABBA deadlocks detected: %u", abba_deadlocks);
    klog_info("Recursive locks detected: %u", recursive_locks);

    klog_info("Lock classes:");
    for (uint32_t i = 0; i < num_classes; i++) {
        klog_info("  [%u] %s: acquires=%u releases=%u mask=0x%x",
                 i, lock_classes[i].name,
                 lock_classes[i].acquire_count,
                 lock_classes[i].release_count,
                 lock_classes[i].usage_mask);
    }

    klog_info("Current lock depth (cpu%u): %u", current_cpu, lock_stacks[current_cpu].depth);
    for (uint32_t i = 0; i < lock_stacks[current_cpu].depth; i++) {
        uint16_t c = lock_stacks[current_cpu].held_locks[i];
        if (c < num_classes) {
            klog_info("    [%u] %s", i, lock_classes[c].name);
        }
    }
}
