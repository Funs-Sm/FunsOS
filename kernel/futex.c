#include "futex.h"
#include "kernel_proc.h"
#include "sched.h"
#include "kheap.h"
#include "spinlock.h"
#include "string.h"
#include "../drivers/keyboard.h"

static futex_global_state_t futex_state;

static inline uint32_t futex_hash(uint32_t *uaddr) {
    uint32_t addr = (uint32_t)uaddr;
    addr ^= (addr >> 16);
    addr ^= (addr >> 8);
    return addr % FUTEX_HASH_SIZE;
}

void futex_init(void) {
    for (int i = 0; i < FUTEX_HASH_SIZE; i++) {
        futex_state.buckets[i].head = NULL;
        futex_state.buckets[i].tail = NULL;
        futex_state.buckets[i].count = 0;
        spinlock_init(&futex_state.buckets[i].lock);
    }
    futex_state.total_waiters = 0;
    futex_state.wake_count = 0;
    futex_state.wait_count = 0;
    futex_state.requeue_count = 0;
}

static futex_q_t *futex_find_waiter(futex_bucket_t *bucket, uint32_t *uaddr) {
    futex_q_t *q = bucket->head;
    while (q) {
        if (q->uaddr == uaddr && q->state == FUTEX_WAITING) {
            return q;
        }
        q = q->next;
    }
    return NULL;
}

static void futex_add_waiter(futex_bucket_t *bucket, futex_q_t *q) {
    q->next = NULL;
    q->prev = bucket->tail;
    if (bucket->tail) {
        bucket->tail->next = q;
    } else {
        bucket->head = q;
    }
    bucket->tail = q;
    bucket->count++;
    futex_state.total_waiters++;
}

static void futex_remove_waiter(futex_bucket_t *bucket, futex_q_t *q) {
    if (q->prev) {
        q->prev->next = q->next;
    } else {
        bucket->head = q->next;
    }
    if (q->next) {
        q->next->prev = q->prev;
    } else {
        bucket->tail = q->prev;
    }
    bucket->count--;
    if (futex_state.total_waiters > 0) {
        futex_state.total_waiters--;
    }
}

static int futex_wake_internal(futex_bucket_t *bucket, uint32_t *uaddr,
                               int count, uint32_t bitset) {
    int woken = 0;
    futex_q_t *q = bucket->head;

    while (q && woken < count) {
        futex_q_t *next = q->next;
        if (q->uaddr == uaddr && q->state == FUTEX_WAITING) {
            if (bitset == 0 || (q->bitset & bitset)) {
                q->state = FUTEX_WOKEN;
                if (q->task) {
                    sched_unblock((pcb_t *)q->task);
                }
                woken++;
            }
        }
        q = next;
    }

    futex_state.wake_count += woken;
    return woken;
}

int futex_wait(uint32_t *uaddr, uint32_t val, uint32_t timeout_ms) {
    return futex_wait_bitset(uaddr, val, timeout_ms, 0xFFFFFFFF);
}

int futex_wait_bitset(uint32_t *uaddr, uint32_t val,
                      uint32_t timeout_ms, uint32_t bitset) {
    if (!uaddr) return -22;

    uint32_t cur_val = *uaddr;
    if (cur_val != val) {
        return -11;
    }

    uint32_t idx = futex_hash(uaddr);
    futex_bucket_t *bucket = &futex_state.buckets[idx];

    futex_q_t q;
    memset(&q, 0, sizeof(q));
    q.uaddr = uaddr;
    q.bitset = bitset;
    q.state = FUTEX_WAITING;
    q.task = (void *)sched_get_current();
    q.pid = sched_get_current() ? sched_get_current()->pid : 0;

    spinlock_lock(&bucket->lock);
    futex_add_waiter(bucket, &q);
    spinlock_unlock(&bucket->lock);

    futex_state.wait_count++;

    pcb_t *curr = sched_get_current();
    if (curr) {
        sched_block(curr, 7);

        if (timeout_ms > 0) {
            uint64_t start = sched_get_tick_count();
            while (q.state == FUTEX_WAITING) {
                if (kb_signal_check()) {
                    q.state = FUTEX_RETRY;
                    break;
                }
                if ((uint32_t)(sched_get_tick_count() - start) >= timeout_ms) {
                    q.state = FUTEX_TIMEDOUT;
                    break;
                }
                sched_sleep(10);
            }
        } else {
            while (q.state == FUTEX_WAITING) {
                if (kb_signal_check()) {
                    q.state = FUTEX_RETRY;
                    break;
                }
                sched_sleep(10);
            }
        }
    }

    spinlock_lock(&bucket->lock);
    if (q.state == FUTEX_WAITING) {
        futex_remove_waiter(bucket, &q);
    }
    spinlock_unlock(&bucket->lock);

    switch (q.state) {
        case FUTEX_WOKEN:
            return 0;
        case FUTEX_TIMEDOUT:
            return -110;
        case FUTEX_RETRY:
            return -4;
        default:
            return -5;
    }
}

int futex_wake(uint32_t *uaddr, int count) {
    return futex_wake_bitset(uaddr, count, 0xFFFFFFFF);
}

int futex_wake_bitset(uint32_t *uaddr, int count, uint32_t bitset) {
    if (!uaddr || count <= 0) return -22;

    uint32_t idx = futex_hash(uaddr);
    futex_bucket_t *bucket = &futex_state.buckets[idx];

    spinlock_lock(&bucket->lock);
    int woken = futex_wake_internal(bucket, uaddr, count, bitset);
    spinlock_unlock(&bucket->lock);

    return woken;
}

int futex_requeue(uint32_t *uaddr, int val, int val2, uint32_t *uaddr2) {
    if (!uaddr || !uaddr2) return -22;

    uint32_t idx1 = futex_hash(uaddr);
    uint32_t idx2 = futex_hash(uaddr2);
    futex_bucket_t *bucket1 = &futex_state.buckets[idx1];
    futex_bucket_t *bucket2 = &futex_state.buckets[idx2];

    int woken = 0;
    int requeued = 0;

    spinlock_lock(&bucket1->lock);
    if (idx1 != idx2) {
        spinlock_lock(&bucket2->lock);
    }

    woken = futex_wake_internal(bucket1, uaddr, val, 0xFFFFFFFF);

    futex_q_t *q = bucket1->head;
    while (q && requeued < val2) {
        futex_q_t *next = q->next;
        if (q->uaddr == uaddr && q->state == FUTEX_WAITING) {
            futex_remove_waiter(bucket1, q);
            q->uaddr = uaddr2;
            q->state = FUTEX_REQUEUED;
            q->state = FUTEX_WAITING;
            futex_add_waiter(bucket2, q);
            requeued++;
        }
        q = next;
    }

    futex_state.requeue_count += requeued;

    if (idx1 != idx2) {
        spinlock_unlock(&bucket2->lock);
    }
    spinlock_unlock(&bucket1->lock);

    return woken + requeued;
}

int futex_cmp_requeue(uint32_t *uaddr, int val, int val2,
                      uint32_t *uaddr2, uint32_t val3) {
    if (!uaddr || !uaddr2) return -22;

    if (*uaddr != val3) {
        return -11;
    }

    return futex_requeue(uaddr, val, val2, uaddr2);
}

int sys_futex(uint32_t *uaddr, int op, uint32_t val,
              uint32_t timeout, uint32_t *uaddr2, uint32_t val3) {
    int op_type = op & 0x7F;

    switch (op_type) {
        case FUTEX_WAIT:
            return futex_wait(uaddr, val, timeout);
        case FUTEX_WAKE:
            return futex_wake(uaddr, (int)val);
        case FUTEX_REQUEUE:
            return futex_requeue(uaddr, (int)val, (int)timeout, uaddr2);
        case FUTEX_CMP_REQUEUE:
            return futex_cmp_requeue(uaddr, (int)val, (int)timeout, uaddr2, val3);
        case FUTEX_WAIT_BITSET:
            return futex_wait_bitset(uaddr, val, timeout, val3);
        case FUTEX_WAKE_BITSET:
            return futex_wake_bitset(uaddr, (int)val, val3);
        default:
            return -38;
    }
}

void futex_get_stats(uint64_t *wait_count, uint64_t *wake_count,
                     uint64_t *requeue_count, uint32_t *total_waiters) {
    if (wait_count) *wait_count = futex_state.wait_count;
    if (wake_count) *wake_count = futex_state.wake_count;
    if (requeue_count) *requeue_count = futex_state.requeue_count;
    if (total_waiters) *total_waiters = futex_state.total_waiters;
}

void futex_dump(void) {
    for (int i = 0; i < FUTEX_HASH_SIZE; i++) {
        futex_bucket_t *bucket = &futex_state.buckets[i];
        if (bucket->count > 0) {
            futex_q_t *q = bucket->head;
            while (q) {
                q = q->next;
            }
        }
    }
}
