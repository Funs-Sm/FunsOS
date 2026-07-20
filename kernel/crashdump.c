/* crashdump.c - 崩溃转储与系统报告实现
 *
 * 崩溃转储使用最小依赖集（vfs + snprintf + timer），在 panic 上下文中安全调用：
 *   - 不获取任何锁
 *   - 不分配内存
 *   - 不调用 FunDB / evlog / kwork
 *
 * 系统报告使用更丰富的 API，仅在正常运行时调用。
 */
#include "crashdump.h"
#include "vfs.h"
#include "klog.h"
#include "timer.h"
#include "version.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "stdint.h"

/* 全局状态（仅记录上次转储路径，无锁，panic 时也安全） */
static struct {
    int      initialized;
    uint32_t last_dump_tick;
    char     last_dump_path[128];
} g_cd;

/* 简单的字符串追加辅助：把 fmt 格式化后追加到 buffer 末尾
 * 返回新的偏移量。如果空间不足，截断。 */
static uint32_t cd_append(char *buf, uint32_t size, uint32_t offset,
                           const char *fmt, ...) {
    if (offset >= size) return offset;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + offset, size - offset, fmt, ap);
    va_end(ap);
    if (n < 0) return offset;
    if ((uint32_t)n >= size - offset) n = size - offset - 1;
    return offset + (uint32_t)n;
}

/* 把 buffer 内容写入文件 */
static int cd_write_file(const char *path, const char *buf, uint32_t size) {
    if (!path || !buf) return -1;

    /* 确保目录存在（best effort） */
    vfs_mkdir("/var", 0755);
    vfs_mkdir("/var/crash", 0755);

    /* 创建文件 */
    if (vfs_creat(path, 0644) != 0 && vfs_creat(path, 0644) != 0) {
        /* 创建失败可能因为已存在，继续尝试打开 */
    }

    file_t *f = NULL;
    if (vfs_open(path, FILE_MODE_WRITE, &f) != 0 || !f) {
        return -2;
    }
    int32_t written = vfs_write(f, buf, size);
    vfs_close(f);
    return written;
}

/* ---- 公共 API ---- */

void crashdump_init(void) {
    memset(&g_cd, 0, sizeof(g_cd));
    /* 确保崩溃目录存在 */
    vfs_mkdir("/var", 0755);
    vfs_mkdir("/var/crash", 0755);
    g_cd.initialized = 1;
    klog_info("crashdump: crash dump subsystem initialized (dir=%s)", CRASHDUMP_DIR);
}

void crashdump_shutdown(void) {
    g_cd.initialized = 0;
}

/* ---- 崩溃转储 ---- */

int crashdump_capture(const char *reason, const char *file, int line) {
    /* 在 panic 上下文中：必须无锁、无 FunDB、无 evlog、无 kmalloc */
    char buf[CRASHDUMP_MAX_SIZE];
    uint32_t off = 0;
    uint32_t tick = (uint32_t)timer_get_ticks();

    /* 构建转储文件路径 */
    char path[128];
    snprintf(path, sizeof(path), "%s%u.txt", CRASHDUMP_DEFAULT_PATH, tick);

    /* 写入头部 */
    off = cd_append(buf, sizeof(buf), off,
                    "===== FUNSOS CRASH DUMP =====\n");
    off = cd_append(buf, sizeof(buf), off,
                    "Kernel: %s\n", KERNEL_STRING);
    off = cd_append(buf, sizeof(buf), off,
                    "Build : %s %s\n", __DATE__, __TIME__);
    off = cd_append(buf, sizeof(buf), off,
                    "Tick  : %u (%u.%02us uptime)\n",
                    tick, tick / 100u, (tick % 100u));
    off = cd_append(buf, sizeof(buf), off, "\n");

    /* Panic 信息 */
    off = cd_append(buf, sizeof(buf), off,
                    "--- Panic ---\n");
    off = cd_append(buf, sizeof(buf), off,
                    "Reason: %s\n", reason ? reason : "<none>");
    off = cd_append(buf, sizeof(buf), off,
                    "File  : %s\n", file ? file : "<unknown>");
    off = cd_append(buf, sizeof(buf), off,
                    "Line  : %d\n", line);
    off = cd_append(buf, sizeof(buf), off, "\n");

    /* 栈回溯（从 EBP 链） */
    off = cd_append(buf, sizeof(buf), off,
                    "--- Stack Trace ---\n");
    uint32_t *ebp;
    asm volatile("mov %%ebp, %0" : "=r"(ebp));
    uint32_t depth = 0;
    while (ebp && depth < 24) {
        uint32_t ret_addr = *(ebp + 1);
        off = cd_append(buf, sizeof(buf), off,
                        "  [%u] 0x%08X\n", depth, ret_addr);
        ebp = (uint32_t *)*ebp;
        depth++;
    }
    off = cd_append(buf, sizeof(buf), off, "\n");

    /* 寄存器（基本） */
    off = cd_append(buf, sizeof(buf), off,
                    "--- Registers ---\n");
    uint32_t cr0, cr2, cr3, cr4;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    asm volatile("mov %%cr2, %0" : "=r"(cr2));
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    off = cd_append(buf, sizeof(buf), off,
                    "CR0=0x%08X CR2=0x%08X CR3=0x%08X CR4=0x%08X\n",
                    cr0, cr2, cr3, cr4);
    uint32_t esp, ebp_val;
    asm volatile("mov %%esp, %0" : "=r"(esp));
    asm volatile("mov %%ebp, %0" : "=r"(ebp_val));
    off = cd_append(buf, sizeof(buf), off,
                    "ESP=0x%08X EBP=0x%08X\n", esp, ebp_val);
    off = cd_append(buf, sizeof(buf), off, "\n");

    /* 内存使用（直接调用 pmm，避免锁） */
    extern uint32_t pmm_get_total_pages(void);
    extern uint32_t pmm_get_used_pages(void);
    uint32_t total_pages = pmm_get_total_pages();
    uint32_t used_pages = pmm_get_used_pages();
    off = cd_append(buf, sizeof(buf), off,
                    "--- Memory ---\n");
    off = cd_append(buf, sizeof(buf), off,
                    "Total: %u pages (%u KB)\n", total_pages, total_pages * 4);
    off = cd_append(buf, sizeof(buf), off,
                    "Used : %u pages (%u KB)\n", used_pages, used_pages * 4);
    off = cd_append(buf, sizeof(buf), off,
                    "Free : %u pages (%u KB)\n",
                    total_pages - used_pages, (total_pages - used_pages) * 4);
    off = cd_append(buf, sizeof(buf), off, "\n");

    /* 当前进程（如果可用） */
    extern uint32_t sched_get_tick_count(void);
    off = cd_append(buf, sizeof(buf), off,
                    "--- Scheduler ---\n");
    off = cd_append(buf, sizeof(buf), off,
                    "Total ticks: %u\n", sched_get_tick_count());
    off = cd_append(buf, sizeof(buf), off, "\n");

    /* 尾部 */
    off = cd_append(buf, sizeof(buf), off,
                    "--- End of Dump ---\n");

    /* 写入文件 */
    int written = cd_write_file(path, buf, off);
    if (written > 0) {
        g_cd.last_dump_tick = tick;
        strncpy(g_cd.last_dump_path, path, sizeof(g_cd.last_dump_path) - 1);
        g_cd.last_dump_path[sizeof(g_cd.last_dump_path) - 1] = '\0';
    }

    /* 同时输出到串口（panic.c 已经做了部分） */
    return written;
}

/* ---- 系统报告 ---- */

/* taskmgr / svcmgr / sysacct 头 */
#include "taskmgr.h"
#include "svcmgr.h"
#include "sysacct.h"

/* 网络接口（最小依赖） */
extern uint32_t net_get_interface_count(void);

int sysreport_generate(const char *path) {
    char buf[CRASHDUMP_MAX_SIZE * 2];  /* 报告更大 */
    uint32_t off = 0;
    uint32_t tick = (uint32_t)timer_get_ticks();

    char default_path[128];
    if (!path) {
        snprintf(default_path, sizeof(default_path),
                 "%s%u.txt", SYSREPORT_DEFAULT_PATH, tick);
        path = default_path;
    }

    /* 确保目录 */
    vfs_mkdir("/var", 0755);
    vfs_mkdir("/var/crash", 0755);

    /* 头部 */
    off = cd_append(buf, sizeof(buf), off,
                    "===== FUNSOS SYSTEM REPORT =====\n");
    off = cd_append(buf, sizeof(buf), off,
                    "Kernel: %s\n", KERNEL_STRING);
    off = cd_append(buf, sizeof(buf), off,
                    "Build : %s %s\n", __DATE__, __TIME__);
    off = cd_append(buf, sizeof(buf), off,
                    "Tick  : %u (%u.%02us uptime)\n",
                    tick, tick / 100u, (tick % 100u));
    off = cd_append(buf, sizeof(buf), off, "\n");

    /* 内存 */
    extern uint32_t pmm_get_total_pages(void);
    extern uint32_t pmm_get_used_pages(void);
    uint32_t total_pages = pmm_get_total_pages();
    uint32_t used_pages = pmm_get_used_pages();
    off = cd_append(buf, sizeof(buf), off, "--- Memory ---\n");
    off = cd_append(buf, sizeof(buf), off,
                    "Total: %u KB\n", total_pages * 4);
    off = cd_append(buf, sizeof(buf), off,
                    "Used : %u KB\n", used_pages * 4);
    off = cd_append(buf, sizeof(buf), off,
                    "Free : %u KB\n", (total_pages - used_pages) * 4);
    off = cd_append(buf, sizeof(buf), off, "\n");

    /* 进程汇总 */
    off = cd_append(buf, sizeof(buf), off, "--- Processes ---\n");
    taskmgr_summary_t ts;
    taskmgr_get_summary(&ts);
    off = cd_append(buf, sizeof(buf), off,
                    "Total=%u Running=%u Ready=%u Blocked=%u Zombie=%u\n",
                    ts.total_procs, ts.running, ts.ready, ts.blocked, ts.zombie);
    off = cd_append(buf, sizeof(buf), off,
                    "Kernel=%u User=%u ContextSwitches=%u\n",
                    ts.kernel_procs, ts.user_procs, ts.context_switches);
    off = cd_append(buf, sizeof(buf), off,
                    "CpuLoad=%u%%\n", ts.cpu_load_percent);
    off = cd_append(buf, sizeof(buf), off, "\n");

    /* Top 10 CPU 进程 */
    off = cd_append(buf, sizeof(buf), off, "--- Top 10 CPU ---\n");
    off = cd_append(buf, sizeof(buf), off,
                    "PID    NAME                STATE      TICKS   PRIO\n");
    taskmgr_proc_t top[10];
    uint32_t n = taskmgr_get_top_cpu(top, 10);
    for (uint32_t i = 0; i < n; i++) {
        off = cd_append(buf, sizeof(buf), off,
                        "%-6d %-20s %-10s %-8u %u\n",
                        top[i].pid, top[i].name,
                        taskmgr_state_name(top[i].state),
                        top[i].ticks_used, top[i].priority);
    }
    off = cd_append(buf, sizeof(buf), off, "\n");

    /* 服务汇总 */
    off = cd_append(buf, sizeof(buf), off, "--- Services ---\n");
    svcmgr_stats_t ss;
    svcmgr_get_stats(&ss);
    off = cd_append(buf, sizeof(buf), off,
                    "Total=%u Running=%u Stopped=%u Failed=%u\n",
                    ss.total_services, ss.running, ss.stopped, ss.failed);
    off = cd_append(buf, sizeof(buf), off,
                    "Starts=%u Stops=%u Restarts=%u Failures=%u\n",
                    ss.total_starts, ss.total_stops,
                    ss.total_restarts, ss.total_failures);
    off = cd_append(buf, sizeof(buf), off, "\n");

    /* 安全审计 */
    off = cd_append(buf, sizeof(buf), off, "--- Security ---\n");
    sysacct_stats_t as;
    sysacct_get_stats(&as);
    off = cd_append(buf, sizeof(buf), off,
                    "Logins=%u Failed=%u ActiveSessions=%u\n",
                    as.total_logins, as.failed_logins, as.active_sessions);
    off = cd_append(buf, sizeof(buf), off,
                    "Locked=%u PassChanges=%u SudoUses=%u\n",
                    as.locked_accounts, as.pass_changes, as.sudo_uses);
    off = cd_append(buf, sizeof(buf), off, "\n");

    /* 网络 */
    off = cd_append(buf, sizeof(buf), off, "--- Network ---\n");
    uint32_t ifc = net_get_interface_count();
    off = cd_append(buf, sizeof(buf), off,
                    "Interfaces: %u\n", ifc);
    off = cd_append(buf, sizeof(buf), off, "\n");

    /* 尾部 */
    off = cd_append(buf, sizeof(buf), off,
                    "--- End of Report ---\n");

    return cd_write_file(path, buf, off);
}

int crashdump_get_last(char *out, uint32_t out_size) {
    if (!out || out_size == 0) return -1;
    if (!g_cd.last_dump_path[0]) return -1;
    strncpy(out, g_cd.last_dump_path, out_size - 1);
    out[out_size - 1] = '\0';
    return 0;
}
