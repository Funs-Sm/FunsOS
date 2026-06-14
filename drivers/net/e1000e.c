/*
 * e1000e.c - Intel 82574L / ICH8/9/10 (e1000e) PCIe 千兆以太网驱动
 *
 * 基于 Intel E1000E 控制器数据手册, 使用 MMIO 方式访问寄存器。
 * 支持 82574L 独立网卡以及 ICH8/ICH9/ICH10 集成网卡。
 * 功能包括: VLAN, Checksum Offload, TSO, RSS, WoL 电源管理。
 */

#include "e1000e.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "kheap.h"
#include "string.h"
#include "irq.h"
#include "klog.h"

/* ---- 私有状态 ---- */
static volatile uint32_t *mmio_base = 0;       /* MMIO base address              */
static int              use_msix    = 0;       /* MSI-X 是否启用               */
static uint8_t          irq_vector  = 0;       /* 中断向量                      */

static e1000e_rx_desc_t *rx_descs = 0;         /* RX descriptor ring             */
static e1000e_tx_desc_t *tx_descs = 0;         /* TX descriptor ring             */

static void *rx_buffers[E1000E_NUM_RX_DESC];
static void *tx_buffers[E1000E_NUM_TX_DESC];

static uint32_t rx_tail = 0;
static uint32_t tx_tail = 0;

static net_interface_t e1000e_iface;
static int             e1000e_inited = 0;

/* Toeplitz RSS 密钥 (Intel 默认值, 40 bytes) */
static const uint8_t e1000e_rss_key[40] = {
    0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
    0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
    0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
    0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
    0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A
};

/* ---- 寄存器读写辅助函数 ---- */
static inline uint32_t e1000e_read_reg(uint32_t offset) {
    return mmio_base[offset / 4];
}

static inline void e1000e_write_reg(uint32_t offset, uint32_t val) {
    mmio_base[offset / 4] = val;
}

/* ---- EEPROM 读取 ---- */
static uint16_t e1000e_read_eeprom(uint8_t offset) {
    uint32_t eerd;
    /* 发起 EEPROM 读取: 设置 START (bit 0) 和地址 (bit 8-15) */
    e1000e_write_reg(E1000E_REG_EERD, (uint32_t)1 | ((uint32_t)offset << 8));
    /* 等待读取完成 (bit 4 DONE) */
    uint32_t timeout = 100000;
    do {
        eerd = e1000e_read_reg(E1000E_REG_EERD);
    } while (!(eerd & (1 << 4)) && --timeout);
    /* 返回低 16 位数据 */
    return (uint16_t)(eerd & 0xFFFF);
}

/* ---- MAC 地址读取 (从 EEPROM / RAL/RAH) ---- */
static void e1000e_read_mac(void) {
    /* 尝试从 EEPROM 字 0-2 读取 MAC 地址 (82574L 等) */
    uint16_t eep_word0 = e1000e_read_eeprom(0x00);
    uint16_t eep_word1 = e1000e_read_eeprom(0x01);
    uint16_t eep_word2 = e1000e_read_eeprom(0x02);

    /* 检查 EEPROM 是否有效: 第一个字非 0x0000 也非 0xFFFF */
    if (eep_word0 != 0x0000 && eep_word0 != 0xFFFF) {
        e1000e_iface.mac.bytes[0] = (uint8_t)(eep_word0 & 0xFF);
        e1000e_iface.mac.bytes[1] = (uint8_t)((eep_word0 >> 8) & 0xFF);
        e1000e_iface.mac.bytes[2] = (uint8_t)(eep_word1 & 0xFF);
        e1000e_iface.mac.bytes[3] = (uint8_t)((eep_word1 >> 8) & 0xFF);
        e1000e_iface.mac.bytes[4] = (uint8_t)(eep_word2 & 0xFF);
        e1000e_iface.mac.bytes[5] = (uint8_t)((eep_word2 >> 8) & 0xFF);
        return;
    }

    /* 回退: 从 RAL/RAH 寄存器读取 (ICH 系列等) */
    uint32_t low  = e1000e_read_reg(E1000E_REG_RAL(0));
    uint32_t high = e1000e_read_reg(E1000E_REG_RAH(0));
    e1000e_iface.mac.bytes[0] = (uint8_t)(low & 0xFF);
    e1000e_iface.mac.bytes[1] = (uint8_t)((low >> 8) & 0xFF);
    e1000e_iface.mac.bytes[2] = (uint8_t)((low >> 16) & 0xFF);
    e1000e_iface.mac.bytes[3] = (uint8_t)((low >> 24) & 0xFF);
    e1000e_iface.mac.bytes[4] = (uint8_t)(high & 0xFF);
    e1000e_iface.mac.bytes[5] = (uint8_t)((high >> 8) & 0xFF);

    klog_info("e1000e: MAC address from RAL/RAH: %02x:%02x:%02x:%02x:%02x:%02x",
              e1000e_iface.mac.bytes[0], e1000e_iface.mac.bytes[1],
              e1000e_iface.mac.bytes[2], e1000e_iface.mac.bytes[3],
              e1000e_iface.mac.bytes[4], e1000e_iface.mac.bytes[5]);
}

/* ---- MDIO (PHY 访问) ---- */
static int e1000e_mdio_read(uint8_t phy_addr, uint8_t reg_addr, uint16_t *val) {
    uint32_t mdic;
    /* 构造 MDIC 命令: OP=READ, PHY addr, REG addr */
    mdic = E1000E_MDIC_OP_READ |
           ((uint32_t)phy_addr << E1000E_MDIC_PHY_SHIFT) |
           ((uint32_t)reg_addr << E1000E_MDIC_REG_SHIFT);
    e1000e_write_reg(E1000E_REG_MDIC, mdic);

    /* 等待 Ready */
    uint32_t timeout = 100000;
    do {
        mdic = e1000e_read_reg(E1000E_REG_MDIC);
    } while (!(mdic & E1000E_MDIC_READY) && --timeout);
    if (timeout == 0) return -1;

    if (mdic & E1000E_MDIC_ERROR) return -1;

    *val = (uint16_t)(mdic & E1000E_MDIC_DATA_MASK);
    return 0;
}

static int e1000e_mdio_write(uint8_t phy_addr, uint8_t reg_addr, uint16_t val) {
    uint32_t mdic;
    mdic = E1000E_MDIC_OP_WRITE |
           ((uint32_t)phy_addr << E1000E_MDIC_PHY_SHIFT) |
           ((uint32_t)reg_addr << E1000E_MDIC_REG_SHIFT) |
           (val & E1000E_MDIC_DATA_MASK);
    e1000e_write_reg(E1000E_REG_MDIC, mdic);

    /* 等待 Ready */
    uint32_t timeout = 100000;
    do {
        mdic = e1000e_read_reg(E1000E_REG_MDIC);
    } while (!(mdic & E1000E_MDIC_READY) && --timeout);
    if (timeout == 0) return -1;

    return (mdic & E1000E_MDIC_ERROR) ? -1 : 0;
}

/* ---- PHY 初始化和自动协商 ---- */
static void e1000e_phy_init(void) {
    /* 等待 PHY 复位完成 (STATUS.PHYRA 位清零) */
    uint32_t timeout = 100000;
    while ((e1000e_read_reg(E1000E_REG_STATUS) & E1000E_STATUS_PHYRA) && timeout--)
        ;

    /* 触发 PHY 复位 */
    e1000e_write_reg(E1000E_REG_CTRL,
        e1000e_read_reg(E1000E_REG_CTRL) | E1000E_CTRL_PHY_RST);

    /* 等待 PHY 复位完成 */
    timeout = 100000;
    while ((e1000e_read_reg(E1000E_REG_STATUS) & E1000E_STATUS_PHYRA) && timeout--)
        ;

    /* 确保自动协商启用 */
    uint16_t phy_ctrl;
    if (e1000e_mdio_read(1, E1000E_PHY_CTRL, &phy_ctrl) == 0) {
        phy_ctrl |= (1 << 12);  /* 启用自动协商 */
        phy_ctrl |= (1 << 9);   /* 重启自动协商 */
        e1000e_mdio_write(1, E1000E_PHY_CTRL, phy_ctrl);
    }

    klog_info("e1000e: PHY initialized, auto-negotiation started");
}

/* ---- 链路检测 ---- */
static int e1000e_check_link(void) {
    uint32_t status = e1000e_read_reg(E1000E_REG_STATUS);
    if (!(status & E1000E_STATUS_LU)) {
        /* 链路断开, 尝试再次启动 */
        e1000e_write_reg(E1000E_REG_CTRL,
            e1000e_read_reg(E1000E_REG_CTRL) | E1000E_CTRL_SLU);
        return 0;
    }
    return 1;
}

/* ---- RSS 初始化 ---- */
static void e1000e_rss_init(void) {
    uint32_t i;

    /* 设置 RSS 随机密钥 (10 x 32-bit words = 40 bytes) */
    for (i = 0; i < 10; i++) {
        uint32_t key_word = ((uint32_t)e1000e_rss_key[i * 4]     ) |
                            ((uint32_t)e1000e_rss_key[i * 4 + 1] << 8) |
                            ((uint32_t)e1000e_rss_key[i * 4 + 2] << 16) |
                            ((uint32_t)e1000e_rss_key[i * 4 + 3] << 24);
        e1000e_write_reg(E1000E_REG_RSSRK + i * 4, key_word);
    }

    /* 设置 RSS 重定向表 (RETA): 全部定向到 queue 0 */
    for (i = 0; i < 128; i++) {
        e1000e_write_reg(E1000E_REG_RETA + i * 4, 0);
    }

    /* 启用 RSS: 对 IPv4/TCP 进行哈希 */
    uint32_t mrqc = E1000E_MRQC_RSS_ENABLE |
                    E1000E_MRQC_RSS_FIELD_IPV4_TCP |
                    E1000E_MRQC_RSS_FIELD_IPV4;
    e1000e_write_reg(E1000E_REG_MRQC, mrqc);
}

/* ---- 读取统计数据 ---- */
static void e1000e_update_stats(void) {
    e1000e_iface.tx_packets += e1000e_read_reg(E1000E_REG_GPTC);
    e1000e_iface.rx_packets += e1000e_read_reg(E1000E_REG_GPRC);
    /* 字节计数: 合并低高 32 位 */
    uint64_t rx_bytes64 = e1000e_read_reg(E1000E_REG_GORCL) |
                         ((uint64_t)e1000e_read_reg(E1000E_REG_GORCH) << 32);
    uint64_t tx_bytes64 = e1000e_read_reg(E1000E_REG_GOTCL) |
                         ((uint64_t)e1000e_read_reg(E1000E_REG_GOTCH) << 32);
    e1000e_iface.rx_bytes = (uint32_t)(rx_bytes64 & 0xFFFFFFFF);
    e1000e_iface.tx_bytes = (uint32_t)(tx_bytes64 & 0xFFFFFFFF);
}

/* ---- 电源管理 (WoL) 初始化 ---- */
static void e1000e_pm_init(void) {
    /* 启用 Magic Packet 唤醒和链路状态变化唤醒 */
    uint32_t wufc = E1000E_WUFC_MAG | E1000E_WUFC_LNKC;
    e1000e_write_reg(E1000E_REG_WUFC, wufc);

    /* 启用 APM PME */
    e1000e_write_reg(E1000E_REG_WUC,
        e1000e_read_reg(E1000E_REG_WUC) |
        E1000E_WUC_APME | E1000E_WUC_PME_EN);

    /* 通知硬件驱动已加载 */
    e1000e_write_reg(E1000E_REG_CTRL_EXT,
        e1000e_read_reg(E1000E_REG_CTRL_EXT) | E1000E_CTRL_EXT_DRV_LOAD);
}

/* ---- 初始化主函数 ---- */
void e1000e_init(uint8_t bus, uint8_t dev, uint8_t func) {
    /* 读取 BAR0 (MMIO 基地址) */
    uint32_t bar0 = pci_read_config(bus, dev, func, 0x10);
    uint32_t phys_addr = bar0 & 0xFFFFFFF0;
    mmio_base = (volatile uint32_t *)vmm_map_physical(phys_addr, 0x20000);

    if (!mmio_base) {
        klog_err("e1000e: Failed to map MMIO at 0x%x", phys_addr);
        return;
    }

    /* 启用 PCI 总线主控 (Bus Master)、内存空间 (Memory Space) 和 I/O 空间 */
    uint32_t cmd = pci_read_config(bus, dev, func, 0x04) & 0xFFFF;
    pci_write_config(bus, dev, func, 0x04, cmd | 0x07);

    /* ---- 软复位设备 ---- */
    e1000e_write_reg(E1000E_REG_CTRL,
        e1000e_read_reg(E1000E_REG_CTRL) | E1000E_CTRL_RST);
    uint32_t timeout = 100000;
    while ((e1000e_read_reg(E1000E_REG_CTRL) & E1000E_CTRL_RST) && timeout--)
        ;
    if (timeout == 0) {
        klog_err("e1000e: Device reset timeout");
        return;
    }

    /* 全局复位后等待 10ms */
    for (volatile int i = 0; i < 10000; i++) __asm__ __volatile__("pause");

    /* 禁止中断 */
    e1000e_write_reg(E1000E_REG_IMC, 0xFFFFFFFF);

    /* ---- MAC 初始化 ---- */
    /* 读取 MAC 地址 */
    e1000e_read_mac();

    /* ---- 分配并初始化描述符环 ---- */
    /* TX descriptors */
    tx_descs = (e1000e_tx_desc_t *)kmalloc(E1000E_NUM_TX_DESC * sizeof(e1000e_tx_desc_t));
    if (!tx_descs) { klog_err("e1000e: Failed to alloc TX descriptors"); return; }
    memset(tx_descs, 0, E1000E_NUM_TX_DESC * sizeof(e1000e_tx_desc_t));

    /* RX descriptors */
    rx_descs = (e1000e_rx_desc_t *)kmalloc(E1000E_NUM_RX_DESC * sizeof(e1000e_rx_desc_t));
    if (!rx_descs) { klog_err("e1000e: Failed to alloc RX descriptors"); return; }
    memset(rx_descs, 0, E1000E_NUM_RX_DESC * sizeof(e1000e_rx_desc_t));

    /* 分配 TX buffers 并初始化描述符 */
    uint32_t i;
    for (i = 0; i < E1000E_NUM_TX_DESC; i++) {
        tx_buffers[i] = kmalloc(E1000E_TX_BUF_SIZE);
        if (!tx_buffers[i]) { klog_err("e1000e: Failed to alloc TX buffer %d", i); return; }
        memset(tx_buffers[i], 0, E1000E_TX_BUF_SIZE);
        tx_descs[i].addr    = (uint64_t)(uintptr_t)tx_buffers[i];
        tx_descs[i].length  = 0;
        tx_descs[i].cso     = 0;
        tx_descs[i].cmd     = 0;
        tx_descs[i].status  = 0;
        tx_descs[i].css     = 0;
        tx_descs[i].special = 0;
    }

    /* 分配 RX buffers 并初始化描述符 */
    for (i = 0; i < E1000E_NUM_RX_DESC; i++) {
        rx_buffers[i] = kmalloc(E1000E_RX_BUF_SIZE);
        if (!rx_buffers[i]) { klog_err("e1000e: Failed to alloc RX buffer %d", i); return; }
        memset(rx_buffers[i], 0, E1000E_RX_BUF_SIZE);
        rx_descs[i].addr     = (uint64_t)(uintptr_t)rx_buffers[i];
        rx_descs[i].length   = 0;
        rx_descs[i].checksum = 0;
        rx_descs[i].status   = 0;   /* DD=0, NIC owns descriptor */
        rx_descs[i].errors   = 0;
        rx_descs[i].special  = 0;
    }

    tx_tail = 0;
    rx_tail = E1000E_NUM_RX_DESC - 1;

    /* ---- 配置接收引擎 ---- */
    e1000e_write_reg(E1000E_REG_RDBAL, (uint32_t)(uintptr_t)rx_descs);
    e1000e_write_reg(E1000E_REG_RDBAH, 0);
    e1000e_write_reg(E1000E_REG_RDLEN, E1000E_NUM_RX_DESC * sizeof(e1000e_rx_desc_t));
    e1000e_write_reg(E1000E_REG_RDH, 0);
    e1000e_write_reg(E1000E_REG_RDT, E1000E_NUM_RX_DESC - 1);

    /* 配置 RCTL:
     * - EN: 启用接收
     * - BAM: 广播接受模式
     * - SECRC: 剥离以太网 CRC
     * - BSIZE_2048: 2KB 接收缓冲区
     * - VFE: VLAN 过滤启用
     */
    e1000e_write_reg(E1000E_REG_RCTL,
        E1000E_RCTL_EN |
        E1000E_RCTL_BAM |
        E1000E_RCTL_SECRC |
        E1000E_RCTL_BSIZE_2048 |
        E1000E_RCTL_VFE |
        E1000E_RCTL_LBM_NONE);

    /* RX Descriptor Control: PTHRESH=1 */
    e1000e_write_reg(E1000E_REG_RXDCTL, (1 << 24));

    /* ---- 配置发送引擎 ---- */
    e1000e_write_reg(E1000E_REG_TDBAL, (uint32_t)(uintptr_t)tx_descs);
    e1000e_write_reg(E1000E_REG_TDBAH, 0);
    e1000e_write_reg(E1000E_REG_TDLEN, E1000E_NUM_TX_DESC * sizeof(e1000e_tx_desc_t));
    e1000e_write_reg(E1000E_REG_TDH, 0);
    e1000e_write_reg(E1000E_REG_TDT, 0);

    /* 配置 TCTL:
     * - EN: 启用发送
     * - PSP: 填充短包
     * - CT=15 (半双工) / 63 (全双工)
     * - COLD=0x40
     */
    e1000e_write_reg(E1000E_REG_TCTL,
        E1000E_TCTL_EN |
        E1000E_TCTL_PSP |
        (0x3F << 4) |       /* CT=63 (全双工) */
        (0x40 << 12));      /* COLD=64         */

    /* TX Descriptor Control */
    e1000e_write_reg(E1000E_REG_TXDCTL, (1 << 24));

    /* TIPG (Inter Packet Gap) - 标准值 */
    e1000e_write_reg(E1000E_REG_TIPG,
        (6 << 0) |          /* IPGT: 6 (10/100/1000)     */
        (8 << 10) |         /* IPGR1: 8 (2 for 1000)     */
        (4 << 20));         /* IPGR2: 4 (6 for 1000)     */

    /* ---- 中断配置 ---- */
    /* 清除所有待处理中断 */
    e1000e_write_reg(E1000E_REG_ICR, 0xFFFFFFFF);

    /* 尝试检测 MSI-X 能力 (通过 PCI 配置空间) */
    uint8_t msi_cap = (uint8_t)(pci_read_config(bus, dev, func, 0x34) & 0xFF);
    if (msi_cap != 0 && msi_cap != 0xFF) {
        /* MSI-X 能力存在, 尝试启用 */
        uint32_t msix_ctrl = pci_read_config(bus, dev, func, msi_cap + 2);
        /* 检查是否为 MSI-X (Capability ID = 0x11) */
        uint8_t cap_id = (uint8_t)pci_read_config(bus, dev, func, msi_cap);
        if (cap_id == 0x11) {
            /* 启用 MSI-X: 设置 Enable 位 (bit 15) */
            pci_write_config(bus, dev, func, msi_cap + 2,
                             msix_ctrl | (1 << 15));
            use_msix = 1;
            klog_info("e1000e: MSI-X enabled");
        }
    }

    /* 注册传统 INTx 中断处理程序 */
    uint8_t irq_line = (uint8_t)(pci_read_config(bus, dev, func, 0x3C) & 0xFF);
    irq_vector = 0x20 + irq_line;
    if (!use_msix) {
        irq_register_handler(irq_vector, e1000e_irq_handler);
    }

    /* 启用中断: TXDW, LSC, RXT0, RXO, RXDMT0 */
    e1000e_write_reg(E1000E_REG_IMS,
        E1000E_ICR_TXDW |
        E1000E_ICR_LSC |
        E1000E_ICR_RXT0 |
        E1000E_ICR_RXO |
        E1000E_ICR_RXDMT0);

    /* ---- PHY 初始化 ---- */
    e1000e_phy_init();

    /* ---- RSS 初始化 ---- */
    e1000e_rss_init();

    /* ---- TSO 支持 ---- */
    /* 硬件总能支持 TSO, 在 send 路径中通过 Context Descriptor 实现 */

    /* ---- 电源管理 (WoL) ---- */
    e1000e_pm_init();

    /* ---- VLAN 设置 ---- */
    /* VLAN EtherType = 0x8100 (标准 802.1Q) */
    e1000e_write_reg(E1000E_REG_VET, 0x8100);
    /* 初始化 VLAN 过滤表 (全部通过) */
    for (i = 0; i < 128; i++) {
        e1000e_write_reg(E1000E_REG_VFTA + i * 4, 0xFFFFFFFF);
    }

    /* ---- 强制链路启动 ---- */
    e1000e_write_reg(E1000E_REG_CTRL,
        e1000e_read_reg(E1000E_REG_CTRL) | E1000E_CTRL_SLU);

    /* 等待链路建立 */
    timeout = 500000;
    while (!(e1000e_read_reg(E1000E_REG_STATUS) & E1000E_STATUS_LU) && timeout--)
        ;
    if (timeout == 0) {
        klog_warn("e1000e: Link not up after init (timeout)");
    } else {
        uint32_t speed_field = e1000e_read_reg(E1000E_REG_STATUS) & E1000E_STATUS_SPEED_MASK;
        const char *speed_str = "Unknown";
        if (speed_field == E1000E_STATUS_SPEED_10)   speed_str = "10";
        else if (speed_field == E1000E_STATUS_SPEED_100) speed_str = "100";
        else if (speed_field == E1000E_STATUS_SPEED_1000) speed_str = "1000";
        klog_info("e1000e: Link up at %s Mbps %s duplex",
                  speed_str,
                  (e1000e_read_reg(E1000E_REG_STATUS) & E1000E_STATUS_FD) ? "Full" : "Half");
    }

    /* ---- 注册网络接口 ---- */
    strcpy(e1000e_iface.name, "em0");
    e1000e_iface.up          = 1;
    e1000e_iface.mtu         = 1500;
    e1000e_iface.flags       = IFF_UP | IFF_BROADCAST | IFF_RUNNING | IFF_MULTICAST;
    e1000e_iface.send        = e1000e_send;
    e1000e_iface.driver_data = (void *)0;
    e1000e_iface.tx_packets  = 0;
    e1000e_iface.rx_packets  = 0;
    e1000e_iface.tx_bytes    = 0;
    e1000e_iface.rx_bytes    = 0;
    e1000e_iface.tx_errors   = 0;
    e1000e_iface.rx_errors   = 0;
    net_register_interface(&e1000e_iface);

    e1000e_inited = 1;
    klog_info("e1000e: Driver initialized successfully, iface=%s", e1000e_iface.name);
}

/* ---- 发送数据包 ---- */
int e1000e_send(net_interface_t *iface, const void *data, uint32_t len) {
    if (!e1000e_inited || len > E1000E_TX_BUF_SIZE) return -1;

    /* 复制数据到 TX 缓冲区 */
    memcpy(tx_buffers[tx_tail], data, len);

    /* 填写 TX 描述符:
     * EOP: 包结束标志
     * IFCS: 让硬件追加 CRC
     * IC: 让硬件计算 IP/TCP/UDP 校验和 (checksum offload)
     * RS: 完成时报告状态
     */
    tx_descs[tx_tail].length  = len;
    tx_descs[tx_tail].cso     = 0;    /* Checksum Offset (由 IC 自动处理) */
    tx_descs[tx_tail].cmd     = E1000E_TX_CMD_EOP |
                                E1000E_TX_CMD_IFCS |
                                E1000E_TX_CMD_IC |
                                E1000E_TX_CMD_RS;
    tx_descs[tx_tail].status  = 0;
    tx_descs[tx_tail].css     = 0;
    tx_descs[tx_tail].special = 0;

    /* 更新尾部指针并通知硬件 */
    uint32_t old_tail = tx_tail;
    tx_tail = (tx_tail + 1) % E1000E_NUM_TX_DESC;
    e1000e_write_reg(E1000E_REG_TDT, tx_tail);

    (void)iface;
    return (int)old_tail;
}

/* ---- 带 TSO 的发送 (TCP Segmentation Offload) ---- */
int e1000e_send_tso(net_interface_t *iface, const void *hdr, uint32_t hdr_len,
                    const void *payload, uint32_t payload_len, uint16_t mss) {
    if (!e1000e_inited) return -1;

    /* TSO 需要:
     * 1. 一个 Context Descriptor (设置 TSO 参数: MSS, 头部长度)
     * 2. 一个 Data Descriptor (指向包含头部+负载的缓冲区)
     */

    /* 步骤 1: 写 Context Descriptor */
    uint32_t ctx_idx = tx_tail;
    e1000e_tx_context_desc_t *ctx = (e1000e_tx_context_desc_t *)&tx_descs[ctx_idx];

    ctx->addr = (uint64_t)(uintptr_t)0;  /* Context descriptor 无 buffer */
    /* TUCSE: TCP/UDP 校验和起始偏移 = hdr_len */
    /* MSS: 最大段大小 */
    /* HDRLEN: 头部长度 / 4 */
    ctx->tcp_opt     = (hdr_len << 9) |       /* HDRLEN (bits 9-16)    */
                       ((uint32_t)mss << 16) | /* MSS (bits 16-30)      */
                       (1 << 0);               /* TSE (bit 0): TSO Ena  */
    ctx->payload_len = payload_len;
    tx_descs[ctx_idx].cmd = E1000E_TX_CMD_DEXT |  /* Context 类型标识 */
                            E1000E_TX_CMD_RS;
    tx_descs[ctx_idx].status = 0;
    tx_tail = (tx_tail + 1) % E1000E_NUM_TX_DESC;

    /* 步骤 2: 写 Data Descriptor (头部+负载合并) */
    uint32_t data_idx = tx_tail;
    uint32_t total_len = hdr_len + payload_len;
    if (total_len > E1000E_TX_BUF_SIZE) return -1;

    memcpy(tx_buffers[data_idx], hdr, hdr_len);
    memcpy((uint8_t *)tx_buffers[data_idx] + hdr_len, payload, payload_len);

    tx_descs[data_idx].length  = total_len;
    tx_descs[data_idx].cso     = 0;
    tx_descs[data_idx].cmd     = E1000E_TX_CMD_EOP |
                                 E1000E_TX_CMD_IFCS |
                                 E1000E_TX_CMD_IC |
                                 E1000E_TX_CMD_RS;
    tx_descs[data_idx].status  = 0;
    tx_descs[data_idx].css     = 0;
    tx_descs[data_idx].special = 0;
    tx_tail = (tx_tail + 1) % E1000E_NUM_TX_DESC;

    /* 通知硬件 */
    e1000e_write_reg(E1000E_REG_TDT, tx_tail);

    (void)iface;
    return (int)data_idx;
}

/* ---- 轮询接收数据包 ---- */
void e1000e_poll(void) {
    if (!e1000e_inited) return;

    uint32_t i;
    for (i = 0; i < E1000E_NUM_RX_DESC; i++) {
        if (rx_descs[i].status & E1000E_RX_STATUS_DD) {
            /* 描述符完成 (DD=1), 数据可用 */
            net_buffer_t *buf = net_alloc_buffer();
            if (buf) {
                uint16_t pkt_len = rx_descs[i].length;
                memcpy(buf->data, rx_buffers[i], pkt_len);
                buf->len    = pkt_len;
                buf->offset = 0;
                buf->iface  = &e1000e_iface;

                /* 检查错误标志 */
                if (rx_descs[i].errors & (E1000E_RX_ERROR_CE |
                                          E1000E_RX_ERROR_SE |
                                          E1000E_RX_ERROR_RXE)) {
                    e1000e_iface.rx_errors++;
                    net_free_buffer(buf);
                } else {
                    e1000e_iface.rx_packets++;
                    e1000e_iface.rx_bytes += pkt_len;
                    net_receive(buf);
                }
            }

            /* 归还描述符给 NIC */
            memset(rx_buffers[i], 0, E1000E_RX_BUF_SIZE);
            rx_descs[i].addr     = (uint64_t)(uintptr_t)rx_buffers[i];
            rx_descs[i].status   = 0;
            rx_descs[i].length   = 0;
            rx_descs[i].errors   = 0;
            rx_descs[i].checksum = 0;
            rx_descs[i].special  = 0;

            /* 更新 RX 尾指针 */
            rx_tail = (rx_tail + 1) % E1000E_NUM_RX_DESC;
            e1000e_write_reg(E1000E_REG_RDT, rx_tail);
        }
    }
}

/* ---- 中断处理 ---- */
void e1000e_irq_handler(regs_t *regs) {
    if (!e1000e_inited) return;

    uint32_t icr = e1000e_read_reg(E1000E_REG_ICR);

    /* 检查是否有需要处理的中断 */
    if (icr == 0) {
        (void)regs;
        return;
    }

    /* 接收相关中断 -> 轮询收取数据包 */
    if (icr & (E1000E_ICR_RXDMT0 | E1000E_ICR_RXT0 | E1000E_ICR_RXO |
               E1000E_ICR_SRPD)) {
        e1000e_poll();
    }

    /* TX 描述符写回中断 */
    if (icr & E1000E_ICR_TXDW) {
        /* 发送完成, 可用于 TX buffer 回收 */
    }

    /* 链路状态变化 */
    if (icr & E1000E_ICR_LSC) {
        /* 重新检查链路状态 */
        uint32_t status = e1000e_read_reg(E1000E_REG_STATUS);
        if (status & E1000E_STATUS_LU) {
            e1000e_iface.flags |= IFF_RUNNING;
        } else {
            e1000e_iface.flags &= ~IFF_RUNNING;
        }
    }

    /* MDIO 访问完成 */
    if (icr & E1000E_ICR_MDAC) {
        /* MDIO 操作完成, 无需特殊处理 */
    }

    /* 清除已处理的中断标志 */
    e1000e_write_reg(E1000E_REG_ICR, icr);

    (void)regs;
}

/* ---- PCI 探测入口 ---- */
int e1000e_probe(void) {
    static const uint16_t e1000e_dev_ids[] = {
        E1000E_DEV_ID_82574L,
        E1000E_DEV_ID_82574LA,
        E1000E_DEV_ID_ICH8_IGP_M,
        E1000E_DEV_ID_ICH8_IGP_AM,
        E1000E_DEV_ID_ICH8_IFE,
        E1000E_DEV_ID_ICH8_IFE_G,
        E1000E_DEV_ID_ICH8_IFE_GT,
        E1000E_DEV_ID_ICH9_IGP_M,
        E1000E_DEV_ID_ICH9_IGP_AM,
        E1000E_DEV_ID_ICH9_IFE,
        E1000E_DEV_ID_ICH9_IFE_G,
        E1000E_DEV_ID_ICH9_IFE_GT,
        E1000E_DEV_ID_ICH10_R_BM,
        E1000E_DEV_ID_ICH10_D_BM,
        E1000E_DEV_ID_ICH10_R_BM_V,
        E1000E_DEV_ID_ICH10_D_BM_V
    };
    static const int num_dev_ids = sizeof(e1000e_dev_ids) / sizeof(e1000e_dev_ids[0]);

    uint16_t bus;
    uint8_t  dev, func;
    int      found = 0;

    for (bus = 0; bus < PCI_MAX_BUSES && !found; bus++) {
        for (dev = 0; dev < PCI_MAX_DEVICES && !found; dev++) {
            for (func = 0; func < PCI_MAX_FUNCTIONS && !found; func++) {
                uint32_t vd = pci_read_config((uint8_t)bus, dev, func, 0x00);
                if (vd == 0xFFFFFFFF || vd == 0) continue;

                uint16_t vendor_id = vd & 0xFFFF;
                uint16_t device_id = (vd >> 16) & 0xFFFF;

                if (vendor_id == E1000E_VENDOR_ID) {
                    int j;
                    for (j = 0; j < num_dev_ids; j++) {
                        if (device_id == e1000e_dev_ids[j]) {
                            found = 1;
                            klog_info("e1000e: Found device %04x:%04x at %02x:%02x.%x",
                                      vendor_id, device_id, bus, dev, func);
                            e1000e_init((uint8_t)bus, dev, func);
                            break;
                        }
                    }
                }
            }
        }
    }

    return found ? 0 : -1;
}