#ifndef NOTIFIER_H
#define NOTIFIER_H

#include "stdint.h"

/* ============================================================
 * Notifier Chain - 内核通知链
 *
 * 用于内核子系统之间的事件通知，支持：
 * - 注册/注销通知回调
 * - 按优先级排序
 * - 多种事件类型
 * ============================================================ */

/* 通知链类型 */
#define NOTIFIER_NET_DEV        0    /* 网络设备状态变化 */
#define NOTIFIER_BLOCK_DEV      1    /* 块设备状态变化 */
#define NOTIFIER_FS             2    /* 文件系统事件 */
#define NOTIFIER_PROCESS        3    /* 进程事件 */
#define NOTIFIER_MEMORY         4    /* 内存事件 */
#define NOTIFIER_POWER          5    /* 电源管理事件 */
#define NOTIFIER_THERMAL        6    /* 温度事件 */
#define NOTIFIER_CPU            7    /* CPU 事件 */
#define NOTIFIER_MAX_CHAINS     8

/* 通知事件（通用） */
#define NOTIFY_EVENT_ADD        1    /* 添加/上线 */
#define NOTIFY_EVENT_REMOVE     2    /* 删除/下线 */
#define NOTIFY_EVENT_CHANGE     3    /* 状态变化 */
#define NOTIFY_EVENT_SUSPEND    4    /* 挂起 */
#define NOTIFY_EVENT_RESUME     5    /* 恢复 */
#define NOTIFY_EVENT_ERROR      6    /* 错误 */

/* 优先级 */
#define NOTIFY_PRIO_HIGHEST     0
#define NOTIFY_PRIO_HIGH       64
#define NOTIFY_PRIO_NORMAL    128
#define NOTIFY_PRIO_LOW       192
#define NOTIFY_PRIO_LOWEST    255

/* 通知回调函数类型
 * 返回 0 = 继续调用下一个回调
 * 返回 非0 = 停止通知链 */
typedef int (*notifier_fn_t)(void *data, uint32_t event);

/* 通知节点 */
typedef struct notifier_block {
    notifier_fn_t callback;
    uint32_t      priority;
    void         *user_data;
    struct notifier_block *next;
} notifier_block_t;

/* ---- API ---- */

/* 初始化通知链子系统 */
void notifier_init(void);

/* 注册通知回调 */
int notifier_register(uint32_t chain, notifier_fn_t callback,
                      uint32_t priority, void *user_data);

/* 注销通知回调 */
int notifier_unregister(uint32_t chain, notifier_fn_t callback);

/* 发送通知事件 */
int notifier_call_chain(uint32_t chain, uint32_t event, void *data);

/* 获取通知链上的回调数量 */
uint32_t notifier_count(uint32_t chain);

#endif /* NOTIFIER_H */
