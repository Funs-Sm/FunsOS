/* logrotate_ext.h - 日志轮转扩展层
 *
 * 在既有 logrotate.c 之上加扩展：
 *   - 定期自动检查（基于 kwork）
 *   - 事件写入 evlog (source="LogRotate")
 *   - 统计信息（轮转次数、节省空间等）
 *   - 与 cron 联动（可选注册 cron 任务）
 *
 * 与 logrotate.c 的关系：本扩展层调用 logrotate_check/logrotate_force，
 *                         不修改其内存结构。
 */
#ifndef LOGROTATE_EXT_H
#define LOGROTATE_EXT_H

#include "stdint.h"

#define LOGROTATE_EXT_DEFAULT_INTERVAL_MS  60000u  /* 默认 60 秒检查一次 */

typedef struct {
    uint32_t total_checks;        /* 累计检查次数 */
    uint32_t total_rotations;     /* 累计轮转次数 */
    uint32_t total_failed;        /* 失败次数 */
    uint32_t last_check_tick;     /* 上次检查时刻 */
    uint32_t last_rotation_tick;  /* 上次轮转时刻 */
    uint32_t check_interval_ms;   /* 检查间隔 */
    uint32_t auto_rotate_enabled; /* 是否启用自动轮转 */
} logrotate_ext_stats_t;

/* ---- 初始化 ---- */
void logrotate_ext_init(void);
void logrotate_ext_shutdown(void);

/* ---- 自动轮转控制 ---- */
int  logrotate_ext_start_auto(uint32_t interval_ms);
int  logrotate_ext_stop_auto(void);
int  logrotate_ext_is_auto_enabled(void);

/* ---- 手动检查所有配置的文件 ---- */
int  logrotate_ext_check_all(void);

/* ---- 设置压缩标志（持久化到 logrotate.c 配置） ----
 * 注意：logrotate.c 的 compress 字段是运行时配置，
 * 此函数通过遍历 logrotate_get_config 来设置。
 */
int  logrotate_ext_set_compress(const char *filepath, int enable);

/* ---- 统计 ---- */
void logrotate_ext_get_stats(logrotate_ext_stats_t *stats);
void logrotate_ext_reset_stats(void);

#endif /* LOGROTATE_EXT_H */
