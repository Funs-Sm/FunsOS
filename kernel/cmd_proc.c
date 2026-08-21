/*
 * kernel/cmd_proc.c - FunsOS Shell 进程命令模块 (PR-3 iter3)
 */

#include "shell.h"
#include "shell_error.h"
#include "cmd_proc.h"
#include "process.h"
#include "sched.h"
#include "pmm.h"
#include "timer.h"
#include "stddef.h"
#include "stdio.h"

extern uint32_t timer_get_ticks(void);

/* cmd_ps - 列出所有进程 */
void cmd_ps(void) {
    shell_print("  PID  STATE       NAME\n");
    for (int i = 0; i < MAX_PROCESSES; i++) {
        pcb_t *p = process_get_pcb(i);
        if (p && p->state != PROCESS_UNUSED) {
            const char *state_str;
            switch (p->state) {
                case PROCESS_READY:   state_str = "READY"; break;
                case PROCESS_RUNNING: state_str = "RUNNING"; break;
                case PROCESS_BLOCKED: state_str = "BLOCKED"; break;
                case PROCESS_ZOMBIE:  state_str = "ZOMBIE"; break;
                default:              state_str = "UNKNOWN"; break;
            }
            char buf[128];
            snprintf(buf, sizeof(buf), "  %3d  %-10s  %s\n", p->pid, state_str, p->name);
            shell_print(buf);
        }
    }
    shell_last_exit_code = 0;
}

/* cmd_kill - 杀进程 */
void cmd_kill(const char *pid_str) {
    if (!pid_str || !*pid_str) {
        shell_err_kill(0);
        shell_last_exit_code = 1;
        return;
    }
    int pid = 0;
    const char *p = pid_str;
    while (*p >= '0' && *p <= '9') {
        pid = pid * 10 + (*p - '0');
        p++;
    }
    pcb_t *proc = process_get_pcb(pid);
    if (!proc || proc->state == PROCESS_UNUSED) {
        shell_err_kill(pid_str);
        shell_last_exit_code = 1;
        return;
    }
    proc->state = PROCESS_ZOMBIE;
    proc->exit_status = 9;
    char buf[64];
    snprintf(buf, sizeof(buf), "Killed process %d\n", pid);
    shell_print(buf);
    shell_last_exit_code = 0;
}

/* cmd_top - 系统资源 */
void cmd_top(void) {
    uint32_t total = pmm_get_total_pages() * 4096;
    uint32_t used = pmm_get_used_pages() * 4096;
    uint32_t uptime = timer_get_ticks() / 100;
    uint32_t cpu_pct = 0;
    pcb_t *cur = sched_get_current();
    if (cur) {
        cpu_pct = (cur->cpu_time * 100) / (uptime > 0 ? uptime : 1);
    }

    char buf[128];
    shell_print("System Resource Usage:\n");
    snprintf(buf, sizeof(buf), "  Uptime:     %u seconds\n", uptime);
    shell_print(buf);
    snprintf(buf, sizeof(buf), "  CPU usage:  %u%%\n", cpu_pct);
    shell_print(buf);
    snprintf(buf, sizeof(buf), "  Memory:     %u KB / %u KB (%u%%)\n",
             used / 1024, total / 1024,
             total > 0 ? (used * 100) / total : 0);
    shell_print(buf);

    int proc_count = 0, running = 0, blocked = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        pcb_t *p = process_get_pcb(i);
        if (p && p->state != PROCESS_UNUSED) {
            proc_count++;
            if (p->state == PROCESS_RUNNING || p->state == PROCESS_READY) running++;
            if (p->state == PROCESS_BLOCKED) blocked++;
        }
    }
    snprintf(buf, sizeof(buf), "  Processes:  %d total, %d running, %d blocked\n",
             proc_count, running, blocked);
    shell_print(buf);
    shell_last_exit_code = 0;
}

/* Module probe */
void cmd_proc_module_init(void) { (void)0; }