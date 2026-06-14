/* funsos_power.h - 电源管理 API
 * 系统电源状态控制、电池信息、休眠/唤醒管理
 */

#ifndef FUNSOS_POWER_H
#define FUNSOS_POWER_H

#include "stdint.h"

/* ---- 电源操作类型 ---- */
typedef enum {
    FUNSOS_POWER_SHUTDOWN = 0,
    FUNSOS_POWER_REBOOT   = 1,
    FUNSOS_POWER_SUSPEND  = 2,
    FUNSOS_POWER_HIBERNATE = 3,
    FUNSOS_POWER_LOGOFF   = 4,
    FUNSOS_POWER_LOCK     = 5
} funsos_power_action_t;

/* ---- 电源状态 ---- */
typedef enum {
    FUNSOS_POWER_STATE_AC    = 0,  /* 交流电源 */
    FUNSOS_POWER_STATE_BATTERY = 1, /* 电池供电 */
    FUNSOS_POWER_STATE_CHARGING = 2, /* 充电中 */
    FUNSOS_POWER_STATE_UNKNOWN = 3
} funsos_power_state_t;

/* ---- 电池信息 ---- */
typedef struct {
    uint32_t percent;           /* 电量百分比 (0-100) */
    uint32_t remaining_minutes; /* 剩余时间（分钟） */
    uint32_t full_capacity_mwh; /* 满电容量 (mWh) */
    uint32_t current_capacity_mwh; /* 当前容量 (mWh) */
    uint32_t voltage_mv;        /* 电压 (mV) */
    uint32_t charge_rate_mw;    /* 充电速率 (mW) */
    uint32_t discharge_rate_mw; /* 放电速率 (mW) */
    uint32_t cycle_count;       /* 充电循环次数 */
    uint8_t  present;           /* 电池是否存在 */
    uint8_t  charging;          /* 是否正在充电 */
    uint8_t  critical;          /* 电量是否严重不足 */
    char     manufacturer[32];  /* 制造商 */
    char     model[32];         /* 型号 */
    char     technology[16];    /* 电池技术 (Li-ion, NiMH, etc.) */
} funsos_battery_info_t;

/* ---- CPU 频率信息 ---- */
typedef struct {
    uint32_t current_freq_khz;   /* 当前频率 (kHz) */
    uint32_t max_freq_khz;       /* 最大频率 (kHz) */
    uint32_t min_freq_khz;       /* 最小频率 (kHz) */
    uint32_t governor;           /* 调频策略 */
    uint32_t temperature_celsius; /* CPU 温度 (摄氏度) */
    uint32_t usage_percent;      /* CPU 使用率 (%) */
} funsos_cpu_info_t;

/* ---- 调频策略 ---- */
#define FUNSOS_GOVERNOR_PERFORMANCE 0
#define FUNSOS_GOVERNOR_POWERSAVE   1
#define FUNSOS_GOVERNOR_ONDEMAND    2
#define FUNSOS_GOVERNOR_CONSERVATIVE 3

/* ---- 电源管理 API ---- */

/* 执行电源操作（关机/重启/休眠等） */
int funsos_power_action(funsos_power_action_t action);

/* 获取当前电源状态 */
funsos_power_state_t funsos_power_get_state(void);

/* 获取电池信息 */
int funsos_battery_get_info(funsos_battery_info_t *info);

/* 获取电池电量百分比 */
int funsos_battery_get_percent(void);

/* 电池是否正在充电 */
int funsos_battery_is_charging(void);

/* 电池电量是否严重不足 */
int funsos_battery_is_critical(void);

/* ---- CPU 频率管理 ---- */

/* 获取 CPU 信息 */
int funsos_cpu_get_info(funsos_cpu_info_t *info);

/* 设置 CPU 调频策略 */
int funsos_cpu_set_governor(uint32_t governor);

/* 获取 CPU 温度 */
int funsos_cpu_get_temperature(void);

/* ---- 屏幕亮度 ---- */

/* 获取屏幕亮度 (0-100) */
int funsos_display_get_brightness(void);

/* 设置屏幕亮度 (0-100) */
int funsos_display_set_brightness(int percent);

/* ---- 空闲/休眠定时器 ---- */

/* 设置系统空闲超时（秒），超时后执行指定操作 */
int funsos_idle_set_timeout(uint32_t seconds, funsos_power_action_t action);

/* 取消空闲超时 */
int funsos_idle_cancel_timeout(void);

/* 获取空闲时间（秒） */
uint32_t funsos_idle_get_time(void);

/* 阻止系统自动休眠（如视频播放时） */
int funsos_power_inhibit(const char *reason);

/* 取消阻止系统自动休眠 */
int funsos_power_uninhibit(void);

#endif /* FUNSOS_POWER_H */