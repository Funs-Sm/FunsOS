#ifndef WATCHDOG_H
#define WATCHDOG_H

/*
 * watchdog.h - 硬件看门狗定时器驱动接口
 *
 * 提供对硬件看门狗定时器(WDT)的抽象访问。
 * 看门狗用于系统可靠性保障：如果系统挂起，WDT超时后自动重启。
 *
 * 支持的硬件:
 *   - Intel ICH WDT (IO port 0x443/0x843)
 *   - IT87xx Super IO WDT
 *   - 通用软件模拟看门狗（基于定时器中断）
 */

#include "stdint.h"

/* ---- 看门狗状态 ---- */
typedef enum {
    WDT_STATE_DISABLED = 0,
    WDT_STATE_RUNNING,
    WDT_STATE_EXPIRED
} wdt_state_t;

/* ---- 看门狗信息 ---- */
typedef struct {
    uint32_t timeout_sec;     /* 当前超时时间(秒) */
    uint32_t min_timeout;     /* 支持的最小超时 */
    uint32_t max_timeout;     /* 支持的最大超时 */
    wdt_state_t state;        /* 当前状态 */
    uint32_t kick_count;      /* 喂狗次数统计 */
    const char  *name;        /* 驱动名称 */
} wdt_info_t;

/* ---- 初始化与探测 ---- */
int  wdt_init(void);                    /* 探测并初始化WDT硬件 */
int  wdt_is_available(void);            /* 检查是否有可用的WDT */

/* ---- 基本操作 ---- */
int  wdt_start(uint32_t timeout_sec);   /* 启动看门狗(秒) */
int  wdt_stop(void);                    /* 停止看门狗 */
int  wdt_kick(void);                    /* 喂狗(重置计时器) */
int  wdt_get_info(wdt_info_t *info);    /* 获取看门狗状态信息 */

/* ---- 高级功能 ---- */
int  wdt_set_pretimeout(uint32_t sec);  /* 设置预超时回调(提前N秒通知) */
int  wdt_get_remaining(uint32_t *sec);  /* 获取剩余时间(秒) */

#endif /* WATCHDOG_H */
