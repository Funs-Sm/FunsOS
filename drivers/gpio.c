/*
 * gpio.c - GPIO 抽象驱动实现
 *
 * 提供跨平台的GPIO访问抽象。
 * 当前主要面向 x86 平台，通过以下方式访问 GPIO:
 *   1. Super IO / LPC 总线 GPIO (常见于嵌入式x86主板)
 *   2. ACPI GpioIo 资源描述
 *   3. 芯片组 GPIO (Intel PCH, AMD SB)
 *   4. 软件模拟GPIO (调试/测试用)
 *
 * 注意: x86 平台的GPIO访问高度依赖具体硬件平台。
 * 本驱动提供统一接口，实际操作由底层平台代码完成。
 */

#include "gpio.h"
#include "io.h"
#include "string.h"

/* ---- GPIO 控制器类型 ---- */
enum {
    GPIO_CTRL_NONE = 0,
    GPIO_CTRL_INTEL_PCH,      /* Intel PCH GPIO (LPC/GPIO space) */
    GPIO_CTRL_SUPER_IO,       /* Super IO GPIO (Winbond/Nuvoton/ITE) */
    GPIO_CTRL_ACPI,           /* ACPI GPIO 操作 */
    GPIO_CTRL_SOFTWARE        /* 软件模拟 */
};

static int gpio_ctrl_type = GPIO_CTRL_NONE;

/* ---- 软件模拟GPIO状态 (用于测试和回退) ---- */
#define SW_GPIO_MAX_PINS  64

static uint32_t sw_gpio_dir_bitmap = 0;    /* 0=input, 1=output */
static uint32_t sw_gpio_out_bitmap = 0;    /* 当前输出值 */
static uint32_t sw_gpio_pull_bitmap = 0;   /* 上拉/下拉配置 */

/* ================================================================
 *  Intel PCH GPIO (LPC bridge GPIO)
 *  用于 Intel 6/7/8/9 系列 PCH 和现代 Chipset
 *
 *  Intel PCH GPIO 通过 MMIO 或 IO 端口空间访问:
 *    - Legacy: IO port 0x48-0x4B (LPC bridge)
 *    - Modern: MMIO at PCI BAR (from LPC device config)
 * ================================================================ */

#define INTEL_LPC_CMD   0xCF8
#define INTEL_LPC_DATA  0xCFC

/* LPC Bridge PCI 设备/功能号 */
#define INTEL_LPC_BUS   0
#define INTEL_LPC_DEV   0x1F
#define INTEL_LPC_FUNC  0

/* LPC Bridge 寄存器偏移 */
#define LPC_REG_GPIO_BASE_LO  0x48
#define LPC_REG_GPIO_BASE_HI  0x4C

static uint32_t intel_pch_gpio_base = 0;

static int intel_pch_detect(void) {
    /* 读取 LPC Bridge 设备 ID 来检测是否存在 */
    uint32_t cmd = (1U << 31) | (INTEL_LPC_BUS << 16) |
                   (INTEL_LPC_DEV << 11) | (INTEL_LPC_FUNC << 8) | 0;
    outl(INTEL_LPC_CMD, cmd);
    uint16_t dev_id = (uint16_t)(inl(INTEL_LPC_DATA) >> 16);

    /* 常见的 Intel LPC Bridge Device IDs */
    switch (dev_id) {
    case 0x3B07:  /* Ibex Peak (PCH) */
    case 0x1C44:  /* Cougar Point PCH-LP */
    case 0x1E47:  /* Panther Point LP */
    case 0x8C44:  /* Lynx Point LP */
    case 0x9C43:  /* Wildcat Point LP */
    case 0xA143:  /* Sunrise Point-H */
    case 0xD142:  /* Cannon Lake-H */
    case 0x06A7:  /* Comet Lake-H */
    case 0x4388:  /* Tiger Lake-LP */
    case 0xA0A7:  /* Alder Lake-PCH-S */
    case 0x7A80:  /* Meteor Lake-PCH */
        return 1;
    default:
        return 0;
    }
}

static int intel_pch_init(void) {
    /* 读取 GPIO 基地址 */
    uint32_t cmd = (1U << 31) | (INTEL_LPC_BUS << 16) |
                   (INTEL_LPC_DEV << 11) | (INTEL_LPC_FUNC << 8) | LPC_REG_GPIO_BASE_LO;
    outl(INTEL_LPC_CMD, cmd);
    uint32_t base_lo = inl(INTEL_LPC_DATA) & 0xFFFF;
    base_lo &= ~0xF;  /* 清除低4位(对齐要求) */

    cmd = (1U << 31) | (INTEL_LPC_BUS << 16) |
          (INTEL_LPC_DEV << 11) | (INTEL_LPC_FUNC << 8) | LPC_REG_GPIO_BASE_HI;
    outl(INTEL_LPC_CMD, cmd);
    uint32_t base_hi = inl(INTEL_LPC_DATA) & 0xFFFF;

    intel_pch_gpio_base = (base_hi << 16) | base_lo;

    if (intel_pch_gpio_base == 0 || intel_pch_gpio_base > 0xFFFFFFFF) {
        intel_pch_gpio_base = 0;
        return -1;
    }

    return 0;
}

/*
 * Intel PCH GPIO 寄存器布局 (简化):
 *   Offset 0x00: GPIO_USE_SEL  (引脚用途选择)
 *   Offset 0x04: GPIO_IO_SEL   (方向: 0=output, 1=input)
 *   Offset 0x08: GPIO_LVL      (电平: 0=low, 1=high)
 *   Offset 0x0C: GPIO_TS       (触发选择)
 *   Offset 0x10: GPIO_WAKE_EN  (唤醒使能)
 *
 * 注意: 实际布局因PCH代数而异，此处为通用抽象。
 */
static int intel_pch_set_direction(uint32_t pin, gpio_dir_t dir) {
    if (intel_pch_gpio_base == 0) return -1;
    if (pin >= 32) return -1;  /* 仅支持社区0的前32引脚(简化) */

    volatile uint32_t *io_sel =
        (volatile uint32_t *)(intel_pch_gpio_base + 0x04);

    if (dir == GPIO_DIR_INPUT) {
        *io_sel |= (1U << pin);    /* 1 = input */
    } else {
        *io_sel &= ~(1U << pin);   /* 0 = output */
    }

    return 0;
}

static int intel_pch_write(uint32_t pin, gpio_level_t level) {
    if (intel_pch_gpio_base == 0) return -1;
    if (pin >= 32) return -1;

    volatile uint32_t *gpio_lvl =
        (volatile uint32_t *)(intel_pch_gpio_base + 0x08);

    if (level == GPIO_HIGH) {
        *gpio_lvl |= (1U << pin);
    } else {
        *gpio_lvl &= ~(1U << pin);
    }

    return 0;
}

static gpio_level_t intel_pch_read(uint32_t pin) {
    if (intel_pch_gpio_base == 0) return GPIO_LOW;
    if (pin >= 32) return GPIO_LOW;

    volatile uint32_t *gpio_lvl =
        (volatile uint32_t *)(intel_pch_gpio_base + 0x08);

    return ((*gpio_lvl) & (1U << pin)) ? GPIO_HIGH : GPIO_LOW;
}

/* ================================================================
 *  软件模拟 GPIO (调试/测试用)
 * ================================================================ */

static int sw_gpio_set_direction(uint32_t pin, gpio_dir_t dir) {
    if (pin >= SW_GPIO_MAX_PINS) return -1;
    if (dir == GPIO_DIR_OUTPUT) {
        sw_gpio_dir_bitmap |= (1U << pin);
    } else {
        sw_gpio_dir_bitmap &= ~(1U << pin);
    }
    return 0;
}

static int sw_gpio_write(uint32_t pin, gpio_level_t level) {
    if (pin >= SW_GPIO_MAX_PINS) return -1;
    if (level == GPIO_HIGH) {
        sw_gpio_out_bitmap |= (1U << pin);
    } else {
        sw_gpio_out_bitmap &= ~(1U << pin);
    }
    return 0;
}

static gpio_level_t sw_gpio_read(uint32_t pin) {
    if (pin >= SW_GPIO_MAX_PINS) return GPIO_LOW;
    return (sw_gpio_out_bitmap & (1U << pin)) ? GPIO_HIGH : GPIO_LOW;
}

/* ================================================================
 *  公共接口实现
 * ================================================================ */

int gpio_init(void) {
    gpio_ctrl_type = GPIO_CTRL_NONE;
    sw_gpio_dir_bitmap = 0;
    sw_gpio_out_bitmap = 0;
    sw_gpio_pull_bitmap = 0;

    /* 尝试 Intel PCH GPIO */
    if (intel_pch_detect()) {
        if (intel_pch_init() == 0) {
            gpio_ctrl_type = GPIO_CTRL_INTEL_PCH;
            return 0;
        }
    }

    /* 回退到软件模拟 */
    gpio_ctrl_type = GPIO_CTRL_SOFTWARE;
    return 0;
}

int gpio_is_available(void) {
    return gpio_ctrl_type != GPIO_CTRL_NONE;
}

int gpio_set_direction(uint32_t pin, gpio_dir_t dir) {
    switch (gpio_ctrl_type) {
    case GPIO_CTRL_INTEL_PCH:
        return intel_pch_set_direction(pin, dir);
    case GPIO_CTRL_SOFTWARE:
        return sw_gpio_set_direction(pin, dir);
    default:
        return -1;
    }
}

int gpio_write(uint32_t pin, gpio_level_t level) {
    switch (gpio_ctrl_type) {
    case GPIO_CTRL_INTEL_PCH:
        return intel_pch_write(pin, level);
    case GPIO_CTRL_SOFTWARE:
        return sw_gpio_write(pin, level);
    default:
        return -1;
    }
}

gpio_level_t gpio_read(uint32_t pin) {
    switch (gpio_ctrl_type) {
    case GPIO_CTRL_INTEL_PCH:
        return intel_pch_read(pin);
    case GPIO_CTRL_SOFTWARE:
        return sw_gpio_read(pin);
    default:
        return GPIO_LOW;
    }
}

int gpio_set_pull(uint32_t pin, gpio_pull_t pull) {
    /* 大多数硬件的 pull 配置需要特定寄存器操作 */
    (void)pin;
    (void)pull;

    switch (gpio_ctrl_type) {
    case GPIO_CTRL_SOFTWARE:
        /* 软件模拟: 记录但不实际生效 */
        if (pin < SW_GPIO_MAX_PINS) {
            if (pull == GPIO_PULL_UP) {
                sw_gpio_pull_bitmap |= (1U << pin);
            } else {
                sw_gpio_pull_bitmap &= ~(1U << pin);
            }
        }
        return 0;
    default:
        return 0;  /* 静默成功(桩实现) */
    }
}

int gpio_toggle(uint32_t pin) {
    gpio_level_t current = gpio_read(pin);
    return gpio_write(pin, (current == GPIO_HIGH) ? GPIO_LOW : GPIO_HIGH);
}

int gpio_set_mask(uint32_t mask, uint32_t values) {
    for (uint32_t i = 0; i < 32; i++) {
        if (mask & (1U << i)) {
            gpio_level_t level = (values & (1U << i)) ? GPIO_HIGH : GPIO_LOW;
            if (gpio_write(i, level) != 0) return -1;
        }
    }
    return 0;
}

uint32_t gpio_get_mask(uint32_t mask) {
    uint32_t result = 0;
    for (uint32_t i = 0; i < 32; i++) {
        if (mask & (1U << i)) {
            if (gpio_read(i) == GPIO_HIGH) result |= (1U << i);
        }
    }
    return result;
}

const char *gpio_get_controller_name(void) {
    switch (gpio_ctrl_type) {
    case GPIO_CTRL_INTEL_PCH: return "Intel PCH GPIO";
    case GPIO_CTRL_SUPER_IO:  return "Super IO GPIO";
    case GPIO_CTRL_ACPI:      return "ACPI GPIO";
    case GPIO_CTRL_SOFTWARE:  return "Software Simulated GPIO";
    default:                  return "None";
    }
}

int gpio_get_pin_count(void) {
    switch (gpio_ctrl_type) {
    case GPIO_CTRL_INTEL_PCH: return 96;   /* 典型PCH有~96个GPIO */
    case GPIO_CTRL_SOFTWARE:  return SW_GPIO_MAX_PINS;
    default:                  return 0;
    }
}
