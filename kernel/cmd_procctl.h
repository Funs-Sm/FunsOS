/*
 * kernel/cmd_procctl.h - Process-control shell commands (PR-5, v0.8.5)
 *
 *   nice [-n N] PID     - adjust a process's scheduling priority
 *   renice -n N PID     - POSIX renice (with -g / -u for group/user)
 *   nohup CMD           - run immune to SIGHUP (best effort, see body)
 *   jobs                - list background jobs (single-tasked: stub)
 *   bg [JOBID]          - resume in background
 *   fg [JOBID]          - resume in foreground
 */
#ifndef _KERNEL_CMD_PROCCTL_H
#define _KERNEL_CMD_PROCCTL_H

void cmd_nice(const char *args);
void cmd_renice(const char *args);
void cmd_nohup(const char *args);
void cmd_jobs(const char *args);
void cmd_bg(const char *args);
void cmd_fg(const char *args);

#endif