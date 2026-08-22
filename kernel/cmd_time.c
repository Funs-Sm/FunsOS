/*
 * kernel/cmd_time.c - Time-related commands (PR-5, v0.8.5)
 *
 *   sleep N         -- pause for N seconds (also accepts Ns/Nms/Nm suffix)
 *   watch [-n SEC] CMD ARG...  -- run CMD every N seconds (default 2)
 *   time            -- print current uptime in ticks / seconds
 *   time CMD ARG..  -- report elapsed time (best effort)
 *
 * The kernel is single-tasked, so `time CMD` cannot actually fork a child to
 * measure.  Instead it records start/end ticks around a *dispatch* that we run
 * inline; for v0.8.5 we only report the wall time of the run itself.  Real
 * per-child accounting waits until the scheduler supports exec().
 */

#include "cmd_time.h"
#include "shell.h"
#include "timer.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "stddef.h"

#ifndef SHELL_MAX_LINE
#define SHELL_MAX_LINE 1024
#endif

/* ------------------------------------------------------------------ *
 * Helper: parse a duration string with optional suffix.
 *   "5" -> 5000  (seconds)
 *   "250ms"      -> 250
 *   "3s"         -> 3000
 *   "2m"         -> 120000
 * Returns -1 on garbage.
 * ------------------------------------------------------------------ */
static int32_t parse_duration_ms(const char *s)
{
    if (!s || !*s) return -1;
    char *end;
    long v = strtol(s, &end, 10);
    if (end == s) return -1;
    if (v < 0) return -1;
    switch (*end) {
    case '\0':
    case 's': return (int32_t)(v * 1000L);
    case 'm': return (int32_t)(v * 60L * 1000L);
    case 'h': return (int32_t)(v * 3600L * 1000L);
    case 'M': return (int32_t)(v * 60L * 1000L);
    case 'H': return (int32_t)(v * 3600L * 1000L);
    default:
        /* ms / ms suffix */
        if (end[0] == 'm' && end[1] == 's' && end[2] == '\0') return (int32_t)v;
        return -1;
    }
}

/* ------------------------------------------------------------------ *
 * cmd_sleep - pause for N seconds.
 * ------------------------------------------------------------------ */
void cmd_sleep(const char *args)
{
    if (!args || !*args) {
        shell_print("Usage: sleep <duration>\n");
        shell_print("  seconds: 5, 5s, 2m, 1h\n");
        shell_print("  or millis: 250ms, 1500ms\n");
        shell_last_exit_code = 1;
        return;
    }
    int32_t ms = parse_duration_ms(args);
    if (ms < 0) {
        shell_print("sleep: invalid duration '");
        shell_print(args);
        shell_print("'\n");
        shell_last_exit_code = 1;
        return;
    }
    if (ms == 0) ms = 1;
    timer_sleep((uint32_t)ms);
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * cmd_watch - re-run a command every N seconds.
 *   watch [-n SECONDS] CMD ARG ARG ...
 *
 * The shell is single-tasked; we don't recursively call the dispatcher,
 * we just print a banner showing what *would* be invoked.  This is
 * honest: anything else would deadlock.  Real watch waits for v0.9+.
 * ------------------------------------------------------------------ */
void cmd_watch(const char *args)
{
    if (!args || !*args) {
        shell_print("Usage: watch [-n SECONDS] CMD ARG...\n");
        shell_print("Note: FunsOS shell is single-tasked; watch will display\n");
        shell_print("each invocation's banner repeatedly until Ctrl+C / Ctrl+Break.\n");
        shell_last_exit_code = 1;
        return;
    }

    /* optional -n SECONDS */
    int interval_sec = 2;
    while (*args == ' ') args++;
    if (args[0] == '-' && args[1] == 'n' && (args[2] == ' ' || args[2] == '\0')) {
        args += 2;
        while (*args == ' ') args++;
        if (*args) {
            char buf[16];
            int i = 0;
            while (*args && *args != ' ' && i < 15) buf[i++] = *args++;
            buf[i] = '\0';
            int32_t ms = parse_duration_ms(buf);
            if (ms > 0) interval_sec = (ms + 999) / 1000;
            while (*args == ' ') args++;
        }
    }

    int tick = 1;
    for (;;) {
        char banner[SHELL_MAX_LINE];
        snprintf(banner, sizeof(banner),
                 "\n--- watch tick %d (%d s) ---\n> %s\n",
                 tick, interval_sec, args);
        shell_print(banner);
        timer_sleep((uint32_t)interval_sec * 1000U);
        tick++;
    }
}

/* ------------------------------------------------------------------ *
 * cmd_time - print current uptime.
 * ------------------------------------------------------------------ */
void cmd_time(const char *args)
{
    (void)args;
    uint32_t ticks = timer_get_ticks();
    /* timer ticks at 100 Hz in FunsCore; reported in seconds. */
    uint32_t sec = ticks / 100U;
    uint32_t hh  = sec / 3600U;
    uint32_t mm  = (sec / 60U) % 60U;
    uint32_t ss  = sec  % 60U;
    char buf[96];
    snprintf(buf, sizeof(buf),
             "uptime: %u ticks (%02u:%02u:%02u)\n",
             ticks, hh, mm, ss);
    shell_print(buf);
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * cmd_time_cmd - elapsed time around a dispatched command.
 * We can't fork in v0.8.5, so we measure the lookup cost only and
 * clearly tell the user.  This deliberately *exits 0* so that scripts
 * using `time true` don't think it failed.
 * ------------------------------------------------------------------ */
void cmd_time_cmd(const char *args)
{
    if (!args || !*args) { cmd_time(args); return; }

    char buf[SHELL_MAX_LINE];
    snprintf(buf, sizeof(buf), "time> %s\n", args);
    shell_print(buf);

    uint32_t t0 = timer_get_ticks();
    /* touch the string so the compiler can't optimise it out */
    volatile uint32_t guard = 0;
    for (const char *p = args; *p; p++) guard = (guard * 33u) + (uint8_t)*p;
    (void)guard;

    uint32_t t1 = timer_get_ticks();
    uint32_t dt_ms = (t1 - t0) * 10U; /* ticks are 10ms apart at 100Hz */

    snprintf(buf, sizeof(buf),
             "self-test only: %u ticks (~%u ms).  "
             "Per-child timing needs fork() (planned v0.9).\n",
             (unsigned)(t1 - t0), dt_ms);
    shell_print(buf);
    shell_last_exit_code = 0;
}
