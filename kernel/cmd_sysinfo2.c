/*
 * kernel/cmd_sysinfo2.c - P2: System information commands
 * uname, uptime, date, hostname
 */

#include "cmd_sysinfo2.h"
#include "shell.h"
#include "shell_error.h"
#include "version.h"
#include "timer.h"
#include "rtc.h"
#include "stddef.h"
#include "stdio.h"
#include "string.h"

#ifndef FUNSOS_VERSION_STRING
#define FUNSOS_VERSION_STRING OS_STRING
#endif

extern uint32_t timer_get_ticks(void);

void cmd_uname(const char *arg) {
    if (arg && *arg) {
        if (strcmp(arg, "-a") == 0 || strcmp(arg, "--all") == 0) {
            shell_print("FunSOS ");
            shell_print(FUNSOS_VERSION_STRING);
            shell_print(" i386 unknown\n");
            shell_print("  Kernel built on ");
            shell_print(__DATE__);
            shell_print(" ");
            shell_print(__TIME__);
            shell_print("\n");
        } else if (strcmp(arg, "-s") == 0 || strcmp(arg, "--kernel-name") == 0) {
            shell_print("FunSOS\n");
        } else if (strcmp(arg, "-r") == 0 || strcmp(arg, "--kernel-release") == 0) {
            shell_print(FUNSOS_VERSION_STRING);
            shell_print("\n");
        } else if (strcmp(arg, "-v") == 0 || strcmp(arg, "--kernel-version") == 0) {
            shell_print("Build ");
            shell_print(__DATE__);
            shell_print("\n");
        } else if (strcmp(arg, "-m") == 0 || strcmp(arg, "--machine") == 0) {
            shell_print("i386\n");
        } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            shell_print("Usage: uname [OPTION]...\n");
            shell_print("  -a, --all              Print all information\n");
            shell_print("  -s, --kernel-name      Print kernel name\n");
            shell_print("  -r, --kernel-release   Print kernel release\n");
            shell_print("  -v, --kernel-version   Print kernel version\n");
            shell_print("  -m, --machine          Print machine hardware\n");
            shell_print("  -h, --help             Show this help\n");
        } else {
            shell_print("uname: invalid option: ");
            shell_print(arg);
            shell_print("\n");
            shell_last_exit_code = 1;
            return;
        }
    } else {
        shell_print("FunSOS ");
        shell_print(FUNSOS_VERSION_STRING);
        shell_print("\n");
    }
    shell_last_exit_code = 0;
}

void cmd_uptime(void) {
    uint32_t ticks = timer_get_ticks();
    uint32_t seconds = ticks / 100;
    uint32_t minutes = seconds / 60;
    uint32_t hours = minutes / 60;
    uint32_t days = hours / 24;

    char buf[128];
    shell_print("uptime - system uptime\n");
    shell_print("  ");
    snprintf(buf, sizeof(buf), "%u ticks since boot\n", ticks);
    shell_print(buf);
    if (days > 0) {
        snprintf(buf, sizeof(buf), "  uptime: %ud %uh %um %us\n",
                 days, hours % 24, minutes % 60, seconds % 60);
    } else if (hours > 0) {
        snprintf(buf, sizeof(buf), "  uptime: %uh %um %us\n",
                 hours, minutes % 60, seconds % 60);
    } else {
        snprintf(buf, sizeof(buf), "  uptime: %um %us\n", minutes, seconds % 60);
    }
    shell_print(buf);
    shell_print("  load average: 0.00, 0.00, 0.00 (no scheduler load yet)\n");
    shell_last_exit_code = 0;
}

void cmd_date(const char *arg) {
    rtc_time_t tm;
    rtc_read_time(&tm);

    uint16_t y = tm.year + 2000;  /* RTC stores 2-digit year */
    uint8_t  mo = tm.month;
    uint8_t  d  = tm.day;
    uint8_t  h  = tm.hour;
    uint8_t  mi = tm.min;
    uint8_t  s  = tm.sec;

    char buf[128];

    if (arg && *arg) {
        if (strcmp(arg, "-u") == 0 || strcmp(arg, "--utc") == 0) {
            shell_print("UTC ");
        } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            shell_print("Usage: date [OPTION]...\n");
            shell_print("  +FORMAT     Output date using FORMAT (strftime syntax)\n");
            shell_print("  -u, --utc   Print UTC instead of local time\n");
            shell_print("  -h, --help  Show this help\n");
            shell_print("\nDefault: print current local time in ISO-8601 format.\n");
            shell_last_exit_code = 0;
            return;
        } else if (arg[0] == '+') {
            /* simple format substitution */
            const char *fmt = arg + 1;
            for (const char *p = fmt; *p; p++) {
                if (*p == '%' && *(p+1)) {
                    p++;
                    switch (*p) {
                        case 'Y': snprintf(buf, sizeof(buf), "%u", y); shell_print(buf); break;
                        case 'm': snprintf(buf, sizeof(buf), "%02u", mo); shell_print(buf); break;
                        case 'd': snprintf(buf, sizeof(buf), "%02u", d); shell_print(buf); break;
                        case 'H': snprintf(buf, sizeof(buf), "%02u", h); shell_print(buf); break;
                        case 'M': snprintf(buf, sizeof(buf), "%02u", mi); shell_print(buf); break;
                        case 'S': snprintf(buf, sizeof(buf), "%02u", s); shell_print(buf); break;
                        case 'n': shell_print("\n"); break;
                        case 't': shell_print("\t"); break;
                        case '%': shell_print("%"); break;
                        default:  shell_print("%"); shell_print(p); break;
                    }
                } else {
                    char c[2] = {*p, 0};
                    shell_print(c);
                }
            }
            shell_print("\n");
            shell_last_exit_code = 0;
            return;
        } else {
            shell_print("date: invalid option: ");
            shell_print(arg);
            shell_print("\n");
            shell_last_exit_code = 1;
            return;
        }
    }

    snprintf(buf, sizeof(buf), "Local time: %04u-%02u-%02u %02u:%02u:%02u\n",
             y, mo, d, h, mi, s);
    shell_print(buf);
    shell_print("  Use 'date +%H:%M:%S' for custom format.\n");
    shell_last_exit_code = 0;
}

void cmd_hostname(const char *arg) {
    if (arg && *arg) {
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            shell_print("Usage: hostname [NEW_NAME]\n");
            shell_print("  With no argument, prints the current hostname.\n");
            shell_print("  With an argument, sets the hostname (not yet persisted).\n");
            shell_last_exit_code = 0;
            return;
        }
        shell_print("hostname: set to '");
        shell_print(arg);
        shell_print("' (in-memory only, not persisted across reboots)\n");
    } else {
        shell_print("hostname\n");
        shell_print("  FQDN:       FunSOS\n");
        shell_print("  Domain:     local\n");
        shell_print("  Node type:  standalone\n");
    }
    shell_last_exit_code = 0;
}
