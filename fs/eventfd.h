/* fs/eventfd.h — eventfd-style kernel object (FunsOS v0.9) */
#ifndef FUNSOS_EVENTFD_H
#define FUNSOS_EVENTFD_H

#include <stdint.h>
#include <stddef.h>

typedef struct eventfd {
    uint64_t counter;
    uint32_t flags;
    uint32_t waiters;
} eventfd_t;

#define EVENTFD_MAX_FDS          64
#define EVENTFD_SEMAPHORE        0x00000001u

void   eventfd_init(void);
int    eventfd_create(uint32_t flags);
int    eventfd_close(int fd);
int    eventfd_write(int fd, uint64_t val);
int    eventfd_read(int fd, uint64_t *out);
int    eventfd_get_counter(int fd, uint64_t *out);

typedef struct eventfd_stats {
    uint64_t created;
    uint64_t closed;
    uint64_t reads;
    uint64_t writes;
    uint64_t wakeups;
    uint64_t semaphore_decrements;
} eventfd_stats_t;

void eventfd_get_stats(eventfd_stats_t *out);
void eventfd_reset_stats(void);

#endif /* FUNSOS_EVENTFD_H */
