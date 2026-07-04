#ifndef HEALTH_H
#define HEALTH_H

#include "stdint.h"

/* ============================================================
 * System Health Monitor - 系统健康监控
 *
 * 监控系统各项健康指标：
 * - CPU 使用率/温度
 * - 内存使用量
 * - 磁盘 I/O
 * - 网络流量
 * - 进程状态
 * ============================================================ */

/* 健康状态 */
typedef enum {
    HEALTH_OK       = 0,   /* 正常 */
    HEALTH_WARNING  = 1,   /* 警告 */
    HEALTH_CRITICAL = 2,   /* 严重 */
    HEALTH_DEAD     = 3    /* 死亡/不可用 */
} health_status_t;

/* 阈值配置 */
typedef struct {
    uint32_t cpu_warn_pct;      /* CPU 警告阈值 (%) */
    uint32_t cpu_crit_pct;      /* CPU 严重阈值 (%) */
    uint32_t mem_warn_pct;      /* 内存警告阈值 (%) */
    uint32_t mem_crit_pct;      /* 内存严重阈值 (%) */
    uint32_t disk_warn_pct;     /* 磁盘警告阈值 (%) */
    uint32_t disk_crit_pct;     /* 磁盘严重阈值 (%) */
    uint32_t temp_warn_c;       /* 温度警告阈值 (°C) */
    uint32_t temp_crit_c;       /* 温度严重阈值 (°C) */
} health_thresholds_t;

/* CPU 统计 */
typedef struct {
    uint32_t user_pct;
    uint32_t sys_pct;
    uint32_t idle_pct;
    uint32_t iowait_pct;
    uint32_t load_avg_1min;
    uint32_t load_avg_5min;
    uint32_t load_avg_15min;
    uint32_t running_procs;
    uint32_t total_procs;
    health_status_t status;
} health_cpu_t;

/* 内存统计 */
typedef struct {
    uint64_t total_kb;
    uint64_t used_kb;
    uint64_t free_kb;
    uint64_t cached_kb;
    uint64_t buffers_kb;
    uint64_t swap_total_kb;
    uint64_t swap_used_kb;
    uint64_t swap_free_kb;
    uint32_t used_pct;
    health_status_t status;
} health_memory_t;

/* 磁盘统计 */
typedef struct {
    char     device[32];
    uint64_t total_kb;
    uint64_t used_kb;
    uint64_t free_kb;
    uint32_t used_pct;
    uint64_t read_bytes;
    uint64_t write_bytes;
    uint32_t io_pct;
    health_status_t status;
} health_disk_t;

/* 网络统计 */
typedef struct {
    char     iface[16];
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint32_t rx_errors;
    uint32_t tx_errors;
    uint32_t speed_mbps;
    health_status_t status;
} health_network_t;

/* 整体健康状态 */
typedef struct {
    health_status_t overall;
    health_cpu_t cpu;
    health_memory_t memory;
    health_disk_t disk;
    health_network_t network;
    uint32_t uptime_seconds;
    uint32_t check_count;
    uint32_t last_check;
} health_report_t;

/* ---- API ---- */

/* 初始化健康监控 */
void health_init(void);

/* 获取健康报告 */
int health_get_report(health_report_t *report);

/* 获取整体健康状态 */
health_status_t health_get_status(void);

/* 设置阈值 */
int health_set_thresholds(const health_thresholds_t *th);

/* 获取阈值 */
int health_get_thresholds(health_thresholds_t *th);

/* 手动触发健康检查 */
int health_check(void);

/* 获取健康状态描述 */
const char *health_status_string(health_status_t status);

#endif /* HEALTH_H */
