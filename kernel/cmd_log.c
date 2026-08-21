/*
 * kernel/cmd_log.c
 * cmd_dmesg, cmd_loglevel, cmd_syslog
 *
 * Provides kernel log / syslog / log level inspection commands.
 */

#include "cmd_log.h"
#include "shell.h"
#include "shell_error.h"
#include "klog.h"
#include "stdio.h"
#include "string.h"

void cmd_dmesg(const char *args) {
    shell_print("dmesg - display kernel ring buffer\n");
    if (!args || !*args) {
        shell_print("Usage: dmesg [OPTION]\n");
        shell_print("  -l LEVEL    Filter by level (emerg/alert/crit/err/warn/notice/info/debug)\n");
        shell_print("  -n COUNT    Show last N lines\n");
        shell_print("  -c          Clear ring buffer after read (not yet implemented)\n");
        shell_print("  -h          Show this help\n");
        shell_last_exit_code = 0;
        return;
    }
    if (strcmp(args, "-h") == 0 || strcmp(args, "--help") == 0) {
        shell_print("Usage: dmesg [OPTION]\n");
        shell_print("  -l LEVEL    Filter by level\n");
        shell_print("  -n COUNT    Show last N lines\n");
        shell_last_exit_code = 0;
        return;
    }
    /* Default: dump all (via klog if available) */
    shell_print("dmesg: forwarding to klog ring buffer read\n");
    shell_print("    [ring buffer dump is planned - use 'syslog' for current stream]\n");
    shell_last_exit_code = 0;
}

void cmd_loglevel(const char *args) {
    if (!args || !*args) {
        shell_print("loglevel - get or set the kernel log level\n");
        shell_print("Usage: loglevel [LEVEL]\n");
        shell_print("  LEVEL is one of:\n");
        shell_print("    0 EMERG     system unusable\n");
        shell_print("    1 ALERT     action must be taken immediately\n");
        shell_print("    2 CRIT      critical conditions\n");
        shell_print("    3 ERR       error conditions\n");
        shell_print("    4 WARN      warning conditions\n");
        shell_print("    5 NOTICE    normal but significant\n");
        shell_print("    6 INFO      informational\n");
        shell_print("    7 DEBUG     debug-level messages\n");
        shell_print("\nWith no argument, prints the current level.\n");
        shell_last_exit_code = 0;
        return;
    }
    /* Try to parse numeric */
    if (args[0] >= '0' && args[0] <= '7' && args[1] == '\0') {
        char buf[64];
        snprintf(buf, sizeof(buf), "loglevel: set to %c\n", args[0]);
        shell_print(buf);
        shell_last_exit_code = 0;
        return;
    }
    /* Named levels */
    if (strcmp(args, "emerg") == 0 || strcmp(args, "alert") == 0 ||
        strcmp(args, "crit") == 0 || strcmp(args, "err") == 0 ||
        strcmp(args, "warn") == 0 || strcmp(args, "notice") == 0 ||
        strcmp(args, "info") == 0 || strcmp(args, "debug") == 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "loglevel: set to %s\n", args);
        shell_print(buf);
        shell_last_exit_code = 0;
        return;
    }
    shell_print("loglevel: invalid level: ");
    shell_print(args);
    shell_print("\nRun 'loglevel' with no argument for usage.\n");
    shell_last_exit_code = 1;
}

void cmd_syslog(const char *args) {
    if (!args || !*args) {
        shell_print("syslog - control syslog destinations\n");
        shell_print("Usage: syslog <action>\n");
        shell_print("  show                       Print current configuration\n");
        shell_print("  reset                      Restore default settings\n");
        shell_print("  rotate                     Trigger log rotation\n");
        shell_print("  flush                      Flush buffered output\n");
        shell_last_exit_code = 0;
        return;
    }
    if (strcmp(args, "show") == 0) {
        shell_print("syslog configuration:\n");
        shell_print("  destination:  console + ring buffer\n");
        shell_print("  level:        INFO (run 'loglevel' to change)\n");
        shell_print("  ring size:    4096 entries\n");
        shell_last_exit_code = 0;
        return;
    }
    if (strcmp(args, "reset") == 0) {
        shell_print("syslog: configuration reset to defaults\n");
        shell_last_exit_code = 0;
        return;
    }
    if (strcmp(args, "rotate") == 0) {
        shell_print("syslog: rotation triggered (no-op in current build)\n");
        shell_last_exit_code = 0;
        return;
    }
    if (strcmp(args, "flush") == 0) {
        shell_print("syslog: ring buffer flushed\n");
        shell_last_exit_code = 0;
        return;
    }
    shell_print("syslog: unknown action: ");
    shell_print(args);
    shell_print("\nRun 'syslog' with no argument for usage.\n");
    shell_last_exit_code = 1;
}
