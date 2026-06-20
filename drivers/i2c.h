/* i2c.h - I2C 总线控制器驱动 */
#ifndef I2C_DRIVER_H
#define I2C_DRIVER_H

#include "stdint.h"
#include "stddef.h"

/* I2C 标准速率 */
#define I2C_SPEED_STANDARD  100000   /* 100 kHz */
#define I2C_SPEED_FAST      400000   /* 400 kHz */
#define I2C_SPEED_FAST_PLUS 1000000  /* 1 MHz */
#define I2C_SPEED_HIGH      3400000  /* 3.4 MHz */

/* I2C 地址模式 */
#define I2C_ADDR_7BIT       0
#define I2C_ADDR_10BIT      1

/* I2C 常见从设备地址 */
#define I2C_ADDR_RTC_DS1307  0x68
#define I2C_ADDR_EEPROM      0x50
#define I2C_ADDR_TEMP_LM75   0x48
#define I2C_ADDR_ACCEL_MMA   0x1D
#define I2C_ADDR_GPIO_PCF8574 0x20
#define I2C_ADDR_ADC_PCA9685 0x40
#define I2C_ADDR_OLED_SSD1306 0x3C

/* I2C 事务类型 */
typedef enum {
    I2C_WRITE,
    I2C_READ
} i2c_direction_t;

/* I2C 消息 (一个事务中的一段读或写) */
typedef struct i2c_msg {
    uint16_t addr;           /* 从设备地址 (7-bit 或 10-bit) */
    uint16_t flags;          /* I2C_M_* 标志 */
    uint16_t len;            /* 数据长度 */
    uint8_t  *buf;           /* 数据缓冲区 */
} i2c_msg_t;

/* I2C 消息标志 */
#define I2C_M_TEN            0x0010   /* 10位芯片地址 */
#define I2C_M_RD             0x0001   /* 读方向 */
#define I2C_M_STOP           0x8000   /* 此消息后发STOP条件 */
#define I2C_M_NOSTART        0x4000   /* 不发START (重复START) */
#define I2C_M_REV_DIR_ADDR   0x2000   /* 反转方向/地址 */
#define I2C_M_IGNORE_NAK     0x1000   /* 忽略NAK */
#define I2C_M_NO_RD_ACK      0x0800   /* 读时不发ACK */
#define I2C_M_RECV_LEN       0x0400   /* 第一字节是长度 */

/* I2C 适配器 (硬件控制器) */
typedef struct i2c_adapter {
    const char *name;        /* 适配器名称 */
    uint32_t    nr;          /* 适配器编号 */
    uint32_t    frequency;   /* 总线频率 (Hz) */
    uint32_t    timeout_ms;  /* 超时毫秒数 */
    int         addressing;  /* I2C_ADDR_7BIT or I2C_ADDR_10BIT */

    /* 低级操作 (由具体硬件实现) */
    int  (*master_xfer)(struct i2c_adapter *adap, i2c_msg_t msgs[], uint32_t num);
    int  (*functionality)(struct i2c_adapter *adap);
    void (*set_frequency)(struct i2c_adapter *adap, uint32_t freq);
    void (*enable)(struct i2c_adapter *adap);
    void (*disable)(struct i2c_adapter *adap);

    void *priv_data;         /* 私有数据 (硬件寄存器基址等) */
} i2c_adapter_t;

/* I2C 驱动 (从设备驱动) */
typedef struct i2c_driver {
    const char *name;        /* 驱动名称 */
    uint16_t    addr_mask;   /* 地址掩码 (用于匹配多个地址) */
    uint16_t    addr;        /* 从设备地址 */
    int  (*probe)(struct i2c_adapter *adap, uint16_t addr);
    int  (*remove)(struct i2c_adapter *adap, uint16_t addr);
    int  (*read_reg)(struct i2c_adapter *adap, uint16_t addr, uint8_t reg);
    int  (*write_reg)(struct i2c_adapter *adap, uint16_t addr, uint8_t reg, uint8_t val);
    void *drv_data;
} i2c_driver_t;

/* ===== I2C 核心 API ===== */

/* 注册I2C适配器 */
int i2c_register_adapter(i2c_adapter_t *adapter);

/* 注销I2C适配器 */
int i2c_unregister_adapter(i2c_adapter_t *adapter);

/* 注册I2C驱动 */
int i2c_register_driver(i2c_driver_t *driver);

/* 注销I2C驱动 */
int i2c_unregister_driver(i2c_driver_t *driver);

/* 传输 (通用) */
int i2c_transfer(i2c_adapter_t *adap, i2c_msg_t msgs[], uint32_t num);

/* 单次主发送 (write) */
int i2c_master_send(i2c_adapter_t *adap, uint16_t addr, const uint8_t *buf, uint16_t len);

/* 单次主接收 (read) */
int i2c_master_recv(i2c_adapter_t *adap, uint16_t addr, uint8_t *buf, uint16_t len);

/* 便捷: 读单个寄存器 */
int i2c_smbus_read_byte(i2c_adapter_t *adap, uint16_t addr, uint8_t reg);

/* 便捷: 写单个寄存器 */
int i2c_smbus_write_byte(i2c_adapter_t *adap, uint16_t addr, uint8_t reg, uint8_t val);

/* 便捷: 读字 */
int i2c_smbus_read_word(i2c_adapter_t *adap, uint16_t addr, uint8_t reg);

/* 便捷: 写字 */
int i2c_smbus_write_word(i2c_adapter_t *adap, uint16_t addr, uint8_t reg, uint16_t val);

/* 便捷: 读块数据 */
int i2c_smbus_read_block(i2c_adapter_t *adap, uint16_t addr, uint8_t reg,
                          uint8_t *buf, uint16_t len);

/* 便捷: 写块数据 */
int i2c_smbus_write_block(i2c_adapter_t *adap, uint16_t addr, uint8_t reg,
                           const uint8_t *buf, uint16_t len);

/* 扫描总线 (发现所有响应的从设备) */
int i2c_scan_bus(i2c_adapter_t *adap, uint8_t found_addrs[], uint16_t max_count);

/* 获取注册的适配器数量 */
uint32_t i2c_adapter_count(void);

/* 获取适配器指针 */
i2c_adapter_t *i2c_get_adapter(uint32_t nr);

/* SMBus 兼容: Block Write-Block Read process call */
int i2c_smbus_process_call(i2c_adapter_t *adap, uint16_t addr,
                            uint8_t cmd, uint16_t data);

/* I2C 核心初始化 */
void i2c_core_init(void);

#endif /* I2C_DRIVER_H */
