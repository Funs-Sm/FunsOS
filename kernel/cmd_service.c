/*
 * kernel/cmd_service.c - PR-N (service command modularization)
 * 服务管理命令实现
 */

#include "cmd_service.h"
#include "shell.h"
#include "svcmgr.h"
#include "kheap.h"
#include "stddef.h"
#include "stdlib.h"
#include "stdio.h"
#include "string.h"

void cmd_service(const char *subcmd, const char *arg1, const char *arg2) {
    if (!subcmd || !*subcmd || strcmp(subcmd, "help") == 0) {
        shell_print("Usage: service <subcommand> [args]\n");
        shell_print("  list                          List all services\n");
        shell_print("  register <name> <cmd> [desc]  Register a service\n");
        shell_print("  unregister <name>            Unregister a service\n");
        shell_print("  start <name>                 Start a service\n");
        shell_print("  stop <name>                  Stop a service\n");
        shell_print("  restart <name>               Restart a service\n");
        shell_print("  status <name>                 Show service status\n");
        shell_print("  enable/disable <name>        Enable/disable a service\n");
        shell_print("  autostart <name> <0|1>       Set autostart flag\n");
        shell_print("  policy <name> <never|on_failure|always>\n");
        shell_print("  history                      Show start/stop history\n");
        shell_print("  stats                        Show service statistics\n");
        shell_print("  startall/stopall             Start/stop all\n");
        shell_last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "list") == 0) {
        svcmgr_service_t *list = (svcmgr_service_t *)kmalloc(
            sizeof(svcmgr_service_t) * SVCMGR_MAX_SERVICES);
        if (!list) {
            shell_print("service: out of memory\n");
            shell_last_exit_code = 1;
            return;
        }
        uint32_t n = svcmgr_list(list, SVCMGR_MAX_SERVICES);
        shell_print("NAME             STATE      POLICY       AUTOSTART  RESTARTS  CMD\n");
        shell_print("---------------- ---------- ------------ --------- --------  ----------------\n");
        for (uint32_t i = 0; i < n; i++) {
            char line[512];
            snprintf(line, sizeof(line), "%-16s %-10s %-12s %-9s %-8u  %s\n",
                     list[i].name,
                     svcmgr_state_name(list[i].state),
                     svcmgr_restart_policy_name(list[i].restart_policy),
                     list[i].autostart ? "yes" : "no",
                     list[i].restart_count,
                     list[i].command);
            shell_print(line);
        }
        char summary[64];
        snprintf(summary, sizeof(summary), "Total: %u service(s)\n", n);
        shell_print(summary);
        kfree(list);
        shell_last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "register") == 0) {
        /* arg1=name, arg2=command (rest of line after name) */
        if (!arg1 || !*arg1 || !arg2 || !*arg2) {
            shell_print("Usage: service register <name> <command> [description...]\n");
            shell_last_exit_code = 1;
            return;
        }
        /* arg2 ???+?????? token ? command???? description */
        char cmd_buf[SVCMGR_MAX_CMD];
        strncpy(cmd_buf, arg2, sizeof(cmd_buf) - 1);
        cmd_buf[sizeof(cmd_buf) - 1] = '\0';
        /* ???? token ?? command?????? description */
        char *cmd = cmd_buf;
        char *desc = NULL;
        char *p = cmd_buf;
        while (*p && *p != ' ') p++;
        if (*p) {
            *p = '\0';
            p++;
            while (*p == ' ') p++;
            if (*p) desc = p;
        }
        int rc = svcmgr_register(arg1, desc ? desc : "", cmd, 0, SVCMGR_RESTART_NEVER);
        if (rc == 0) {
            shell_print("Service registered\n");
            shell_last_exit_code = 0;
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "Failed to register service (rc=%d)\n", rc);
            shell_print(buf);
            shell_last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "unregister") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: service unregister <name>\n");
            shell_last_exit_code = 1;
            return;
        }
        int rc = svcmgr_unregister(arg1);
        if (rc == 0) {
            shell_print("Service unregistered\n");
            shell_last_exit_code = 0;
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "Failed (rc=%d). Service may not exist or is running.\n", rc);
            shell_print(buf);
            shell_last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "start") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: service start <name>\n");
            shell_last_exit_code = 1;
            return;
        }
        int rc = svcmgr_start(arg1);
        if (rc == 0) {
            shell_print("Service started\n");
            shell_last_exit_code = 0;
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "Failed to start service (rc=%d)\n", rc);
            shell_print(buf);
            shell_last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "stop") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: service stop <name>\n");
            shell_last_exit_code = 1;
            return;
        }
        int rc = svcmgr_stop(arg1);
        if (rc == 0) {
            shell_print("Service stopped\n");
            shell_last_exit_code = 0;
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "Failed to stop service (rc=%d)\n", rc);
            shell_print(buf);
            shell_last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "restart") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: service restart <name>\n");
            shell_last_exit_code = 1;
            return;
        }
        int rc = svcmgr_restart(arg1);
        if (rc == 0) {
            shell_print("Service restarted\n");
            shell_last_exit_code = 0;
        } else {
            char buf[64];
            snprintf(buf, sizeof(buf), "Failed to restart service (rc=%d)\n", rc);
            shell_print(buf);
            shell_last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "status") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: service status <name>\n");
            shell_last_exit_code = 1;
            return;
        }
        svcmgr_service_t *s = svcmgr_find(arg1);
        if (!s) {
            shell_print("Service not found\n");
            shell_last_exit_code = 1;
            return;
        }
        char line[256];
        shell_print("Service details:\n");
        snprintf(line, sizeof(line), "  Name:          %s\n", s->name);
        shell_print(line);
        snprintf(line, sizeof(line), "  Description:   %s\n", s->description);
        shell_print(line);
        snprintf(line, sizeof(line), "  Command:       %s\n", s->command);
        shell_print(line);
        snprintf(line, sizeof(line), "  State:         %s\n", svcmgr_state_name(s->state));
        shell_print(line);
        snprintf(line, sizeof(line), "  Restart policy: %s\n",
                 svcmgr_restart_policy_name(s->restart_policy));
        shell_print(line);
        snprintf(line, sizeof(line), "  Autostart:     %s\n", s->autostart ? "yes" : "no");
        shell_print(line);
        snprintf(line, sizeof(line), "  Enabled:       %s\n", s->enabled ? "yes" : "no");
        shell_print(line);
        snprintf(line, sizeof(line), "  Start tick:    %u\n", s->start_tick);
        shell_print(line);
        snprintf(line, sizeof(line), "  Stop tick:     %u\n", s->stop_tick);
        shell_print(line);
        snprintf(line, sizeof(line), "  Restart count: %u\n", s->restart_count);
        shell_print(line);
        snprintf(line, sizeof(line), "  Failure count:  %u\n", s->failure_count);
        shell_print(line);
        snprintf(line, sizeof(line), "  Run count:     %u\n", s->run_count);
        shell_print(line);
        snprintf(line, sizeof(line), "  Last exit:     %d\n", s->last_exit_code);
        shell_print(line);
        snprintf(line, sizeof(line), "  Dependencies:  %u\n", s->dep_count);
        shell_print(line);
        for (uint8_t i = 0; i < s->dep_count; i++) {
            snprintf(line, sizeof(line), "    - %s\n", s->dependencies[i]);
            shell_print(line);
        }
        shell_last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "enable") == 0 || strcmp(subcmd, "disable") == 0) {
        if (!arg1 || !*arg1) {
            shell_print("Usage: service enable|disable <name>\n");
            shell_last_exit_code = 1;
            return;
        }
        int rc = svcmgr_set_enabled(arg1, subcmd[0] == 'e');
        if (rc == 0) {
            shell_print(subcmd[0] == 'e' ? "Service enabled\n" : "Service disabled\n");
            shell_last_exit_code = 0;
        } else {
            shell_print("Failed\n");
            shell_last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "autostart") == 0) {
        if (!arg1 || !*arg1 || !arg2 || !*arg2) {
            shell_print("Usage: service autostart <name> <0|1>\n");
            shell_last_exit_code = 1;
            return;
        }
        int v = atoi(arg2);
        int rc = svcmgr_set_autostart(arg1, v ? 1 : 0);
        if (rc == 0) {
            shell_print("Autostart updated\n");
            shell_last_exit_code = 0;
        } else {
            shell_print("Failed\n");
            shell_last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "policy") == 0) {
        if (!arg1 || !*arg1 || !arg2 || !*arg2) {
            shell_print("Usage: service policy <name> <never|on_failure|always>\n");
            shell_last_exit_code = 1;
            return;
        }
        svcmgr_restart_policy_t p;
        if (strcmp(arg2, "never") == 0) p = SVCMGR_RESTART_NEVER;
        else if (strcmp(arg2, "on_failure") == 0) p = SVCMGR_RESTART_ON_FAILURE;
        else if (strcmp(arg2, "always") == 0) p = SVCMGR_RESTART_ALWAYS;
        else {
            shell_print("Invalid policy. Use: never | on_failure | always\n");
            shell_last_exit_code = 1;
            return;
        }
        int rc = svcmgr_set_restart_policy(arg1, p);
        if (rc == 0) {
            shell_print("Restart policy updated\n");
            shell_last_exit_code = 0;
        } else {
            shell_print("Failed\n");
            shell_last_exit_code = 1;
        }
        return;
    }

    if (strcmp(subcmd, "history") == 0) {
        svcmgr_history_t *hist = (svcmgr_history_t *)kmalloc(
            sizeof(svcmgr_history_t) * SVCMGR_HISTORY_SIZE);
        if (!hist) {
            shell_print("service: out of memory\n");
            shell_last_exit_code = 1;
            return;
        }
        uint32_t n = svcmgr_history_list(hist, SVCMGR_HISTORY_SIZE);
        shell_print("SERVICE          START     STOP      EXIT  STATE\n");
        shell_print("---------------- --------- --------- ----- --------\n");
        for (uint32_t i = 0; i < n; i++) {
            char line[256];
            snprintf(line, sizeof(line), "%-16s %-9u %-9u %-5d %s\n",
                     hist[i].service_name,
                     hist[i].start_tick,
                     hist[i].stop_tick,
                     hist[i].exit_code,
                     svcmgr_state_name(hist[i].state));
            shell_print(line);
        }
        char summary[64];
        snprintf(summary, sizeof(summary), "Total: %u record(s)\n", n);
        shell_print(summary);
        kfree(hist);
        shell_last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "stats") == 0) {
        svcmgr_stats_t s;
        svcmgr_get_stats(&s);
        char line[128];
        shell_print("Service Statistics:\n");
        snprintf(line, sizeof(line), "  Total services: %u\n", s.total_services);
        shell_print(line);
        snprintf(line, sizeof(line), "  Running:         %u\n", s.running);
        shell_print(line);
        snprintf(line, sizeof(line), "  Stopped:         %u\n", s.stopped);
        shell_print(line);
        snprintf(line, sizeof(line), "  Failed:          %u\n", s.failed);
        shell_print(line);
        snprintf(line, sizeof(line), "  Total starts:    %u\n", s.total_starts);
        shell_print(line);
        snprintf(line, sizeof(line), "  Total stops:     %u\n", s.total_stops);
        shell_print(line);
        snprintf(line, sizeof(line), "  Total restarts:  %u\n", s.total_restarts);
        shell_print(line);
        snprintf(line, sizeof(line), "  Total failures:  %u\n", s.total_failures);
        shell_print(line);
        shell_last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "startall") == 0) {
        int n = svcmgr_start_all();
        char buf[64];
        snprintf(buf, sizeof(buf), "Started %d autostart service(s)\n", n);
        shell_print(buf);
        shell_last_exit_code = 0;
        return;
    }

    if (strcmp(subcmd, "stopall") == 0) {
        int n = svcmgr_stop_all();
        char buf[64];
        snprintf(buf, sizeof(buf), "Stopped %d service(s)\n", n);
        shell_print(buf);
        shell_last_exit_code = 0;
        return;
    }

    shell_print("Unknown subcommand. Try 'service help'\n");
    shell_last_exit_code = 1;
}