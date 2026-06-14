/* funsos_time.h - 时间/日期 API
 * 系统时间、日历、定时器、时区管理
 */

#ifndef FUNSOS_TIME_H
#define FUNSOS_TIME_H

#include "stdint.h"

/* ---- 时间结构 ---- */
typedef struct {
    uint32_t year;          /* 年 (例如 2026) */
    uint32_t month;         /* 月 (1-12) */
    uint32_t day;           /* 日 (1-31) */
    uint32_t hour;          /* 时 (0-23) */
    uint32_t minute;        /* 分 (0-59) */
    uint32_t second;        /* 秒 (0-59) */
    uint32_t millisecond;   /* 毫秒 (0-999) */
    uint32_t weekday;       /* 星期几 (0=周日, 6=周六) */
    uint32_t yearday;       /* 一年中的第几天 (1-366) */
    int32_t  timezone_offset; /* 时区偏移（分钟，正=东） */
} funsos_datetime_t;

/* ---- 时间戳 ---- */
typedef uint64_t funsos_timestamp_t;  /* 毫秒级 Unix 时间戳 */

/* ---- 时间间隔 ---- */
typedef struct {
    int32_t days;
    int32_t hours;
    int32_t minutes;
    int32_t seconds;
    int32_t milliseconds;
} funsos_timespan_t;

/* ---- 日期时间 API ---- */

/* 获取当前系统时间 */
int funsos_get_datetime(funsos_datetime_t *dt);

/* 设置系统时间 */
int funsos_set_datetime(const funsos_datetime_t *dt);

/* 获取当前 Unix 时间戳（毫秒） */
funsos_timestamp_t funsos_get_timestamp(void);

/* 获取当前 Unix 时间戳（秒） */
uint32_t funsos_get_timestamp_sec(void);

/* 时间戳转日期时间 */
int funsos_timestamp_to_datetime(funsos_timestamp_t ts, funsos_datetime_t *dt);

/* 日期时间转时间戳 */
funsos_timestamp_t funsos_datetime_to_timestamp(const funsos_datetime_t *dt);

/* ---- 时间格式化 ---- */

/* 格式化日期时间（类似 strftime） */
int funsos_format_datetime(const funsos_datetime_t *dt, const char *format, char *buf, uint32_t bufsize);

/* 获取 ISO 8601 格式时间字符串 */
int funsos_get_iso8601(char *buf, uint32_t bufsize);

/* 获取可读的时间字符串 */
int funsos_get_time_string(char *buf, uint32_t bufsize);

/* 获取可读的日期字符串 */
int funsos_get_date_string(char *buf, uint32_t bufsize);

/* ---- 时区管理 ---- */

/* 获取当前时区偏移（分钟） */
int32_t funsos_get_timezone_offset(void);

/* 设置时区 */
int funsos_set_timezone(const char *timezone_name);

/* 获取时区名称 */
const char *funsos_get_timezone_name(void);

/* 获取可用时区列表 */
int funsos_list_timezones(char *buf, uint32_t bufsize);

/* ---- 高精度定时器 ---- */

#ifndef FUNSOS_TIMER_CALLBACK_T_DEFINED
#define FUNSOS_TIMER_CALLBACK_T_DEFINED
typedef void (*funsos_timer_callback_t)(void *user_data);
#endif

/* 创建高精度定时器（毫秒级） */
int funsos_timer_create(uint32_t interval_ms, int repeat, funsos_timer_callback_t callback, void *user_data);

/* 启动定时器 */
int funsos_timer_start(int timer_id);

/* 停止定时器 */
int funsos_timer_stop(int timer_id);

/* 销毁定时器 */
int funsos_timer_destroy(int timer_id);

/* 获取定时器剩余时间 */
uint32_t funsos_timer_get_remaining(int timer_id);

/* ---- 挂起/延迟 ---- */

/* 睡眠指定毫秒 */
int funsos_sleep_ms(uint32_t milliseconds);

/* 睡眠指定秒 */
int funsos_sleep(uint32_t seconds);

/* 获取系统启动以来的毫秒数 */
uint64_t funsos_get_uptime_ms(void);

/* 获取系统启动以来的秒数 */
uint32_t funsos_get_uptime_sec(void);

/* ---- 时间差计算 ---- */

/* 计算两个时间之间的差值 */
funsos_timespan_t funsos_time_diff(const funsos_datetime_t *t1, const funsos_datetime_t *t2);

/* 比较两个日期时间（返回 -1=t1<t2, 0=相等, 1=t1>t2） */
int funsos_time_compare(const funsos_datetime_t *t1, const funsos_datetime_t *t2);

/* ---- 闹钟/提醒 ---- */

/* 设置一次性闹钟 */
int funsos_alarm_set(const funsos_datetime_t *time, funsos_timer_callback_t callback, void *user_data);

/* 取消闹钟 */
int funsos_alarm_cancel(int alarm_id);

/* 获取待处理的闹钟数量 */
int funsos_alarm_get_count(void);

#endif /* FUNSOS_TIME_H */