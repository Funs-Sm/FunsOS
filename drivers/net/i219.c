/*
 * i219.c - Intel I219-V / I219-LM 千兆以太网控制器驱动
 *
 * 基于 Intel E1000/I225 架构, 使用 MMIO 方式访问。
 * I219 与 I225 寄存器布局非常相似，但 I219 有额外的 PHY 配置步骤。
 * TX/RX 描述符结构与 E1000/I225 完全相同。
 */

#include "i219.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "kheap.h"
#include "string.h"
#include "irq.h"

/* ---- 私有状态 ---- */
static volatile uint32_t *mmio_base;

static i219_rx_desc_t *rx_descs;
static i219_tx_desc_t *tx_descs;

static void *rx_buffers[I219_NUM_RX_DESC];
static void *tx_buffers[I219_NUM_TX_DESC];

static uint32_t rx_tail;
static uint32_t tx_tail;

static net_interface_t i219_iface;
static int i219_inited = 0;

/* ---- 寄存器读写辅助函数 ---- */

static inline uint32_t i219_read_reg(uint32_t offset) {
    return mmio_base[offset / 4];
}

static inline void i219_write_reg(uint32_t offset, uint32_t val) {
    mmio_base[offset / 4] = val;
}

/* ---- I219 特有 PHY 配置 ---- */
static void i219_phy_configure(void) {
    /* I219 需要额外的 PHY 配置步骤:
     * 1. 等待 PHY 复位完成
     * 2. 配置 PHY 电源管理
     * 3. 设置 PHY 中断掩码 */

    /* 等待 PHY 复位完成 (STATUS.PHYRA 位清零) */
    uint32_t timeout = 100000;
    while ((i219_read_reg(I219_REG_STATUS) & (1 << 10)) && timeout--)
        ;

    /* 配置 PHY 控制寄存器 - 启用 PHY */
    i219_write_reg(I219_REG_PHY_CTRL,
        i219_read_reg(I219_REG_PHY_CTRL) & ~(1 << 6));

    /* 等待 PHY 链路建立 */
    timeout = 500000;
    while (!(i219_read_reg(I219_REG_STATUS) & (1 << 1)) && timeout--)
        ;
}

/* ---- MAC 地址读取 ---- */
static void i219_read_mac(void) {
    /* 从 RAL/RAH 寄存器读取 MAC 地址 */
    uint32_t low = i219_read_reg(I219_REG_RAL);
    uint32_t high = i219_read_reg(I219_REG_RAH);
    i219_iface.mac.bytes[0] = low & 0xFF;
    i219_iface.mac.bytes[1] = (low >> 8) & 0xFF;
    i219_iface.mac.bytes[2] = (low >> 16) & 0xFF;
    i219_iface.mac.bytes[3] = (low >> 24) & 0xFF;
    i219_iface.mac.bytes[4] = high & 0xFF;
    i219_iface.mac.bytes[5] = (high >> 8) & 0xFF;
}

/* ---- 初始化 ---- */
void i219_init(uint8_t bus, uint8_t dev, uint8_t func) {
    /* 读取 BAR0 (MMIO 基地址) */
    uint32_t bar0 = pci_read_config(bus, dev, func, 0x10);
    uint32_t phys_addr = bar0 & 0xFFFFFFF0;
    mmio_base = (volatile uint32_t *)vmm_map_physical(phys_addr, 0x20000);

    /* 启用 PCI 总线主控和内存空间 */
    uint32_t cmd = pci_read_config(bus, dev, func, 0x04) & 0xFFFF;
    pci_write_config(bus, dev, func, 0x04, cmd | 0x07);

    /* 软复位设备: 设置 CTRL.RST 位 */
    i219_write_reg(I219_REG_CTRL,
        i219_read_reg(I219_REG_CTRL) | I219_CTRL_RST);

    /* 等待复位完成 */
    uint32_t timeout = 100000;
    while ((i219_read_reg(I219_REG_CTRL) & I219_CTRL_RST) && timeout--)
        ;
    if (timeout == 0) return;  /* 复位超时 */

    /* I219 特有: PHY 配置 */
    i219_phy_configure();

    /* 清除统计寄存器 */
    (void)i219_read_reg(I219_REG_STATUS);

    /* 读取 MAC 地址 */
    i219_read_mac();

    /* 分配 TX 描述符环 */
    tx_descs = (i219_tx_desc_t *)kmalloc(I219_NUM_TX_DESC * sizeof(i219_tx_desc_t));
    memset(tx_descs, 0, I219_NUM_TX_DESC * sizeof(i219_tx_desc_t));

    /* 分配 RX 描述符环 */
    rx_descs = (i219_rx_desc_t *)kmalloc(I219_NUM_RX_DESC * sizeof(i219_rx_desc_t));
    memset(rx_descs, 0, I219_NUM_RX_DESC * sizeof(i219_rx_desc_t));

    /* 分配 TX/RX 缓冲区并初始化描述符 */
    uint32_t i;
    for (i = 0; i < I219_NUM_TX_DESC; i++) {
        tx_buffers[i] = kmalloc(I219_TX_BUF_SIZE);
        tx_descs[i].addr = (uint64_t)(uint32_t)tx_buffers[i];
        tx_descs[i].length = 0;
        tx_descs[i].cso = 0;
        tx_descs[i].cmd = 0;
        tx_descs[i].status = 0;
        tx_descs[i].css = 0;
        tx_descs[i].special = 0;
    }

    for (i = 0; i < I219_NUM_RX_DESC; i++) {
        rx_buffers[i] = kmalloc(I219_RX_BUF_SIZE);
        memset(rx_buffers[i], 0, I219_RX_BUF_SIZE);
        rx_descs[i].addr = (uint64_t)(uint32_t)rx_buffers[i];
        rx_descs[i].length = 0;
        rx_descs[i].checksum = 0;
        rx_descs[i].status = 0;   /* DD=0 表示 NIC 拥有此描述符 */
        rx_descs[i].errors = 0;
        rx_descs[i].special = 0;
    }

    tx_tail = 0;
    rx_tail = I219_NUM_RX_DESC - 1;

    /* ---- 配置接收引擎 ---- */

    /* 设置 RX 描述符基地址 */
    i219_write_reg(I219_REG_RDBAL, (uint32_t)(uintptr_t)rx_descs);
    i219_write_reg(I219_REG_RDBAH, 0);
    i219_write_reg(I219_REG_RDLEN, I219_NUM_RX_DESC * sizeof(i219_rx_desc_t));

    /* 设置 RX 头尾指针 */
    i219_write_reg(I219_REG_RDH, 0);
    i219_write_reg(I219_REG_RDT, I219_NUM_RX_DESC - 1);

    /* 配置 RX 控制寄存器:
     * 启用接收、广播接受、剥离 CRC、缓冲区大小 2048 */
    i219_write_reg(I219_REG_RCTL,
        I219_RCTL_EN |
        I219_RCTL_BAM |
        I219_RCTL_SECRC |
        I219_RCTL_BSIZE_2048 |
        I219_RCTL_LBM_NONE);

    /* 使能 RX 描述符控制 (阈值设置) */
    i219_write_reg(I219_REG_RXDCTL, (1 << 24));  /* PTHRESH=1 */

    /* ---- 配置发送引擎 ---- */

    /* 设置 TX 描述符基地址 */
    i219_write_reg(I219_REG_TDBAL, (uint32_t)(uintptr_t)tx_descs);
    i219_write_reg(I219_REG_TDBAH, 0);
    i219_write_reg(I219_REG_TDLEN, I219_NUM_TX_DESC * sizeof(i219_tx_desc_t));

    /* 设置 TX 头尾指针 */
    i219_write_reg(I219_REG_TDH, 0);
    i219_write_reg(I219_REG_TDT, 0);

    /* 配置 TX 控制寄存器:
     * 启用发送、填充短包、碰撞阈值、碰撞距离 */
    i219_write_reg(I219_REG_TCTL,
        I219_TCTL_EN |
        I219_TCTL_PSP |
        (0x0F << 4) |       /* CT: Collision Threshold = 15 */
        (0x40 << 12));      /* COLD: Collision Distance = 64 */

    /* 使能 TX 描述符控制 */
    i219_write_reg(I219_REG_TXDCTL, (1 << 24));  /* PTHRESH=1 */

    /* ---- 中断配置 ---- */

    /* 清除所有待处理中断 */
    i219_write_reg(I219_REG_ICR, I219_ICR_ALL);

    /* 屏蔽所有中断后重新启用需要的 */
    i219_write_reg(I219_REG_IMC, 0xFFFFFFFF);

    /* 注册中断处理程序 */
    uint8_t irq_line = pci_read_config(bus, dev, func, 0x3C) & 0xFF;
    irq_register_handler(0x20 + irq_line, i219_irq_handler);

    /* 启用需要的中断: TX 写回、RX 定时器、链路状态变化等 */
    i219_write_reg(I219_REG_IMS,
        I219_ICR_TXDW |
        I219_ICR_LSC |
        I219_ICR_RXT0 |
        I219_ICR_RXO |
        I219_ICR_RXDMT0);

    /* ---- 强制链路启动 (I219 特性) ---- */
    i219_write_reg(I219_REG_CTRL,
        i219_read_reg(I219_REG_CTRL) | I219_CTRL_SLU);

    /* 注册网络接口 */
    strcpy(i219_iface.name, "i219");
    i219_iface.up = 1;
    i219_iface.mtu = 1500;
    i219_iface.send = i219_send;
    i219_iface.driver_data = (void *)0;
    net_register_interface(&i219_iface);

    i219_inited = 1;
}

/* ---- 发送数据包 ---- */
int i219_send(net_interface_t *iface, const void *data, uint32_t len) {
    if (!i219_inited || len > I219_TX_BUF_SIZE) return -1;

    /* 复制数据到 TX 缓冲区 */
    memcpy(tx_buffers[tx_tail], data, len);

    /* 填写 TX 描述符:
     * RS (Report Status): 完成时更新 status 字段
     * IC (Insert Checksum): 让硬件计算校验和
     * EOP (End of Packet): 包结束标志 */
    tx_descs[tx_tail].length = len;
    tx_descs[tx_tail].cmd =
        (1 << 0) |   /* RS: Report Status */
        (1 << 1) |   /* IC: Insert Checksum */
        (1 << 3);    /* EOP: End of Packet */
    tx_descs[tx_tail].status = 0;

    /* 更新尾部指针并通知硬件 */
    uint32_t old_tail = tx_tail;
    tx_tail = (tx_tail + 1) % I219_NUM_TX_DESC;
    i219_write_reg(I219_REG_TDT, tx_tail);

    (void)iface;
    return (int)old_tail;
}

/* ---- 轮询接收数据包 ---- */
void i219_poll(void) {
    if (!i219_inited) return;

    uint32_t i;
    for (i = 0; i < I219_NUM_RX_DESC; i++) {
        if (rx_descs[i].status & I219_RXDESC_STATUS_DD) {
            /* 描述符已完成 (DD=1), 数据可用 */

            net_buffer_t *buf = net_alloc_buffer();
            if (buf) {
                uint16_t pkt_len = rx_descs[i].length;
                memcpy(buf->data, rx_buffers[i], pkt_len);
                buf->len = pkt_len;
                buf->offset = 0;
                buf->iface = &i219_iface;
                net_receive(buf);
            }

            /* 归还描述符给 NIC: 清除 DD 位, 重置 buffer 地址 */
            memset(rx_buffers[i], 0, I219_RX_BUF_SIZE);
            rx_descs[i].addr = (uint64_t)(uint32_t)rx_buffers[i];
            rx_descs[i].status = 0;
            rx_descs[i].length = 0;
            rx_descs[i].errors = 0;
            rx_descs[i].checksum = 0;
            rx_descs[i].special = 0;

            /* 更新 RX 尾指针 */
            rx_tail = (rx_tail + 1) % I219_NUM_RX_DESC;
            i219_write_reg(I219_REG_RDT, rx_tail);
        }
    }
}

/* ---- 中断处理 ---- */
void i219_irq_handler(regs_t *regs) {
    if (!i219_inited) return;

    uint32_t icr = i219_read_reg(I219_REG_ICR);

    if (icr & I219_ICR_RXDMT0 || icr & I219_ICR_RXT0 || icr & I219_ICR_RXO) {
        /* 接收相关中断 -> 轮询收取数据包 */
        i219_poll();
    }
    if (icr & I219_ICR_TXDW) {
        /* TX 描述符写回中断 -> 发送完成, 无需特殊处理 */
    }
    if (icr & I219_ICR_LSC) {
        /* 链路状态变化中断 */
    }

    /* 清除已处理的中断标志 (写 ICR 清除) */
    i219_write_reg(I219_REG_ICR, icr);

    (void)regs;
}

/* ---- PCI 探测入口 ---- */
int i219_probe(void) {
    /* 扫描 PCI 总线查找 Intel I219 系列设备 */
    uint16_t bus;
    uint8_t dev, func;
    int found = 0;

    for (bus = 0; bus < PCI_MAX_BUSES && !found; bus++) {
        for (dev = 0; dev < PCI_MAX_DEVICES && !found; dev++) {
            for (func = 0; func < PCI_MAX_FUNCTIONS && !found; func++) {
                uint32_t vd = pci_read_config((uint8_t)bus, dev, func, 0x00);
                if (vd == 0xFFFFFFFF || vd == 0) continue;

                uint16_t vendor_id = vd & 0xFFFF;
                uint16_t device_id = (vd >> 16) & 0xFFFF;

                if (vendor_id == I219_VENDOR_ID &&
                    (device_id == I219V_DEVICE_ID ||
                     device_id == I219LM_DEVICE_ID ||
                     device_id == I219V2_DEVICE_ID ||
                     device_id == I219LM2_DEVICE_ID)) {
                    found = 1;
                    i219_init((uint8_t)bus, dev, func);
                }
            }
        }
    }

    return found ? 0 : -1;
}
