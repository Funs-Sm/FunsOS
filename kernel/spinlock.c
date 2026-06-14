#include "spinlock.h"
#include "sync.h"

void ticket_lock_init(ticket_lock_t *lock) {
    lock->next_ticket = 0;
    lock->now_serving = 0;
}

void ticket_lock_acquire(ticket_lock_t *lock) {
    uint32_t ticket;
    __asm__ volatile(
        "lock xaddl %0, %1"
        : "=r"(ticket), "+m"(lock->next_ticket)
        : "0"(1)
        : "memory"
    );

    while (ticket != lock->now_serving) {
        __asm__ volatile("pause");
    }
}

void ticket_lock_release(ticket_lock_t *lock) {
    __asm__ volatile(
        "lock incl %0"
        : "+m"(lock->now_serving)
        :
        : "memory"
    );
}

uint32_t spinlock_irq_save(spinlock_t *lock) {
    uint32_t flags;
    __asm__ volatile(
        "pushfl\n"
        "popl %0\n"
        "cli"
        : "=r"(flags)
    );
    spinlock_lock(lock);
    return flags;
}

void spinlock_irq_restore(spinlock_t *lock, uint32_t flags) {
    spinlock_unlock(lock);
    if (flags & 0x200) {
        __asm__ volatile("sti");
    }
}
