/*
 * b57.h - Broadcom NetXtreme BCM57xx 千兆以太网控制器驱动头文件
 *
 * 支持 BCM57xx 系列网卡:
 *   BCM5714/5715/5716/5717/5718/5719/5720/5721/5722
 *   BCM5700/5701/5702/5703/5704/5750/5751/5752/5753
 *   BCM57780/57785/57786/57787/57788
 *
 * 基于 Broadcom Tigon3 架构, 使用 MMIO 方式访问。
 */

#ifndef B57_H
#define B57_H

#include "net.h"
#include "stdint.h"
#include "kernel_types.h"

/* ---- PCI 设备 ID ---- */
#define B57_VENDOR_ID           0x14E4   /* Broadcom Corporation */

/* BCM57xx 系列设备 ID 列表 */
#define B57_DEV_5700            0x1644   /* BCM5700  */
#define B57_DEV_5701            0x1645   /* BCM5701  */
#define B57_DEV_5702            0x1646   /* BCM5702  */
#define B57_DEV_5703            0x1647   /* BCM5703  */
#define B57_DEV_5704            0x1648   /* BCM5704  */
#define B57_DEV_5750            0x1676   /* BCM5750  */
#define B57_DEV_5751            0x1677   /* BCM5751  */
#define B57_DEV_5752            0x1678   /* BCM5752  */
#define B57_DEV_5753            0x1679   /* BCM5753  */
#define B57_DEV_57780           0x169B   /* BCM57780 */
#define B57_DEV_57781           0x1690   /* BCM57781 */
#define B57_DEV_57782           0x16F6   /* BCM57782 */
#define B57_DEV_57783           0x16F7   /* BCM57783 */
#define B57_DEV_57784           0x16F8   /* BCM57784 */
#define B57_DEV_57785           0x16F9   /* BCM57785 */
#define B57_DEV_57786           0x16FA   /* BCM57786 */
#define B57_DEV_57787           0x16FB   /* BCM57787 */
#define B57_DEV_57788           0x16FC   /* BCM57788 */
#define B57_DEV_57789           0x1657   /* BCM57789 (BCM5709) */
#define B57_DEV_5780            0x166A   /* BCM5780  */
#define B57_DEV_5781            0x166B   /* BCM5781  */
#define B57_DEV_5782            0x167A   /* BCM5782  */
#define B57_DEV_5786            0x169C   /* BCM5786  */
#define B57_DEV_5787            0x169D   /* BCM5787  */
#define B57_DEV_5788            0x169E   /* BCM5788  */
#define B57_DEV_5789            0x169F   /* BCM5789  */
#define B57_DEV_5709            0x1639   /* BCM5709  */
#define B57_DEV_5714            0x1638   /* BCM5714  */
#define B57_DEV_5715            0x163B   /* BCM5715  */
#define B57_DEV_5716            0x163C   /* BCM5716  */
#define B57_DEV_5717            0x163D   /* BCM5717  */
#define B57_DEV_5718            0x163E   /* BCM5718  */
#define B57_DEV_5719            0x163F   /* BCM5719  */
#define B57_DEV_5720            0x165F   /* BCM5720  */
#define B57_DEV_5721            0x1660   /* BCM5721  */
#define B57_DEV_5722            0x1661   /* BCM5722  */

/* ---- 关键寄存器偏移 (相对于 MMIO base) ---- */

/* 控制与状态寄存器 */
#define B57_REG_MAC_CTRL        0x0680  /* MAC 控制寄存器         */
#define B57_REG_MAC_STATUS      0x0684  /* MAC 状态寄存器         */
#define B57_REG_MAC_EVT_ENAB    0x0688  /* MAC 事件使能寄存器     */
#define B57_REG_MAC_LED_CTRL    0x0690  /* LED 控制寄存器         */

/* 接收控制 */
#define B57_REG_RCV_LPCNT       0x06A0  /* RX 低优先级计数        */
#define B57_REG_RCV_LPMT        0x06A4  /* RX 低优先级阈值        */
#define B57_REG_RCV_RULES_CFG   0x0550  /* 接收规则配置          */
#define B57_REG_DMA_RCV_CTRL    0x01C0  /* DMA 接收控制           */
#define B57_REG_RCV_BD_ADDR     0x1200  /* RX 描述符基地址低      */
#define B57_REG_RCV_BD_IDX      0x1204  /* RX 描述符索引          */
#define B57_REG_RCV_STD_PROD_IDX 0x1238  /* 标准接收生产索引     */
#define B57_REG_RCV_RET_CON_IDX_0 0x1240 /* 返回环消费索引 0    */
#define B57_REG_RCV_LIST_PLACE  0x1244  /* 接收列表放置          */
#define B57_REG_RCV_BD_REPL_THRESH 0x1248 /* RX BD 替换阈值      */

/* 发送控制 */
#define B57_REG_DMA_TX_CTRL     0x0440  /* DMA 发送控制           */
#define B57_REG_TX_BD_ADDR      0x1400  /* TX 描述符基地址        */
#define B57_REG_TX_BD_IDX       0x1404  /* TX 描述符索引          */
#define B57_REG_TX_MODE         0x0450  /* TX 模式寄存器          */
#define B57_REG_TX_LENGTHS      0x0454  /* TX 长度寄存器          */
#define B57_REG_MSI_CONTROL     0x0680  /* MSI 控制寄存器         */

/* 中断相关 */
#define B57_REG_MISC_HOST_CTRL  0x0680  /* 主机杂项控制           */
#define B57_REG_DMAC_MODE       0x0200  /* DMA 模式               */
#define B57_REG_MISC_LOCAL_CTRL 0x0684  /* 本地杂项控制           */
#define B57_REG_IRQMASK_LO      0x0688  /* IRQ 屏蔽低位           */
#define B57_REG_IRQMASK_HI      0x068C  /* IRQ 屏蔽高位           */
#define B57_REG_IRQSTAT_LO      0x06A0  /* IRQ 状态低位           */
#define B57_REG_IRQSTAT_HI      0x06A4  /* IRQ 状态高位           */
#define B57_REG_IRQHOST_ERR     0x06AC  /* IRQ 主机错误           */
#define B57_REG_IRQ_COAL        0x06B0  /* 中断合并参数           */

/* 链路状态 */
#define B57_REG_MI_STATUS       0x0434  /* MI 状态寄存器          */
#define B57_REG_PHY_ACCESS      0x04C0  /* PHY 访问寄存器         */
#define B57_REG_MAC_RX_STATUS   0x06A0  /* MAC 接收状态           */

/* NVRAM / VPD */
#define B57_REG_NVM_CFG1       0x6C00  /* NVRAM 配置 1           */
#define B57_REG_NVM_COMMAND     0x7000  /* NVRAM 命令             */
#define B57_REG_NVM_ADDRESS     0x7004  /* NVRAM 地址             */
#define B57_REG_NVM_READ        0x7008  /* NVRAM 数据读出         */
#define B57_REG_NVM_WRITE       0x700C  /* NVRAM 数据写入         */

/* 统计寄存器 */
#define B57_REG_STAT_IFACE_IN_UCAST  0x0400  /* 入站单播统计     */
#define B57_REG_STAT_IFACE_IN_MCAST  0x0404  /* 入站多播统计     */
#define B57_REG_STAT_IFACE_OUT_UCAST 0x0410  /* 出站单播统计     */

/* ---- MAC_CTRL 寄存器位定义 ---- */
#define B57_MAC_CTRL_TX_EN      (1 << 0)   /* 发送使能             */
#define B57_MAC_CTRL_RX_EN      (1 << 1)   /* 接收使能             */
#define B57_MAC_CTRL_LOOPBK     (1 << 2)   /* 回环模式             */
#define B57_MAC_CTRL_PROMISC    (1 << 3)   /* 混杂模式             */
#define B57_MAC_CTRL_ALLMULTI   (1 << 4)   /* 全多播接受           */
#define B57_MAC_CTRL_GHDMODE    (1 << 5)   /* 巨型帧模式           */
#define B57_MAC_CTRL_SPEED_MASK (3 << 14)  /* 速度选择掩码         */
#define B57_MAC_CTRL_FULL_DUPLEX (1 << 23) /* 全双工               */
#define B57_MAC_CTRL_RESET      (1UL << 31) /* 软复位              */

/* ---- MAC_STATUS 寄存器位定义 ---- */
#define B57_MAC_STATUS_LINK_UP  (1 << 0)   /* 链路已建立           */
#define B57_MAC_STATUS_SYNCED   (1 << 1)   /* 同步完成             */
#define B57_MAC_STATUS_SIG_DET  (1 << 2)   /* 信号检测             */
#define B57_MAC_STATUS_SPEED_10 (0 << 12)  /* 10Mbps               */
#define B57_MAC_STATUS_SPEED_100 (1 << 12) /* 100Mbps              */
#define B57_MAC_STATUS_SPEED_1000 (2 << 12)/* 1000Mbps             */
#define B57_MAC_STATUS_FDX      (1 << 15)  /* 全双工标志           */
#define B57_MAC_STATUS_HW_ERR   (1 << 29)  /* 硬件错误             */

/* ---- DMA 寄存器位定义 ---- */
#define B57_DMA_RX_ENABLE       (1 << 0)   /* DMA 接收使能         */
#define B57_DMA_TX_ENABLE       (1 << 0)   /* DMA 发送使能         */
#define B57_DMA_CMD_END         (1 << 24)  /* 命令结束标志         */
#define B57_DMA_ATTN_ENABLE     (1 << 27)  /* 注意信号使能         */
#define B57_DMA_SRST            (1UL << 31) /* DMA 软复位           */

/* ---- 中断位定义 ---- */
#define B57_INT_LSERR           (1 << 0)   /* 链路状态错误        */
#define B57_INT_DMADONE         (1 << 1)   /* DMA 完成             */
#define B57_INT_RXDONE          (1 << 2)   /* 接收完成             */
#define B57_INT_TXDONE          (1 << 3)   /* 发送完成             */
#define B57_INT_LINKCHG         (1 << 4)   /* 链路状态变化         */
#define B57_INT_RXMEMERR        (1 << 5)   /* RX 内存错误          */
#define B57_INT_TXMEMERR        (1 << 6)   /* TX 内存错误          */
#define B57_INT_MACSTAT         (1 << 7)   /* MAC 状态变化         */
#define B57_INT_HOSTERR         (1 << 8)   /* 主机错误             */
#define B57_INT_ALL             0xFFFFFFFF

/* ---- TX/RX 描述符结构 ---- */

/* BCM57xx TX 描述符 (16 bytes) */
typedef struct {
    uint32_t flags_len;        /* 标志和长度 */
    uint32_t vlan_tag;         /* VLAN 标签 */
    uint32_t buf_addr_lo;      /* 缓冲区地址低32位 */
    uint32_t buf_addr_hi;      /* 缓冲区地址高32位 (64位扩展) */
} b57_tx_desc_t;

/* BCM57xx RX 描述符 (16 bytes) - 返回环格式 */
typedef struct {
    uint16_t len;              /* 帧长度 */
    uint16_t idx;              /* 索引 */
    uint16_t flags;            /* 标志 */
    uint16_t err_flags;        /* 错误标志 */
    uint16_t vtag;             /* VLAN 标签 */
    uint16_t rss_hash;         /* RSS 哈希 */
    uint32_t opaque;           /* 不透明数据 */
} b57_rx_return_desc_t;

/* BCM57xx RX Buffer 描述符 */
typedef struct {
    uint32_t flags_len;        /* 标志和长度 */
    uint32_t idx;              /* 索引 */
    uint32_t addr_lo;          /* 地址低位 */
    uint32_t addr_hi;          /* 地址高位 */
} b57_rx_bd_t;

/* ---- TX 描述符标志位 ---- */
#define B57_TX_FLAG_END         (1 << 0)   /* 包结束               */
#define B57_TX_FLAG_SOP         (1 << 1)   /* 包开始               */
#define B57_TX_FLAG_INTR        (1 << 13)  /* 请求中断             */
#define B57_TX_FLAG_COAL_NOW    (1 << 14)  /* 立即合并中断         */
#define B57_TX_FLAG_UDP_CKSUM   (1 << 16)  /* UDP 校验和           */
#define B57_TX_FLAG_TCP_CKSUM   (1 << 17)  /* TCP 校验和           */
#define B57_TX_FLAG_IP_CKSUM    (1 << 18)  /* IP 校验和            */
#define B57_TX_FLAG_VLAN_TAG    (1 << 19)  /* VLAN 标签插入        */

/* ---- RX 返回描述符标志位 ---- */
#define B57_RX_FLAG_ERROR       (1 << 0)   /* 错误                 */
#define B57_RX_FLAG_IPV4        (1 << 1)   /* IPv4 包              */
#define B57_RX_FLAG_IPV6        (1 << 2)   /* IPv6 包              */
#define B57_RX_FLAG_TCP         (1 << 3)   /* TCP                  */
#define B57_RX_FLAG_UDP         (1 << 4)   /* UDP                  */
#define B57_RX_FLAG_IP_CKSUM_OK (1 << 5)   /* IP 校验和正确        */
#define B57_RX_FLAG_TCPUDP_CKSUM_OK (1 << 6) /* TCP/UDP 校验和正确 */

/* ---- 描述符环大小 ---- */
#define B57_NUM_TX_DESC         256
#define B57_NUM_RX_DESC         256
#define B57_RX_BUF_SIZE         2048
#define B57_TX_BUF_SIZE         2048

/* ---- 公共接口 ---- */
void b57_init(uint8_t bus, uint8_t dev, uint8_t func);
int b57_send(net_interface_t *iface, const void *data, uint32_t len);
void b57_poll(void);
void b57_irq_handler(regs_t *regs);

/* ---- 链路状态检测 ---- */
int b57_link_up(void);

/* ---- PCI 探测入口 ---- */
int b57_probe(void);

#endif /* B57_H */
