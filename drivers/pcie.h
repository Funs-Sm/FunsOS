/* pcie.h - PCI Express 总线驱动 */
#ifndef PCIE_DRIVER_H
#define PCIE_DRIVER_H

#include "stdint.h"
#include "stddef.h"

/* PCIe 配置空间寄存器偏移 */
#define PCIE_DEV_VENDOR     0x00
#define PCIE_COMMAND_STATUS 0x04
#define PCIE_CLASS_REVISION 0x08
#define PCIE_BIST_HDR_CACHE 0x0C
#define PCIE_BAR0           0x10
#define PCIE_BAR1           0x14
#define PCIE_BAR2           0x18
#define PCIE_BAR3           0x1C
#define PCIE_BAR4           0x20
#define PCIE_BAR5           0x24
#define PCIE_CIS_PTR        0x28
#define PCIE_SUBSYS_ID      0x2C
#define PCIE_EXP_ROM        0x30
#define PCIE_CAP_PTR        0x34
#define PCIE_INT_LINE       0x3C
#define PCIE_INT_PIN        0x3D

/* PCIe 扩展能力 (Capability IDs) */
#define PCIE_CAP_ID_PM      0x01    /* Power Management */
#define PCIE_CAP_ID_MSI     0x05    /* Message Signaled Interrupts */
#define PCIE_CAP_ID_MSIX    0x11    /* MSI-X */
#define PCIE_CAP_ID_PCIE    0x10    /* PCI Express */

/* PCIe 设备类型 */
#define PCIE_TYPE_ENDPOINT      0
#define PCIE_TYPE_LEGACY_EP     1
#define PCIE_TYPE_ROOT_PORT     4
#define PCIE_TYPE_UPSTREAM      5
#define PCIE_TYPE_DOWNSTREAM    6
#define PCIE_TYPE_PCI_BRIDGE    7
#define PCIE_TYPE_PCIE_BRIDGE   8
#define PCIE_TYPE_RC_INTEP      9

/* MSI 控制位 */
#define MSI_ENABLE          0x0001
#define MSI_64BIT_CAP       0x0080
#define MSI_PERVEC_MASK     0x0100
#define MSIX_ENABLE         0x8000
#define MSIX_FUNC_MASK      0x4000

/* PCIe 设备描述 */
typedef struct pcie_device {
    uint8_t  bus;
    uint8_t  dev;
    uint8_t  func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  revision;
    uint16_t command;
    uint16_t status;
    uint32_t bar[6];           /* Base Address Registers */
    uint32_t bar_flags[6];     /* BAR 类型标志 (I/O vs Memory) */
    uint32_t irq_line;
    uint8_t  irq_pin;
    uint8_t  header_type;

    /* PCIe 特有 */
    uint8_t  pcie_cap_offset;  /* PCIe Capability offset */
    uint8_t  device_type;      /* PCIe Device Type */
    uint8_t  link_speed;       /* Link Speed (GT/s) */
    uint8_t  link_width;       /* Link Width (lanes) */
    uint16_t dev_ctrl;         /* Device Control */
    uint16_t dev_status;       /* Device Status */

    /* MSI */
    uint8_t  msi_capable;      /* 支持MSI */
    uint8_t  msi_cap_offset;   /* MSI capability offset */
    uint16_t msi_msg_num;      /* MSI消息数量 */
    uint32_t msi_address;      /* MSI目标地址 */
    uint16_t msi_data;         /* MSI数据 */

    /* MSI-X */
    uint8_t  msix_capable;     /* 支持MSI-X */
    uint8_t  msix_cap_offset;  /* MSI-X capability offset */
    uint16_t msix_table_size;  /* MSI-X表大小 */
    uint32_t msix_table_bar;   /* MSI-X表所在BAR */
    uint32_t msix_pba_bar;     /* MSI-X PBA所在BAR */

    /* 驱动绑定 */
    void *driver_data;
    const char *driver_name;
    struct pcie_device *next;
} pcie_device_t;

/* PCIe 链路状态 */
typedef struct {
    uint8_t speed;             /* 当前链路速度编码 */
    uint8_t width;             /* 当前链路宽度 */
    uint8_t negotiated_speed;  /* 协商速度 */
    uint8_t max_payload;       /* Max Payload Size (128/256/512/1024/2048/4096) */
} pcie_link_state_t;

/* 初始化PCIe总线扫描 */
int pcie_init(void);

/* 枚举所有PCIe设备 */
int pcie_enumerate(void);

/* 按设备ID查找 */
pcie_device_t *pcie_find_device(uint16_t vendor_id, uint16_t device_id, int index);

/* 按类别查找 */
pcie_device_t *pcie_find_class(uint8_t class_code, uint8_t subclass, int index);

/* 读取配置空间 (32bit) */
uint32_t pcie_config_read32(pcie_device_t *dev, uint32_t offset);

/* 写入配置空间 (32bit) */
void pcie_config_write32(pcie_device_t *dev, uint32_t offset, uint32_t value);

/* 读取配置空间 (8bit/16bit) */
uint8_t  pcie_config_read8(pcie_device_t *dev, uint32_t offset);
uint16_t pcie_config_read16(pcie_device_t *dev, uint32_t offset);
void     pcie_config_write8(pcie_device_t *dev, uint32_t offset, uint8_t value);
void     pcie_config_write16(pcie_device_t *dev, uint32_t offset, uint16_t value);

/* 使能内存空间和I/O空间访问 */
void pcie_enable_bus_master(pcie_device_t *dev);

/* 分配并启用MSI中断 */
int pcie_enable_msi(pcie_device_t *dev, uint32_t vector, void (*handler)(int));

/* 分配并启用MSI-X中断 */
int pcie_enable_msix(pcie_device_t *dev, uint16_t table_index, uint32_t vector, void (*handler)(int));

/* 获取PCIe链路状态 */
int pcie_get_link_state(pcie_device_t *dev, pcie_link_state_t *state);

/* 设置Max Payload Size */
int pcie_set_max_payload(pcie_device_t *dev, uint8_t size);

/* 遍历所有设备 */
typedef void (*pcie_enum_callback)(pcie_device_t *dev, void *data);
void pcie_foreach(pcie_enum_callback cb, void *data);

/* 获取设备总数 */
uint32_t pcie_device_count(void);

/* 打印设备列表 (调试用) */
void pcie_dump_devices(void);

/* PCIe 电源管理 */
int pcie_set_power_state(pcie_device_t *dev, uint8_t state);  /* D0-D3hot/D3cold */

#endif /* PCIE_DRIVER_H */
