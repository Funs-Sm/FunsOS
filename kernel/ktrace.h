#ifndef KTRACE_H
#define KTRACE_H

#include "stdint.h"
#include "stddef.h"
#include "stdarg.h"

/* ============================================================
 * Kernel Tracing Subsystem - 内核跟踪子系统
 *
 * 提供结构化事件跟踪功能，区别于 klog（日志行）和 perf（计数器）：
 *   - 环形缓冲区存储带时间戳的事件
 *   - 按类别（category）过滤事件
 *   - 按级别（level）过滤事件
 *   - 支持格式化消息
 *   - 支持统计与导出
 *
 * 典型用法：
 *   ktrace_init();                 // 启动时初始化
 *   ktrace_enable(KTRACE_CAT_SCHED | KTRACE_CAT_FS);
 *   ktrace_event(KTRACE_CAT_SCHED, KTRACE_LEVEL_INFO, "sched switch");
 *   ktrace_eventf(KTRACE_CAT_SYSCALL, KTRACE_LEVEL_INFO,
 *                "open pid=%d path=%s", pid, path);
 *   ktrace_dump(64);               // 转储最近 64 条到 klog
 * ============================================================ */

/* 事件类别（位掩码，可组合） */
#define KTRACE_CAT_SCHED    0x0001u  /* 调度器事件 */
#define KTRACE_CAT_FS       0x0002u  /* 文件系统事件 */
#define KTRACE_CAT_NET      0x0004u  /* 网络事件 */
#define KTRACE_CAT_MEM      0x0008u  /* 内存管理事件 */
#define KTRACE_CAT_IRQ      0x0010u  /* 中断事件 */
#define KTRACE_CAT_SYSCALL  0x0020u  /* 系统调用事件 */
#define KTRACE_CAT_PROC     0x0040u  /* 进程事件 */
#define KTRACE_CAT_TIMER    0x0080u  /* 定时器事件 */
#define KTRACE_CAT_DEFAULT  0x0000u  /* 未分类 */
#define KTRACE_CAT_ALL      0xFFFFu  /* 全部类别 */

/* 类别数量（用于统计数组大小） */
#define KTRACE_CAT_COUNT    8

/* 事件级别 */
#define KTRACE_LEVEL_DEBUG  0
#define KTRACE_LEVEL_INFO   1
#define KTRACE_LEVEL_WARN   2
#define KTRACE_LEVEL_ERROR  3

/* 单条事件最大消息长度 */
#define KTRACE_MSG_MAX      96

/* 默认环形缓冲区容量（事件数，必须是 2 的幂） */
#define KTRACE_DEFAULT_CAP  256

/* 单条跟踪事件 */
typedef struct ktrace_event {
    uint64_t timestamp;     /* 时间戳（TSC） */
    uint32_t pid;           /* 进程 ID */
    uint32_t category;      /* 类别掩码 */
    uint8_t  level;         /* 级别 */
    uint8_t  cpu;           /* CPU ID */
    uint16_t reserved;       /* 保留字段 */
    char     msg[KTRACE_MSG_MAX]; /* 消息内容 */
} ktrace_event_t;

/* 跟踪统计 */
typedef struct ktrace_stats {
    uint64_t total_events;              /* 总记录事件数 */
    uint64_t dropped_events;            /* 因缓冲区满丢弃的事件数 */
    uint64_t events_per_cat[KTRACE_CAT_COUNT]; /* 每个类别的事件数 */
    uint32_t enabled_mask;              /* 当前启用的类别掩码 */
    uint32_t min_level;                /* 最低允许级别 */
    uint32_t buffer_capacity;           /* 环形缓冲区容量 */
    uint32_t events_in_buffer;          /* 当前缓冲区中的事件数 */
    uint32_t max_buffered;             /* 历史最大缓冲数 */
} ktrace_stats_t;

/* ---- 初始化 ---- */
void ktrace_init(void);

/* ---- 类别控制 ---- */
void ktrace_enable(uint32_t category_mask);
void ktrace_disable(uint32_t category_mask);
void ktrace_set_mask(uint32_t category_mask);
uint32_t ktrace_get_mask(void);

/* ---- 级别控制 ---- */
void ktrace_set_level(uint32_t level);
uint32_t ktrace_get_level(void);

/* ---- 跟踪事件 ---- */
void ktrace_event(uint32_t category, uint8_t level, const char *msg);
void ktrace_eventf(uint32_t category, uint8_t level, const char *fmt, ...);
void ktrace_eventf_va(uint32_t category, uint8_t level,
                      const char *fmt, va_list args);

/* ---- 缓冲区操作 ---- */
void ktrace_clear(void);
int  ktrace_resize(uint32_t new_capacity);

/* ---- 统计 ---- */
void ktrace_get_stats(ktrace_stats_t *stats);
void ktrace_reset_stats(void);

/* ---- 导出 ---- */
/* 转储最近 count 条事件到 klog（count=0 表示全部） */
void ktrace_dump(uint32_t count);

/* 读取事件到用户缓冲区，返回实际读取的事件数 */
uint32_t ktrace_read(ktrace_event_t *out, uint32_t max_count);

/* 类别名称辅助 */
const char *ktrace_cat_name(uint32_t category);
const char *ktrace_level_name(uint8_t level);

/* 当前时间戳（TSC） */
uint64_t ktrace_now(void);

/* 便捷宏 */
#define ktrace_sched(msg)    ktrace_event(KTRACE_CAT_SCHED,   KTRACE_LEVEL_INFO, msg)
#define ktrace_fs(msg)       ktrace_event(KTRACE_CAT_FS,      KTRACE_LEVEL_INFO, msg)
#define ktrace_net(msg)      ktrace_event(KTRACE_CAT_NET,     KTRACE_LEVEL_INFO, msg)
#define ktrace_mem(msg)      ktrace_event(KTRACE_CAT_MEM,     KTRACE_LEVEL_INFO, msg)
#define ktrace_irq(msg)      ktrace_event(KTRACE_CAT_IRQ,     KTRACE_LEVEL_INFO, msg)
#define ktrace_syscall(msg)  ktrace_event(KTRACE_CAT_SYSCALL, KTRACE_LEVEL_INFO, msg)
#define ktrace_proc(msg)     ktrace_event(KTRACE_CAT_PROC,    KTRACE_LEVEL_INFO, msg)
#define ktrace_timer(msg)    ktrace_event(KTRACE_CAT_TIMER,   KTRACE_LEVEL_INFO, msg)

#endif /* KTRACE_H */
