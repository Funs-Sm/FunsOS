#ifndef SPINLOCK_NEW_H
#define SPINLOCK_NEW_H

#include "stdint.h"
#include "sync.h"

typedef struct {
    volatile uint32_t next_ticket;
    volatile uint32_t now_serving;
} ticket_lock_t;

void ticket_lock_init(ticket_lock_t *lock);
void ticket_lock_acquire(ticket_lock_t *lock);
void ticket_lock_release(ticket_lock_t *lock);
uint32_t spinlock_irq_save(spinlock_t *lock);
void spinlock_irq_restore(spinlock_t *lock, uint32_t flags);

#endif
