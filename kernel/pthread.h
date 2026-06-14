#ifndef PTHREAD_H
#define PTHREAD_H

#include "stdint.h"
#include "stddef.h"

typedef uint32_t pthread_t;
typedef struct {
    uint32_t flags;
} pthread_attr_t;

typedef struct {
    uint32_t value;
} pthread_mutex_t;
#define PTHREAD_MUTEX_INITIALIZER {0}

typedef struct {
    uint32_t value;
    uint32_t waiters;
} pthread_cond_t;
#define PTHREAD_COND_INITIALIZER {0, 0}

typedef struct {
    uint32_t value;
} pthread_rwlock_t;

typedef uint32_t pthread_key_t;
#define PTHREAD_KEYS_MAX 128

/* Thread creation */
int pthread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*start_routine)(void *), void *arg);
pthread_t pthread_self(void);
int pthread_equal(pthread_t t1, pthread_t t2);
void pthread_exit(void *retval);
int pthread_join(pthread_t thread, void **retval);
int pthread_detach(pthread_t thread);
int pthread_cancel(pthread_t thread);

/* Mutex */
int pthread_mutex_init(pthread_mutex_t *mutex, const void *attr);
int pthread_mutex_destroy(pthread_mutex_t *mutex);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_trylock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);

/* Condition variable */
int pthread_cond_init(pthread_cond_t *cond, const void *attr);
int pthread_cond_destroy(pthread_cond_t *cond);
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
int pthread_cond_signal(pthread_cond_t *cond);
int pthread_cond_broadcast(pthread_cond_t *cond);

/* Read-write lock */
int pthread_rwlock_init(pthread_rwlock_t *rwlock, const void *attr);
int pthread_rwlock_destroy(pthread_rwlock_t *rwlock);
int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock);
int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock);
int pthread_rwlock_unlock(pthread_rwlock_t *rwlock);

/* Thread-specific data (TLS) */
int pthread_key_create(pthread_key_t *key, void (*destructor)(void *));
int pthread_key_delete(pthread_key_t key);
void *pthread_getspecific(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void *value);

/* Scheduling */
int pthread_setpriority(pthread_t thread, int prio);
int pthread_getpriority(pthread_t thread);

#endif
