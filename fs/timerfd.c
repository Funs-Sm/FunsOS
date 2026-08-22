/* fs/timerfd.c — timerfd implementation (FunsOS v0.9)
 *
 * Each timerfd has an absolute expiration and an optional repeating interval.
 * Reads return the number of expirations since the last read and clear that
 * counter.  Time is expressed in "kernel ticks" provided by the caller via
 * timerfd_tick() — typically driven from the timer ISR.
 */
#include "timerfd.h"
#include "../lib/kprintf.h"
#include "../lib/string.h"

static timerfd_t g_pool[TIMERFD_MAX_FDS];
static int       g_inuse[TIMERFD_MAX_FDS];
static timerfd_stats_t g_stats;

void timerfd_init(void)
{
    for (int i = 0; i < TIMERFD_MAX_FDS; ++i) {
        g_pool[i].expires_at_ticks = 0;
        g_pool[i].interval_ticks   = 0;
        g_pool[i].expirations      = 0;
        g_pool[i].clock_id         = TIMERFD_CLOCK_MONOTONIC;
        g_pool[i].active           = 0;
        g_pool[i].waiters          = 0;
        g_inuse[i] = 0;
    }
    g_stats.created      = 0;
    g_stats.closed       = 0;
    g_stats.settime_calls= 0;
    g_stats.reads        = 0;
    g_stats.expirations  = 0;
    g_stats.wakeups      = 0;
}

int timerfd_create(uint32_t clock_id)
{
    for (int i = 0; i < TIMERFD_MAX_FDS; ++i) {
        if (!g_inuse[i]) {
            g_pool[i].expires_at_ticks = 0;
            g_pool[i].interval_ticks   = 0;
            g_pool[i].expirations      = 0;
            g_pool[i].clock_id         = clock_id;
            g_pool[i].active           = 0;
            g_pool[i].waiters          = 0;
            g_inuse[i] = 1;
            g_stats.created++;
            return i;
        }
    }
    return -1;
}

int timerfd_settime(int fd, uint64_t initial_ticks, uint64_t interval_ticks)
{
    if (fd < 0 || fd >= TIMERFD_MAX_FDS || !g_inuse[fd]) return -1;
    /*
     * The caller is expected to pass "now" as 0 + initial relative offset;
     * we approximate "now" by leaving expires_at_ticks to be absolute in
     * caller-supplied units.  A real implementation would resolve "now" via
     * a clock source; here we store the user's value directly and rely on
     * timerfd_tick() to advance the absolute clock.
     */
    g_pool[fd].expires_at_ticks = initial_ticks;
    g_pool[fd].interval_ticks   = interval_ticks;
    g_pool[fd].expirations      = 0;
    g_pool[fd].active           = (initial_ticks != 0) ? 1 : 0;
    g_stats.settime_calls++;
    return 0;
}

int timerfd_gettime(int fd, uint64_t *exp, uint64_t *interval)
{
    if (fd < 0 || fd >= TIMERFD_MAX_FDS || !g_inuse[fd]) return -1;
    if (exp == NULL || interval == NULL) return -1;
    *exp      = g_pool[fd].expires_at_ticks;
    *interval = g_pool[fd].interval_ticks;
    return 0;
}

int timerfd_read(int fd, uint64_t *out)
{
    if (fd < 0 || fd >= TIMERFD_MAX_FDS || !g_inuse[fd]) return -1;
    if (out == NULL) return -1;
    if (g_pool[fd].expirations == 0) return -1; /* EAGAIN */
    *out = g_pool[fd].expirations;
    g_pool[fd].expirations = 0;
    g_stats.reads++;
    return 0;
}

int timerfd_tick(uint64_t now_ticks)
{
    for (int i = 0; i < TIMERFD_MAX_FDS; ++i) {
        if (!g_inuse[i] || !g_pool[i].active) continue;
        if (now_ticks >= g_pool[i].expires_at_ticks) {
            g_pool[i].expirations++;
            g_stats.expirations++;
            if (g_pool[i].waiters > 0) {
                g_pool[i].waiters--;
                g_stats.wakeups++;
            }
            if (g_pool[i].interval_ticks > 0) {
                g_pool[i].expires_at_ticks += g_pool[i].interval_ticks;
            } else {
                g_pool[i].active = 0;
            }
        }
    }
    return 0;
}

int timerfd_close(int fd)
{
    if (fd < 0 || fd >= TIMERFD_MAX_FDS || !g_inuse[fd]) return -1;
    g_pool[fd].expires_at_ticks = 0;
    g_pool[fd].interval_ticks   = 0;
    g_pool[fd].expirations      = 0;
    g_pool[fd].clock_id         = TIMERFD_CLOCK_MONOTONIC;
    g_pool[fd].active           = 0;
    g_pool[fd].waiters          = 0;
    g_inuse[fd] = 0;
    g_stats.closed++;
    return 0;
}

void timerfd_get_stats(timerfd_stats_t *out)
{
    if (out == NULL) return;
    *out = g_stats;
}

void timerfd_reset_stats(void)
{
    g_stats.created      = 0;
    g_stats.closed       = 0;
    g_stats.settime_calls= 0;
    g_stats.reads        = 0;
    g_stats.expirations  = 0;
    g_stats.wakeups      = 0;
}
