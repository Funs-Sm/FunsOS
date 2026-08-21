/*
 * kernel/cmd_crontab.c - PR-N (crontab command modularization)
 * 定时任务命令实现
 */

#include "cmd_crontab.h"
#include "shell.h"
#include "cron.h"
#include "kheap.h"
#include "stddef.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

/* ===== cron helper functions (from shell.c) ===== */

const char *cron_type_name(cron_type_t t) {
    switch (t) {
        case CRON_TYPE_ONCE:     return "ONCE";
        case CRON_TYPE_INTERVAL: return "INTERVAL";
        case CRON_TYPE_DAILY:    return "DAILY";
        default:                 return "?";
    }
}

const char *cron_state_name(cron_state_t s) {
    switch (s) {
        case CRON_STATE_DISABLED: return "OFF";
        case CRON_STATE_ENABLED:  return "ON";
        case CRON_STATE_RUNNING:  return "RUN";
        case CRON_STATE_ERROR:    return "ERR";
        default:                  return "?";
    }
}

/* ===== cmd_crontab implementation ===== */

void cmd_crontab(const char *subcmd, const char *arg1, const char *arg2) {
    if (!subcmd || !*subcmd || strcmp(subcmd, "help") == 0) {
        shell_print("Usage: crontab <subcommand> [args]\n");
        shell_print("  list                      List all scheduled jobs\n");
        shell_print("  add <type> <interval> <cmd> Add a job (type=once/interval/daily)\n");
        shell_print("  run <id>                  Run job immediately\n");
        shell_print("  enable/disable <id>       Enable or disable a job\n");
        shell_print("  remove <id>               Remove a job\n");
        shell_print("  stats                     Show statistics\n");
        shell_print("  reset                     Reset statistics\n");
        shell_last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "list") == 0) {
        cron_job_t jobs[64];
        uint32_t n = cron_list_jobs(jobs, 64);
        if (n == 0) {
            shell_print("No scheduled jobs.\n");
        } else {
            shell_print("ID  Type      State  Interval   Command\n");
            shell_print("--  --------  -----  ---------  -------\n");
            for (uint32_t i = 0; i < n; i++) {
                char line[256];
                snprintf(line, sizeof(line), "%-3u %-8s  %-5s  %9u  %s\n",
                         jobs[i].id,
                         cron_type_name(jobs[i].type),
                         cron_state_name(jobs[i].state),
                         jobs[i].interval_sec,
                         jobs[i].command);
                shell_print(line);
            }
            char buf[32];
            snprintf(buf, sizeof(buf), "Total: %u job(s)\n", n);
            shell_print(buf);
        }
        shell_last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "stats") == 0) {
        cron_stats_t s;
        cron_get_stats(&s);
        char buf[128];
        shell_print("Cron Statistics:\n");
        snprintf(buf, sizeof(buf), "  Total jobs:   %u\n", s.total_jobs);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "  Active:       %u\n", s.enabled_jobs);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "  Total runs:   %u\n", s.total_runs);
        shell_print(buf);
        snprintf(buf, sizeof(buf), "  Total errors: %u\n", s.total_errors);
        shell_print(buf);
        shell_last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "reset") == 0) {
        cron_reset_stats();
        shell_print("Statistics reset.\n");
        shell_last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "run") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: crontab run <id>\n");
            shell_last_exit_code = 1;
            return;
        }
        uint32_t id = (uint32_t)atoi(arg1);
        int rc = cron_run_job_now(id);
        if (rc == 0) {
            shell_print("Job executed.\n");
            shell_last_exit_code = 0;
        } else {
            shell_print("Job not found or execution failed.\n");
            shell_last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "add") == 0) {
        if (!arg1 || !*arg1 || !arg2 || !*arg2) {
            shell_print("Usage: crontab add <type> <interval_ticks> <command>\n");
            shell_last_exit_code = 1;
            return;
        }
        cron_type_t type = CRON_TYPE_ONCE;
        if (strcmp(arg1, "once") == 0) type = CRON_TYPE_ONCE;
        else if (strcmp(arg1, "interval") == 0) type = CRON_TYPE_INTERVAL;
        else if (strcmp(arg1, "daily") == 0) type = CRON_TYPE_DAILY;
        else {
            shell_print("Invalid type. Use: once | interval | daily\n");
            shell_last_exit_code = 1;
            return;
        }
        /* arg2 is interval_ticks, find command in arg3... */
        uint32_t interval = (uint32_t)atoi(arg2);
        const char *cmd = arg2;
        while (*cmd && *cmd != ' ') cmd++;
        while (*cmd == ' ') cmd++;
        if (!*cmd) {
            shell_print("Usage: crontab add <type> <interval> <command>\n");
            shell_last_exit_code = 1;
            return;
        }
        cron_job_t job;
        memset(&job, 0, sizeof(job));
        job.type = type;
        job.interval_sec = interval;
        strncpy(job.command, cmd, sizeof(job.command) - 1);
        uint32_t id = cron_add_job(&job);
        if (id > 0) {
            char buf[64];
            snprintf(buf, sizeof(buf), "Job added with ID %u\n", id);
            shell_print(buf);
            shell_last_exit_code = 0;
        } else {
            shell_print("Failed to add job\n");
            shell_last_exit_code = 1;
        }
        return;
    }

    shell_print("Unknown subcommand. Try 'crontab help'\n");
    shell_last_exit_code = 1;
}
