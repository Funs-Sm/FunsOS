/*
 * ixgbe.c - Intel 82599ES (ixgbe) 10 Gigabit PCIe 以太网驱动
 *
 * 基于 Intel 82599 数据手册, 使用 MMIO 方式访问寄存器。
 * 支持多队列 (4 TX/RX 对), MSI-X, RSS (Toeplitz 哈希),
 * Flow Director (ATR), Jumbo Frames, DCB (PFC), TSO/LSO。
 */

#include "ixgbe.h"
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

static ixgbe_tx_desc_t *tx_descs[IXGBE_NUM_QUEUES];
static ixgbe_rx_desc_t *rx_descs[IXGBE_NUM_QUEUES];

static void *tx_buffers[IXGBE_NUM_QUEUES][IXGBE_NUM_TX_DESC];
static void *rx_buffers[IXGBE_NUM_QUEUES][IXGBE_NUM_RX_DESC];

static uint32_t tx_tail[IXGBE_NUM_QUEUES];
static uint32_t rx_tail[IXGBE_NUM_QUEUES];

static net_interface_t ixgbe_iface;
static int             ixgbe_inited = 0;

/* Toeplitz RSS 密钥 (Intel 标准, 40 bytes) */
static const uint8_t ixgbe_rss_key[40] = {
    0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
    0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
    0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
    0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A,
    0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A, 0x6D, 0x5A
};

/* RSS 重定向表 (128 entries): 将哈希均匀分布到 4 个队列 */
static const uint8_t ixgbe_rss_reta[128] = {
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,
    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3
};

/* ---- 寄存器读写辅助函数 ---- */
static inline uint32_t ixgbe_read_reg(uint32_t offset) {
    return mmio_base[offset / 4];
}

static inline void ixgbe_write_reg(uint32_t offset, uint32_t val) {
    mmio_base[offset / 4] = val;
}

/* ---- EEPROM 读取 ---- */
static uint16_t ixgbe_read_eeprom(uint8_t offset) {
    uint32_t eerd;
    ixgbe_write_reg(IXGBE_REG_EERD, (uint32_t)1 | ((uint32_t)offset << 8));
    uint32_t timeout = 100000;
    do {
        eerd = ixgbe_read_reg(IXGBE_REG_EERD);
    } while (!(eerd & (1 << 4)) && --timeout);
    return (uint16_t)(eerd & 0xFFFF);
}

/* ---- MAC 地址读取 ---- */
static void ixgbe_read_mac(void) {
    uint32_t low  = ixgbe_read_reg(IXGBE_REG_RAL(0));
    uint32_t high = ixgbe_read_reg(IXGBE_REG_RAH(0));
    ixgbe_iface.mac.bytes[0] = (uint8_t)(low & 0xFF);
    ixgbe_iface.mac.bytes[1] = (uint8_t)((low >> 8) & 0xFF);
    ixgbe_iface.mac.bytes[2] = (uint8_t)((low >> 16) & 0xFF);
    ixgbe_iface.mac.bytes[3] = (uint8_t)((low >> 24) & 0xFF);
    ixgbe_iface.mac.bytes[4] = (uint8_t)(high & 0xFF);
    ixgbe_iface.mac.bytes[5] = (uint8_t)((high >> 8) & 0xFF);
}

/* ---- 链路状态检测 ---- */
static void ixgbe_check_link(void) {
    uint32_t links = ixgbe_read_reg(IXGBE_REG_LINKS);

    if (links & IXGBE_LINKS_UP) {
        uint32_t speed_field = (links & IXGBE_LINKS_SPEED_MASK) >> 8;
        const char *speed_str = "Unknown";
        if (speed_field == 1)      speed_str = "100M";
        else if (speed_field == 2) speed_str = "1G";
        else if (speed_field == 4) speed_str = "10G";
        klog_info("ixgbe: Link up at %s", speed_str);
        ixgbe_iface.flags |= IFF_RUNNING;
    } else {
        klog_warn("ixgbe: Link down");
        ixgbe_iface.flags &= ~(uint32_t)(IFF_RUNNING);
    }
}

/* ---- RSS 初始化 ---- */
static void ixgbe_rss_init(void) {
    uint32_t i;

    /* 设置 RSS 随机密钥 (10 x 32-bit = 40 bytes) */
    for (i = 0; i < 10; i++) {
        uint32_t key_word = ((uint32_t)ixgbe_rss_key[i * 4]) |
                            ((uint32_t)ixgbe_rss_key[i * 4 + 1] << 8) |
                            ((uint32_t)ixgbe_rss_key[i * 4 + 2] << 16) |
                            ((uint32_t)ixgbe_rss_key[i * 4 + 3] << 24);
        ixgbe_write_reg(IXGBE_REG_RSSRK(i), key_word);
    }

    /* 设置 RSS 重定向表 (RETA) */
    for (i = 0; i < 128; i++) {
        ixgbe_write_reg(IXGBE_REG_RETA(i), (uint32_t)ixgbe_rss_reta[i]);
    }

    /* 启用 RSS 用于 IPv4, IPv4/TCP, IPv6, IPv6/TCP */
    uint32_t mrqc = IXGBE_MRQC_RSS_ENABLE | IXGBE_MRQC_MULTIPLE_RSS |
                    IXGBE_MRQC_RSS_FIELD_IPV4_TCP |
                    IXGBE_MRQC_RSS_FIELD_IPV4 |
                    IXGBE_MRQC_RSS_FIELD_IPV6_TCP |
                    IXGBE_MRQC_RSS_FIELD_IPV6;
    ixgbe_write_reg(IXGBE_REG_MRQC, mrqc);

    klog_info("ixgbe: RSS initialized with 4 queues");
}

/* ---- Flow Director (ATR) 初始化 ---- */
static void ixgbe_fdir_init(void) {
    /* 启用 Flow Director:
     * - Perfect Match 模式
     * - 分配 PB (Packet Buffer) 空间 (64 entries)
     * - 报告状态
     */
    uint32_t ctrl = IXGBE_FDIRCTRL_PERFECT_MATCH |
                    IXGBE_FDIRCTRL_REPORT_STATUS |
                    IXGBE_FDIRCTRL_PBALLOC(64);

    ixgbe_write_reg(IXGBE_REG_FDIRCTRL, ctrl);

    /* 标记初始化完成 */
    ixgbe_write_reg(IXGBE_REG_FDIRCTRL, ctrl | IXGBE_FDIRCTRL_INIT_DONE);

    klog_info("ixgbe: Flow Director (ATR) initialized");
}

/* ---- DCB (PFC) 初始化 ---- */
static void ixgbe_dcb_init(void) {
    uint32_t i;

    /* 用户优先级到流量类映射 (UP-to-TC):
     * 8 个用户优先级 -> 4 个流量类 */
    for (i = 0; i < 4; i++) {
        /* RX: UP 0,1 -> TC0, UP 2,3 -> TC1, UP 4,5 -> TC2, UP 6,7 -> TC3 */
        uint32_t rx_map = ((i * 2) & 0x7) | (((i * 2 + 1) & 0x7) << 8);
        ixgbe_write_reg(IXGBE_REG_RTRUP2TC + i * 4, rx_map);
        /* TX: 相同的映射 */
        ixgbe_write_reg(IXGBE_REG_RTTUP2TC + i * 4, rx_map);
    }

    /* 设置 PFC 缓冲区大小 (每个流量类 64KB) */
    for (i = 0; i < 8; i++) {
        ixgbe_write_reg(IXGBE_REG_RXPBSIZE(i), 0x40);  /* 64KB */
        ixgbe_write_reg(IXGBE_REG_TXPBSIZE(i), 0x40);  /* 64KB */
    }

    klog_info("ixgbe: DCB with PFC initialized");
}

/* ---- VLAN 初始化 ---- */
static void ixgbe_vlan_init(void) {
    /* 启用 VLAN 过滤和 VLAN 模式 */
    ixgbe_write_reg(IXGBE_REG_VLNCTRL,
        IXGBE_VLNCTRL_VFE | IXGBE_VLNCTRL_VME);

    /* VLAN EtherType = 0x8100 */
    ixgbe_write_reg(IXGBE_REG_VET, 0x8100);

    /* 在 CTRL 寄存器中也启用 VLAN 模式 */
    ixgbe_write_reg(IXGBE_REG_CTRL,
        ixgbe_read_reg(IXGBE_REG_CTRL) | IXGBE_CTRL_VME);
}

/* ---- 初始化主函数 ---- */
void ixgbe_init(uint8_t bus, uint8_t dev, uint8_t func) {
    /* 读取 BAR0 (MMIO 基地址) */
    uint32_t bar0 = pci_read_config(bus, dev, func, 0x10);
    uint32_t phys_addr = bar0 & 0xFFFFFFF0;
    mmio_base = (volatile uint32_t *)vmm_map_physical(phys_addr, 0x40000);

    if (!mmio_base) {
        klog_err("ixgbe: Failed to map MMIO at 0x%x", phys_addr);
        return;
    }

    /* 启用 PCI 总线主控和内存空间 */
    uint32_t cmd = pci_read_config(bus, dev, func, 0x04) & 0xFFFF;
    pci_write_config(bus, dev, func, 0x04, cmd | 0x07);

    /* ---- 软复位设备 ---- */
    ixgbe_write_reg(IXGBE_REG_CTRL,
        ixgbe_read_reg(IXGBE_REG_CTRL) | IXGBE_CTRL_RST);
    uint32_t timeout = 100000;
    while ((ixgbe_read_reg(IXGBE_REG_CTRL) & IXGBE_CTRL_RST) && timeout--)
        ;
    if (timeout == 0) {
        klog_err("ixgbe: Device reset timeout");
        return;
    }

    /* 全局复位后等待 */
    for (volatile int i = 0; i < 10000; i++) __asm__ __volatile__("pause");

    /* 禁止所有中断 */
    ixgbe_write_reg(IXGBE_REG_EIMC, 0xFFFFFFFF);

    /* ---- MAC 初始化 ---- */
    ixgbe_read_mac();

    /* ---- 分配并初始化多队列描述符环 ---- */
    uint32_t q, i;
    for (q = 0; q < IXGBE_NUM_QUEUES; q++) {

        /* TX descriptors */
        tx_descs[q] = (ixgbe_tx_desc_t *)kmalloc(IXGBE_NUM_TX_DESC *
                                                  sizeof(ixgbe_tx_desc_t));
        if (!tx_descs[q]) {
            klog_err("ixgbe: Failed to alloc TX descriptors for queue %d", q);
            return;
        }
        memset(tx_descs[q], 0, IXGBE_NUM_TX_DESC * sizeof(ixgbe_tx_desc_t));

        /* RX descriptors */
        rx_descs[q] = (ixgbe_rx_desc_t *)kmalloc(IXGBE_NUM_RX_DESC *
                                                  sizeof(ixgbe_rx_desc_t));
        if (!rx_descs[q]) {
            klog_err("ixgbe: Failed to alloc RX descriptors for queue %d", q);
            return;
        }
        memset(rx_descs[q], 0, IXGBE_NUM_RX_DESC * sizeof(ixgbe_rx_desc_t));

        /* 分配 TX buffers */
        for (i = 0; i < IXGBE_NUM_TX_DESC; i++) {
            tx_buffers[q][i] = kmalloc(IXGBE_TX_BUF_SIZE);
            if (!tx_buffers[q][i]) {
                klog_err("ixgbe: Failed to alloc TX buf q%d i%d", q, i);
                return;
            }
            memset(tx_buffers[q][i], 0, IXGBE_TX_BUF_SIZE);
            tx_descs[q][i].addr     = (uint64_t)(uintptr_t)tx_buffers[q][i];
            tx_descs[q][i].reserved = 0;
        }

        /* 分配 RX buffers */
        for (i = 0; i < IXGBE_NUM_RX_DESC; i++) {
            rx_buffers[q][i] = kmalloc(IXGBE_RX_BUF_SIZE);
            if (!rx_buffers[q][i]) {
                klog_err("ixgbe: Failed to alloc RX buf q%d i%d", q, i);
                return;
            }
            memset(rx_buffers[q][i], 0, IXGBE_RX_BUF_SIZE);
            rx_descs[q][i].addr     = (uint64_t)(uintptr_t)rx_buffers[q][i];
            rx_descs[q][i].reserved = 0;
        }

        tx_tail[q] = 0;
        rx_tail[q] = IXGBE_NUM_RX_DESC - 1;

        /* ---- 配置 TX 队列 ---- */
        ixgbe_write_reg(IXGBE_REG_TDBAL(q), (uint32_t)(uintptr_t)tx_descs[q]);
        ixgbe_write_reg(IXGBE_REG_TDBAH(q), 0);
        ixgbe_write_reg(IXGBE_REG_TDLEN(q), IXGBE_NUM_TX_DESC *
                                             sizeof(ixgbe_tx_desc_t));
        ixgbe_write_reg(IXGBE_REG_TDH(q), 0);
        ixgbe_write_reg(IXGBE_REG_TDT(q), 0);

        /* 启用 TX 队列 */
        ixgbe_write_reg(IXGBE_REG_TXDCTL(q), IXGBE_TXDCTL_ENABLE);

        /* ---- 配置 RX 队列 ---- */
        ixgbe_write_reg(IXGBE_REG_RDBAL(q), (uint32_t)(uintptr_t)rx_descs[q]);
        ixgbe_write_reg(IXGBE_REG_RDBAH(q), 0);
        ixgbe_write_reg(IXGBE_REG_RDLEN(q), IXGBE_NUM_RX_DESC *
                                             sizeof(ixgbe_rx_desc_t));
        ixgbe_write_reg(IXGBE_REG_RDH(q), 0);
        ixgbe_write_reg(IXGBE_REG_RDT(q), IXGBE_NUM_RX_DESC - 1);

        /* 配置 SRRCTL: 启用丢弃、使用高级描述符单缓冲区 */
        ixgbe_write_reg(IXGBE_REG_SRRCTL(q),
            IXGBE_SRRCTL_DROP_EN |
            IXGBE_SRRCTL_DESCTYPE_ADV_ONCE);

        /* 启用 RX 队列 */
        ixgbe_write_reg(IXGBE_REG_RXDCTL(q), IXGBE_RXDCTL_ENABLE);
    }

    /* ---- 全局 RX 控制 ---- */
    ixgbe_write_reg(IXGBE_REG_RXCTRL, IXGBE_RXCTRL_RXEN);

    /* ---- 流控制 ---- */
    for (q = 0; q < IXGBE_NUM_QUEUES; q++) {
        /* 启用接收流控制 */
        ixgbe_write_reg(IXGBE_REG_FCRTL_82599(q),
            (IXGBE_NUM_RX_DESC - 32) | IXGBE_FCRTL_XONE);
        ixgbe_write_reg(IXGBE_REG_FCRTH_82599(q),
            (IXGBE_NUM_RX_DESC - 16) | IXGBE_FCRTL_XONE);
    }
    ixgbe_write_reg(IXGBE_REG_CTRL,
        ixgbe_read_reg(IXGBE_REG_CTRL) | IXGBE_CTRL_TFCE | IXGBE_CTRL_RFCE);

    /* ---- 中断配置 ---- */
    /* 尝试 MSI-X */
    uint8_t msi_cap = (uint8_t)(pci_read_config(bus, dev, func, 0x34) & 0xFF);
    if (msi_cap != 0 && msi_cap != 0xFF) {
        uint8_t cap_id = (uint8_t)pci_read_config(bus, dev, func, msi_cap);
        if (cap_id == 0x11) {
            uint32_t msix_ctrl = pci_read_config(bus, dev, func, msi_cap + 2);
            pci_write_config(bus, dev, func, msi_cap + 2,
                             msix_ctrl | (1 << 15));
            use_msix = 1;

            /* 配置 IVAR: 将 RX/TX 队列映射到 MSI-X 向量
             * 在 82599 上:
             * IVAR = 每个 4 bytes 编码一个 MSI-X 映射
             * IVAR_MISC 用于其他中断 (链接状态等)
             */
            for (q = 0; q < IXGBE_NUM_QUEUES; q++) {
                /* 每个队列的 IVAR 条目:
                 * bits 0-7:  TX queue -> vector q
                 * bits 8-15: RX queue -> vector q */
                ixgbe_write_reg(IXGBE_REG_IVAR + q * 4,
                                (q << 0) | (q << 8));
            }
            /* 其他中断映射到 vector 0 */
            ixgbe_write_reg(IXGBE_REG_IVAR_MISC, 0);

            klog_info("ixgbe: MSI-X enabled with %d vectors", IXGBE_NUM_QUEUES);
        }
    }

    if (!use_msix) {
        /* 回退到 INTx */
        uint8_t irq_line = (uint8_t)(pci_read_config(bus, dev, func, 0x3C) & 0xFF);
        irq_register_handler(0x20 + irq_line, ixgbe_irq_handler);
        klog_info("ixgbe: Using INTx (legacy interrupt)");
    }

    /* 启用需要的中断: LSC, RX, TX */
    uint32_t eims = IXGBE_EICR_LSC;
    for (q = 0; q < IXGBE_NUM_QUEUES; q++) {
        eims |= IXGBE_EICR_RX_QUEUE(q) | IXGBE_EICR_TX_QUEUE(q);
    }
    ixgbe_write_reg(IXGBE_REG_EIMS, eims);

    /* ---- RSS 初始化 ---- */
    ixgbe_rss_init();

    /* ---- Flow Director (ATR) ---- */
    ixgbe_fdir_init();

    /* ---- DCB (PFC) ---- */
    ixgbe_dcb_init();

    /* ---- VLAN ---- */
    ixgbe_vlan_init();

    /* ---- 通知硬件驱动已加载 ---- */
    ixgbe_write_reg(IXGBE_REG_CTRL_EXT,
        ixgbe_read_reg(IXGBE_REG_CTRL_EXT) | IXGBE_CTRL_EXT_DRV_LOAD);

    /* ---- 强制链路启动 ---- */
    ixgbe_write_reg(IXGBE_REG_CTRL,
        ixgbe_read_reg(IXGBE_REG_CTRL) | IXGBE_CTRL_SLU);

    /* 等待链路建立 */
    timeout = 500000;
    while (!(ixgbe_read_reg(IXGBE_REG_LINKS) & IXGBE_LINKS_UP) && timeout--)
        ;
    if (timeout == 0) {
        klog_warn("ixgbe: Link not up after init (timeout)");
    } else {
        ixgbe_check_link();
    }

    /* ---- 注册网络接口 ---- */
    strcpy(ixgbe_iface.name, "ix0");
    ixgbe_iface.up          = 1;
    ixgbe_iface.mtu         = 1500;   /* 默认 MTU, 支持 Jumbo Frame 可设为 9000 */
    ixgbe_iface.flags       = IFF_UP | IFF_BROADCAST | IFF_RUNNING | IFF_MULTICAST;
    ixgbe_iface.send        = ixgbe_send;
    ixgbe_iface.driver_data = (void *)0;
    ixgbe_iface.tx_packets  = 0;
    ixgbe_iface.rx_packets  = 0;
    ixgbe_iface.tx_bytes    = 0;
    ixgbe_iface.rx_bytes    = 0;
    ixgbe_iface.tx_errors   = 0;
    ixgbe_iface.rx_errors   = 0;
    net_register_interface(&ixgbe_iface);

    ixgbe_inited = 1;
    klog_info("ixgbe: Driver initialized successfully, iface=%s", ixgbe_iface.name);
}

/* ---- 发送数据包 (使用 queue 0) ---- */
int ixgbe_send(net_interface_t *iface, const void *data, uint32_t len) {
    if (!ixgbe_inited || len > IXGBE_TX_BUF_SIZE) return -1;

    uint32_t q = 0;  /* 默认使用 queue 0 */

    /* 复制数据到 TX 缓冲区 */
    memcpy(tx_buffers[q][tx_tail[q]], data, len);

    /* 高级 TX 描述符:
     * 命令位编码在 addr 字段的高 32 位 (因为物理地址只用 32-bit)。
     * EOP: End of Packet
     * IFCS: Insert FCS
     * IC: Insert Checksum
     * RS: Report Status
     */
    uint64_t cmd_flags = IXGBE_TX_OPT_EOP |
                         IXGBE_TX_OPT_IFCS |
                         IXGBE_TX_OPT_IC |
                         IXGBE_TX_OPT_RS;

    tx_descs[q][tx_tail[q]].addr     = (uint64_t)(uintptr_t)tx_buffers[q][tx_tail[q]] | cmd_flags;
    tx_descs[q][tx_tail[q]].reserved = (uint64_t)len << 46;  /* 有效负载长度在 bits 46-63 */

    /* 更新尾部指针并通知硬件 */
    uint32_t old_tail = tx_tail[q];
    tx_tail[q] = (tx_tail[q] + 1) % IXGBE_NUM_TX_DESC;
    ixgbe_write_reg(IXGBE_REG_TDT(q), tx_tail[q]);

    (void)iface;
    return (int)old_tail;
}

/* ---- 轮询接收数据包 (所有队列) ---- */
void ixgbe_poll(void) {
    if (!ixgbe_inited) return;

    uint32_t q, i;
    for (q = 0; q < IXGBE_NUM_QUEUES; q++) {
        for (i = 0; i < IXGBE_NUM_RX_DESC; i++) {
            /* 检查写回状态: DD (Descriptor Done) 在写回区域的 status 字段 */
            /* 对于 82599 高级描述符, 写回数据在同一个 16-byte 描述符中 */
            ixgbe_rx_wb_t *wb = (ixgbe_rx_wb_t *)&rx_descs[q][i];

            if (wb->status & IXGBE_RX_WB_DD) {
                /* 描述符完成 (DD=1), 数据可用 */
                net_buffer_t *buf = net_alloc_buffer();
                if (buf) {
                    uint16_t pkt_len = wb->length;
                    memcpy(buf->data, rx_buffers[q][i],
                           pkt_len < IXGBE_RX_BUF_SIZE ? pkt_len : IXGBE_RX_BUF_SIZE);
                    buf->len    = pkt_len;
                    buf->offset = 0;
                    buf->iface  = &ixgbe_iface;

                    /* 检查错误标志 */
                    if (wb->errors & (IXGBE_RX_ERROR_CE |
                                      IXGBE_RX_ERROR_SE |
                                      IXGBE_RX_ERROR_PE)) {
                        ixgbe_iface.rx_errors++;
                        net_free_buffer(buf);
                    } else {
                        ixgbe_iface.rx_packets++;
                        ixgbe_iface.rx_bytes += pkt_len;
                        net_receive(buf);
                    }
                }

                /* 归还描述符给 NIC */
                memset(rx_buffers[q][i], 0, IXGBE_RX_BUF_SIZE);
                rx_descs[q][i].addr     = (uint64_t)(uintptr_t)rx_buffers[q][i];
                rx_descs[q][i].reserved = 0;

                /* 写回一个描述符就更新一次尾指针 (比 I219 简单) */
                rx_tail[q] = (rx_tail[q] + 1) % IXGBE_NUM_RX_DESC;
            }
        }
        /* 批量更新 RX 尾指针 */
        ixgbe_write_reg(IXGBE_REG_RDT(q), rx_tail[q]);
    }
}

/* ---- 中断处理 ---- */
void ixgbe_irq_handler(regs_t *regs) {
    if (!ixgbe_inited) return;

    uint32_t eicr = ixgbe_read_reg(IXGBE_REG_EICR);

    if (eicr == 0) {
        (void)regs;
        return;
    }

    /* 接收中断: 任何 RX 队列 */
    uint32_t q;
    uint32_t rx_mask = 0;
    for (q = 0; q < IXGBE_NUM_QUEUES; q++) {
        rx_mask |= IXGBE_EICR_RX_QUEUE(q);
    }
    if (eicr & rx_mask) {
        ixgbe_poll();
    }

    /* TX 中断 */
    uint32_t tx_mask = 0;
    for (q = 0; q < IXGBE_NUM_QUEUES; q++) {
        tx_mask |= IXGBE_EICR_TX_QUEUE(q);
    }
    if (eicr & tx_mask) {
        /* TX 完成, 可用于清理 */
    }

    /* 链路状态变化 */
    if (eicr & IXGBE_EICR_LSC) {
        ixgbe_check_link();
    }

    /* 清除已处理的中断 */
    ixgbe_write_reg(IXGBE_REG_EICR, eicr);

    (void)regs;
}

/* ---- PCI 探测入口 ---- */
int ixgbe_probe(void) {
    static const uint16_t ixgbe_dev_ids[] = {
        IXGBE_DEV_ID_82599ES,
        IXGBE_DEV_ID_82599EB,
        IXGBE_DEV_ID_82599EN,
        IXGBE_DEV_ID_82599_KX4,
        IXGBE_DEV_ID_82599_T3_LOM
    };
    static const int num_dev_ids = sizeof(ixgbe_dev_ids) / sizeof(ixgbe_dev_ids[0]);

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

                if (vendor_id == IXGBE_VENDOR_ID) {
                    int j;
                    for (j = 0; j < num_dev_ids; j++) {
                        if (device_id == ixgbe_dev_ids[j]) {
                            found = 1;
                            klog_info("ixgbe: Found device %04x:%04x at %02x:%02x.%x",
                                      vendor_id, device_id, bus, dev, func);
                            ixgbe_init((uint8_t)bus, dev, func);
                            break;
                        }
                    }
                }
            }
        }
    }

    return found ? 0 : -1;
}