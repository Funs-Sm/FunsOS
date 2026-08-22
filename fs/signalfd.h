/* fs/signalfd.h — signalfd-style kernel object (FunsOS v0.9) */
#ifndef FUNSOS_SIGNALFD_H
#define FUNSOS_SIGNALFD_H

#include <stdint.h>

typedef struct signalfd {
    uint32_t mask;          /* signals to forward */
    uint32_t pending;       /* bitmask of pending signals */
    uint32_t last_signo;    /* last signal number popped */
    uint32_t waiters;
} signalfd_t;

#define SIGNALFD_MAX_FDS         32
#define SIGNALFD_MAX_SIGNO       31

void   signalfd_init(void);
int    signalfd_create(uint32_t mask);
int    signalfd_signal(int signo);
int    signalfd_read(int fd, uint32_t *out_signo);
int    signalfd_close(int fd);
int    signalfd_set_mask(int fd, uint32_t mask);

typedef struct signalfd_stats {
    uint64_t created;
    uint64_t closed;
    uint64_t signals_delivered;
    uint64_t signals_dropped_mask;
    uint64_t reads;
    uint64_t wakeups;
} signalfd_stats_t;

void signalfd_get_stats(signalfd_stats_t *out);
void signalfd_reset_stats(void);

#endif /* FUNSOS_SIGNALFD_H */
