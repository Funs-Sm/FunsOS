#include "sync.h"
#include "sched.h"
#include "process.h"
#include "kheap.h"
#include "stddef.h"

extern pcb_t *current_process;

static inline int32_t xchg(volatile int32_t *addr, int32_t newval) {
    asm volatile("xchg %0, %1" : "=r"(newval), "+m"(*addr) : "0"(newval));
    return newval;
}

void mutex_init(mutex_t *m) {
    m->locked = 0;
    m->owner = NULL;
    m->wait_queue = NULL;
}

void mutex_lock(mutex_t *m) {
    if (m->locked == 0) {
        m->locked = 1;
        m->owner = current_process;
        return;
    }

    wait_node_t *node = (wait_node_t *)kmalloc(sizeof(wait_node_t));
    node->process = current_process;
    node->next = NULL;

    if (m->wait_queue == NULL) {
        m->wait_queue = node;
    } else {
        wait_node_t *curr = m->wait_queue;
        while (curr->next != NULL) {
            curr = curr->next;
        }
        curr->next = node;
    }

    sched_block_current(BLOCK_REASON_MUTEX);
}

void mutex_unlock(mutex_t *m) {
    if (m->wait_queue != NULL) {
        wait_node_t *node = m->wait_queue;
        m->wait_queue = node->next;
        m->owner = node->process;
        sched_unblock(node->process);
        kfree(node);
    } else {
        m->locked = 0;
        m->owner = NULL;
    }
}

int mutex_trylock(mutex_t *m) {
    if (m->locked == 0) {
        m->locked = 1;
        m->owner = current_process;
        return 1;
    }
    return 0;
}

void sem_init(sem_t *s, int32_t value) {
    s->count = value;
    s->wait_queue = NULL;
}

void sem_wait(sem_t *s) {
    s->count--;
    if (s->count < 0) {
        wait_node_t *node = (wait_node_t *)kmalloc(sizeof(wait_node_t));
        node->process = current_process;
        node->next = NULL;

        if (s->wait_queue == NULL) {
            s->wait_queue = node;
        } else {
            wait_node_t *curr = s->wait_queue;
            while (curr->next != NULL) {
                curr = curr->next;
            }
            curr->next = node;
        }

        sched_block_current(BLOCK_REASON_WAIT);
    }
}

void sem_post(sem_t *s) {
    s->count++;
    if (s->count <= 0) {
        if (s->wait_queue != NULL) {
            wait_node_t *node = s->wait_queue;
            s->wait_queue = node->next;
            sched_unblock(node->process);
            kfree(node);
        }
    }
}

int sem_getvalue(sem_t *s) {
    return s->count;
}

void spinlock_init(spinlock_t *lock) {
    lock->locked = 0;
}

void spinlock_lock(spinlock_t *lock) {
    while (xchg(&lock->locked, 1) == 1) {
        asm volatile("pause");
    }
}

void spinlock_unlock(spinlock_t *lock) {
    asm volatile("" : : : "memory");
    lock->locked = 0;
}
