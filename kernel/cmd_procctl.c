/*
 * kernel/cmd_procctl.c - Process-control shell commands (PR-5, v0.9)
 *
 * nice / renice:  backed by sched_set_priority() on the target PCB.
 * nohup / jobs / bg / fg:  FunsOS is single-tasked, so these commands
 *   honestly report that background job control is not yet supported.
 */

#include "cmd_procctl.h"
#include "shell.h"
#include "sched.h"
#include "process.h"
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
 * nice [-n DELTA] [PID]
 *
 * Without PID, we set the *current* shell's effective priority (lower
 * numeric value = lower priority = "nicer").  With PID, we adjust the
 * target PCB if it exists.
 * ------------------------------------------------------------------ */
void cmd_nice(const char *args)
{
    int delta = 10; /* default +10 = "nicer" */
    pid_t pid = -1;
    const char *p = args;
    const char *tok = next_token(&p);
    if (tok && tok[0] == '-' && tok[1] == 'n' && tok[2] == '\0') {
        tok = next_token(&p);
        if (!tok) {
            shell_print("nice: -n requires an argument\n");
            shell_last_exit_code = 1;
            return;
        }
        delta = (int)strtol(tok, NULL, 10);
        tok = next_token(&p);
    }
    if (tok) {
        pid = (pid_t)strtol(tok, NULL, 10);
    } else {
        shell_print("nice: needs a PID target (current shell has no PID yet)\n");
        shell_last_exit_code = 1;
        return;
    }
    pcb_t *proc = process_get_pcb(pid);
    if (!proc) {
        char buf[64];
        snprintf(buf, sizeof(buf), "nice: no such PID %d\n", (int)pid);
        shell_print(buf);
        shell_last_exit_code = 1;
        return;
    }
    uint32_t new_prio = (uint32_t)((int)proc->priority + delta);
    if ((int)new_prio < 0) new_prio = 0;
    if (sched_set_priority(proc, new_prio) != 0) {
        shell_print("nice: scheduler refused priority change\n");
        shell_last_exit_code = 1;
        return;
    }
    char buf[64];
    snprintf(buf, sizeof(buf),
             "nice: pid %d priority -> %u\n", (int)pid, new_prio);
    shell_print(buf);
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * renice -n N PID
 * ------------------------------------------------------------------ */
void cmd_renice(const char *args)
{
    const char *p = args;
    const char *tok = next_token(&p);
    if (!tok || tok[0] != '-' || tok[1] != 'n') {
        shell_print("Usage: renice -n DELTA PID\n");
        shell_last_exit_code = 1;
        return;
    }
    tok = next_token(&p);
    if (!tok) {
        shell_print("renice: missing DELTA\n");
        shell_last_exit_code = 1;
        return;
    }
    int delta = (int)strtol(tok, NULL, 10);
    tok = next_token(&p);
    if (!tok) {
        shell_print("renice: missing PID\n");
        shell_last_exit_code = 1;
        return;
    }
    pid_t pid = (pid_t)strtol(tok, NULL, 10);
    pcb_t *proc = process_get_pcb(pid);
    if (!proc) {
        char buf[64];
        snprintf(buf, sizeof(buf), "renice: no such PID %d\n", (int)pid);
        shell_print(buf);
        shell_last_exit_code = 1;
        return;
    }
    uint32_t new_prio = (uint32_t)((int)proc->priority + delta);
    if ((int)new_prio < 0) new_prio = 0;
    if (sched_set_priority(proc, new_prio) != 0) {
        shell_print("renice: scheduler refused priority change\n");
        shell_last_exit_code = 1;
        return;
    }
    char buf[64];
    snprintf(buf, sizeof(buf),
             "renice: pid %d priority -> %u\n", (int)pid, new_prio);
    shell_print(buf);
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * nohup CMD ARG...
 *
 * We can't fork yet; we tell the user the limitation and exit 0.
 * ------------------------------------------------------------------ */
void cmd_nohup(const char *args)
{
    if (!args || !*args) {
        shell_print("Usage: nohup CMD ARG...\n");
        shell_print("Note: FunsOS shell is single-tasked, so nohup is a no-op.\n");
        shell_last_exit_code = 1;
        return;
    }
    char buf[SHELL_MAX_LINE];
    snprintf(buf, sizeof(buf),
             "nohup: %s (would ignore SIGHUP)\n", args);
    shell_print(buf);
    shell_print("Note: background invocation needs fork()/exec() (planned v0.9).\n");
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * jobs / bg / fg - honest stubs
 * ------------------------------------------------------------------ */
void cmd_jobs(const char *args)
{
    (void)args;
    shell_print("jobs: FunsOS has no job table (single-tasked shell).\n");
    shell_last_exit_code = 0;
}

void cmd_bg(const char *args)
{
    (void)args;
    shell_print("bg: not applicable - single-tasked shell.\n");
    shell_last_exit_code = 1;
}

void cmd_fg(const char *args)
{
    (void)args;
    shell_print("fg: not applicable - single-tasked shell.\n");
    shell_last_exit_code = 1;
}