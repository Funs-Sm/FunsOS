/*
 * kernel/cmd_sysinfo.c - PR-N (sysinfo modularization)
 * 系统信息命令实现
 */

#include "cmd_sysinfo.h"
#include "shell.h"
#include "version.h"
#include "pmm.h"
#include "timer.h"
#include "sched.h"
#include "smp.h"
#include "cpufreq.h"
#include "pkgmgr.h"
#include "user.h"
#include "permission.h"
#include "process.h"
#include "vfs.h"
#include "../drivers/vesa.h"
#include "../net/net.h"
#include "stddef.h"
#include "stdio.h"
#include "string.h"
#include "kheap.h"

/* shell.c 暴露的全局变量 */
extern int vbe_mode_active;
extern mount_t *mount_list;

void print_box_line(char left, char mid, char right, char fill, int w) {
    char buf[128];
    int i = 0;
    buf[i++] = left;
    for (int j = 0; j < w - 2; j++) buf[i++] = fill;
    buf[i++] = right;
    buf[i++] = '\n';
    buf[i] = '\0';
    shell_print(buf);
}

void print_box_row(const char *label, const char *value, int w) {
    char buf[128];
    int label_len = strlen(label);
    int value_len = strlen(value);
    int i = 0;
    buf[i++] = '|';
    buf[i++] = ' ';
    for (int j = 0; j < label_len && j < 20; j++) buf[i++] = label[j];
    for (int j = label_len; j < 20; j++) buf[i++] = ' ';
    buf[i++] = ' ';
    for (int j = 0; j < value_len && j < w - 25; j++) buf[i++] = value[j];
    for (int j = value_len; j < w - 25; j++) buf[i++] = ' ';
    buf[i++] = ' ';
    buf[i++] = '|';
    buf[i++] = '\n';
    buf[i] = '\0';
    shell_print(buf);
}

void print_box_separator(int w) {
    char buf[128];
    int i = 0;
    buf[i++] = '|';
    for (int j = 0; j < w - 2; j++) buf[i++] = '-';
    buf[i++] = '|';
    buf[i++] = '\n';
    buf[i] = '\0';
    shell_print(buf);
}

void print_box_title(const char *title, int w) {
    char buf[128];
    int title_len = strlen(title);
    int pad = (w - 2 - title_len) / 2;
    int i = 0;
    buf[i++] = '|';
    for (int j = 0; j < pad; j++) buf[i++] = ' ';
    for (int j = 0; j < title_len && j < w - 2; j++) buf[i++] = title[j];
    for (int j = 0; j < w - 2 - pad - title_len; j++) buf[i++] = ' ';
    buf[i++] = '|';
    buf[i++] = '\n';
    buf[i] = '\0';
    shell_print(buf);
}

uint32_t count_processes(void) {
    uint32_t count = 0;
    for (pid_t pid = 0; pid < 1024; pid++) {
        if (process_get_pcb(pid) != NULL) {
            count++;
        }
    }
    return count > 0 ? count : 1;
}

void cmd_sysinfo(void) {
    char val[64];
    int box_w = 60;

    uint32_t total_pages = pmm_get_total_pages();
    uint32_t used_pages = pmm_get_used_pages();
    uint32_t free_pages = pmm_get_free_pages();
    uint64_t total_mem = (uint64_t)total_pages * 4096;
    uint64_t used_mem = (uint64_t)used_pages * 4096;
    uint64_t free_mem = (uint64_t)free_pages * 4096;

    uint32_t uptime_sec = timer_get_ticks() / 100;
    uint32_t up_days = uptime_sec / 86400;
    uint32_t up_hours = (uptime_sec % 86400) / 3600;
    uint32_t up_mins = (uptime_sec % 3600) / 60;
    uint32_t up_secs = uptime_sec % 60;

    uint32_t proc_count = count_processes();

    uint32_t cpu_count = smp_get_cpu_count();
    cpufreq_info_t *freq_info = cpufreq_get_info();
    uint32_t cpu_freq = freq_info ? freq_info->current_freq : 0;

    uint32_t pkg_count = pkgmgr_get_count();

    uint32_t net_if_count = net_get_interface_count();

    /* ==== ??? ==== */
    print_box_line('+', '-', '+', '-', box_w);
    print_box_title(OS_STRING " - System Information", box_w);
    print_box_line('+', '-', '+', '-', box_w);

    /* ==== ???? ==== */
    print_box_title(" System", box_w);
    print_box_separator(box_w);
    print_box_row("OS Name:", OS_NAME, box_w);
    print_box_row("OS Version:", KERNEL_VERSION, box_w);
    print_box_row("Kernel:", KERNEL_STRING, box_w);
    snprintf(val, sizeof(val), "%u process(es)", proc_count);
    print_box_row("Processes:", val, box_w);
    snprintf(val, sizeof(val), "%u package(s)", pkg_count);
    print_box_row("Packages:", val, box_w);
    print_box_line('+', '-', '+', '-', box_w);

    /* ==== CPU ?? ==== */
    print_box_title(" CPU", box_w);
    print_box_separator(box_w);
    print_box_row("Architecture:", "x86 (32-bit)", box_w);
    snprintf(val, sizeof(val), "%u core(s)", cpu_count);
    print_box_row("Cores:", val, box_w);
    if (cpu_freq > 0) {
        snprintf(val, sizeof(val), "%u MHz", cpu_freq);
    } else {
        strncpy(val, "N/A", sizeof(val) - 1);
        val[sizeof(val) - 1] = '\0';
    }
    print_box_row("Frequency:", val, box_w);
    print_box_row("Model:", "Intel/AMD Compatible", box_w);
    print_box_line('+', '-', '+', '-', box_w);

    /* ==== ???? ==== */
    print_box_title(" Memory", box_w);
    print_box_separator(box_w);
    if (total_mem >= 1024 * 1024) {
        snprintf(val, sizeof(val), "%u MB", (uint32_t)(total_mem / 1024 / 1024));
    } else {
        snprintf(val, sizeof(val), "%u KB", (uint32_t)(total_mem / 1024));
    }
    print_box_row("Total:", val, box_w);
    if (used_mem >= 1024 * 1024) {
        snprintf(val, sizeof(val), "%u MB", (uint32_t)(used_mem / 1024 / 1024));
    } else {
        snprintf(val, sizeof(val), "%u KB", (uint32_t)(used_mem / 1024));
    }
    print_box_row("Used:", val, box_w);
    if (free_mem >= 1024 * 1024) {
        snprintf(val, sizeof(val), "%u MB", (uint32_t)(free_mem / 1024 / 1024));
    } else {
        snprintf(val, sizeof(val), "%u KB", (uint32_t)(free_mem / 1024));
    }
    print_box_row("Free:", val, box_w);
    snprintf(val, sizeof(val), "%u pages", total_pages);
    print_box_row("Page Count:", val, box_w);
    print_box_line('+', '-', '+', '-', box_w);

    /* ==== ???? ==== */
    print_box_title(" Uptime", box_w);
    print_box_separator(box_w);
    if (up_days > 0) {
        snprintf(val, sizeof(val), "%ud %uh %um %us", up_days, up_hours, up_mins, up_secs);
    } else if (up_hours > 0) {
        snprintf(val, sizeof(val), "%uh %um %us", up_hours, up_mins, up_secs);
    } else if (up_mins > 0) {
        snprintf(val, sizeof(val), "%um %us", up_mins, up_secs);
    } else {
        snprintf(val, sizeof(val), "%us", up_secs);
    }
    print_box_row("Uptime:", val, box_w);
    snprintf(val, sizeof(val), "%u seconds", uptime_sec);
    print_box_row("Total Seconds:", val, box_w);
    print_box_line('+', '-', '+', '-', box_w);

    /* ==== ???? ==== */
    print_box_title(" Filesystem", box_w);
    print_box_separator(box_w);
    {
        extern mount_t *mount_list;
        mount_t *mnt = mount_list;
        int mnt_count = 0;
        while (mnt) {
            mnt_count++;
            mnt = mnt->next;
        }
        snprintf(val, sizeof(val), "%d mount(s)", mnt_count);
        print_box_row("Mount Points:", val, box_w);
        print_box_row("Root FS:", "VFS (multi-fs)", box_w);
        print_box_row("Dev FS:", "/dev (devfs)", box_w);
    }
    print_box_line('+', '-', '+', '-', box_w);

    /* ==== ???? ==== */
    print_box_title(" Network", box_w);
    print_box_separator(box_w);
    snprintf(val, sizeof(val), "%u interface(s)", net_if_count);
    print_box_row("Interfaces:", val, box_w);
    for (uint32_t i = 0; i < net_if_count; i++) {
        net_interface_t *iface = net_get_interface(i);
        if (iface) {
            snprintf(val, sizeof(val), "%s (%s)",
                     iface->name,
                     (iface->flags & IFF_UP) ? "UP" : "DOWN");
            char label[24];
            snprintf(label, sizeof(label), "  eth%u:", i);
            print_box_row(label, val, box_w);
        }
    }
    print_box_line('+', '-', '+', '-', box_w);

    /* ==== ?? ==== */
    print_box_title(" Display", box_w);
    print_box_separator(box_w);
    if (vbe_mode_active) {
        vbe_mode_info_t *vbe = vbe_get_current_mode();
        if (vbe) {
            snprintf(val, sizeof(val), "%ux%ux%u (VBE)",
                     vbe->width, vbe->height, vbe->bpp);
        } else {
            strncpy(val, "VBE mode", sizeof(val) - 1);
            val[sizeof(val) - 1] = '\0';
        }
    } else {
        strncpy(val, "80x25 (VGA text)", sizeof(val) - 1);
        val[sizeof(val) - 1] = '\0';
    }
    print_box_row("Mode:", val, box_w);
    print_box_line('+', '-', '+', '-', box_w);

    /* ==== ??????? ==== */
    print_box_title(" User & Permissions", box_w);
    print_box_separator(box_w);
    {
        user_t *u = user_get_current();
        if (u) {
            print_box_row("Current User:", u->username, box_w);
            snprintf(val, sizeof(val), "%u", u->uid);
            print_box_row("UID:", val, box_w);
            snprintf(val, sizeof(val), "%u", u->gid);
            print_box_row("GID:", val, box_w);
            print_box_row("Role:", perm_level_name(perm_get_level()), box_w);
            snprintf(val, sizeof(val), "%s", u->is_admin ? "Yes" : "No");
            print_box_row("Admin:", val, box_w);
            snprintf(val, sizeof(val), "%s", perm_is_sover() ? "Full" :
                                           perm_is_admin() ? "Elevated" : "Standard");
            print_box_row("Privilege:", val, box_w);
            snprintf(val, sizeof(val), "%u user(s)", user_count());
            print_box_row("Total Users:", val, box_w);
            snprintf(val, sizeof(val), "%u group(s)", group_count());
            print_box_row("Total Groups:", val, box_w);
        } else {
            print_box_row("Current User:", "nobody", box_w);
            print_box_row("UID:", "65534", box_w);
            print_box_row("Role:", "Nobody", box_w);
        }
    }
    print_box_line('+', '-', '+', '-', box_w);

    shell_last_exit_code = 0;
}
