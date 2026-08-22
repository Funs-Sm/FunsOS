/* fs/timerfd.h — timerfd-style kernel object (FunsOS v0.9) */
#ifndef FUNSOS_TIMERFD_H
#define FUNSOS_TIMERFD_H

#include <stdint.h>

typedef struct timerfd {
    uint64_t expires_at_ticks;
    uint64_t interval_ticks;
    uint64_t expirations;
    uint32_t clock_id;
    uint32_t active;
    uint32_t waiters;
} timerfd_t;

#define TIMERFD_MAX_FDS          32
#define TIMERFD_CLOCK_MONOTONIC  1
#define TIMERFD_CLOCK_REALTIME   0

void   timerfd_init(void);
int    timerfd_create(uint32_t clock_id);
int    timerfd_settime(int fd, uint64_t initial_ticks, uint64_t interval_ticks);
int    timerfd_gettime(int fd, uint64_t *exp, uint64_t *interval);
int    timerfd_read(int fd, uint64_t *out);
int    timerfd_tick(uint64_t now_ticks);
int    timerfd_close(int fd);

typedef struct timerfd_stats {
    uint64_t created;
    uint64_t closed;
    uint64_t settime_calls;
    uint64_t reads;
    uint64_t expirations;
    uint64_t wakeups;
} timerfd_stats_t;

void timerfd_get_stats(timerfd_stats_t *out);
void timerfd_reset_stats(void);

#endif /* FUNSOS_TIMERFD_H */
