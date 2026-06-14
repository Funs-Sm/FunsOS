/* power_service.h - FUNSOS 电源管理服务
 * 系统电源状态管理、电池监控、自动休眠
 */

#ifndef POWER_SERVICE_H
#define POWER_SERVICE_H

#include "stdint.h"

/* 电源操作 */
#define POWER_ACTION_SHUTDOWN  0
#define POWER_ACTION_REBOOT    1
#define POWER_ACTION_SUSPEND   2
#define POWER_ACTION_HIBERNATE 3
#define POWER_ACTION_LOGOFF    4
#define POWER_ACTION_LOCK      5

/* 电源状态 */
#define POWER_STATE_AC        0
#define POWER_STATE_BATTERY   1
#define POWER_STATE_CHARGING  2
#define POWER_STATE_CRITICAL  3
#define POWER_STATE_UNKNOWN   4

/* 电源配置 */
typedef struct {
    uint32_t idle_timeout_sec;       /* 空闲超时（秒），0=禁用 */
    uint32_t suspend_timeout_sec;    /* 休眠超时（秒），0=禁用 */
    uint32_t display_off_timeout_sec; /* 关闭显示器超时（秒），0=禁用 */
    uint32_t low_battery_warn;       /* 低电量警告阈值 (%) */
    uint32_t critical_battery;       /* 严重低电量阈值 (%) */
    uint32_t critical_action;        /* 严重低电量时执行的操作 */
    uint8_t  auto_suspend;           /* 是否启用自动休眠 */
    uint8_t  wake_on_lan;            /* 是否启用网络唤醒 */
    uint8_t  wake_on_keyboard;       /* 是否启用键盘唤醒 */
    uint8_t  wake_on_mouse;          /* 是否启用鼠标唤醒 */
} power_config_t;

/* 电源事件 */
typedef struct {
    uint32_t type;           /* 事件类型 */
    uint32_t timestamp;      /* 时间戳 */
    uint32_t data;           /* 事件数据 */
    char     message[128];   /* 事件描述 */
} power_event_t;

/* 电源事件类型 */
#define POWER_EVENT_AC_CONNECTED    1
#define POWER_EVENT_AC_DISCONNECTED 2
#define POWER_EVENT_BATTERY_LOW     3
#define POWER_EVENT_BATTERY_CRITICAL 4
#define POWER_EVENT_CHARGING_START  5
#define POWER_EVENT_CHARGING_STOP   6
#define POWER_EVENT_SUSPEND         7
#define POWER_EVENT_RESUME          8
#define POWER_EVENT_SHUTDOWN        9

/* 初始化电源服务 */
int power_service_init(void);

/* 启动电源服务 */
int power_service_start(void);

/* 停止电源服务 */
void power_service_stop(void);

/* 执行电源操作 */
int power_service_action(uint32_t action);

/* 获取当前电源状态 */
uint32_t power_service_get_state(void);

/* 获取电池信息 */
int power_service_get_battery(int *percent, int *charging, int *remaining_minutes);

/* 获取 CPU 信息 */
int power_service_get_cpu_info(uint32_t *freq_khz, uint32_t *temp_celsius, uint32_t *usage_percent);

/* 设置屏幕亮度 */
int power_service_set_brightness(int percent);

/* 获取屏幕亮度 */
int power_service_get_brightness(void);

/* 设置电源配置 */
int power_service_set_config(const power_config_t *config);

/* 获取电源配置 */
const power_config_t *power_service_get_config(void);

/* 获取电池信息详细 */
int power_service_get_battery_detailed(char *buf, uint32_t bufsize);

/* 阻止自动休眠 */
int power_service_inhibit(const char *reason);

/* 取消阻止自动休眠 */
int power_service_uninhibit(void);

/* 获取空闲时间 */
uint32_t power_service_get_idle_time(void);

/* 获取电源事件历史 */
int power_service_get_events(power_event_t *events, uint32_t max_events);

/* 电源服务更新（周期性调用） */
void power_service_update(void);

#endif /* POWER_SERVICE_H */