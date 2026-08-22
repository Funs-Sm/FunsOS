/*
 * kernel/cmd_kdebug.h - Kernel-debug shell commands (PR-5, v0.9)
 *
 *   perf                - print performance counters
 *   perf start|stop|reset
 *   stacktrace [N]      - dump the current kernel call stack (N frames)
 *   ktrace              - print last N kernel trace events
 *   ktrace clear        - drop the kernel trace buffer
 *   ktrace on|off MASK  - enable / disable categories
 *   tracepoint [on NAME | off NAME | stats]
 */
#ifndef _KERNEL_CMD_KDEBUG_H
#define _KERNEL_CMD_KDEBUG_H

void cmd_perf(const char *args);
void cmd_stacktrace(const char *args);
void cmd_ktrace(const char *args);
void cmd_tracepoint(const char *args);

#endif