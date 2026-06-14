#ifndef GPIO_H
#define GPIO_H

/*
 * gpio.h - GPIO (通用输入输出) 抽象驱动接口
 *
 * 提供跨平台的GPIO访问抽象。支持:
 *   - x86 GPIO (通过 Super IO / LPC / ACPI)
 *   - SoC GPIO (ARM/RISC-V 平台通用)
 *   - 软件模拟GPIO (调试用)
 *
 * 使用方式:
 *   gpio_init()              -- 初始化
 *   gpio_set_direction(pin, OUT) -- 设置为输出
 *   gpio_write(pin, HIGH)     -- 输出高电平
 *   val = gpio_read(pin)      -- 读取引脚状态
 */

#include <stdint.h>

/* ---- GPIO 引脚定义 (x86 平台) ---- */
#define GPIO_INVALID    0xFFFFFFFF

/* ---- 方向 ---- */
typedef enum {
    GPIO_DIR_INPUT  = 0,
    GPIO_DIR_OUTPUT = 1
} gpio_dir_t;

/* ---- 电平值 ---- */
typedef enum {
    GPIO_LOW  = 0,
    GPIO_HIGH = 1
} gpio_level_t;

/* ---- 上拉/下拉配置 ---- */
typedef enum {
    GPIO_PULL_NONE  = 0,
    GPIO_PULL_UP    = 1,
    GPIO_PULL_DOWN  = 2
} gpio_pull_t;

/* ---- 中断触发模式 ---- */
typedef enum {
    GPIO_INT_DISABLE = 0,
    GPIO_INT_RISING  = 1,  /* 上升沿触发 */
    GPIO_INT_FALLING = 2,  /* 下降沿触发 */
    GPIO_INT_BOTH    = 3,  /* 双边沿触发 */
    GPIO_INT_LEVEL_HIGH = 4,  /* 高电平触发 */
    GPIO_INT_LEVEL_LOW = 5   /* 低电平触发 */
} gpio_int_mode_t;

/* ---- GPIO 引脚信息 ---- */
typedef struct {
    uint32_t pin;             /* 引脚编号 */
    const char *name;         /* 引脚名称(可选) */
    gpio_dir_t direction;
    gpio_pull_t pull;
    uint8_t active_low;       /* 低电平有效标志 */
} gpio_pin_config_t;

/* ---- 初始化与探测 ---- */
int  gpio_init(void);
int  gpio_is_available(void);

/* ---- 基本读写 ---- */
int  gpio_set_direction(uint32_t pin, gpio_dir_t dir);
int  gpio_write(uint32_t pin, gpio_level_t level);
gpio_level_t gpio_read(uint32_t pin);

/* ---- 高级配置 ---- */
int  gpio_set_pull(uint32_t pin, gpio_pull_t pull);
int  gpio_toggle(uint32_t pin);            /* 翻转引脚状态 */

/* ---- 批量操作 ---- */
int  gpio_set_mask(uint32_t mask, uint32_t values);  /* 按位设置多个引脚 */
uint32_t gpio_get_mask(uint32_t mask);                /* 按位读取多个引脚 */

/* ---- 信息查询 ---- */
const char *gpio_get_controller_name(void);
int  gpio_get_pin_count(void);

#endif /* GPIO_H */
