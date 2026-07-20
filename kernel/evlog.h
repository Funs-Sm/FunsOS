/* evlog.h - 系统事件日志子系统
 *
 * 持久化结构化事件日志，类似 Windows Event Log。
 *   - FunDB 持久化（/var/db/evlog.db）
 *   - 事件源（source）注册与过滤
 *   - 严重性分级（INFO / WARNING / ERROR / CRITICAL）
 *   - 事件类别（system / security / app / sysadmin / driver / network）
 *   - 自动剪裁（retention）保留最近 N 条
 *   - 查询过滤（按源、严重性、事件 ID、数量）
 *
 * 与现有日志体系的关系：
 *   - klog   : 内核日志行（基础输出，不持久化）
 *   - syslog : 带 facility/level 规则的环形缓冲（可选文件/网络）
 *   - ktrace : 结构化事件 ring buffer（短消息，不持久化）
 *   - evlog  : 结构化 + 持久化 + 可查询（本子系统）
 */
#ifndef EVLOG_H
#define EVLOG_H

#include "stdint.h"
#include "stddef.h"
#include "stdarg.h"

#define EVLOG_MAX_SOURCES       32
#define EVLOG_MAX_SOURCE_NAME   32
#define EVLOG_MAX_MSG           256
#define EVLOG_DB_PATH            "/var/db/evlog.db"
#define EVLOG_DEFAULT_RETENTION 1000u   /* 默认保留最近 1000 条 */
#define EVLOG_MAX_RETENTION     10000u
#define EVLOG_INVALID_ID        0u
#define EVLOG_TICKS_PER_SEC     100u   /* timer 运行在 100Hz，与 timer.c 一致 */

/* 严重性级别 */
typedef enum {
    EVLOG_SEV_INFO     = 0,
    EVLOG_SEV_WARNING  = 1,
    EVLOG_SEV_ERROR    = 2,
    EVLOG_SEV_CRITICAL = 3,
} evlog_severity_t;

#define EVLOG_SEV_COUNT  4

/* 事件类别 */
typedef enum {
    EVLOG_CAT_NONE     = 0,
    EVLOG_CAT_SYSTEM   = 1,   /* 系统事件 */
    EVLOG_CAT_SECURITY = 2,   /* 安全/认证 */
    EVLOG_CAT_APP      = 3,   /* 应用程序 */
    EVLOG_CAT_SYSADMIN = 4,   /* 系统管理 */
    EVLOG_CAT_DRIVER   = 5,   /* 驱动 */
    EVLOG_CAT_NETWORK  = 6,   /* 网络 */
} evlog_category_t;

/* 事件记录（用于写入与查询返回） */
typedef struct evlog_record {
    uint32_t id;                                  /* 自增记录 ID */
    uint64_t timestamp;                           /* 时间戳（ticks since boot） */
    uint32_t event_id;                            /* 应用自定义事件 ID */
    char     source[EVLOG_MAX_SOURCE_NAME];       /* 事件源名称 */
    uint8_t  severity;                            /* evlog_severity_t */
    uint8_t  category;                            /* evlog_category_t */
    uint16_t pid;                                 /* 进程 ID（0=内核） */
    char     message[EVLOG_MAX_MSG];              /* 消息文本 */
} evlog_record_t;

/* 事件源注册信息 */
typedef struct {
    char     name[EVLOG_MAX_SOURCE_NAME];
    uint8_t  min_severity;   /* 低于此级别不记录（>=） */
    uint8_t  enabled;        /* 是否启用 */
    uint32_t event_count;    /* 该源累计记录数 */
} evlog_source_t;

/* 统计 */
typedef struct {
    uint32_t total_records;
    uint32_t total_sources;
    uint32_t per_severity[EVLOG_SEV_COUNT];
    uint32_t retention_limit;
    uint32_t pruned;            /* 累计剪裁条数 */
    uint32_t dropped;           /* 因源禁用或满而丢弃的条数 */
} evlog_stats_t;

/* 查询过滤器 */
typedef struct {
    const char *source;       /* NULL = 全部源 */
    uint8_t  min_severity;    /* >= 此级别（0=全部） */
    uint32_t event_id;        /* 0 = 全部 */
    uint32_t limit;           /* 0 = 默认 100 */
} evlog_filter_t;

/* ============================================================
 * 事件监视规则（Watch Rules）
 *
 * 当新写入的事件匹配某条规则时，更新该规则的匹配统计
 * （匹配计数、最后匹配时间、最后匹配记录 ID）。
 * 不执行回调命令，避免在任意上下文中触发副作用。
 * 用户可通过 shell 查询匹配状态用于监控告警。
 * ============================================================ */

#define EVLOG_MAX_WATCHES  16
#define EVLOG_WATCH_NAME   32

typedef struct evlog_watch {
    uint32_t id;                                  /* 规则 ID（>=1） */
    char     name[EVLOG_WATCH_NAME];              /* 规则名称 */
    char     source[EVLOG_MAX_SOURCE_NAME];       /* 匹配源（空=任意） */
    uint8_t  min_severity;                        /* 最低严重性（0=任意） */
    uint32_t event_id;                            /* 匹配事件 ID（0=任意） */
    uint8_t  enabled;
    uint32_t match_count;                         /* 累计匹配次数 */
    uint64_t last_match_tick;                     /* 最后匹配时刻 */
    uint32_t last_match_id;                       /* 最后匹配记录 ID */
    char     last_match_msg[64];                  /* 最后匹配消息摘要 */
} evlog_watch_t;

/* ---- 初始化 ---- */
void evlog_init(void);
void evlog_shutdown(void);

/* ---- 事件源管理 ---- */
int  evlog_register_source(const char *name);
int  evlog_unregister_source(const char *name);
int  evlog_source_set_min_severity(const char *name, uint8_t min_sev);
int  evlog_source_enable(const char *name, int enabled);
int  evlog_source_count(void);
int  evlog_list_sources(evlog_source_t *out, uint32_t max_count);

/* ---- 写入 ---- */
void evlog_write(const char *source, uint8_t severity, uint8_t category,
                 uint32_t event_id, const char *fmt, ...);
void evlog_write_va(const char *source, uint8_t severity, uint8_t category,
                    uint32_t event_id, const char *fmt, va_list args);

/* 便捷宏（默认类别 EVLOG_CAT_SYSTEM） */
#define evlog_info(src, eid, fmt, ...)   \
    evlog_write(src, EVLOG_SEV_INFO,     EVLOG_CAT_SYSTEM, eid, fmt, ##__VA_ARGS__)
#define evlog_warn(src, eid, fmt, ...)   \
    evlog_write(src, EVLOG_SEV_WARNING,  EVLOG_CAT_SYSTEM, eid, fmt, ##__VA_ARGS__)
#define evlog_err(src, eid, fmt, ...)    \
    evlog_write(src, EVLOG_SEV_ERROR,    EVLOG_CAT_SYSTEM, eid, fmt, ##__VA_ARGS__)
#define evlog_crit(src, eid, fmt, ...)    \
    evlog_write(src, EVLOG_SEV_CRITICAL, EVLOG_CAT_SYSTEM, eid, fmt, ##__VA_ARGS__)

/* ---- 查询 ---- */
/* 返回实际填入 out 的记录数（按时间倒序，最新在前） */
uint32_t evlog_query(const evlog_filter_t *filter,
                     evlog_record_t *out, uint32_t max_count);

/* 按 ID 获取单条记录，0=成功，-1=未找到 */
int evlog_get_by_id(uint32_t id, evlog_record_t *out);

/* ---- 管理 ---- */
void evlog_clear(void);
int  evlog_set_retention(uint32_t limit);
void evlog_get_stats(evlog_stats_t *stats);
void evlog_reset_stats(void);
/* 仅保留最近 keep_recent 条，返回实际剪裁条数 */
int  evlog_prune(uint32_t keep_recent);

/* ---- 导出 ---- */
/* 将匹配 filter 的记录以文本格式导出到文件。
 * 返回写入的记录数，<0 表示出错。 */
int evlog_export(const char *path, const evlog_filter_t *filter);

/* ---- 监视规则 ---- */
/* 添加监视规则，返回分配的规则 ID（>0），<=0 表示失败 */
int  evlog_watch_add(const char *name, const char *source,
                     uint8_t min_severity, uint32_t event_id);
int  evlog_watch_remove(uint32_t id);
int  evlog_watch_enable(uint32_t id, int enabled);
int  evlog_watch_count(void);
int  evlog_watch_list(evlog_watch_t *out, uint32_t max_count);
int  evlog_watch_reset_stats(uint32_t id);

/* ---- 辅助 ---- */
const char *evlog_severity_name(uint8_t sev);
const char *evlog_category_name(uint8_t cat);

#endif /* EVLOG_H */
