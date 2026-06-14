#ifndef SYNC_H
#define SYNC_H

#include "kernel_types.h"

typedef struct wait_node {
    pcb_t *process;
    struct wait_node *next;
} wait_node_t;

typedef struct {
    volatile int32_t locked;
    pcb_t *owner;
    struct wait_node *wait_queue;
} mutex_t;

typedef struct {
    volatile int32_t count;
    struct wait_node *wait_queue;
} sem_t;

typedef struct {
    volatile int32_t locked;
} spinlock_t;

void mutex_init(mutex_t *m);
void mutex_lock(mutex_t *m);
void mutex_unlock(mutex_t *m);
int mutex_trylock(mutex_t *m);

void sem_init(sem_t *s, int32_t count);
void sem_wait(sem_t *s);
void sem_post(sem_t *s);
int sem_getvalue(sem_t *s);

void spinlock_init(spinlock_t *sp);
void spinlock_lock(spinlock_t *sp);
void spinlock_unlock(spinlock_t *sp);

#endif
