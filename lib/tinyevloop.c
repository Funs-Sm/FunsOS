/* lib/tinyevloop.c — minimal poll-set helper (FunsOS v0.9)
 *
 * Not a full epoll: tracks up to TINYEV_MAX_FDS (fd, eventmask, callback)
 * tuples and lets a caller invoke tinyev_one_shot() with a caller-provided
 * is_ready() predicate.  Mainly useful for the *fd family of objects in
 * fs/ (eventfd, timerfd, signalfd) which expose a counter that can be
 * inspected cheaply.
 */
#include "tinyevloop.h"
#include "kprintf.h"
#include "string.h"

static tinyev_stats_t g_stats;

void tinyev_init(tinyev_t *e)
{
    if (e == NULL) return;
    for (int i = 0; i < TINYEV_MAX_FDS; ++i) {
        e->fds[i]    = -1;
        e->events[i] = 0;
        e->userdata[i] = NULL;
        e->cb[i]     = NULL;
    }
    e->count = 0;
}

int tinyev_add(tinyev_t *e, int fd, uint32_t events, tinyev_ready_fn cb, void *userdata)
{
    if (e == NULL || fd < 0 || cb == NULL) return -1;
    if (e->count >= TINYEV_MAX_FDS) return -1;
    for (int i = 0; i < TINYEV_MAX_FDS; ++i) {
        if (e->fds[i] == -1) {
            e->fds[i]      = fd;
            e->events[i]   = events;
            e->userdata[i] = userdata;
            e->cb[i]       = cb;
            e->count++;
            g_stats.adds++;
            return 0;
        }
    }
    return -1;
}

int tinyev_del(tinyev_t *e, int fd)
{
    if (e == NULL) return -1;
    for (int i = 0; i < TINYEV_MAX_FDS; ++i) {
        if (e->fds[i] == fd) {
            e->fds[i]      = -1;
            e->events[i]   = 0;
            e->userdata[i] = NULL;
            e->cb[i]       = NULL;
            e->count--;
            g_stats.dels++;
            return 0;
        }
    }
    return -1;
}

int tinyev_one_shot(tinyev_t *e, int (*is_ready)(int fd), int max_iter)
{
    if (e == NULL || is_ready == NULL) return 0;
    int dispatches = 0;
    for (int i = 0; i < TINYEV_MAX_FDS && dispatches < max_iter; ++i) {
        int fd = e->fds[i];
        if (fd < 0) continue;
        if (is_ready(fd)) {
            e->cb[i](fd, e->events[i], e->userdata[i]);
            g_stats.ready_hits++;
            g_stats.dispatches++;
            dispatches++;
        } else {
            g_stats.ready_misses++;
        }
    }
    return dispatches;
}

void tinyev_get_stats(tinyev_stats_t *out)
{
    if (out == NULL) return;
    *out = g_stats;
}

void tinyev_reset_stats(void)
{
    g_stats.adds        = 0;
    g_stats.dels        = 0;
    g_stats.dispatches  = 0;
    g_stats.ready_hits  = 0;
    g_stats.ready_misses= 0;
}
