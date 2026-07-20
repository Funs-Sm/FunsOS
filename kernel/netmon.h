/* netmon.h - 网络监视子系统
 *
 * 周期性采样网络接口统计（tx/rx 包数、字节数、错误数），
 * 检测接口状态变化和错误激增，事件写入 evlog（source="NetMon"）。
 *
 *   - 基于 kwork 周期性采样（默认 5 秒）
 *   - 历史快照存储在 FunDB（/var/db/netmon.db）
 *   - 检测：新接口、接口 down、错误率激增
 *   - Shell 命令：netmon list/stats/history/clear
 */
#ifndef NETMON_H
#define NETMON_H

#include "stdint.h"
#include "stddef.h"

#define NETMON_MAX_INTERFACES  8
#define NETMON_IFNAME_LEN       16
#define NETMON_DB_PATH          "/var/db/netmon.db"
#define NETMON_SAMPLE_INTERVAL  5000u   /* 采样间隔 ms */
#define NETMON_HISTORY_SIZE     32      /* 内存中保留的历史快照数 */
#define NETMON_ERROR_THRESHOLD  10      /* 错误增量阈值（触发告警） */

/* 接口快照（某时刻的接口统计） */
typedef struct {
    char     name[NETMON_IFNAME_LEN];
    uint32_t ip;
    uint32_t mask;
    uint32_t gateway;
    uint8_t  up;
    uint8_t  flags;
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t tx_bytes;
    uint32_t rx_bytes;
    uint32_t tx_errors;
    uint32_t rx_errors;
    uint64_t sample_tick;
} netmon_if_snapshot_t;

/* 历史采样点（所有接口的一个时间点快照） */
typedef struct {
    uint64_t sample_tick;
    uint32_t interface_count;
    netmon_if_snapshot_t interfaces[NETMON_MAX_INTERFACES];
} netmon_sample_t;

/* 统计 */
typedef struct {
    uint32_t total_samples;
    uint32_t interfaces_seen;
    uint32_t state_changes;
    uint32_t error_alerts;
    uint64_t last_sample_tick;
    uint32_t sample_interval_ms;
} netmon_stats_t;

/* ---- 初始化 ---- */
void netmon_init(void);
void netmon_shutdown(void);

/* ---- 采样 ---- */
/* 立即采样一次。可在外部调用或由 kwork 定期触发 */
void netmon_sample(void);

/* ---- 查询 ---- */
/* 获取最新一次采样的接口快照 */
uint32_t netmon_get_latest(netmon_if_snapshot_t *out, uint32_t max_count);

/* 获取历史采样（最新在前） */
uint32_t netmon_get_history(netmon_sample_t *out, uint32_t max_count);

/* ---- 统计 ---- */
void netmon_get_stats(netmon_stats_t *stats);
void netmon_reset_stats(void);

/* ---- 管理 ---- */
void netmon_clear_history(void);

/* 设置采样间隔（ms），重启 kwork 周期任务 */
int netmon_set_interval(uint32_t interval_ms);

#endif /* NETMON_H */
