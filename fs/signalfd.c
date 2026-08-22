/* fs/signalfd.c — signalfd implementation (FunsOS v0.9)
 *
 * A signalfd accepts signals on a per-fd mask and lets the owner poll/read
 * them.  We track pending signals as a bitmap of signo 1..SIGNALFD_MAX_SIGNO.
 */
#include "signalfd.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"

static signalfd_t g_pool[SIGNALFD_MAX_FDS];
static int        g_inuse[SIGNALFD_MAX_FDS];
static signalfd_stats_t g_stats;

void signalfd_init(void)
{
    for (int i = 0; i < SIGNALFD_MAX_FDS; ++i) {
        g_pool[i].mask       = 0;
        g_pool[i].pending    = 0;
        g_pool[i].last_signo = 0;
        g_pool[i].waiters    = 0;
        g_inuse[i] = 0;
    }
    g_stats.created              = 0;
    g_stats.closed               = 0;
    g_stats.signals_delivered    = 0;
    g_stats.signals_dropped_mask = 0;
    g_stats.reads                = 0;
    g_stats.wakeups              = 0;
}

int signalfd_create(uint32_t mask)
{
    for (int i = 0; i < SIGNALFD_MAX_FDS; ++i) {
        if (!g_inuse[i]) {
            g_pool[i].mask       = mask & ((1u << (SIGNALFD_MAX_SIGNO + 1)) - 1u);
            g_pool[i].pending    = 0;
            g_pool[i].last_signo = 0;
            g_pool[i].waiters    = 0;
            g_inuse[i] = 1;
            g_stats.created++;
            return i;
        }
    }
    return -1;
}

int signalfd_signal(int signo)
{
    if (signo <= 0 || signo > SIGNALFD_MAX_SIGNO) return -1;
    for (int i = 0; i < SIGNALFD_MAX_FDS; ++i) {
        if (!g_inuse[i]) continue;
        uint32_t bit = 1u << signo;
        if (!(g_pool[i].mask & bit)) {
            g_stats.signals_dropped_mask++;
            continue;
        }
        g_pool[i].pending |= bit;
        g_stats.signals_delivered++;
        if (g_pool[i].waiters > 0) {
            g_pool[i].waiters--;
            g_stats.wakeups++;
        }
    }
    return 0;
}

int signalfd_read(int fd, uint32_t *out_signo)
{
    if (fd < 0 || fd >= SIGNALFD_MAX_FDS || !g_inuse[fd]) return -1;
    if (out_signo == NULL) return -1;
    if (g_pool[fd].pending == 0) return -1;
    /* find lowest set bit */
    uint32_t p = g_pool[fd].pending;
    int signo = 1;
    while (signo <= SIGNALFD_MAX_SIGNO) {
        if (p & (1u << signo)) {
            g_pool[fd].pending &= ~(1u << signo);
            g_pool[fd].last_signo = (uint32_t)signo;
            *out_signo = (uint32_t)signo;
            g_stats.reads++;
            return 0;
        }
        signo++;
    }
    return -1;
}

int signalfd_set_mask(int fd, uint32_t mask)
{
    if (fd < 0 || fd >= SIGNALFD_MAX_FDS || !g_inuse[fd]) return -1;
    g_pool[fd].mask = mask & ((1u << (SIGNALFD_MAX_SIGNO + 1)) - 1u);
    return 0;
}

int signalfd_close(int fd)
{
    if (fd < 0 || fd >= SIGNALFD_MAX_FDS || !g_inuse[fd]) return -1;
    g_pool[fd].mask       = 0;
    g_pool[fd].pending    = 0;
    g_pool[fd].last_signo = 0;
    g_pool[fd].waiters    = 0;
    g_inuse[fd] = 0;
    g_stats.closed++;
    return 0;
}

void signalfd_get_stats(signalfd_stats_t *out)
{
    if (out == NULL) return;
    *out = g_stats;
}

void signalfd_reset_stats(void)
{
    g_stats.created              = 0;
    g_stats.closed               = 0;
    g_stats.signals_delivered    = 0;
    g_stats.signals_dropped_mask = 0;
    g_stats.reads                = 0;
    g_stats.wakeups              = 0;
}
