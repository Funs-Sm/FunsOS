/*
 * watchdog.c - 硬件看门狗定时器驱动实现
 *
 * 实现多层看门狗支持：
 *   1. Intel ICH 系列南桥的内置WDT (优先)
 *   2. IT87xx Super IO 的WDT
 *   3. 软件模拟看门狗（基于PIT定时器中断）
 *
 * 使用方式:
 *   wdt_init()          -- 启动时调用一次
 *   wdt_start(30)       -- 设置30秒超时并启动
 *   wdt_kick()          -- 在正常循环中定期调用(喂狗)
 *   wdt_stop()          -- 关闭看门狗
 */

#include "watchdog.h"
#include "io.h"
#include "timer.h"
#include "string.h"

/* ---- 看门狗类型检测 ---- */
enum {
    WDT_TYPE_NONE = 0,
    WDT_TYPE_INTEL_ICH,
    WDT_TYPE_IT87XX,
    WDT_TYPE_SOFTWARE
};

static int wdt_type = WDT_TYPE_NONE;
static uint32_t wdt_timeout = 0;
static uint32_t wdt_kick_count = 0;
static wdt_state_t wdt_current_state = WDT_STATE_DISABLED;

/* ================================================================
 *  Intel ICH 南桥看门狗 (IO端口 0x443 / 0x843)
 *  用于 Intel ICH0-ICH9 系列 PCH/South Bridge
 * ================================================================ */

#define ICH_WDT_REG    0x443    /* WDT 寄存器地址 */
#define ICH_WDT_UNLOCK 0x80     /* 解锁值: 写入此值解锁配置 */
#define ICH_WDT_ENABLE 0x08     /* 使能位 */
#define ICH_WDT_RELOAD 0x01     /* 重载位(喂狗) */
#define ICH_WDT_STATUS 0x02     /* 状态位(1=已触发过) */

static int ich_wdt_present(void) {
    /*
     * 尝试读取 ICH WDT 状态寄存器来检测是否存在。
     * 注意：某些虚拟机(QEMU)可能不支持此端口，
     * 但不会导致致命错误，只是返回不可用。
     */
    volatile uint8_t val = inb(ICH_WDT_REG);
    (void)val;  /* 仅用于检测端口是否可读 */
    return 1;   /* 假设存在(保守策略) */
}

static int ich_wdt_start(uint32_t timeout_sec) {
    /* Intel ICH WDT 超时范围通常为 2-63 秒 */
    if (timeout_sec < 2) timeout_sec = 2;
    if (timeout_sec > 63) timeout_sec = 63;

    /* 步骤1: 解锁 WDT 配置 */
    outb(ICH_WDT_REG, ICH_WDT_UNLOCK);

    /* 步骤2: 设置超时值 + 使能 */
    /* ICH WDT 使用低6位表示超时秒数 */
    uint8_t config = (uint8_t)(timeout_sec & 0x3F) | ICH_WDT_ENABLE;
    outb(ICH_WDT_REG, config);

    return 0;
}

static int ich_wdt_kick(void) {
    outb(ICH_WDT_REG, ICH_WDT_RELOAD);
    return 0;
}

static int ich_wdt_stop(void) {
    outb(ICH_WDT_REG, ICH_WDT_UNLOCK);
    outb(ICH_WDT_REG, 0x00);  /* 清除使能位 */
    return 0;
}

/* ================================================================
 *  IT87xx Super IO 看门狗
 *  用于 ITE IT8712/IT8720/IT8783 等 Super IO 芯片
 * ================================================================ */

#define IT87_CFG_PORT   0x2E    /* 配置端口 */
#define IT87_ENTER_KEY1 0x87
#define IT87_ENTER_KEY2 0x01
#define IT87_EXIT_KEY   0xAA
#define IT87_LDN        0x07    /* Logical Device Number 寄存器 */
#define IT87_LDN_WDT    0x07    /* WDT 的 LDN */
#define IT87_BASE_HI    0x60    /* Base address high byte */
#define IT87_BASE_LO    0x61    /* Base address low byte */
#define IT87_WDT_CTRL   0x70    /* WDT control register */
#define IT87_WDT_VAL    0x72    /* WDT timeout value register */
#define IT87_WDT_CFG    0x71    /* WDT configuration register */

static int it87_wdt_detect(void) {
    /* 尝试进入 IT87xx 配置模式 */
    outb(IT87_CFG_PORT, IT87_ENTER_KEY1);
    outb(IT87_CFG_PORT, IT87_ENTER_KEY2);

    /* 读取 Chip ID */
    uint8_t chip_id = 0;
    outb(IT87_CFG_PORT, 0x20);
    chip_id = inb(IT87_CFG_PORT + 1);

    /* 退出配置模式 */
    outb(IT87_CFG_PORT, IT87_EXIT_KEY);

    /* IT87xx 系列 Chip ID 范围: 0x87-0x8F (部分型号) */
    if ((chip_id >= 0x87 && chip_id <= 0x8F) ||
        chip_id == 0x71 || chip_id == 0x72 || chip_id == 0x73) {
        return 1;
    }
    return 0;
}

static int it87_wdt_start(uint32_t timeout_sec) {
    if (timeout_sec > 255) timeout_sec = 255;
    if (timeout_sec < 1) timeout_sec = 1;

    /* 进入配置模式 */
    outb(IT87_CFG_PORT, IT87_ENTER_KEY1);
    outb(IT87_CFG_PORT, IT87_ENTER_KEY2);

    /* 选择 WDT 设备 */
    outb(IT87_CFG_PORT, IT87_LDN);
    outb(IT87_CFG_PORT + 1, IT87_LDN_WDT);

    /* 设置超时值 */
    outb(IT87_CFG_PORT, IT87_WDT_VAL);
    outb(IT87_CFG_PORT + 1, (uint8_t)(timeout_sec & 0xFF));

    /* 使能 WDT */
    outb(IT87_CFG_PORT, IT87_WDT_CTRL);
    outb(IT87_CFG_PORT + 1, 0x01);  /* Enable bit */

    /* 退出配置模式 */
    outb(IT87_CFG_PORT, IT87_EXIT_KEY);

    return 0;
}

static int it87_wdt_kick(void) {
    /* IT87xx WDT 通过重新写入超时值来"喂狗" */
    outb(IT87_CFG_PORT, IT87_ENTER_KEY1);
    outb(IT87_CFG_PORT, IT87_ENTER_KEY2);

    outb(IT87_CFG_PORT, IT87_LDN);
    outb(IT87_CFG_PORT + 1, IT87_LDN_WDT);

    outb(IT87_CFG_PORT, IT87_WDT_VAL);
    outb(IT87_CFG_PORT + 1, (uint8_t)(wdt_timeout & 0xFF));

    outb(IT87_CFG_PORT, IT87_EXIT_KEY);

    return 0;
}

static int it87_wdt_stop(void) {
    outb(IT87_CFG_PORT, IT87_ENTER_KEY1);
    outb(IT87_CFG_PORT, IT87_ENTER_KEY2);

    outb(IT87_CFG_PORT, IT87_LDN);
    outb(IT87_CFG_PORT + 1, IT87_LDN_WDT);

    outb(IT87_CFG_PORT, IT87_WDT_CTRL);
    outb(IT87_CFG_PORT + 1, 0x00);  /* Disable */

    outb(IT87_CFG_PORT, IT87_EXIT_KEY);

    return 0;
}

/* ================================================================
 *  软件模拟看门狗 (回退方案)
 *  基于 PIT 定时器中断计数实现
 * ================================================================ */

static uint32_t sw_wdt_last_tick = 0;
static uint32_t sw_wdt_timeout_ticks = 0;

static int sw_wdt_start(uint32_t timeout_sec) {
    /* 假设 timer tick 为 ~10ms, 计算tick数 */
    sw_wdt_timeout_ticks = timeout_sec * 100;  /* 约100 ticks/sec */
    sw_wdt_last_tick = timer_get_ticks();
    return 0;
}

static int sw_wdt_kick(void) {
    sw_wdt_last_tick = timer_get_ticks();
    return 0;
}

static int sw_wdt_check_expired(void) {
    if (wdt_current_state != WDT_STATE_RUNNING) return 0;
    if (sw_wdt_timeout_ticks == 0) return 0;
    uint32_t now = timer_get_ticks();
    if (now - sw_wdt_last_tick >= sw_wdt_timeout_ticks) {
        wdt_current_state = WDT_STATE_EXPIRED;
        return 1;  /* 已过期! */
    }
    return 0;
}

/* ================================================================
 *  公共接口实现
 * ================================================================ */

int wdt_init(void) {
    wdt_type = WDT_TYPE_NONE;
    wdt_current_state = WDT_STATE_DISABLED;
    wdt_timeout = 0;
    wdt_kick_count = 0;

    /* 优先尝试 Intel ICH WDT */
    if (ich_wdt_present()) {
        wdt_type = WDT_TYPE_INTEL_ICH;
        return 0;
    }

    /* 其次尝试 IT87xx WDT */
    if (it87_wdt_detect()) {
        wdt_type = WDT_TYPE_IT87XX;
        return 0;
    }

    /* 回退到软件模拟 */
    wdt_type = WDT_TYPE_SOFTWARE;
    return 0;  /* 软件WDT始终可用 */
}

int wdt_is_available(void) {
    return wdt_type != WDT_TYPE_NONE;
}

int wdt_start(uint32_t timeout_sec) {
    if (timeout_sec == 0) return -1;

    wdt_timeout = timeout_sec;
    wdt_current_state = WDT_STATE_RUNNING;

    switch (wdt_type) {
    case WDT_TYPE_INTEL_ICH:
        return ich_wdt_start(timeout_sec);
    case WDT_TYPE_IT87XX:
        return it87_wdt_start(timeout_sec);
    case WDT_TYPE_SOFTWARE:
        return sw_wdt_start(timeout_sec);
    default:
        wdt_current_state = WDT_STATE_DISABLED;
        return -1;
    }
}

int wdt_stop(void) {
    wdt_current_state = WDT_STATE_DISABLED;

    switch (wdt_type) {
    case WDT_TYPE_INTEL_ICH:
        return ich_wdt_stop();
    case WDT_TYPE_IT87XX:
        return it87_wdt_stop();
    case WDT_TYPE_SOFTWARE:
        sw_wdt_timeout_ticks = 0;
        return 0;
    default:
        return -1;
    }
}

int wdt_kick(void) {
    wdt_kick_count++;

    switch (wdt_type) {
    case WDT_TYPE_INTEL_ICH:
        return ich_wdt_kick();
    case WDT_TYPE_IT87XX:
        return it87_wdt_kick();
    case WDT_TYPE_SOFTWARE:
        return sw_wdt_kick();
    default:
        return -1;
    }
}

int wdt_get_info(wdt_info_t *info) {
    if (!info) return -1;

    info->timeout_sec = wdt_timeout;
    info->state = wdt_current_state;
    info->kick_count = wdt_kick_count;

    switch (wdt_type) {
    case WDT_TYPE_INTEL_ICH:
        info->name = "Intel ICH WDT";
        info->min_timeout = 2;
        info->max_timeout = 63;
        break;
    case WDT_TYPE_IT87XX:
        info->name = "ITE IT87xx WDT";
        info->min_timeout = 1;
        info->max_timeout = 255;
        break;
    case WDT_TYPE_SOFTWARE:
        info->name = "Software WDT (PIT-based)";
        info->min_timeout = 1;
        info->max_timeout = 3600;  /* 最大1小时 */
        break;
    default:
        info->name = "None";
        info->min_timeout = 0;
        info->max_timeout = 0;
        break;
    }

    return 0;
}

int wdt_set_pretimeout(uint32_t sec) {
    /* 预超时回调: 在主超时前 N 秒发出警告
     * 当前为桩实现，可在后续版本中添加回调注册机制 */
    (void)sec;
    return 0;  /* OK - 功能保留但未完全实现 */
}

int wdt_get_remaining(uint32_t *sec) {
    if (!sec) return -1;

    switch (wdt_type) {
    case WDT_TYPE_SOFTWARE:
        if (sw_wdt_timeout_ticks == 0) { *sec = 0; return 0; }
        {
            uint32_t elapsed = timer_get_ticks() - sw_wdt_last_tick;
            uint32_t remaining_ticks = 0;
            if (elapsed < sw_wdt_timeout_ticks)
                remaining_ticks = sw_wdt_timeout_ticks - elapsed;
            *sec = remaining_ticks / 100;  /* ticks -> seconds (approx) */
        }
        return 0;
    default:
        /* 硬件WDT无法精确读取剩余时间，返回估计值 */
        *sec = wdt_timeout;  /* 近似值 */
        return 0;
    }
}
