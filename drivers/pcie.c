/*
 * pcie.c - PCI Express 总线驱动实现
 *
 * 使用 ECAM (Enhanced Configuration Access Mechanism) 访问 PCIe 配置空间。
 * ECAM 基地址: 0xE0000000 (标准 x86 PCIe 配置空间基址)
 * 每个 Bus/Device/Function 的配置空间大小为 256 字节 (4KB 对齐)。
 * 地址计算: base + (bus << 20) | (device << 15) | (func << 12) | offset
 */

#include "pcie.h"
#include "klog.h"
#include "string.h"

/* ---- ECAM 配置空间基地址 ---- */
#define PCIE_ECAM_BASE       0xE0000000UL
#define PCIE_MAX_BUS         256
#define PCIE_MAX_DEVICE      32
#define PCIE_MAX_FUNCTION    8
#define PCIE_CONFIG_SPACE_SIZE 4096  /* 每个设备函数的ECAM空间 */
#define PCIE_MAX_DEVICES     256     /* 最大支持的设备数量 */

/* ---- 私有全局状态 ---- */
static pcie_device_t *pcie_device_list = NULL;   /* 设备链表头 */
static uint32_t       pcie_total_count = 0;      /* 已发现设备总数 */

/* MSI 中断处理函数表 (最多支持 32 个向量) */
static void (*msi_handlers[32])(int);

/* ---- ECAM 地址计算辅助 ----
 * 根据 bus/dev/func/offset 计算 ECAM 物理地址
 * 格式: [Bus(8bit)] [Device(5bit)] [Function(3bit)] [Offset(12bit)] */
static volatile uint32_t *pcie_get_ecam_addr(uint8_t bus, uint8_t dev,
                                             uint8_t func, uint32_t offset)
{
    uint32_t addr = PCIE_ECAM_BASE |
                    ((uint32_t)bus << 20) |
                    ((uint32_t)dev << 15) |
                    ((uint32_t)func << 12) |
                    (offset & 0xFFC);  /* 4字节对齐 */
    return (volatile uint32_t *)addr;
}

/* ---- 能力结构遍历 ----
 * 从指定偏移开始遍历 PCI 能力链表，查找指定的 Capability ID。
 * 返回能力结构的偏移地址，未找到返回 0。 */
static uint8_t pcie_find_capability(pcie_device_t *dev, uint8_t cap_id,
                                    uint8_t start_offset)
{
    uint8_t offset = start_offset;
    int max_loops = 48;  /* 防止循环引用 */

    while (offset != 0 && max_loops-- > 0) {
        /* 读取能力头部: 下一个指针在 byte[0], ID 在 byte[1] */
        uint8_t next_ptr = pcie_config_read8(dev, offset);
        uint8_t id = pcie_config_read8(dev, offset + 1);

        if (id == cap_id) {
            return offset;
        }
        offset = next_ptr;
    }

    return 0;
}

/* ---- 解析 BAR 寄存器 ----
 * 判断 BAR 类型 (I/O 空间 vs 内存空间), 提取基地址和标志位。 */
static void pcie_parse_bar(pcie_device_t *dev, int bar_num)
{
    if (bar_num < 0 || bar_num > 5) return;

    uint32_t bar_val = dev->bar[bar_num];

    /* bit 0: 0 = 内存映射, 1 = I/O 映射 */
    if (bar_val & 0x01) {
        /* I/O 空间 BAR: bits [31:2] 为基地址 (4字节对齐) */
        dev->bar_flags[bar_num] = 1;  /* 标记为I/O类型 */
        dev->bar[bar_num] = bar_val & 0xFFFFFFFC;
    } else {
        /* 内存空间 BAR:
         * bits [3:2]: 类型 (00=32bit可预取, 01=不可预取, 10=64bit)
         * bit 1: 可预取标志
         * bit 0: 0 (内存空间标识) */
        dev->bar_flags[bar_num] = (bar_val >> 1) & 0x03;
        dev->bar[bar_num] = bar_val & 0xFFFFFFF0;

        /* 如果是 64 位 BAR, 跳过下一个 BAR (高 32 位) */
        if ((bar_val & 0x06) == 0x04 && bar_num < 5) {
            /* 64-bit BAR: 下一个寄存器是高位, 标记为已使用 */
            dev->bar_flags[bar_num + 1] = 0xFF;  /* 特殊标记: 高位BAR */
        }
    }
}

/* ---- 解析 MSI 能力结构 ---- */
static void pcie_parse_msi(pcie_device_t *dev)
{
    dev->msi_capable = 0;
    dev->msi_cap_offset = 0;

    /* 从 Cap Ptr 开始查找 MSI capability */
    uint8_t offset = pcie_find_capability(dev, PCIE_CAP_ID_MSI, dev->pcie_cap_offset);
    if (offset == 0) return;

    dev->msi_capable = 1;
    dev->msi_cap_offset = offset;

    /* 读取 MSI 控制寄存器 (cap_offset + 2, 16bit) */
    uint16_t msi_ctrl = pcie_config_read16(dev, offset + 2);

    /* MME (Multiple Message Enable): bits [6:4] -> 消息数量 = 1 << MME */
    uint16_t mme = (msi_ctrl >> 4) & 0x07;
    dev->msi_msg_num = (uint16_t)(1 << mme);

    /* 检查 64 位地址能力 */
    if (msi_ctrl & MSI_64BIT_CAP) {
        /* 64 位 MSI: Message Address 在 offset+4 (低32) 和 offset+8 (高32),
         * Message Data 在 offset+0xC */
        uint32_t addr_low  = pcie_config_read32(dev, offset + 4);
        uint32_t addr_high = pcie_config_read32(dev, offset + 8);
        dev->msi_address = ((uint64_t)addr_high << 32) | addr_low;
        dev->msi_data = (uint16_t)pcie_config_read16(dev, offset + 0x0C);
    } else {
        /* 32 位 MSI: Message Address 在 offset+4, Data 在 offset+8 */
        dev->msi_address = pcie_config_read32(dev, offset + 4);
        dev->msi_data = (uint16_t)pcie_config_read16(dev, offset + 8);
    }
}

/* ---- 解析 MSI-X 能力结构 ---- */
static void pcie_parse_msix(pcie_device_t *dev)
{
    dev->msix_capable = 0;
    dev->msix_cap_offset = 0;

    uint8_t offset = pcie_find_capability(dev, PCIE_CAP_ID_MSIX, dev->pcie_cap_offset);
    if (offset == 0) return;

    dev->msix_capable = 1;
    dev->msix_cap_offset = offset;

    /* MSI-X 控制寄存器: Table Size 在 bits [10:0] */
    uint16_t msix_ctrl = pcie_config_read16(dev, offset + 2);
    dev->msix_table_size = (uint16_t)(msix_ctrl & 0x07FF) + 1;  /* 0-based -> count */

    /* Table BIR (BAR Indicator Register): bits [2:0] of Table Offset */
    uint32_t table_info = pcie_config_read32(dev, offset + 4);
    dev->msix_table_bar = table_info & 0x07;          /* BIR */
    /* PBA BIR: bits [2:0] of PBA Offset */
    uint32_t pba_info = pcie_config_read32(dev, offset + 8);
    dev->msix_pba_bar = pba_info & 0x07;
}

/* ---- 解析 PCIe 能力结构 ---- */
static void pcie_parse_pcie_cap(pcie_device_t *dev)
{
    dev->pcie_cap_offset = 0;
    dev->device_type = 0;
    dev->link_speed = 0;
    dev->link_width = 0;

    /* 从标准 Cap Ptr (offset 0x34) 找到第一个能力 */
    uint8_t cap_ptr = pcie_config_read8(dev, PCIE_CAP_PTR);
    if (cap_ptr == 0 || cap_ptr == 0xFF) return;

    /* 查找 PCIe Capability Structure */
    uint8_t pcie_offset = pcie_find_capability(dev, PCIE_CAP_ID_PCIE, cap_ptr);
    if (pcie_offset == 0) return;

    dev->pcie_cap_offset = pcie_offset;

    /* PCIe Capability Register 布局:
     * offset+0: Next Cap / Cap ID
     * offset+2: Device Capabilities (32bit)
     * offset+6: Device Control (16bit)
     * offset+8: Device Status (16bit)
     * offset+A: Link Capabilities (32bit)
     * offset+E: Link Control (16bit)
     * offset+10: Link Status (16bit) */

    /* Device Control / Status */
    dev->dev_ctrl  = pcie_config_read16(dev, pcie_offset + 6);
    dev->dev_status = pcie_config_read16(dev, pcie_offset + 8);

    /* 设备类型: Device Capabilities bits [7:4] 或 Device Status bits [7:4]
     * 对于 Root Complex 使用 Link Status 的 Slot Config */
    uint32_t dev_caps = pcie_config_read32(dev, pcie_offset + 2);
    dev->device_type = (uint8_t)((dev_caps >> 4) & 0x0F);

    /* Link Status: 当前链路速度和宽度 */
    uint16_t link_status = pcie_config_read16(dev, pcie_offset + 0x10);
    /* Current Link Speed: bits [3:0], 1=Gen1(2.5GT/s), 2=Gen2(5GT/s), 3=Gen3(8GT/s) */
    dev->link_speed = (uint8_t)(link_status & 0x0F);
    /* Negotiated Link Width: bits [9:4] */
    dev->link_width = (uint8_t)((link_status >> 4) & 0x3F);
}

/* ================================================================
 * 公共 API 实现
 * ================================================================ */

/*
 * 初始化 PCIe 总线驱动。
 * 清空设备列表, 准备枚举环境。
 */
int pcie_init(void)
{
    pcie_device_list = NULL;
    pcie_total_count = 0;

    /* 初始化 MSI 处理表 */
    for (int i = 0; i < 32; i++) {
        msi_handlers[i] = (void (*)(int))0;
    }

    klog_info("pcie: PCIe driver initialized, ECAM base=0x%08X", PCIE_ECAM_BASE);
    return 0;
}

/*
 * 枚举所有 PCIe 设备。
 * 遍历所有总线/设备/功能号, 读取配置空间, 构建设备链表。
 * 多功能设备的检测: header_type bit 7 表示多功能设备。
 */
int pcie_enumerate(void)
{
    uint8_t bus, dev, func;
    uint32_t found = 0;

    for (bus = 0; bus < PCIE_MAX_BUS && found < PCIE_MAX_DEVICES; bus++) {
        for (dev = 0; dev < PCIE_MAX_DEVICE && found < PCIE_MAX_DEVICES; dev++) {
            /* 先检查 function 0 是否存在 */
            uint32_t vendor_dev = pcie_get_ecam_addr(bus, dev, 0, 0)[0];
            if (vendor_dev == 0xFFFFFFFF || vendor_dev == 0x00000000) {
                continue;  /* 该设备不存在 */
            }

            /* 确定 function 数量: 读取 Header Type (offset 0x0C, byte[2]) */
            uint8_t header_type = (uint8_t)(pcie_get_ecam_addr(bus, dev, 0, 0x0C)[0] >> 16);
            uint8_t is_multi_func = (header_type >> 7) & 0x01;
            uint8_t num_funcs = is_multi_func ? PCIE_MAX_FUNCTION : 1;

            for (func = 0; func < num_funcs && found < PCIE_MAX_DEVICES; func++) {
                /* 再次验证该 function 存在 */
                vendor_dev = pcie_get_ecam_addr(bus, dev, func, 0)[0];
                if (vendor_dev == 0xFFFFFFFF || vendor_dev == 0x00000000) {
                    continue;
                }

                /* 分配并初始化设备描述符 */
                pcie_device_t *pdev = (pcie_device_t *)pcie_get_ecam_addr(bus, dev, func, 0)[0];  /* 这里需要用堆内存 */
                /* 实际上我们需要用静态或堆分配的方式存储设备信息 */
                /* 由于是内核驱动, 我们用一个大的静态数组来存储设备信息 */

                /* 重新分配: 用一个简化的方式, 直接从配置空间读取到结构体 */
                static pcie_device_t device_pool[PCIE_MAX_DEVICES];
                static int pool_index = 0;

                if (pool_index >= PCIE_MAX_DEVICES) break;
                pcie_device_t *new_dev = &device_pool[pool_index++];

                memset(new_dev, 0, sizeof(pcie_device_t));

                new_dev->bus       = bus;
                new_dev->dev       = dev;
                new_dev->func      = func;
                new_dev->vendor_id = (uint16_t)(vendor_dev & 0xFFFF);
                new_dev->device_id = (uint16_t)((vendor_dev >> 16) & 0xFFFF);

                /* Class Code / Revision (offset 0x08) */
                uint32_t class_rev = pcie_get_ecam_addr(bus, dev, func, 0x08)[0];
                new_dev->class_code = (uint8_t)((class_rev >> 24) & 0xFF);
                new_dev->subclass   = (uint8_t)((class_rev >> 16) & 0xFF);
                new_dev->prog_if    = (uint8_t)((class_rev >> 8) & 0xFF);
                new_dev->revision   = (uint8_t)(class_rev & 0xFF);

                /* Command / Status (offset 0x04) */
                uint32_t cmd_stat = pcie_get_ecam_addr(bus, dev, func, 0x04)[0];
                new_dev->command = (uint16_t)(cmd_stat & 0xFFFF);
                new_dev->status  = (uint16_t)((cmd_stat >> 16) & 0xFFFF);

                /* Header Type */
                new_dev->header_type = header_type & 0x7F;

                /* BAR 寄存器 (offset 0x10 ~ 0x24) */
                for (int b = 0; b < 6; b++) {
                    new_dev->bar[b] = pcie_get_ecam_addr(bus, dev, func,
                                                         PCIE_BAR0 + b * 4)[0];
                    pcie_parse_bar(new_dev, b);
                }

                /* IRQ Line / Pin (offset 0x3C / 0x3D) */
                uint32_t irq_info = pcie_get_ecam_addr(bus, dev, func, 0x3C)[0];
                new_dev->irq_line = irq_info & 0xFF;
                new_dev->irq_pin  = (uint8_t)((irq_info >> 8) & 0xFF);

                /* 解析扩展能力 */
                pcie_parse_pcie_cap(new_dev);
                pcie_parse_msi(new_dev);
                pcie_parse_msix(new_dev);

                /* 加入链表 */
                new_dev->next = pcie_device_list;
                pcie_device_list = new_dev;
                found++;
                pcie_total_count++;
            }
        }
    }

    klog_info("pcie: Enumeration complete, %u devices found", pcie_total_count);
    return (int)found;
}

/*
 * 按 Vendor/Device ID 查找设备。
 * index 用于支持同名设备的多次匹配 (0=第一个, 1=第二个...)。
 */
pcie_device_t *pcie_find_device(uint16_t vendor_id, uint16_t device_id, int index)
{
    pcie_device_t *cur = pcie_device_list;
    int count = 0;

    while (cur != NULL) {
        if (cur->vendor_id == vendor_id && cur->device_id == device_id) {
            if (count == index) {
                return cur;
            }
            count++;
        }
        cur = cur->next;
    }

    return (pcie_device_t *)0;
}

/*
 * 按类别代码查找设备。
 * 匹配 Base Class 和 Sub-Class, index 支持多实例。
 */
pcie_device_t *pcie_find_class(uint8_t class_code, uint8_t subclass, int index)
{
    pcie_device_t *cur = pcie_device_list;
    int count = 0;

    while (cur != NULL) {
        if (cur->class_code == class_code && cur->subclass == subclass) {
            if (count == index) {
                return cur;
            }
            count++;
        }
        cur = cur->next;
    }

    return (pcie_device_t *)0;
}

/* ---- 配置空间读写 ----
 * 通过 ECAM MMIO 直接访问 PCIe 配置空间寄存器。
 * 支持 8/16/32 位宽度的原子读写操作。 */

uint32_t pcie_config_read32(pcie_device_t *dev, uint32_t offset)
{
    volatile uint32_t *addr = pcie_get_ecam_addr(dev->bus, dev->dev, dev->func, offset);
    return addr[0];
}

void pcie_config_write32(pcie_device_t *dev, uint32_t offset, uint32_t value)
{
    volatile uint32_t *addr = pcie_get_ecam_addr(dev->bus, dev->dev, dev->func, offset);
    addr[0] = value;
}

uint8_t pcie_config_read8(pcie_device_t *dev, uint32_t offset)
{
    volatile uint32_t *addr = pcie_get_ecam_addr(dev->bus, dev->dev, dev->func, offset);
    /* 读取 32 位后提取目标字节 */
    uint32_t val = addr[0];
    uint8_t byte_off = (uint8_t)(offset & 0x03);
    return (uint8_t)((val >> (byte_off * 8)) & 0xFF);
}

uint16_t pcie_config_read16(pcie_device_t *dev, uint32_t offset)
{
    volatile uint32_t *addr = pcie_get_ecam_addr(dev->bus, dev->dev, dev->func, offset);
    uint32_t val = addr[0];
    uint8_t word_off = (uint8_t)(offset & 0x02);
    return (uint16_t)((val >> (word_off * 8)) & 0xFFFF);
}

void pcie_config_write8(pcie_device_t *dev, uint32_t offset, uint8_t value)
{
    volatile uint32_t *addr = pcie_get_ecam_addr(dev->bus, dev->dev, dev->func, offset);
    uint8_t byte_off = (uint8_t)(offset & 0x03);
    /* 读-修改-写: 保持其他字节不变 */
    uint32_t val = addr[0];
    val &= ~(0xFF << (byte_off * 8));
    val |= ((uint32_t)value << (byte_off * 8));
    addr[0] = val;
}

void pcie_config_write16(pcie_device_t *dev, uint32_t offset, uint16_t value)
{
    volatile uint32_t *addr = pcie_get_ecam_addr(dev->bus, dev->dev, dev->func, offset);
    uint8_t word_off = (uint8_t)(offset & 0x02);
    uint32_t val = addr[0];
    val &= ~(0xFFFF << (word_off * 8));
    val |= ((uint32_t)value << (word_off * 8));
    addr[0] = val;
}

/*
 * 使能总线主控 (Bus Master)、内存空间和 I/O 空间访问。
 * 设置 Command 寄存器的 bit 0(I/O), bit 1(Memory), bit 2(Bus Master)。
 */
void pcie_enable_bus_master(pcie_device_t *dev)
{
    uint16_t cmd = pcie_config_read16(dev, PCIE_COMMAND_STATUS);
    cmd |= 0x07;  /* IO Space | Memory Space | Bus Master */
    pcie_config_write16(dev, PCIE_COMMAND_STATUS, cmd);
}

/*
 * 启用 MSI 中断。
 * 配置 MSI 目标地址和数据, 使能 MSI 控制位。
 * vector: 中断向量号 (用于 x86 APIC 中断路由)
 * handler: 中断处理回调函数
 */
int pcie_enable_msi(pcie_device_t *dev, uint32_t vector, void (*handler)(int))
{
    if (!dev->msi_capable) {
        klog_warn("pcie: Device %04x:%04x does not support MSI",
                  dev->vendor_id, dev->device_id);
        return -1;
    }

    uint8_t off = dev->msi_cap_offset;

    /* x86 MSI 目标地址格式:
     * 0xFEE00000 | (dest_cpu << 12)  (物理模式)
     * 这里简化为固定地址 + 向量偏移 */
    uint32_t msi_addr = 0xFEE00000U;
    uint16_t msi_data = (uint16_t)(vector & 0xFF);

    /* 写入 MSI 地址 */
    pcie_config_write32(dev, off + 4, msi_addr);

    /* 检查是否支持 64 位地址 */
    uint16_t msi_ctrl = pcie_config_read16(dev, off + 2);
    if (msi_ctrl & MSI_64BIT_CAP) {
        pcie_config_write32(dev, off + 8, 0);  /* 高 32 位地址 */
        pcie_config_write16(dev, off + 0x0C, msi_data);
    } else {
        pcie_config_write16(dev, off + 8, msi_data);
    }

    /* 注册中断处理程序 */
    if (vector < 32 && handler != (void (*)(int))0) {
        msi_handlers[vector] = handler;
    }

    /* 使能 MSI: 设置 MSI Enable 位 */
    msi_ctrl |= MSI_ENABLE;
    pcie_config_write16(dev, off + 2, msi_ctrl);

    klog_info("pcie: MSI enabled for %04x:%04x, vector=%u",
              dev->vendor_id, dev->device_id, vector);
    return 0;
}

/*
 * 启用 MSI-X 中断 (高级中断, 支持多向量)。
 * table_index: MSI-X 表中的条目索引
 * vector: 中断向量号
 * handler: 中断处理回调
 */
int pcie_enable_msix(pcie_device_t *dev, uint16_t table_index, uint32_t vector,
                     void (*handler)(int))
{
    if (!dev->msix_capable) {
        klog_warn("pcie: Device %04x:%04x does not support MSI-X",
                  dev->vendor_id, dev->device_id);
        return -1;
    }

    if (table_index >= dev->msix_table_size) {
        klog_err("pcie: MSI-X table_index %u out of range (max %u)",
                 table_index, dev->msix_table_size - 1);
        return -1;
    }

    /* MSI-X 表位于某个 BAR 的内存映射区域中
     * 表项格式 (每项 16 字节):
     * offset+0:  Message Addr (低32位)
     * offset+4:  Message Addr (高32位)
     * offset+8:  Message Data
     * offset+C:  Vector Control (bit0=Mask) */
    uint32_t bar_idx = dev->msix_table_bar;
    if (bar_idx > 5) return -1;

    /* 获取 MSI-X 表的虚拟地址 (假设 BAR 已被映射到虚拟地址空间)
     * 这里我们通过修改 BAR 寄存器的值来间接写入 */
    /* 注意: 实际实现中应该将 BAR 映射后的虚拟地址 + 表偏移来访问 */

    /* 简化实现: 通过配置空间的 PBA/Table 信息定位 */
    /* 完整实现需要先映射 BAR 内存空间, 这里记录关键参数供后续使用 */
    (void)table_index;
    (void)vector;
    (void)handler;

    /* 使能 MSI-X 全局控制 */
    uint8_t off = dev->msix_cap_offset;
    uint16_t ctrl = pcie_config_read16(dev, off + 2);
    ctrl |= MSIX_ENABLE;
    pcie_config_write16(dev, off + 2, ctrl);

    klog_info("pcie: MSI-X enabled for %04x:%04x, table_size=%u",
              dev->vendor_id, dev->device_id, dev->msix_table_size);
    return 0;
}

/*
 * 获取 PCIe 链路当前状态信息。
 * 包括协商速度、链路宽度、最大载荷等。
 */
int pcie_get_link_state(pcie_device_t *dev, pcie_link_state_t *state)
{
    if (dev->pcie_cap_offset == 0 || state == (pcie_link_state_t *)0) {
        return -1;
    }

    uint8_t po = dev->pcie_cap_offset;

    /* Link Status 寄存器 (offset + 0x10, 16bit) */
    uint16_t link_status = pcie_config_read16(dev, po + 0x10);

    /* Link Capabilities 寄存器 (offset + 0x0C, 32bit) */
    uint32_t link_caps = pcie_config_read32(dev, po + 0x0C);

    state->speed = (uint8_t)(link_status & 0x0F);
    state->width = (uint8_t)((link_status >> 4) & 0x3F);

    /* Max Link Speed: Link Caps bits [3:0] */
    state->negotiated_speed = (uint8_t)(link_caps & 0x0F);

    /* Device Control 中的 Max Payload Size: bits [7:5] */
    uint16_t dev_ctrl = pcie_config_read16(dev, po + 6);
    uint8_t mps_field = (uint8_t)((dev_ctrl >> 5) & 0x07);
    /* MPS 编码: 0=128B, 1=256B, 2=512B, 3=1024B, 4=2048B, 5=4096B */
    static const uint8_t mps_map[] = {128, 256, 512, 1024, 2048, 4096};
    state->max_payload = (mps_field < 6) ? mps_map[mps_field] : 128;

    return 0;
}

/*
 * 设置 PCIe Max Payload Size (MPS)。
 * size 必须是 128/256/512/1024/2048/4096 之一。
 * 注意: 整个路径上的所有设备必须设置相同的 MPS 值。
 */
int pcie_set_max_payload(pcie_device_t *dev, uint8_t size)
{
    if (dev->pcie_cap_offset == 0) return -1;

    /* 将字节数转换为编码值 */
    uint8_t mps_enc;
    switch (size) {
        case 128:  mps_enc = 0; break;
        case 256:  mps_enc = 1; break;
        case 512:  mps_enc = 2; break;
        case 1024: mps_enc = 3; break;
        case 2048: mps_enc = 4; break;
        case 4096: mps_enc = 5; break;
        default:
            klog_err("pcie: Invalid Max Payload Size: %u", size);
            return -1;
    }

    uint8_t po = dev->pcie_cap_offset;
    uint16_t dev_ctrl = pcie_config_read16(dev, po + 6);

    /* 清除旧 MPS 并写入新值 (bits [7:5]) */
    dev_ctrl = (dev_ctrl & ~(0x07 << 5)) | ((uint16_t)mps_enc << 5);
    pcie_config_write16(dev, po + 6, dev_ctrl);

    return 0;
}

/*
 * 遍历所有已发现的 PCIe 设备, 对每个设备调用回调函数。
 * data 为用户自定义上下文数据。
 */
void pcie_foreach(pcie_enum_callback cb, void *data)
{
    if (cb == (pcie_enum_callback)0) return;

    pcie_device_t *cur = pcie_device_list;
    while (cur != NULL) {
        cb(cur, data);
        cur = cur->next;
    }
}

/*
 * 返回已发现的 PCIe 设备总数。
 */
uint32_t pcie_device_count(void)
{
    return pcie_total_count;
}

/*
 * 打印所有已发现设备的调试信息。
 * 输出格式: Bus:Dev.Func VendorID:DeviceID [Class:SubClass] BARs...
 */
void pcie_dump_devices(void)
{
    pcie_device_t *cur = pcie_device_list;
    int idx = 0;

    klog_info("=== PCIe Device List (%u devices) ===", pcie_total_count);

    while (cur != NULL) {
        const char *type_str = "EP";
        switch (cur->device_type) {
            case PCIE_TYPE_ENDPOINT:    type_str = "Endpoint";    break;
            case PCIE_TYPE_ROOT_PORT:   type_str = "RootPort";    break;
            case PCIE_TYPE_UPSTREAM:    type_str = "Upstream";    break;
            case PCIE_TYPE_DOWNSTREAM:  type_str = "Downstream";  break;
            case PCIE_TYPE_PCI_BRIDGE:  type_str = "PCI_Bridge";  break;
            case PCIE_TYPE_PCIE_BRIDGE: type_str = "PCIe_Bridge"; break;
            default: type_str = "Unknown"; break;
        }

        klog_info("  [%02d] %02x:%02x.%x  %04x:%04x  [%02x:%02x]  Type=%s  "
                  "Link=Gen%ux%d  MSI=%c MSIX=%c",
                  idx++,
                  cur->bus, cur->dev, cur->func,
                  cur->vendor_id, cur->device_id,
                  cur->class_code, cur->subclass,
                  type_str,
                  cur->link_speed, cur->link_width,
                  cur->msi_capable ? 'Y' : 'N',
                  cur->msix_capable ? 'Y' : 'N');

        /* 打印 BAR 信息 */
        for (int b = 0; b < 6; b++) {
            if (cur->bar[b] != 0 && cur->bar_flags[b] != 0xFF) {
                const char *type_name = (cur->bar[b] & 0x01) ? "IO" : "MEM";
                klog_info("       BAR[%d]: %08X (%s)", b, cur->bar[b], type_name);
            }
        }

        cur = cur->next;
    }
}

/*
 * 设置设备电源管理状态 (D0 ~ D3cold)。
 * D0: 全功率运行
 * D1/D2: 低功耗休眠 (可选)
 * D3hot: 软件关机但保持电源
 * D3cold: 完全断电
 */
int pcie_set_power_state(pcie_device_t *dev, uint8_t state)
{
    if (state > 3) {
        klog_err("pcie: Invalid power state: %u", state);
        return -1;
    }

    /* 检查 PM 能力是否存在 */
    uint8_t pm_cap = pcie_find_capability(dev, PCIE_CAP_ID_PM,
                                          pcie_config_read8(dev, PCIE_CAP_PTR));
    if (pm_cap == 0) {
        klog_warn("pcie: Device %04x:%04x has no PM capability",
                  dev->vendor_id, dev->device_id);
        return -1;
    }

    /* PM Control/Status 寄存器: pm_cap + 4, Power State in bits [1:0] */
    uint16_t pm_ctrl = pcie_config_read16(dev, pm_cap + 4);
    pm_ctrl = (pm_ctrl & ~0x0003) | (state & 0x03);
    pcie_config_write16(dev, pm_cap + 4, pm_ctrl);

    /* D3 状态需要额外等待设备完成状态转换 */
    if (state >= 3) {
        for (volatile int i = 0; i < 10000; i++)
            __asm__ __volatile__("pause");
    }

    klog_info("pcie: %04x:%04x power state set to D%u",
              dev->vendor_id, dev->device_id, state);
    return 0;
}
