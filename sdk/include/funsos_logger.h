#ifndef FUNSOS_LOGGER_H
#define FUNSOS_LOGGER_H

#include "stdint.h"
#include "stddef.h"

/*
 * FUNSOS SDK - 日志系统子模块
 *
 * 提供应用级日志记录、查询和过滤 API
 * 桥接到内核 klog 子系统，并提供应用级独立环形缓冲
 *
 * 版本: 1.0.0 (FunsCore v0.8 / SDK 1.4.0 新增)
 */

#define FUNSOS_LOGGER_API_VERSION  0x0100

/* ---- 日志级别 ---- */
#define FUNSOS_LOG_EMERG    0   /* 系统不可用 */
#define FUNSOS_LOG_ALERT    1   /* 必须立即采取行动 */
#define FUNSOS_LOG_CRIT     2   /* 严重情况 */
#define FUNSOS_LOG_ERR      3   /* 错误 */
#define FUNSOS_LOG_WARNING  4   /* 警告 */
#define FUNSOS_LOG_NOTICE   5   /* 正常但重要 */
#define FUNSOS_LOG_INFO     6   /* 信息 */
#define FUNSOS_LOG_DEBUG    7   /* 调试 */
#define FUNSOS_LOG_TRACE    8   /* 跟踪 */

/* ---- 日志来源分类 ---- */
#define FUNSOS_LOG_CAT_KERNEL   0   /* 内核消息 */
#define FUNSOS_LOG_CAT_DRIVER   1   /* 驱动消息 */
#define FUNSOS_LOG_CAT_FS       2   /* 文件系统 */
#define FUNSOS_LOG_CAT_NET      3   /* 网络 */
#define FUNSOS_LOG_CAT_USER     4   /* 用户态 */
#define FUNSOS_LOG_CAT_SECURITY 5   /* 安全审计 */
#define FUNSOS_LOG_CAT_SYSTEMD  6   /* 系统服务 */

#define FUNSOS_LOG_MAX_MSG     256   /* 单条日志最大长度 */
#define FUNSOS_LOG_RING_SIZE   1024  /* 应用级环形缓冲条目数 */

/* ---- 日志条目结构 ---- */
typedef struct {
    uint32_t timestamp;     /* 时间戳 (tick) */
    uint32_t level;         /* 日志级别 */
    uint32_t category;      /* 分类 */
    uint32_t pid;           /* 进程 ID (模拟) */
    char     source[32];    /* 来源标签 */
    char     message[FUNSOS_LOG_MAX_MSG];
} funsos_log_entry_t;

/* ---- 过滤器 ---- */
typedef struct {
    uint32_t min_level;          /* 最低级别 */
    uint32_t category_mask;      /* 分类掩码 */
    char     source_filter[32];  /* 来源过滤 (空表示所有) */
} funsos_log_filter_t;

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 基础日志 API ---- */

/*
 * 记录一条日志
 * 参数: level - FUNSOS_LOG_*; category - FUNSOS_LOG_CAT_*;
 *       source - 来源标签 (如 "app:foo"); fmt - printf 风格格式
 */
void funsos_log_write(uint32_t level, uint32_t category,
                     const char *source, const char *fmt, ...);

/*
 * 便捷宏
 */
#define funsos_log_emerg(cat, src, ...)  funsos_log_write(FUNSOS_LOG_EMERG,   cat, src, __VA_ARGS__)
#define funsos_log_alert(cat, src, ...)  funsos_log_write(FUNSOS_LOG_ALERT,   cat, src, __VA_ARGS__)
#define funsos_log_crit(cat, src, ...)   funsos_log_write(FUNSOS_LOG_CRIT,    cat, src, __VA_ARGS__)
#define funsos_log_err(cat, src, ...)    funsos_log_write(FUNSOS_LOG_ERR,     cat, src, __VA_ARGS__)
#define funsos_log_warn(cat, src, ...)   funsos_log_write(FUNSOS_LOG_WARNING, cat, src, __VA_ARGS__)
#define funsos_log_notice(cat, src, ...) funsos_log_write(FUNSOS_LOG_NOTICE,  cat, src, __VA_ARGS__)
#define funsos_log_info(cat, src, ...)   funsos_log_write(FUNSOS_LOG_INFO,    cat, src, __VA_ARGS__)
#define funsos_log_debug(cat, src, ...)  funsos_log_write(FUNSOS_LOG_DEBUG,   cat, src, __VA_ARGS__)
#define funsos_log_trace(cat, src, ...)  funsos_log_write(FUNSOS_LOG_TRACE,   cat, src, __VA_ARGS__)

/* ---- 日志查询 ---- */

/*
 * 获取日志条目总数 (应用级环形缓冲)
 */
uint32_t funsos_log_count(void);

/*
 * 读取指定索引的日志条目
 * 参数: index - 条目索引 (0 为最旧); entry - 接收条目
 * 返回: 0 成功, -1 失败 (索引越界)
 */
int funsos_log_get(uint32_t index, funsos_log_entry_t *entry);

/*
 * 按过滤器读取多条日志
 * 参数: filter - 过滤器 (NULL 表示所有); entries - 接收数组;
 *       max_count - 缓冲区最大容量; start_index - 起始索引
 * 返回: 实际读取的条目数
 */
uint32_t funsos_log_query(const funsos_log_filter_t *filter,
                         funsos_log_entry_t *entries,
                         uint32_t max_count,
                         uint32_t start_index);

/*
 * 将日志缓冲区导出为文本
 * 参数: buf - 接收文本; max_len - 缓冲区大小; filter - 过滤器
 * 返回: 实际写入的字节数
 */
uint32_t funsos_log_dump(char *buf, uint32_t max_len,
                        const funsos_log_filter_t *filter);

/* ---- 内核日志 (dmesg) ---- */

/*
 * 获取内核日志条目数
 */
uint32_t funsos_log_kernel_count(void);

/*
 * 读取内核日志到文本缓冲区
 * 参数: buf - 缓冲区; max_len - 缓冲区大小
 * 返回: 实际写入的字节数
 */
uint32_t funsos_log_kernel_read(char *buf, uint32_t max_len);

/*
 * 清除内核日志
 */
void funsos_log_kernel_clear(void);

/* ---- 配置 ---- */

/*
 * 设置当前进程的最低日志级别 (低于此级别的日志被丢弃)
 */
void funsos_log_set_level(uint32_t min_level);

/*
 * 获取当前最低日志级别
 */
uint32_t funsos_log_get_level(void);

/*
 * 清除应用级日志缓冲
 */
void funsos_log_clear(void);

/*
 * 启用/禁用某分类的日志记录
 * 参数: category - 分类; enable - 1 启用, 0 禁用
 */
void funsos_log_set_category_enabled(uint32_t category, int enable);

/*
 * 检查某分类是否启用
 */
int funsos_log_is_category_enabled(uint32_t category);

#ifdef __cplusplus
}
#endif

#endif /* FUNSOS_LOGGER_H */
