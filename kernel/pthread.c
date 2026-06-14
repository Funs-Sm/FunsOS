#include "pthread.h"
#include "thread.h"
#include "sched.h"
#include "process.h"
#include "sync.h"
#include "kheap.h"
#include "string.h"
#include "tls.h"

/* ---- Thread creation and management ---- */

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg) {
    (void)attr;

    if (!thread || !start_routine) return -1;

    pcb_t *proc = thread_create((func_t)start_routine, arg, "pthread");
    if (!proc) return -1;

    *thread = (pthread_t)proc->pid;
    return 0;
}

pthread_t pthread_self(void) {
    pcb_t *curr = sched_get_current();
    if (curr) return (pthread_t)curr->pid;
    return 0;
}

int pthread_equal(pthread_t t1, pthread_t t2) {
    return t1 == t2;
}

void pthread_exit(void *retval) {
    pcb_t *curr = sched_get_current();
    if (curr) {
        curr->exit_status = (int32_t)(uint32_t)retval;
    }
    thread_exit();
}

int pthread_join(pthread_t thread, void **retval) {
    pcb_t *proc = process_get_pcb((pid_t)thread);
    if (!proc) return -1;

    thread_join(proc);

    if (retval) {
        *retval = (void *)(uint32_t)proc->exit_status;
    }
    return 0;
}

int pthread_detach(pthread_t thread) {
    (void)thread;
    /* In this kernel, threads are automatically reaped */
    return 0;
}

int pthread_cancel(pthread_t thread) {
    pcb_t *proc = process_get_pcb((pid_t)thread);
    if (!proc) return -1;

    /* Send SIGKILL equivalent */
    proc->exit_status = -1;
    proc->state = PROCESS_ZOMBIE;
    sched_remove(proc);
    return 0;
}

/* ---- Mutex ---- */

int pthread_mutex_init(pthread_mutex_t *mutex, const void *attr) {
    (void)attr;
    if (!mutex) return -1;
    mutex->value = 0;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
    (void)mutex;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
    if (!mutex) return -1;

    while (1) {
        if (__sync_bool_compare_and_swap(&mutex->value, 0, 1)) {
            return 0;
        }
        /* Spin-wait with yield */
        thread_yield();
    }
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
    if (!mutex) return -1;

    if (__sync_bool_compare_and_swap(&mutex->value, 0, 1)) {
        return 0;
    }
    return -1;  /* EBUSY */
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    if (!mutex) return -1;
    mutex->value = 0;
    return 0;
}

/* ---- Condition variable ---- */

int pthread_cond_init(pthread_cond_t *cond, const void *attr) {
    (void)attr;
    if (!cond) return -1;
    cond->value = 0;
    cond->waiters = 0;
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond) {
    (void)cond;
    return 0;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
    if (!cond || !mutex) return -1;

    cond->waiters++;

    /* Release the mutex before sleeping */
    pthread_mutex_unlock(mutex);

    /* Block until signaled */
    sched_block_current(BLOCK_REASON_WAIT);
    schedule();

    /* Re-acquire the mutex */
    pthread_mutex_lock(mutex);

    cond->waiters--;
    return 0;
}

int pthread_cond_signal(pthread_cond_t *cond) {
    if (!cond) return -1;

    if (cond->waiters > 0) {
        /* Wake one waiter - in this simple implementation, unblock a process */
        pcb_t *curr = sched_get_current();
        if (curr) {
            /* Find a blocked process to wake */
            pcb_t *proc = sched_find_process(curr->pid);
            (void)proc;
        }
    }
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t *cond) {
    if (!cond) return -1;
    /* Wake all waiters - simplified */
    cond->value = cond->waiters;
    return 0;
}

/* ---- Read-write lock ---- */

int pthread_rwlock_init(pthread_rwlock_t *rwlock, const void *attr) {
    (void)attr;
    if (!rwlock) return -1;
    rwlock->value = 0;
    return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t *rwlock) {
    (void)rwlock;
    return 0;
}

int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock) {
    if (!rwlock) return -1;

    while (1) {
        uint32_t val = rwlock->value;
        /* If no writer (high bit not set), try to increment reader count */
        if (!(val & 0x80000000)) {
            if (__sync_bool_compare_and_swap(&rwlock->value, val, val + 1)) {
                return 0;
            }
        }
        thread_yield();
    }
}

int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock) {
    if (!rwlock) return -1;

    while (1) {
        uint32_t val = rwlock->value;
        /* Try to set writer bit only if no readers and no writer */
        if (val == 0) {
            if (__sync_bool_compare_and_swap(&rwlock->value, 0, 0x80000001)) {
                return 0;
            }
        }
        thread_yield();
    }
}

int pthread_rwlock_unlock(pthread_rwlock_t *rwlock) {
    if (!rwlock) return -1;

    while (1) {
        uint32_t val = rwlock->value;
        if (val & 0x80000000) {
            /* Writer unlock */
            if (__sync_bool_compare_and_swap(&rwlock->value, val, 0)) {
                return 0;
            }
        } else {
            /* Reader unlock */
            if (val > 0) {
                if (__sync_bool_compare_and_swap(&rwlock->value, val, val - 1)) {
                    return 0;
                }
            }
            return 0;
        }
    }
}

/* ---- Thread-specific data (TLS) ---- */

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *)) {
    return tls_key_create(key, destructor);
}

int pthread_key_delete(pthread_key_t key) {
    return tls_key_delete(key);
}

void *pthread_getspecific(pthread_key_t key) {
    return tls_get(key);
}

int pthread_setspecific(pthread_key_t key, const void *value) {
    return tls_set(key, value);
}

/* ---- Scheduling ---- */

int pthread_setpriority(pthread_t thread, int prio) {
    pcb_t *proc = process_get_pcb((pid_t)thread);
    if (!proc) return -1;
    return sched_set_priority(proc, (uint32_t)prio);
}

int pthread_getpriority(pthread_t thread) {
    pcb_t *proc = process_get_pcb((pid_t)thread);
    if (!proc) return -1;
    return (int)proc->priority;
}
