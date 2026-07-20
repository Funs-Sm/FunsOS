/* crashdump.h - 内核崩溃转储与系统报告 (Crash Dump & System Report)
 *
 * 两个功能：
 *   1) 崩溃转储：当 kernel_panic() 被调用时，把崩溃上下文（消息、文件、行号、
 *      寄存器、栈回溯、当前 PID、内存使用）写入 /var/crash/dump-<tick>.txt
 *      - 必须无锁、无 FunDB、无动态分配（panic 时系统状态不可信）
 *      - 仅用 vfs_creat + vfs_open(WRITE) + vfs_write
 *
 *   2) 系统报告：在正常运行时由 shell `sysreport` 触发，生成完整的系统状态
 *      快照（klog 摘要、evlog 最近事件、调度统计、内存、进程列表、服务、网络等）
 *      写入指定路径，便于诊断和归档。
 *
 * 与既有模块的关系：
 *   - panic.c       : 调用 crashdump_capture() 在 halt 前持久化上下文
 *   - klog/evlog    : 系统报告引用其内容（不修改）
 *   - taskmgr/svcmgr : 系统报告聚合它们的统计
 */
#ifndef CRASHDUMP_H
#define CRASHDUMP_H

#include "stdint.h"

#define CRASHDUMP_DIR           "/var/crash"
#define CRASHDUMP_DEFAULT_PATH  "/var/crash/dump-"
#define SYSREPORT_DEFAULT_PATH  "/var/crash/report-"

/* 崩溃转储内容上限 */
#define CRASHDUMP_MAX_SIZE      8192

/* ---- 初始化 ---- */
void crashdump_init(void);
void crashdump_shutdown(void);

/* ---- 崩溃转储（panic 时调用，无锁安全） ----
 * 返回写入的字节数，<0 表示失败。
 */
int crashdump_capture(const char *reason, const char *file, int line);

/* ---- 系统报告（正常运行时调用） ----
 * path 为 NULL 时使用默认路径 /var/crash/report-<tick>.txt
 * 返回写入的字节数，<0 表示失败。
 */
int sysreport_generate(const char *path);

/* ---- 查询最近的崩溃转储文件路径 ----
 * 填入最近一次崩溃转储的路径，返回 0=成功，-1=无转储
 */
int crashdump_get_last(char *out, uint32_t out_size);

#endif /* CRASHDUMP_H */
