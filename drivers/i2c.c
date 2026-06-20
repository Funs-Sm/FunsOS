/*
 * i2c.c - I2C 总线控制器驱动实现
 *
 * 架构概述:
 * - I2C Core: 适配器/驱动注册、匹配、扫描等核心逻辑
 * - Software Bit-Bang Adapter: 使用 GPIO 模拟 I2C 时序的默认适配器
 *   (适用于没有硬件 I2C 控制器的系统, 或作为调试/备用方案)
 *
 * I2C 总线协议要点:
 * - 两根信号线: SDA (数据) 和 SCL (时钟), 均为开漏输出 + 上拉电阻
 * - START 条件: SCL 高电平时 SDA 从高到低跳变
 * - STOP 条件: SCL 高电平时 SDA 从低到高跳变
 * - 数据传输: SCL 低电平期间 SDA 变化, 高电平期间采样
 * - ACK/NACK: 接收方在每字节后拉低 SDA 表示 ACK
 * - 7-bit 地址: 首字节 = [7bit地址][R/W], R/W=0 写, R/W=1 读
 *
 * 软件位时序 (Standard Mode, 100kHz):
 *   半周期 ~5us, 通过延时循环实现精确时序控制
 */

#include "i2c.h"
#include "klog.h"
#include "string.h"

/* ================================================================
 * 全局状态和配置
 * ================================================================ */

/* ---- 最大支持的适配器数量 ---- */
#define I2C_MAX_ADAPTERS     8
#define I2C_MAX_DRIVERS      16

/* ---- 适配器注册表 ---- */
static i2c_adapter_t *g_adapters[I2C_MAX_ADAPTERS];
static uint32_t       g_adapter_count = 0;

/* ---- 驱动注册表 ---- */
static i2c_driver_t  *g_drivers[I2C_MAX_DRIVERS];
static uint32_t       g_driver_count = 0;

/* ---- 软件位模拟适配器实例 ---- */
static i2c_adapter_t g_bitbang_adapter;

/* ---- 位模拟 GPIO 配置 (可通过 priv_data 或全局变量配置) ---- */
struct i2c_bitbang_gpio {
    volatile uint32_t *sda_reg;   /* SDA 数据寄存器指针 */
    volatile uint32_t *scl_reg;   /* SCL 数据寄存器指针 */
    volatile uint32_t *ddr_reg;   /* 方向寄存器指针 (1=output, 0=input) */
    uint32_t sda_bit;             /* SDA 引脚位掩码 */
    uint32_t scl_bit;             /* SCL 引脚位掩码 */
};

static struct i2c_bitbang_gpio g_bb_gpio;

/* ---- 延时参数 (根据目标 CPU 频率调整) ----
 * Standard Mode (100kHz): T_low >= 4.7us, T_high >= 4.0us
 * Fast Mode (400kHz):     T_low >= 1.3us, T_high >= 0.6us
 * 这里使用简单的循环计数延时, 实际值需要校准 */
static uint32_t g_bb_delay_us = 5;  /* 默认标准模式半周期 */

/* ---- 延时辅助函数 ---- */
static void bitbang_delay(uint32_t us)
{
    /* 简单的忙等待延时
     * 假设每个循环约 50ns (20MHz CPU), 则 1us ≈ 20 循环 */
    volatile uint32_t count = us * 20;
    while (count--) {
        __asm__ __volatile__("nop");
    }
}

/*
 * ================================================================
 * 软件位模拟 I2C 底层操作
 * ================================================================
 */

/* 设置 SDA 方向: output=1 输出模式, output=0 输入模式 */
static void sda_set_dir(int output)
{
    if (output) {
        *g_bb_gpio.ddr_reg |= g_bb_gpio.sda_bit;
    } else {
        *g_bb_gpio.ddr_reg &= ~g_bb_gpio.sda_bit;
    }
}

/* 设置 SCL 方向 */
static void scl_set_dir(int output)
{
    if (output) {
        *g_bb_gpio.ddr_reg |= g_bb_gpio.scl_bit;
    } else {
        *g_bb_gpio.ddr_reg &= ~g_bb_gpio.scl_bit;
    }
}

/* 写 SDA 电平 (必须在输出模式下调用) */
static void sda_set(int level)
{
    if (level) {
        /* 释放总线 (开漏: 输出高实际是释放, 由上拉拉高) */
        *g_bb_gpio.ddr_reg &= ~g_bb_gpio.sda_bit;  /* 切换到输入 = 释放 */
    } else {
        /* 拉低 */
        *g_bb_gpio.ddr_reg |= g_bb_gpio.sda_bit;     /* 切换到输出 */
        *g_bb_gpio.sda_reg &= ~g_bb_gpio.sda_bit;     /* 输出低 */
    }
}

/* 写 SCL 电平 */
static void scl_set(int level)
{
    if (level) {
        *g_bb_gpio.ddr_reg &= ~g_bb_gpio.scl_bit;  /* 释放时钟线 */
    } else {
        *g_bb_gpio.ddr_reg |= g_bb_gpio.scl_bit;
        *g_bb_gpio.scl_reg &= ~g_bb_gpio.scl_bit;
    }
}

/* 读 SDA 当前电平 */
static int sda_read(void)
{
    return (*g_bb_gpio.sda_reg & g_bb_gpio.sda_bit) ? 1 : 0;
}

/* 读 SCL 当前电平 */
static int scl_read(void)
{
    return (*g_bb_gpio.scl_reg & g_bb_gpio.scl_bit) ? 1 : 0;
}

/*
 * 发送 I2C START 条件。
 *
 * 时序:
 * 1. SDA 和 SCL 都释放 (高)
 * 2. SDA 先拉低 (在 SCL 高期间)
 * 3. 然后 SCL 拉低
 *
 * 这标志着一次传输的开始。
 */
static void bitbang_start(void)
{
    sda_set_dir(1);  /* SDA 输出 */
    scl_set_dir(1);  /* SCL 输出 */

    /* 初始状态: 两条线都高 */
    sda_set(1);
    scl_set(1);
    bitbang_delay(g_bb_delay_us);

    /* START: SDA 在 SCL 高期间拉低 */
    sda_set(0);      /* SDA -> LOW */
    bitbang_delay(g_bb_delay_us);

    /* SCL 也拉低, 进入数据传输阶段 */
    scl_set(0);      /* SCL -> LOW */
    bitbang_delay(g_bb_delay_us);
}

/*
 * 发送 I2C STOP 条件。
 *
 * 时序:
 * 1. SDA 先确保低
 * 2. SCL 释放变高
 * 3. 在 SCL 高期间 SDA 释放变高
 */
static void bitbang_stop(void)
{
    sda_set_dir(1);
    scl_set_dir(1);

    /* 确保 SDA 为低 */
    sda_set(0);
    bitbang_delay(g_bb_delay_us);

    /* SCL 变高 */
    scl_set(1);
    bitbang_delay(g_bb_delay_us);

    /* STOP: SDA 在 SCL 高期间变高 */
    sda_set(1);      /* SDA -> HIGH (释放) */
    bitbang_delay(g_bb_delay_us);
}

/*
 * 发送一个字节数据 (MSB first)。
 *
 * 每发送一位:
 * 1. SCL 低电平期间设置 SDA
 * 2. SCL 高电平保持 (设备采样)
 * 3. SCL 回低准备下一位
 *
 * 返回接收到的 ACK 位: 0=ACK, 1=NACK
 */
static int bitbang_write_byte(uint8_t data)
{
    int ack;

    for (int i = 7; i >= 0; i--) {
        /* SCL 低电平期间设置数据 */
        sda_set((data >> i) & 0x01);
        bitbang_delay(g_bb_delay_us);

        /* SCL 脉冲: 高电平让设备采样 */
        scl_set(1);
        bitbang_delay(g_bb_delay_us);
        scl_set(0);
        bitbang_delay(g_bb_delay_us);
    }

    /* 读取 ACK: 释放 SDA, 发一个 SCL 脉冲读 ACK */
    sda_set_dir(0);  /* SDA 切换到输入 (释放) */
    bitbang_delay(g_bb_delay_us);

    scl_set(1);
    bitbang_delay(g_bb_delay_us);

    ack = sda_read();  /* 0=ACK, 1=NACK */

    scl_set(0);
    bitbang_delay(g_bb_delay_us);

    sda_set_dir(1);  /* 恢复 SDA 输出 */

    return ack ? 1 : 0;  /* 返回 NACK 标志 */
}

/*
 * 接收一个字节数据 (MSB first)。
 *
 * 每接收一位:
 * 1. SCL 高电平时读取 SDA
 * 2. SCL 回低准备下一位
 *
 * ack_nack: 最后发送 ACK(0)/NACK(1)
 */
static uint8_t bitbang_read_byte(int ack_nack)
{
    uint8_t data = 0;

    sda_set_dir(0);  /* SDA 输入模式 (由从机驱动) */

    for (int i = 7; i >= 0; i--) {
        bitbang_delay(g_bb_delay_us);
        scl_set(1);  /* 时钟高, 设备放数据到 SDA */
        bitbang_delay(g_bb_delay_us);

        data |= (uint8_t)(sda_read() << i);

        scl_set(0);
        bitbang_delay(g_bb_delay_us);
    }

    /* 发送 ACK/NACK */
    sda_set_dir(1);  /* SDA 输出模式 */
    sda_set(ack_nack ? 1 : 0);  /* NACK=1(释放), ACK=0(拉低) */
    bitbang_delay(g_bb_delay_us);

    scl_set(1);
    bitbang_delay(g_bb_delay_us);
    scl_set(0);
    bitbang_delay(g_bb_delay_us);

    return data;
}


/*
 * ================================================================
 * 软件位模拟适配器接口函数
 * ================================================================
 */

/*
 * 位模拟主传输函数。
 *
 * 处理一个或多个连续的 I2C 消息。
 * 支持组合事务: [写地址+寄存器][读数据] 的典型模式。
 *
 * msgs: 消息数组
 * num: 消息数量
 * 返回: 成功处理的消息数, 或负数表示错误
 */
static int bitbang_master_xfer(i2c_adapter_t *adap, i2c_msg_t msgs[], uint32_t num)
{
    (void)adap;

    if (!msgs || num == 0) return 0;

    int processed = 0;

    for (uint32_t i = 0; i < num; i++) {
        i2c_msg_t *msg = &msgs[i];

        /* 第一条消息前发 START, 后续消息视 NOSTART 标志决定 */
        if (i == 0 || !(msg->flags & I2C_M_NOSTART)) {
            if (i != 0) {
                /* 重复 START (不先 STOP) */
                sda_set(1);
                bitbang_delay(g_bb_delay_us);
                scl_set(1);
                bitbang_delay(g_bb_delay_us);
                sda_set(0);
                bitbang_delay(g_bb_delay_us);
                scl_set(0);
                bitbang_delay(g_bb_delay_us);
            } else {
                bitbang_start();
            }
        }

        /* 发送地址字节: [7bit addr][R/W] */
        uint8_t addr_byte = (uint8_t)((msg->addr & 0x7F) << 1);
        if (msg->flags & I2C_M_RD) {
            addr_byte |= 0x01;  /* Read */
        }

        int nack = bitbang_write_byte(addr_byte);
        if (nack && !(msg->flags & I2C_M_IGNORE_NAK)) {
            /* 地址未被应答, 设备不存在 */
            bitbang_stop();
            return processed > 0 ? (int)processed : -1;
        }

        /* 处理数据部分 */
        if (msg->flags & I2C_M_RD) {
            /* 读操作: 从设备接收数据 */
            for (uint16_t j = 0; j < msg->len && msg->buf; j++) {
                int last = (j == msg->len - 1) ? 1 : 0;
                if (msg->flags & I2C_M_STOP && j == msg->len - 1) {
                    last = 1;  /* 最后字节发 NACK */
                }
                msg->buf[j] = bitbang_read_byte(last);
            }
        } else {
            /* 写操作: 向设备发送数据 */
            for (uint16_t j = 0; j < msg->len && msg->buf; j++) {
                nack = bitbang_write_byte(msg->buf[j]);
                if (nack && !(msg->flags & I2C_M_IGNORE_NAK)) {
                    bitbang_stop();
                    return processed > 0 ? (int)processed : -1;
                }
            }
        }

        /* 如果设置了 STOP 标志, 或者这是最后一条消息, 发送 STOP */
        if ((msg->flags & I2C_M_STOP) || (i == num - 1)) {
            bitbang_stop();
        }

        processed++;
    }

    return processed;
}

/*
 * 返回位模拟适配器的功能标志。
 * 软件模拟支持基本功能, 不支持 10-bit 地址和高频模式。
 */
static int bitbang_functionality(i2c_adapter_t *adap)
{
    (void)adap;
    /* 支持: I2C (非 SMBus), 7-bit 寻址, 标准/快速模式 */
    return 0x00000001 |  /* I2C */
           0x00000002 |  /* 10-bit address (模拟支持但不可靠) */
           0x00000004 |  /* SMBUS emulation */
           0x00000040;   /* Protocol mangling */
}

/*
 * 设置位模拟频率。
 * 调整延时参数以改变总线速率。
 */
static void bitbang_set_frequency(i2c_adapter_t *adap, uint32_t freq)
{
    (void)adap;
    /* 计算半周期延时 (微秒)
     * freq = 1 / (2 * delay_us) => delay_us = 1 / (2 * freq) */
    if (freq > 0) {
        g_bb_delay_us = 500000 / freq;  /* 近似计算 */
        if (g_bb_delay_us < 1) g_bb_delay_us = 1;
        adap->frequency = freq;
    }
}

/*
 * 使能位模拟适配器。
 * 将 SDA/SCL 设置为空闲状态 (高电平/输入模式)。
 */
static void bitbang_enable(i2c_adapter_t *adap)
{
    (void)adap;
    sda_set_dir(0);  /* SDA 输入 (释放) */
    scl_set_dir(0);  /* SCL 输入 (释放) */
    klog_info("i2c: Bit-bang adapter enabled");
}

/*
 * 禁用位模拟适配器。
 */
static void bitbang_disable(i2c_adapter_t *adap)
{
    (void)adap;
    sda_set_dir(0);
    scl_set_dir(0);
    klog_info("i2c: Bit-bang adapter disabled");
}


/*
 * ================================================================
 * I2C 核心 API 实现
 * ================================================================
 */

/*
 * I2C 子系统初始化。
 *
 * 注册默认的软件位模拟适配器,
 * 清空所有注册表。
 */
void i2c_core_init(void)
{
    memset(g_adapters, 0, sizeof(g_adapters));
    memset(g_drivers, 0, sizeof(g_drivers));
    g_adapter_count = 0;
    g_driver_count = 0;

    /* 初始化并注册默认的位模拟适配器 */
    memset(&g_bitbang_adapter, 0, sizeof(i2c_adapter_t));
    g_bitbang_adapter.name       = "i2c-bitbang";
    g_bitbang_adapter.nr         = 0;
    g_bitbang_adapter.frequency  = I2C_SPEED_STANDARD;
    g_bitbang_adapter.timeout_ms = 100;
    g_bitbang_adapter.addressing = I2C_ADDR_7BIT;
    g_bitbang_adapter.master_xfer    = bitbang_master_xfer;
    g_bitbang_adapter.functionality  = bitbang_functionality;
    g_bitbang_adapter.set_frequency  = bitbang_set_frequency;
    g_bitbang_adapter.enable         = bitbang_enable;
    g_bitbang_adapter.disable        = bitbang_disable;
    g_bitbang_adapter.priv_data      = &g_bb_gpio;

    /* 自动注册默认适配器 */
    i2c_register_adapter(&g_bitbang_adapter);

    klog_info("i2c: Core initialized, default bit-bang adapter registered");
}

/*
 * 注册 I2C 适配器到核心。
 *
 * 将适配器添加到全局注册表,
 * 如果适配器有 enable 函数则自动调用。
 */
int i2c_register_adapter(i2c_adapter_t *adapter)
{
    if (!adapter || g_adapter_count >= I2C_MAX_ADAPTERS) {
        klog_err("i2c: Failed to register adapter (full or NULL)");
        return -1;
    }

    /* 检查是否已存在相同编号的适配器 */
    for (uint32_t i = 0; i < g_adapter_count; i++) {
        if (g_adapters[i] && g_adapters[i]->nr == adapter->nr) {
            klog_warn("i2c: Adapter %u already registered", adapter->nr);
            return -1;
        }
    }

    g_adapters[g_adapter_count++] = adapter;

    /* 使能适配器 */
    if (adapter->enable) {
        adapter->enable(adapter);
    }

    klog_info("i2c: Adapter '%s' registered as bus %u", adapter->name, adapter->nr);
    return 0;
}

/*
 * 注销 I2C 适配器。
 */
int i2c_unregister_adapter(i2c_adapter_t *adapter)
{
    if (!adapter) return -1;

    for (uint32_t i = 0; i < g_adapter_count; i++) {
        if (g_adapters[i] == adapter) {
            if (adapter->disable) {
                adapter->disable(adapter);
            }
            /* 移除 (将后续元素前移) */
            for (uint32_t j = i; j < g_adapter_count - 1; j++) {
                g_adapters[j] = g_adapters[j + 1];
            }
            g_adapters[--g_adapter_count] = (i2c_adapter_t *)0;
            klog_info("i2c: Adapter '%s' unregistered", adapter->name);
            return 0;
        }
    }

    return -1;
}

/*
 * 注册 I2C 从设备驱动。
 */
int i2c_register_driver(i2c_driver_t *driver)
{
    if (!driver || g_driver_count >= I2C_MAX_DRIVERS) {
        return -1;
    }

    g_drivers[g_driver_count++] = driver;

    /* 尝试在所有已注册的适配器上探测该驱动 */
    for (uint32_t i = 0; i < g_adapter_count; i++) {
        if (g_adapters[i] && driver->probe) {
            driver->probe(g_adapters[i], driver->addr);
        }
    }

    klog_info("i2c: Driver '%s' registered for address 0x%02X",
              driver->name, driver->addr);
    return 0;
}

/*
 * 注销 I2C 从设备驱动。
 */
int i2c_unregister_driver(i2c_driver_t *driver)
{
    if (!driver) return -1;

    for (uint32_t i = 0; i < g_driver_count; i++) {
        if (g_drivers[i] == driver) {
            /* 对所有适配器调用 remove */
            for (uint32_t j = 0; j < g_adapter_count; j++) {
                if (g_adapters[j] && driver->remove) {
                    driver->remove(g_adapters[j], driver->addr);
                }
            }
            for (uint32_t k = i; k < g_driver_count - 1; k++) {
                g_drivers[k] = g_drivers[k + 1];
            }
            g_drivers[--g_driver_count] = (i2c_driver_t *)0;
            return 0;
        }
    }

    return -1;
}

/*
 * 通用 I2C 传输函数。
 *
 * 将一组 I2C 消息提交给指定适配器处理。
 * 这是所有高级操作的底层基础。
 */
int i2c_transfer(i2c_adapter_t *adap, i2c_msg_t msgs[], uint32_t num)
{
    if (!adap || !adap->master_xfer || !msgs || num == 0) return -1;

    return adap->master_xfer(adap, msgs, num);
}

/*
 * 单次主发送 (Master Transmit)。
 *
 * 向指定从设备地址发送一帧数据。
 * 内部构造一条 WRITE 消息并调用 i2c_transfer。
 */
int i2c_master_send(i2c_adapter_t *adap, uint16_t addr, const uint8_t *buf, uint16_t len)
{
    if (!adap || !buf || len == 0) return -1;

    i2c_msg_t msg;
    msg.addr  = addr;
    msg.flags = I2C_M_STOP;  /* 发送后产生 STOP */
    msg.len   = len;
    msg.buf   = (uint8_t *)buf;

    int ret = i2c_transfer(adap, &msg, 1);
    return (ret == 1) ? (int)len : ret;
}

/*
 * 单次主接收 (Master Receive)。
 *
 * 从指定从设备地址接收一帧数据。
 */
int i2c_master_recv(i2c_adapter_t *adap, uint16_t addr, uint8_t *buf, uint16_t len)
{
    if (!adap || !buf || len == 0) return -1;

    i2c_msg_t msg;
    msg.addr  = addr;
    msg.flags = I2C_M_RD | I2C_M_STOP;
    msg.len   = len;
    msg.buf   = buf;

    int ret = i2c_transfer(adap, &msg, 1);
    return (ret == 1) ? (int)len : ret;
}

/*
 * SMBus 兼容: 读单个字节。
 *
 * 典型流程: START -> [Addr+Write] -> [Reg] -> ReSTART
 *          -> [Addr+Read] -> [Data] -> STOP
 *
 * 即: 先写寄存器地址, 再读一字节数据。
 */
int i2c_smbus_read_byte(i2c_adapter_t *adap, uint16_t addr, uint8_t reg)
{
    if (!adap) return -1;

    uint8_t data;
    i2c_msg_t msgs[2];

    /* 消息 0: 写寄存器地址 (无 STOP) */
    msgs[0].addr  = addr;
    msgs[0].flags = 0;              /* Write, no STOP */
    msgs[0].len   = 1;
    msgs[0].buf   = &reg;

    /* 消息 1: 读一字节 (带 STOP) */
    msgs[1].addr  = addr;
    msgs[1].flags = I2C_M_RD | I2C_M_NOSTART | I2C_M_STOP;
    msgs[1].len   = 1;
    msgs[1].buf   = &data;

    int ret = i2c_transfer(adap, msgs, 2);
    return (ret == 2) ? (int)data : -1;
}

/*
 * SMBus 兼容: 写单个字节。
 *
 * 流程: START -> [Addr+Write] -> [Reg] -> [Data] -> STOP
 */
int i2c_smbus_write_byte(i2c_adapter_t *adap, uint16_t addr, uint8_t reg, uint8_t val)
{
    if (!adap) return -1;

    uint8_t buf[2];
    buf[0] = reg;
    buf[1] = val;

    i2c_msg_t msg;
    msg.addr  = addr;
    msg.flags = I2C_M_STOP;
    msg.len   = 2;
    msg.buf   = buf;

    int ret = i2c_transfer(adap, &msg, 1);
    return (ret == 1) ? 0 : -1;
}

/*
 * SMBus 兼容: 读字 (16-bit little-endian)。
 *
 * 连续读取两个字节, 低字节在前。
 */
int i2c_smbus_read_word(i2c_adapter_t *adap, uint16_t addr, uint8_t reg)
{
    if (!adap) return -1;

    uint8_t data[2];
    i2c_msg_t msgs[2];

    msgs[0].addr  = addr;
    msgs[0].flags = 0;
    msgs[0].len   = 1;
    msgs[0].buf   = &reg;

    msgs[1].addr  = addr;
    msgs[1].flags = I2C_M_RD | I2C_M_NOSTART | I2C_M_STOP;
    msgs[1].len   = 2;
    msgs[1].buf   = data;

    int ret = i2c_transfer(adap, msgs, 2);
    if (ret != 2) return -1;

    /* Little-endian: data[0]=low, data[1]=high */
    return (int)data[0] | ((int)data[1] << 8);
}

/*
 * SMBus 兼容: 写字 (16-bit little-endian)。
 */
int i2c_smbus_write_word(i2c_adapter_t *adap, uint16_t addr, uint8_t reg, uint16_t val)
{
    if (!adap) return -1;

    uint8_t buf[3];
    buf[0] = reg;
    buf[1] = (uint8_t)(val & 0xFF);        /* Low byte */
    buf[2] = (uint8_t)((val >> 8) & 0xFF);  /* High byte */

    i2c_msg_t msg;
    msg.addr  = addr;
    msg.flags = I2C_M_STOP;
    msg.len   = 3;
    msg.buf   = buf;

    int ret = i2c_transfer(adap, &msg, 1);
    return (ret == 1) ? 0 : -1;
}

/*
 * SMBus 兼容: 读块数据。
 *
 * 先写寄存器地址, 然后读取一块数据。
 * 第一个返回的字节通常包含块长度 (I2C_M_RECV_LEN 模式),
 * 但这里使用固定长度的读取方式。
 */
int i2c_smbus_read_block(i2c_adapter_t *adap, uint16_t addr, uint8_t reg,
                          uint8_t *buf, uint16_t len)
{
    if (!adap || !buf || len == 0) return -1;

    i2c_msg_t msgs[2];

    msgs[0].addr  = addr;
    msgs[0].flags = 0;
    msgs[0].len   = 1;
    msgs[0].buf   = &reg;

    msgs[1].addr  = addr;
    msgs[1].flags = I2C_M_RD | I2C_M_NOSTART | I2C_M_STOP;
    msgs[1].len   = len;
    msgs[1].buf   = buf;

    int ret = i2c_transfer(adap, msgs, 2);
    return (ret == 2) ? (int)len : -1;
}

/*
 * SMBus 兼容: 写块数据。
 *
 * 第一个字节为起始寄存器地址, 后续为数据块。
 */
int i2c_smbus_write_block(i2c_adapter_t *adap, uint16_t addr, uint8_t reg,
                           const uint8_t *buf, uint16_t len)
{
    if (!adap || !buf || len == 0 || len > 255) return -1;

    /* 分配临时缓冲区: [reg] + [data...] */
    static uint8_t block_buf[256];  /* 最大 1 字节 reg + 255 字节数据 */
    block_buf[0] = reg;
    for (uint16_t i = 0; i < len; i++) {
        block_buf[1 + i] = buf[i];
    }

    i2c_msg_t msg;
    msg.addr  = addr;
    msg.flags = I2C_M_STOP;
    msg.len   = 1 + len;
    msg.buf   = block_buf;

    int ret = i2c_transfer(adap, &msg, 1);
    return (ret == 1) ? 0 : -1;
}

/*
 * 扫描 I2C 总线, 发现所有响应的从设备。
 *
 * 遍历 7-bit 地址空间 (0x03 ~ 0x77, 排除保留地址),
 * 对每个地址尝试发送一个空写操作 (只发地址, 无数据),
 * 检查是否收到 ACK 应答。
 *
 * found_addrs: 输出缓冲区, 存储发现的设备地址
 * max_count: 缓冲区最大容量
 * 返回: 发现的设备数量
 */
int i2c_scan_bus(i2c_adapter_t *adap, uint8_t found_addrs[], uint16_t max_count)
{
    if (!adap || !found_addrs || max_count == 0) return 0;

    uint16_t found = 0;

    klog_info("i2c: Scanning bus %u (%s)...", adap->nr, adap->name);

    /*
     * I2C 7-bit 地址范围:
     * 0x00-0x02: 保留 (General Call, CBUS, 保留)
     * 0x03-0x07: 保留
     * 0x08-0x77: 可用地址范围 (120 个地址)
     * 0x78-0x7F: 10-bit 地址预留
     *
     * 实际扫描 0x08 到 0x77
     */
    for (uint8_t addr = 0x08; addr <= 0x77 && found < max_count; addr++) {
        /* 尝试发送一个零长度写 (只发地址字节, 检查 ACK) */
        i2c_msg_t msg;
        msg.addr  = addr;
        msg.flags = I2C_M_STOP | I2C_M_IGNORE_NAK;  /* 忽略 NACK 错误 */
        msg.len   = 0;
        msg.buf   = (uint8_t *)0;

        int ret = i2c_transfer(adap, &msg, 1);

        if (ret == 1) {
            /* 设备响应了 ACK */
            found_addrs[found++] = addr;
            klog_info("i2c:  Device found at 0x%02X on bus %u", addr, adap->nr);
        }
    }

    klog_info("i2c: Scan complete, %u device(s) found on bus %u", found, adap->nr);
    return (int)found;
}

/*
 * 获取当前注册的适配器总数。
 */
uint32_t i2c_adapter_count(void)
{
    return g_adapter_count;
}

/*
 * 按编号获取适配器指针。
 */
i2c_adapter_t *i2c_get_adapter(uint32_t nr)
{
    for (uint32_t i = 0; i < g_adapter_count; i++) {
        if (g_adapters[i] && g_adapters[i]->nr == nr) {
            return g_adapters[i];
        }
    }
    return (i2c_adapter_t *)0;
}

/*
 * SMBus Process Call (Block Write - Block Read)。
 *
 * 这是一个组合操作:
 * 1. 向 cmd 寄存器写入 16-bit 数据 (Block Write)
 * 2. 从同一寄存器读取 16-bit 结果 (Block Read)
 *
 * 常用于 EEPROM 等设备的原子读写操作。
 */
int i2c_smbus_process_call(i2c_adapter_t *adap, uint16_t addr,
                            uint8_t cmd, uint16_t data)
{
    if (!adap) return -1;

    /* Phase 1: 写命令和数据 */
    uint8_t wbuf[3];
    wbuf[0] = cmd;
    wbuf[1] = (uint8_t)(data & 0xFF);
    wbuf[2] = (uint8_t)((data >> 8) & 0xFF);

    i2c_msg_t msgs[2];

    msgs[0].addr  = addr;
    msgs[0].flags = 0;              /* Write, no stop */
    msgs[0].len   = 3;
    msgs[0].buf   = wbuf;

    /* Phase 2: 读回结果 */
    uint8_t rbuf[2];
    msgs[1].addr  = addr;
    msgs[1].flags = I2C_M_RD | I2C_M_NOSTART | I2C_M_STOP;
    msgs[1].len   = 2;
    msgs[1].buf   = rbuf;

    int ret = i2c_transfer(adap, msgs, 2);
    if (ret != 2) return -1;

    return (int)rbuf[0] | ((int)rbuf[1] << 8);
}
