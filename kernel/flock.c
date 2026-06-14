#include "flock.h"
#include "kheap.h"
#include "string.h"
#include "sync.h"

static file_lock_t *lock_list = NULL;
static spinlock_t flock_lock;

void flock_init(void) {
    lock_list = NULL;
    spinlock_init(&flock_lock);
}

/* Check if two lock regions overlap */
static int regions_overlap(uint64_t s1, uint64_t l1, uint64_t s2, uint64_t l2) {
    uint64_t e1, e2;

    /* Length 0 means to EOF */
    if (l1 == 0) e1 = 0xFFFFFFFFFFFFFFFFULL;
    else e1 = s1 + l1 - 1;

    if (l2 == 0) e2 = 0xFFFFFFFFFFFFFFFFULL;
    else e2 = s2 + l2 - 1;

    return (s1 <= e2 && s2 <= e1);
}

/* Check if a new lock conflicts with an existing lock */
static int lock_conflicts(file_lock_t *existing, uint32_t new_pid,
                          uint32_t new_type, uint64_t new_start, uint64_t new_len) {
    /* Locks from the same process don't conflict */
    if (existing->pid == new_pid) return 0;

    /* Check region overlap */
    if (!regions_overlap(existing->start, existing->len, new_start, new_len)) return 0;

    /* Shared locks don't conflict with each other */
    if (existing->type == LOCK_SH && new_type == LOCK_SH) return 0;

    /* Any other combination conflicts */
    return 1;
}

int flock_acquire(uint32_t pid, uint32_t inode, uint32_t type,
                  uint64_t start, uint64_t len, uint32_t flags) {
    if (type != LOCK_SH && type != LOCK_EX) return -1;

    spinlock_lock(&flock_lock);

    /* Check for conflicts with existing locks */
    file_lock_t *cur = lock_list;
    while (cur) {
        if (cur->inode_num == inode &&
            lock_conflicts(cur, pid, type, start, len)) {
            /* Conflict found */
            if (flags & LOCK_NB) {
                spinlock_unlock(&flock_lock);
                return -1;  /* Non-blocking, return error */
            }
            /* In a real kernel we would block here. Since this is a
               freestanding kernel without blocking on locks, return -1. */
            spinlock_unlock(&flock_lock);
            return -1;
        }
        cur = cur->next;
    }

    /* Remove any existing lock by this process on this inode */
    file_lock_t **pp = &lock_list;
    while (*pp) {
        if ((*pp)->pid == pid && (*pp)->inode_num == inode) {
            file_lock_t *old = *pp;
            *pp = old->next;
            kfree(old);
        } else {
            pp = &(*pp)->next;
        }
    }

    /* Create new lock */
    file_lock_t *lock = (file_lock_t *)kmalloc(sizeof(file_lock_t));
    if (!lock) {
        spinlock_unlock(&flock_lock);
        return -1;
    }
    memset(lock, 0, sizeof(file_lock_t));
    lock->pid = pid;
    lock->inode_num = inode;
    lock->type = type;
    lock->start = start;
    lock->len = len;
    lock->next = lock_list;
    lock_list = lock;

    spinlock_unlock(&flock_lock);
    return 0;
}

int flock_release(uint32_t pid, uint32_t inode) {
    spinlock_lock(&flock_lock);

    file_lock_t **pp = &lock_list;
    int found = 0;
    while (*pp) {
        if ((*pp)->pid == pid && (*pp)->inode_num == inode) {
            file_lock_t *old = *pp;
            *pp = old->next;
            kfree(old);
            found = 1;
        } else {
            pp = &(*pp)->next;
        }
    }

    spinlock_unlock(&flock_lock);
    return found ? 0 : -1;
}

int flock_check(uint32_t inode, uint64_t offset, uint32_t access_type) {
    /* access_type: 0 = read, 1 = write */
    spinlock_lock(&flock_lock);

    file_lock_t *cur = lock_list;
    while (cur) {
        if (cur->inode_num == inode) {
            uint64_t end;
            if (cur->len == 0)
                end = 0xFFFFFFFFFFFFFFFFULL;
            else
                end = cur->start + cur->len - 1;

            if (offset >= cur->start && offset <= end) {
                /* Region is locked. Check if access conflicts. */
                if (cur->type == LOCK_EX) {
                    /* Exclusive lock blocks both read and write */
                    spinlock_unlock(&flock_lock);
                    return -1;
                }
                if (cur->type == LOCK_SH && access_type == 1) {
                    /* Shared lock blocks write */
                    spinlock_unlock(&flock_lock);
                    return -1;
                }
            }
        }
        cur = cur->next;
    }

    spinlock_unlock(&flock_lock);
    return 0;
}

void flock_release_all(uint32_t pid) {
    spinlock_lock(&flock_lock);

    file_lock_t **pp = &lock_list;
    while (*pp) {
        if ((*pp)->pid == pid) {
            file_lock_t *old = *pp;
            *pp = old->next;
            kfree(old);
        } else {
            pp = &(*pp)->next;
        }
    }

    spinlock_unlock(&flock_lock);
}
