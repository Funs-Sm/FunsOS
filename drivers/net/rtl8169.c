/*
 * rtl8169.c - Realtek RTL8169/RTL8168/RTL8167 PCIe 千兆网卡驱动
 *
 * 基于 MMIO 方式访问, 参照 e1000.c 的架构模式实现。
 * 支持 RTL8169 (0x8169), RTL8168 (0x8168), RTL8167 (0x8167)。
 */

#include "rtl8169.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "kheap.h"
#include "string.h"
#include "irq.h"

/* ---- 私有状态 ---- */
static volatile uint32_t *mmio_base;

static rtl8169_rx_desc_t *rx_descs;
static rtl8169_tx_desc_t *tx_descs;

static void *rx_buffers[RTL8169_NUM_RX_DESC];
static void *tx_buffers[RTL8169_NUM_TX_DESC];

static uint32_t rx_tail;
static uint32_t tx_tail;
static uint32_t tx_head;

static net_interface_t rtl8169_iface;
static int rtl8169_inited = 0;

/* ---- 寄存器读写辅助函数 ---- */

static inline uint32_t rtl8169_read_reg(uint32_t offset) {
    return mmio_base[offset / 4];
}

static inline void rtl8169_write_reg(uint32_t offset, uint32_t val) {
    mmio_base[offset / 4] = val;
}

/* ---- MAC 地址读取 ---- */
static void rtl8169_read_mac(void) {
    /* RTL8169 的 MAC 地址存储在 IDR0-IDR5 (偏移 0x00-0x05),
     * 每个 ID 寄存器是一个字节, 通过 MMIO 字节方式读取 */
    volatile uint8_t *mac_regs = (volatile uint8_t *)mmio_base;
    rtl8169_iface.mac.bytes[0] = mac_regs[0x00];
    rtl8169_iface.mac.bytes[1] = mac_regs[0x01];
    rtl8169_iface.mac.bytes[2] = mac_regs[0x02];
    rtl8169_iface.mac.bytes[3] = mac_regs[0x03];
    rtl8169_iface.mac.bytes[4] = mac_regs[0x04];
    rtl8169_iface.mac.bytes[5] = mac_regs[0x05];
}

/* ---- 初始化 ---- */
void rtl8169_init(uint8_t bus, uint8_t dev, uint8_t func) {
    /* 读取 BAR2 (MMIO 基地址) - RTL8169 通常使用 BAR2 做 MMIO */
    uint32_t bar = pci_read_config(bus, dev, func, 0x18);
    uint32_t phys_addr;
    if ((bar & 1) == 0 && (bar & 6) == 0) {
        /* 内存空间映射 */
        phys_addr = bar & 0xFFFFFFF0;
        mmio_base = (volatile uint32_t *)vmm_map_physical(phys_addr, 0x10000);
    } else {
        /* 尝试 BAR0 作为备选 */
        bar = pci_read_config(bus, dev, func, 0x10);
        phys_addr = bar & 0xFFFFFFF0;
        mmio_base = (volatile uint32_t *)vmm_map_physical(phys_addr, 0x10000);
    }

    /* 启用 PCI 总线主控和内存空间 */
    uint32_t cmd = pci_read_config(bus, dev, func, 0x04) & 0xFFFF;
    pci_write_config(bus, dev, func, 0x04, cmd | 0x07);

    /* 软复位网卡 */
    rtl8169_write_reg(RTL8169_REG_CMD, RTL8169_CMD_RST);
    uint32_t timeout = 100000;
    while ((rtl8169_read_reg(RTL8169_REG_CMD) & RTL8169_CMD_RST) && timeout--)
        ;
    if (timeout == 0) return;  /* 复位超时 */

    /* 读取 MAC 地址 */
    rtl8169_read_mac();

    /* 分配 TX 描述符环 */
    tx_descs = (rtl8169_tx_desc_t *)kmalloc(RTL8169_NUM_TX_DESC * sizeof(rtl8169_tx_desc_t));
    memset(tx_descs, 0, RTL8169_NUM_TX_DESC * sizeof(rtl8169_tx_desc_t));

    /* 分配 RX 描述符环 */
    rx_descs = (rtl8169_rx_desc_t *)kmalloc(RTL8169_NUM_RX_DESC * sizeof(rtl8169_rx_desc_t));
    memset(rx_descs, 0, RTL8169_NUM_RX_DESC * sizeof(rtl8169_rx_desc_t));

    /* 分配 TX/RX 缓冲区并初始化描述符 */
    uint32_t i;
    for (i = 0; i < RTL8169_NUM_TX_DESC; i++) {
        tx_buffers[i] = kmalloc(RTL8169_TX_BUF_SIZE);
        tx_descs[i].addr = (uint64_t)(uint32_t)tx_buffers[i];
        tx_descs[i].opts = 0;
        tx_descs[i].reserved = 0;
        /* 设置最后一个描述符的 EOR 位 */
        if (i == RTL8169_NUM_TX_DESC - 1)
            tx_descs[i].opts |= RTL8169_TX_EOR;
    }

    for (i = 0; i < RTL8169_NUM_RX_DESC; i++) {
        rx_buffers[i] = kmalloc(RTL8169_RX_BUF_SIZE);
        memset(rx_buffers[i], 0, RTL8169_RX_BUF_SIZE);
        rx_descs[i].addr = (uint64_t)(uint32_t)rx_buffers[i];
        rx_descs[i].opts = RTL8169_RX_OWN;  /* 所有权归 NIC */
        rx_descs[i].reserved = 0;
        /* 设置最后一个描述符的 EOR 位 */
        if (i == RTL8169_NUM_RX_DESC - 1)
            rx_descs[i].opts |= RTL8169_RX_EOR;
    }

    tx_head = 0;
    tx_tail = 0;
    rx_tail = 0;

    /* 配置 TX: 设置 TX 描述符基地址 */
    uint32_t tx_phys = (uint32_t)(uintptr_t)tx_descs;
    rtl8169_write_reg(RTL8169_REG_TNPDS, tx_phys & 0xFFFFFFFF);
    rtl8169_write_reg(RTL8169_REG_TNPPS, 0);  /* 低 32 位系统无需高 32 位 */

    /* 配置 RX: 设置 RX 描述符基地址 */
    uint32_t rx_phys = (uint32_t)(uintptr_t)rx_descs;
    rtl8169_write_reg(RTL8169_REG_RDSAR, rx_phys);

    /* 配置接收控制寄存器:
     * 接收广播包、多播包、物理匹配包, 启用 WRAP 模式 */
    rtl8169_write_reg(RTL8169_REG_RCR,
        RTL8169_RCR_ACCEPT_PHYS_MATCH |
        RTL8169_RCR_ACCEPT_MULTICAST |
        RTL8169_RCR_ACCEPT_BROADCAST |
        RTL8169_RCR_WRAP |
        (RTL8169_RX_BUF_SIZE << 11));  /* Max RX 长度 */

    /* 配置发送控制寄存器 */
    rtl8169_write_reg(RTL8169_REG_TCR,
        RTL8169_TCR_MAX_DMA_2048 | (6 << 8));

    /* 清除并设置中断屏蔽: 允许 RX OK 和 TX OK 中断 */
    rtl8169_write_reg(RTL8169_REG_ISR, 0xFFFF);
    rtl8169_write_reg(RTL8169_REG_IMR,
        RTL8169_ISR_ROK | RTL8169_ISR_TOK | RTL8169_ISR_RER | RTL8169_ISR_TER);

    /* 注册中断处理程序 */
    uint8_t irq_line = pci_read_config(bus, dev, func, 0x3C) & 0xFF;
    irq_register_handler(0x20 + irq_line, rtl8169_irq_handler);

    /* 使能 TX 和 RX */
    rtl8169_write_reg(RTL8169_REG_CMD,
        rtl8169_read_reg(RTL8169_REG_CMD) | RTL8169_CMD_TE | RTL8169_CMD_RE);

    /* 注册网络接口 */
    strcpy(rtl8169_iface.name, "rtk0");
    rtl8169_iface.up = 1;
    rtl8169_iface.mtu = 1500;
    rtl8169_iface.send = rtl8169_send;
    rtl8169_iface.driver_data = (void *)0;
    net_register_interface(&rtl8169_iface);

    rtl8169_inited = 1;
}

/* ---- 发送数据包 ---- */
int rtl8169_send(net_interface_t *iface, const void *data, uint32_t len) {
    if (!rtl8169_inited || len > RTL8169_TX_BUF_SIZE) return -1;

    /* 检查当前 TX 描述符是否空闲 (OWN 位为 0 表示 host 可用) */
    if (tx_descs[tx_tail].opts & RTL8169_TX_OWN) {
        return -1;  /* 描述符忙, 等待 NIC 释放 */
    }

    /* 复制数据到 TX 缓冲区 */
    memcpy(tx_buffers[tx_tail], data, len);

    /* 填写 TX 描述符:
     * RTL8169 TX descriptor: opts 低 13 位 = 包长度 */
    tx_descs[tx_tail].opts =
        RTL8169_TX_OWN |      /* 所有权交给 NIC */
        RTL8169_TX_FS |       /* First Segment */
        RTL8169_TX_LS |       /* Last Segment  */
        (len & 0x3FFF);       /* 包长度 (低 13 位) */

    /* 更新尾部指针 */
    uint32_t old_tail = tx_tail;
    tx_tail = (tx_tail + 1) % RTL8169_NUM_TX_DESC;

    /* 触发 NIC 发送 (写 TNPDS 尾指针通知 NIC) */
    rtl8169_write_reg(RTL8169_REG_TNPDS,
        (uint32_t)((uintptr_t)&tx_descs[tx_tail]));

    (void)iface;
    return (int)old_tail;
}

/* ---- 轮询接收数据包 ---- */
void rtl8169_poll(void) {
    if (!rtl8169_inited) return;

    while (!(rx_descs[rx_tail].opts & RTL8169_RX_OWN)) {
        /* 描述符所有权已回到 host, 可以读取数据 */

        /* 检查接收状态 */
        if (rx_descs[rx_tail].opts & RTL8169_RX_RES) {
            /* 接收成功, 提取包长度 (低 13 位) */
            uint16_t pkt_len = rx_descs[rx_tail].opts & 0x3FFF;

            net_buffer_t *buf = net_alloc_buffer();
            if (buf) {
                memcpy(buf->data, rx_buffers[rx_tail], pkt_len);
                buf->len = pkt_len;
                buf->offset = 0;
                buf->iface = &rtl8169_iface;
                net_receive(buf);
            }
        }

        /* 归还描述符给 NIC */
        memset(rx_buffers[rx_tail], 0, RTL8169_RX_BUF_SIZE);
        rx_descs[rx_tail].opts = RTL8169_RX_OWN;
        if (rx_tail == RTL8169_NUM_RX_DESC - 1)
            rx_descs[rx_tail].opts |= RTL8169_RX_EOR;

        rx_tail = (rx_tail + 1) % RTL8169_NUM_RX_DESC;
    }
}

/* ---- 中断处理 ---- */
void rtl8169_irq_handler(regs_t *regs) {
    if (!rtl8169_inited) return;

    uint32_t isr = rtl8169_read_reg(RTL8169_REG_ISR);

    if (isr & RTL8169_ISR_ROK) {
        /* 接收完成中断 -> 轮询收取所有可用包 */
        rtl8169_poll();
    }
    if (isr & RTL8169_ISR_TOK) {
        /* 发送完成中断 -> 无需特殊处理 */
    }
    if (isr & RTL8169_ISR_RER) {
        /* 接收错误 */
    }
    if (isr & RTL8169_ISR_TER) {
        /* 发送错误 */
    }

    /* 清除已处理的中断标志 */
    rtl8169_write_reg(RTL8169_REG_ISR, isr);

    (void)regs;
}

/* ---- PCI 探测入口 ---- */
int rtl8169_probe(void) {
    /* 扫描 PCI 总线查找 RTL8169 系列设备 */
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

                if (vendor_id == RTL8169_VENDOR_ID &&
                    (device_id == RTL8169_DEVICE_ID ||
                     device_id == RTL8168_DEVICE_ID ||
                     device_id == RTL8167_DEVICE_ID)) {
                    found = 1;
                    rtl8169_init((uint8_t)bus, dev, func);
                }
            }
        }
    }

    return found ? 0 : -1;
}
