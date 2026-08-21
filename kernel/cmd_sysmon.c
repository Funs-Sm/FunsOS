/*
 * kernel/cmd_sysmon.c
 * cmd_iostat, cmd_vmstat, cmd_mpstat, cmd_pidstat
 *
 * System monitoring commands - report per-CPU, VM, IO, per-process stats.
 */

#include "cmd_sysmon.h"
#include "shell.h"
#include "shell_error.h"
#include "pmm.h"
#include "sched.h"
#include "process.h"
#include "stdio.h"
#include "string.h"

void cmd_iostat(const char *args) {
    if (!args || !*args) {
        shell_print("iostat - report CPU and I/O statistics\n");
        shell_print("Usage: iostat [interval] [count]\n");
        shell_print("  interval   seconds between samples\n");
        shell_print("  count      number of samples to print\n");
        shell_print("  With no argument, prints a single snapshot.\n");
        shell_last_exit_code = 0;
        return;
    }
    shell_print("iostat: interval=");
    shell_print(args);
    shell_print(" (stub - per-CPU counters not yet wired)\n");
    shell_print("  (run 'cpu' for current utilization)\n");
    shell_last_exit_code = 0;
}

void cmd_vmstat(const char *args) {
    if (!args || !*args) {
        shell_print("vmstat - report virtual memory statistics\n");
        shell_print("Usage: vmstat [interval] [count]\n");
        shell_print("  Reports paging, swapping, faults, and memory totals.\n");
        shell_print("  With no argument, prints a single snapshot.\n");
        shell_last_exit_code = 0;
        return;
    }
    shell_print("vmstat: interval=");
    shell_print(args);
    shell_print(" (stub - limited counters wired; see 'meminfo' for current memory)\n");
    shell_last_exit_code = 0;
}

void cmd_mpstat(const char *args) {
    if (!args || !*args) {
        shell_print("mpstat - per-CPU statistics\n");
        shell_print("Usage: mpstat [interval] [count]\n");
        shell_print("  Reports per-CPU time spent in user/system/idle.\n");
        shell_print("  With no argument, prints one snapshot.\n");
        shell_last_exit_code = 0;
        return;
    }
    shell_print("mpstat: interval=");
    shell_print(args);
    shell_print(" (stub - per-CPU affinity not yet wired)\n");
    shell_last_exit_code = 0;
}

void cmd_pidstat(const char *args) {
    if (!args || !*args) {
        shell_print("pidstat - per-process statistics\n");
        shell_print("Usage: pidstat [pid] [interval]\n");
        shell_print("  pid        Target process (default: all)\n");
        shell_print("  interval   Seconds between samples\n");
        shell_print("\n  Use 'top' for an interactive summary.\n");
        shell_last_exit_code = 0;
        return;
    }
    shell_print("pidstat: target='");
    shell_print(args);
    shell_print("' (stub - per-process accounting not yet wired)\n");
    shell_print("  (use 'ps' to list processes today)\n");
    shell_last_exit_code = 0;
}
