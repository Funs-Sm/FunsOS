/*
 * kernel/cmd_mem.c - FunsOS Shell 内存管理命令模块 (PR-4 iter2)
 *
 * �?kernel/shell.c 中拆分出来的内存相关命令实现:
 *   - cmd_free       : 显示内存使用情况 (PR-4 iter2)
 *   - cmd_meminfo    : 详细内存信息 (PR-4 iter3, 计划)
 *
 * 共享状�? shell_last_exit_code (定义�?shell.c, �?shell.h �?extern)
 */

#include "shell.h"
#include "cmd_mem.h"
#include "stdio.h"
#include "pmm.h"

/* ============================================================
 * cmd_free - 显示内存使用情况
 * 迁移�?kernel/shell.c (PR-4 iter2)
 * ============================================================ */
void cmd_free(void) {
    uint32_t total_pages = pmm_get_total_pages();
    uint32_t used_pages = pmm_get_used_pages();
    uint32_t free_pages = pmm_get_free_pages();

    shell_print("free - display memory usage\n");
    shell_print("              total    used    free\n");
    char buf[128];
    snprintf(buf, sizeof(buf), "Mem (KB):  %6u  %6u  %6u\n",
             total_pages * 4, used_pages * 4, free_pages * 4);
    shell_print(buf);
    snprintf(buf, sizeof(buf), "Mem (MB):  %6u  %6u  %6u\n",
             (total_pages * 4) / 1024, (used_pages * 4) / 1024, (free_pages * 4) / 1024);
    shell_print(buf);
    shell_print("\n  'free -h' for human-readable, 'meminfo' for breakdown.\n");
    shell_last_exit_code = 0;
}

/* ============================================================
 * cmd_meminfo - 详细内存信息
 * 迁移�?kernel/shell.c (PR-4 iter3)
 * ============================================================ */
void cmd_meminfo(void) {
    uint32_t total_pages = pmm_get_total_pages();
    uint32_t free_pages = pmm_get_free_pages();
    uint32_t total = total_pages * 4;
    uint32_t free_mem = free_pages * 4;
    uint32_t used = total - free_mem;

    char buf[128];
    shell_print("Memory Information:\n");
    snprintf(buf, sizeof(buf), "  Total:     %u KB (%u MB)\n", total, total / 1024);
    shell_print(buf);
    snprintf(buf, sizeof(buf), "  Used:      %u KB (%u MB)\n", used, used / 1024);
    shell_print(buf);
    snprintf(buf, sizeof(buf), "  Free:      %u KB (%u MB)\n", free_mem, free_mem / 1024);
    shell_print(buf);
    snprintf(buf, sizeof(buf), "  Usage:     %u%%\n", total > 0 ? (used * 100) / total : 0);
    shell_print(buf);
    shell_print("\n");
    shell_print("  Kernel heap:  dynamic\n");
    shell_print("  Page cache:   0 KB\n");
    shell_print("  Buffers:      0 KB\n");
    shell_print("  Swap:         0 KB / 0 KB\n");
    shell_last_exit_code = 0;
}

/*
 * 模块探针
 */
void cmd_mem_module_init(void) {
    (void)0;
}

/*
 * PR-4 iter3 (v0.9 计划):
 *   - cmd_meminfo (12277 �? 详细内容)
 *   - cmd_vmstat
 *   - cmd_kmemleak
 *   - cmd_slabinfo
 *   - cmd_buddyinfo
 *   - cmd_pagetypeinfo
 *   - cmd_zoneinfo
 */