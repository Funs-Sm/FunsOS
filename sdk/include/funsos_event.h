#ifndef FUNSOS_EVENT_H
#define FUNSOS_EVENT_H

/*
 * funsos_event.h - FunsOS 事件系统头文件
 *
 * 提供跨模块的事件发布/订阅机制。
 * 支持同步和异步事件分发、事件过滤器、优先级队列。
 */

#include "stdint.h"
#include "stddef.h"

/* ---- 事件类型枚举 ---- */
enum {
    EVENT_NONE = 0,
    /* GUI事件 */
    EVENT_KEY_DOWN    = 0x100,
    EVENT_KEY_UP      = 0x101,
    EVENT_MOUSE_MOVE  = 0x102,
    EVENT_MOUSE_DOWN  = 0x103,
    EVENT_MOUSE_UP    = 0x104,
    EVENT_MOUSE_WHEEL = 0x105,
    EVENT_MOUSE_ENTER = 0x106,
    EVENT_MOUSE_LEAVE = 0x107,
    EVENT_TOUCH_BEGIN = 0x108,
    EVENT_TOUCH_MOVE  = 0x109,
    EVENT_TOUCH_END   = 0x10A,
    EVENT_RESIZE      = 0x10B,
    EVENT_FOCUS_IN    = 0x10C,
    EVENT_FOCUS_OUT   = 0x10D,
    EVENT_CLOSE       = 0x10E,
    EVENT_PAINT       = 0x10F,
    /* 窗口事件 */
    WINDOW_CREATE   = 0x200,
    WINDOW_DESTROY  = 0x201,
    WINDOW_SHOW     = 0x202,
    WINDOW_HIDE     = 0x203,
    WINDOW_MINIMIZE = 0x204,
    WINDOW_MAXIMIZE = 0x205,
    WINDOW_RESTORE  = 0x206,
    WINDOW_MOVE     = 0x207,
    WINDOW_RESIZE   = 0x208,
    /* 系统事件 */
    SYS_TIMER       = 0x300,
    SYS_NETWORK     = 0x301,
    SYS_DEVICE      = 0x302,
    SYS_FILE_CHANGE = 0x303,
    SYS_PROCESS_EXIT= 0x304,
    SYS_LOW_MEMORY  = 0x305,
    /* 自定义事件基址 */
    EVENT_CUSTOM_BASE = 0x8000
};

/* ---- 事件标志 ---- */
#define EVENT_FLAG_CAPTURED  0x01
#define EVENT_FLAG_CONSUMED  0x02
#define EVENT_FLAG_QUEUED    0x04
#define EVENT_FLAG_ASYNC     0x08

/* ---- 事件结构体 ---- */
typedef struct funsos_event {
    uint32_t type;           /* 事件类型 */
    uint32_t flags;          /* 标志位 */
    uint64_t timestamp;      /* 时间戳(ms) */
    uint32_t target_id;      /* 目标ID(窗口/控件) */
    uint32_t source_id;      /* 来源ID */
    union {
        struct { int key, scancode, mod; } key;
        struct { int x, y, button, clicks; } mouse;
        struct { int x, y, w, h; } geometry;
        struct { void *data; uint32_t size; } custom;
    } param;
} funsos_event_t;

/* ---- 回调函数类型 ---- */
typedef int (*event_callback_t)(funsos_event_t *event, void *user_data);

/* ---- 监听器配置常量 ---- */
#define EVENT_MAX_LISTENERS_PER_TYPE 16
#define EVENT_QUEUE_SIZE             256
#define EVENT_MAX_TYPES              64
#define EVENT_MAX_FILTERS            8

/* ---- 过滤器函数类型 ---- */
typedef int (*event_filter_t)(funsos_event_t *event, void *user_data);

/* ---- 统计信息结构体 ---- */
typedef struct event_stats {
    uint64_t posted_total;                    /* 发布总数 */
    uint64_t dispatched_total;                /* 分发总数 */
    uint64_t dropped_overflow;                /* 队列溢出丢弃数 */
    uint32_t listener_counts[EVENT_MAX_TYPES]; /* 各类型监听器计数 */
} event_stats_t;

/* ================================================================
 *  核心初始化
 * ================================================================ */

/*
 * 初始化事件系统（分配内部数据结构，清零状态）
 * 返回: 0 成功, -1 失败
 */
void event_system_init(void);

/* ================================================================
 *  事件发布 API
 * ================================================================ */

/*
 * 同步发布事件（立即分发给所有订阅者）
 * 参数: event - 待发布的事件指针
 * 返回: >=0 成功分发的监听器数量, <0 错误码
 */
int event_post(funsos_event_t *event);

/*
 * 异步发布事件（入队等待后续 poll 处理）
 * 参数: event - 待发布的事件指针
 * 返回: 0 成功入队, -1 队列已满
 */
int event_post_async(funsos_event_t *event);

/*
 * 延迟发布事件（在指定毫秒后自动触发）
 * 参数: event - 待发布的事件; delay_ms - 延迟毫秒数
 * 返回: 0 成功, -1 失败
 */
int event_post_delayed(funsos_event_t *event, uint32_t delay_ms);

/* ================================================================
 *  订阅 / 取消订阅 API
 * ================================================================ */

/*
 * 订阅指定类型的事件
 * 参数: type - 事件类型; cb - 回调函数; ud - 用户数据; pri - 优先级(0最高)
 * 返回: 0 成功, -1 参数无效或监听器已满
 */
int event_subscribe(uint32_t type, event_callback_t cb, void *ud, uint32_t pri);

/*
 * 取消订阅指定类型的回调
 * 参数: type - 事件类型; cb - 要移除的回调函数
 * 返回: 0 成功移除, -1 未找到匹配的监听器
 */
int event_unsubscribe(uint32_t type, event_callback_t cb);

/*
 * 启用/禁用指定监听器
 * 参数: type - 事件类型; cb - 回调函数; en - 1启用/0禁用
 * 返回: 0 成功, -1 未找到
 */
int event_enable_listener(uint32_t type, event_callback_t cb, uint8_t en);

/* ================================================================
 *  异步队列操作 API
 * ================================================================ */

/*
 * 从异步队列中取出一个待处理事件
 * 参数: out_event - 接收事件的缓冲区
 * 返回: 1 取到事件, 0 队列为空, -1 错误
 */
int event_poll(funsos_event_t *out_event);

/*
 * 查看异步队列是否有待处理事件
 * 返回: 队列中的事件数量, 0 表示无事件
 */
int event_peek(void);

/*
 * 处理异步队列中的所有排队事件（逐个取出并同步分发）
 * 返回: 实际处理的事件数量
 */
int event_flush(void);

/* ================================================================
 *  事件过滤器 API
 * ================================================================ */

/*
 * 安装全局事件过滤器
 * 参数: filter - 过滤器回调; ud - 用户数据
 * 返回: 0 成功, -1 过滤器槽已满
 */
int event_add_filter(event_filter_t filter, void *ud);

/*
 * 移除指定的全局事件过滤器
 * 参数: filter - 要移除的过滤器回调
 * 返回: 0 成功移除, -1 未找到
 */
int event_remove_filter(event_filter_t filter);

/* ================================================================
 *  统计 API
 * ================================================================ */

/*
 * 获取事件系统运行统计
 * 返回: 统计结构体的副本
 */
event_stats_t event_get_stats(void);

/*
 * 重置所有统计计数器归零
 */
void event_reset_stats(void);

#endif /* FUNSOS_EVENT_H */
