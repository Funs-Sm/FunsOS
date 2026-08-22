/* lib/tinyevloop.h — minimal poll-set helper (FunsOS v0.9)
 *
 * Inspired by epoll but kernel-side: it tracks which "fds" (eventfd/timerfd/
 * signalfd indices) are registered with what events, and runs a pass that
 * calls callbacks for fds whose underlying object reports "ready".
 */
#ifndef FUNSOS_TINYEVLOOP_H
#define FUNSOS_TINYEVLOOP_H

#include <stdint.h>

#define TINYEV_MAX_FDS    32

/* Event bits.  A subscriber ORs the events it cares about. */
#define TINYEV_READ       0x01u
#define TINYEV_WRITE      0x02u
#define TINYEV_EDGE       0x04u

typedef int (*tinyev_ready_fn)(int fd, uint32_t events, void *userdata);

typedef struct tinyev {
    int            fds[TINYEV_MAX_FDS];
    uint32_t       events[TINYEV_MAX_FDS];
    void          *userdata[TINYEV_MAX_FDS];
    tinyev_ready_fn cb[TINYEV_MAX_FDS];
    int            count;
} tinyev_t;

void tinyev_init(tinyev_t *e);
int  tinyev_add(tinyev_t *e, int fd, uint32_t events, tinyev_ready_fn cb, void *userdata);
int  tinyev_del(tinyev_t *e, int fd);
int  tinyev_one_shot(tinyev_t *e, int (*is_ready)(int fd), int max_iter);

typedef struct tinyev_stats {
    uint64_t adds;
    uint64_t dels;
    uint64_t dispatches;
    uint64_t ready_hits;
    uint64_t ready_misses;
} tinyev_stats_t;

void tinyev_get_stats(tinyev_stats_t *out);
void tinyev_reset_stats(void);

#endif /* FUNSOS_TINYEVLOOP_H */
