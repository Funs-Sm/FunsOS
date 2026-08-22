/* fs/eventfd.c — eventfd implementation (FunsOS v0.9)
 *
 * In-kernel notifier counter: write adds, read consumes. When the EFD_SEMAPHORE
 * flag is set, read decrements by 1 instead of zeroing the counter.
 */
#include "eventfd.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"

static eventfd_t g_pool[EVENTFD_MAX_FDS];
static int       g_inuse[EVENTFD_MAX_FDS];
static eventfd_stats_t g_stats;

void eventfd_init(void)
{
    for (int i = 0; i < EVENTFD_MAX_FDS; ++i) {
        g_pool[i].counter = 0;
        g_pool[i].flags   = 0;
        g_pool[i].waiters = 0;
        g_inuse[i] = 0;
    }
    g_stats.created = 0;
    g_stats.closed  = 0;
    g_stats.reads   = 0;
    g_stats.writes  = 0;
    g_stats.wakeups = 0;
    g_stats.semaphore_decrements = 0;
}

int eventfd_create(uint32_t flags)
{
    for (int i = 0; i < EVENTFD_MAX_FDS; ++i) {
        if (!g_inuse[i]) {
            g_pool[i].counter = 0;
            g_pool[i].flags   = flags & EVENTFD_SEMAPHORE;
            g_pool[i].waiters = 0;
            g_inuse[i] = 1;
            g_stats.created++;
            return i;
        }
    }
    return -1;
}

int eventfd_close(int fd)
{
    if (fd < 0 || fd >= EVENTFD_MAX_FDS || !g_inuse[fd]) return -1;
    g_pool[fd].counter = 0;
    g_pool[fd].flags   = 0;
    g_pool[fd].waiters = 0;
    g_inuse[fd] = 0;
    g_stats.closed++;
    return 0;
}

int eventfd_write(int fd, uint64_t val)
{
    if (fd < 0 || fd >= EVENTFD_MAX_FDS || !g_inuse[fd]) return -1;
    g_pool[fd].counter += val;
    if (g_pool[fd].waiters > 0) {
        g_stats.wakeups++;
        g_pool[fd].waiters--;
    }
    g_stats.writes++;
    return 0;
}

int eventfd_read(int fd, uint64_t *out)
{
    if (fd < 0 || fd >= EVENTFD_MAX_FDS || !g_inuse[fd]) return -1;
    if (out == NULL) return -1;
    if (g_pool[fd].counter == 0) return -1; /* EAGAIN-style */
    if (g_pool[fd].flags & EVENTFD_SEMAPHORE) {
        *out = 1;
        g_pool[fd].counter -= 1;
        g_stats.semaphore_decrements++;
    } else {
        *out = g_pool[fd].counter;
        g_pool[fd].counter = 0;
    }
    g_stats.reads++;
    return 0;
}

int eventfd_get_counter(int fd, uint64_t *out)
{
    if (fd < 0 || fd >= EVENTFD_MAX_FDS || !g_inuse[fd]) return -1;
    if (out == NULL) return -1;
    *out = g_pool[fd].counter;
    return 0;
}

void eventfd_get_stats(eventfd_stats_t *out)
{
    if (out == NULL) return;
    *out = g_stats;
}

void eventfd_reset_stats(void)
{
    g_stats.created = 0;
    g_stats.closed  = 0;
    g_stats.reads   = 0;
    g_stats.writes  = 0;
    g_stats.wakeups = 0;
    g_stats.semaphore_decrements = 0;
}
