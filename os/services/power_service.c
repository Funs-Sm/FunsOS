/* power_service.c - FUNSOS 电源管理服务实现
 * 管理电源状态、电池监控、自动休眠等功能
 */

#include "power_service.h"
#include "sys_api.h"
#include "string.h"
#include "klog.h"
#include "stddef.h"

/* 最大事件历史 */
#define MAX_POWER_EVENTS 64

/* 内部状态 */
static power_config_t power_config;
static power_event_t power_events[MAX_POWER_EVENTS];
static uint32_t event_count = 0;
static uint32_t event_index = 0;
static uint32_t service_running = 0;
static uint32_t inhibit_count = 0;
static uint32_t last_activity_tick = 0;
static uint32_t current_brightness = 100;
static uint32_t current_power_state = POWER_STATE_AC;

/* 静态函数 */
static void power_add_event(uint32_t type, uint32_t data, const char *message);
static void power_check_battery(void);
static void power_check_idle(void);

int power_service_init(void)
{
    /* 默认配置 */
    memset(&power_config, 0, sizeof(power_config));
    power_config.idle_timeout_sec = 600;        /* 10 分钟空闲 */
    power_config.suspend_timeout_sec = 1800;    /* 30 分钟休眠 */
    power_config.display_off_timeout_sec = 300; /* 5 分钟关闭显示器 */
    power_config.low_battery_warn = 20;         /* 20% 警告 */
    power_config.critical_battery = 5;          /* 5% 严重 */
    power_config.critical_action = POWER_ACTION_HIBERNATE;
    power_config.auto_suspend = 0;              /* 默认不自动休眠 */
    power_config.wake_on_keyboard = 1;
    power_config.wake_on_mouse = 1;

    memset(power_events, 0, sizeof(power_events));
    event_count = 0;
    event_index = 0;
    inhibit_count = 0;
    last_activity_tick = 0;
    current_brightness = 100;
    current_power_state = POWER_STATE_AC;

    klog_info("Power service initialized");
    return 0;
}

int power_service_start(void)
{
    if (service_running) return 0;
    service_running = 1;
    klog_info("Power service started");
    return 0;
}

void power_service_stop(void)
{
    service_running = 0;
    klog_info("Power service stopped");
}

int power_service_action(uint32_t action)
{
    klog_info("Power action: %d", action);

    switch (action) {
    case POWER_ACTION_SHUTDOWN:
        power_add_event(POWER_EVENT_SHUTDOWN, 0, "System shutdown");
        /* 执行关机流程 */
        break;
    case POWER_ACTION_REBOOT:
        /* 执行重启流程 */
        break;
    case POWER_ACTION_SUSPEND:
        power_add_event(POWER_EVENT_SUSPEND, 0, "System suspend");
        break;
    case POWER_ACTION_HIBERNATE:
        power_add_event(POWER_EVENT_SUSPEND, 0, "System hibernate");
        break;
    case POWER_ACTION_LOGOFF:
        break;
    case POWER_ACTION_LOCK:
        break;
    default:
        return -1;
    }
    return 0;
}

uint32_t power_service_get_state(void)
{
    return current_power_state;
}

int power_service_get_battery(int *percent, int *charging, int *remaining_minutes)
{
    /* 从系统获取电池信息 */
    if (percent) *percent = 100;  /* 默认：满电 */
    if (charging) *charging = 0;   /* 默认：不在充电 */
    if (remaining_minutes) *remaining_minutes = -1; /* 未知 */
    return 0;
}

int power_service_get_cpu_info(uint32_t *freq_khz, uint32_t *temp_celsius, uint32_t *usage_percent)
{
    if (freq_khz) *freq_khz = 2000000;  /* 默认 2GHz */
    if (temp_celsius) *temp_celsius = 45; /* 默认 45°C */
    if (usage_percent) *usage_percent = 25; /* 默认 25% */
    return 0;
}

int power_service_set_brightness(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    current_brightness = (uint32_t)percent;

    /* 通过 ACPI 或 VESA 设置亮度 */
    klog_info("Display brightness set to %d%%", percent);
    return 0;
}

int power_service_get_brightness(void)
{
    return (int)current_brightness;
}

int power_service_set_config(const power_config_t *config)
{
    if (config == NULL) return -1;
    memcpy(&power_config, config, sizeof(power_config_t));
    klog_info("Power config updated");
    return 0;
}

const power_config_t *power_service_get_config(void)
{
    return &power_config;
}

int power_service_get_battery_detailed(char *buf, uint32_t bufsize)
{
    if (buf == NULL || bufsize == 0) return -1;
    /* 返回电池信息字符串 */
    int percent = 100, charging = 0, remaining = -1;
    power_service_get_battery(&percent, &charging, &remaining);

    /* 简易格式化 */
    memset(buf, 0, bufsize);
    return 0;
}

int power_service_inhibit(const char *reason)
{
    inhibit_count++;
    klog_info("Power inhibit: %s (count=%d)", reason ? reason : "unknown", inhibit_count);
    return 0;
}

int power_service_uninhibit(void)
{
    if (inhibit_count > 0) inhibit_count--;
    klog_info("Power uninhibit (count=%d)", inhibit_count);
    return 0;
}

uint32_t power_service_get_idle_time(void)
{
    /* 获取系统空闲时间 */
    return 0;
}

int power_service_get_events(power_event_t *events, uint32_t max_events)
{
    if (events == NULL || max_events == 0) return 0;

    uint32_t count = (event_count < max_events) ? event_count : max_events;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t idx = (event_index + MAX_POWER_EVENTS - count + i) % MAX_POWER_EVENTS;
        memcpy(&events[i], &power_events[idx], sizeof(power_event_t));
    }
    return (int)count;
}

void power_service_update(void)
{
    if (!service_running) return;

    power_check_battery();
    power_check_idle();
}

/* ---- 内部函数 ---- */

static void power_add_event(uint32_t type, uint32_t data, const char *message)
{
    power_event_t *ev = &power_events[event_index];
    ev->type = type;
    ev->timestamp = 0; /* 获取系统 ticks */
    ev->data = data;
    if (message) {
        uint32_t len = (uint32_t)strlen(message);
        if (len > 127) len = 127;
        memcpy(ev->message, message, len);
        ev->message[len] = '\0';
    }

    event_index = (event_index + 1) % MAX_POWER_EVENTS;
    if (event_count < MAX_POWER_EVENTS) event_count++;
}

static void power_check_battery(void)
{
    int percent = 0, charging = 0;
    power_service_get_battery(&percent, &charging, NULL);

    /* 更新电源状态 */
    if (charging) {
        current_power_state = POWER_STATE_CHARGING;
    } else {
        current_power_state = POWER_STATE_BATTERY;
    }

    /* 检查低电量 */
    if (percent <= (int)power_config.critical_battery) {
        power_add_event(POWER_EVENT_BATTERY_CRITICAL, (uint32_t)percent, "Battery critically low");
        current_power_state = POWER_STATE_CRITICAL;
        /* 执行严重低电量操作 */
        if (power_config.critical_action != 0) {
            power_service_action(power_config.critical_action);
        }
    } else if (percent <= (int)power_config.low_battery_warn) {
        power_add_event(POWER_EVENT_BATTERY_LOW, (uint32_t)percent, "Battery low");
    }
}

static void power_check_idle(void)
{
    if (power_config.auto_suspend == 0) return;
    if (inhibit_count > 0) return;

    uint32_t idle_time = power_service_get_idle_time();
    if (power_config.suspend_timeout_sec > 0 &&
        idle_time >= power_config.suspend_timeout_sec) {
        power_service_action(POWER_ACTION_SUSPEND);
    }
}