/*
 * kernel/cmd_kdebug.c - Kernel-debug shell commands (PR-5, v0.9)
 *
 *   perf        print perf counters (TSC + profile counters)
 *   perf start  begin a recording
 *   perf stop   end a recording
 *   perf reset  clear counters
 *   stacktrace  dump the kernel call stack
 *   ktrace      show / clear / enable / disable kernel trace buffer
 *   tracepoint  on/off a named tracepoint; show stats
 */

#include "cmd_kdebug.h"
#include "shell.h"
#include "perf.h"
#include "stacktrace.h"
#include "ktrace.h"
#include "tracepoint.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "stddef.h"

#ifndef SHELL_MAX_LINE
#define SHELL_MAX_LINE 1024
#endif

static const char *next_token(const char **p)
{
    while (**p == ' ') (*p)++;
    if (**p == '\0') return NULL;
    const char *start = *p;
    while (**p && **p != ' ') (*p)++;
    if (**p) (*p)++;
    return start;
}

/* ------------------------------------------------------------------ *
 * perf
 * ------------------------------------------------------------------ */
void cmd_perf(const char *args)
{
    const char *p = args;
    const char *sub = next_token(&p);
    if (!sub) {
        perf_print_stats();
        shell_last_exit_code = 0;
        return;
    }
    if (strcmp(sub, "start") == 0) {
        perf_start();
        shell_print("perf: start\n");
        shell_last_exit_code = 0;
        return;
    }
    if (strcmp(sub, "stop") == 0) {
        perf_stop();
        shell_print("perf: stop\n");
        shell_last_exit_code = 0;
        return;
    }
    if (strcmp(sub, "reset") == 0) {
        perf_reset();
        shell_print("perf: reset\n");
        shell_last_exit_code = 0;
        return;
    }
    if (strcmp(sub, "tsc") == 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "TSC: %llu\n",
                 (unsigned long long)perf_read_tsc());
        shell_print(buf);
        shell_last_exit_code = 0;
        return;
    }
    shell_print("Usage: perf [start|stop|reset|tsc]\n");
    shell_last_exit_code = 1;
}

/* ------------------------------------------------------------------ *
 * stacktrace
 * ------------------------------------------------------------------ */
void cmd_stacktrace(const char *args)
{
    int max_depth = 16;
    const char *p = args;
    const char *tok = next_token(&p);
    if (tok) {
        int n = (int)strtol(tok, NULL, 10);
        if (n > 0 && n <= 64) max_depth = n;
    }
    stacktrace_t tr[64];
    int got = stacktrace_save(tr, max_depth);
    shell_print("stacktrace:\n");
    stacktrace_print(tr);
    char buf[64];
    snprintf(buf, sizeof(buf), "(captured %d / requested %d frames)\n",
             got, max_depth);
    shell_print(buf);
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * ktrace
 * ------------------------------------------------------------------ */
void cmd_ktrace(const char *args)
{
    const char *p = args;
    const char *sub = next_token(&p);
    if (!sub) {
        ktrace_stats_t st;
        ktrace_get_stats(&st);
        char buf[SHELL_MAX_LINE];
        snprintf(buf, sizeof(buf),
                 "ktrace: enabled=0x%x level=%u total=%llu dropped=%llu "
                 "buf=%u/%u\n",
                 (unsigned)st.enabled_mask, (unsigned)st.min_level,
                 (unsigned long long)st.total_events,
                 (unsigned long long)st.dropped_events,
                 (unsigned)st.events_in_buffer, (unsigned)st.buffer_capacity);
        shell_print(buf);
        ktrace_dump(32);
        shell_last_exit_code = 0;
        return;
    }
    if (strcmp(sub, "clear") == 0) {
        ktrace_clear();
        shell_print("ktrace: cleared\n");
        shell_last_exit_code = 0;
        return;
    }
    if (strcmp(sub, "on") == 0) {
        const char *mask_s = next_token(&p);
        if (!mask_s) {
            shell_print("Usage: ktrace on MASK\n");
            shell_last_exit_code = 1;
            return;
        }
        uint32_t mask = (uint32_t)strtoul(mask_s, NULL, 0);
        ktrace_enable(mask);
        shell_print("ktrace: on\n");
        shell_last_exit_code = 0;
        return;
    }
    if (strcmp(sub, "off") == 0) {
        const char *mask_s = next_token(&p);
        if (!mask_s) {
            shell_print("Usage: ktrace off MASK\n");
            shell_last_exit_code = 1;
            return;
        }
        uint32_t mask = (uint32_t)strtoul(mask_s, NULL, 0);
        ktrace_disable(mask);
        shell_print("ktrace: off\n");
        shell_last_exit_code = 0;
        return;
    }
    shell_print("Usage: ktrace [clear|on MASK|off MASK]\n");
    shell_last_exit_code = 1;
}

/* ------------------------------------------------------------------ *
 * tracepoint
 * ------------------------------------------------------------------ */
void cmd_tracepoint(const char *args)
{
    const char *p = args;
    const char *sub = next_token(&p);
    if (!sub || strcmp(sub, "stats") == 0) {
        tracepoint_print_stats();
        shell_last_exit_code = 0;
        return;
    }
    if (strcmp(sub, "on") == 0 || strcmp(sub, "enable") == 0) {
        const char *name = next_token(&p);
        if (!name) {
            shell_print("Usage: tracepoint on NAME\n");
            shell_last_exit_code = 1;
            return;
        }
        shell_print(tracepoint_enable(name) == 0
                    ? "tracepoint: enabled\n"
                    : "tracepoint: not found\n");
        shell_last_exit_code = 0;
        return;
    }
    if (strcmp(sub, "off") == 0 || strcmp(sub, "disable") == 0) {
        const char *name = next_token(&p);
        if (!name) {
            shell_print("Usage: tracepoint off NAME\n");
            shell_last_exit_code = 1;
            return;
        }
        shell_print(tracepoint_disable(name) == 0
                    ? "tracepoint: disabled\n"
                    : "tracepoint: not found\n");
        shell_last_exit_code = 0;
        return;
    }
    shell_print("Usage: tracepoint [on NAME|off NAME|stats]\n");
    shell_last_exit_code = 1;
}