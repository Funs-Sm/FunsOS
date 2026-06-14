/*
 * b57.c - Broadcom NetXtreme BCM57xx 千兆以太网控制器驱动实现
 *
 * 支持 BCM57xx 系列 (BCM5700~57788, BCM5714~5722 等)
 * 基于 Broadcom Tigon3 架构, 使用 MMIO 方式访问设备。
 *
 * 主要功能:
 *   - PCI 设备探测与初始化
 *   - TX/RX 描述符环管理
 *   - 链路状态检测
 *   - 中断驱动与轮询混合模式
 *   - 基本的数据包发送/接收
 */

#include "b57.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "kheap.h"
#include "string.h"
#include "irq.h"

/* ---- 私有状态 ---- */
static volatile uint32_t *mmio_base;

static b57_tx_desc_t *tx_descs;
static b57_rx_bd_t *rx_bds;
static b57_rx_return_desc_t *rx_returns;

static void *rx_buffers[B57_NUM_RX_DESC];
static void *tx_buffers[B57_NUM_TX_DESC];

static uint32_t tx_prod_idx;
static uint32_t rx_prod_idx;
static uint32_t rx_cons_idx;

static net_interface_t b57_iface;
static int b57_inited = 0;
static uint32_t link_status = 0;

/* ---- 寄存器读写辅助函数 ---- */

/* 读取 32 位寄存器 */
static inline uint32_t b57_read_reg(uint32_t offset) {
    return mmio_base[offset / 4];
}

/* 写入 32 位寄存器 */
static inline void b57_write_reg(uint32_t offset, uint32_t val) {
    mmio_base[offset / 4] = val;
}

/* 等待操作完成 (轮询状态位) */
static void b57_wait_bits(uint32_t offset, uint32_t mask, uint32_t expected) {
    uint32_t timeout = 1000000;
    while (((b57_read_reg(offset) & mask) != expected) && timeout--)
        ;
}

/* ---- MAC 地址读取 ---- */
/*
 * 从 NVRAM 或 EEPROM 读取 MAC 地址。
 * BCM57xx 将 MAC 地址存储在 NVRAM 配置区域中,
 * 通过 NVRAM 命令寄存器读取。
 */
static void b57_read_mac(void) {
    /* 从 NVRAM 配置 1 寄存器的低 32 位读取 MAC 地址前半部分 */
    uint32_t nvram_cfg1 = b57_read_reg(B57_REG_NVM_CFG1);
    b57_iface.mac.bytes[0] = nvram_cfg1 & 0xFF;
    b57_iface.mac.bytes[1] = (nvram_cfg1 >> 8) & 0xFF;
    b57_iface.mac.bytes[2] = (nvram_cfg1 >> 16) & 0xFF;
    b57_iface.mac.bytes[3] = (nvram_cfg1 >> 24) & 0xFF;

    /* MAC 地址后半部分从 NVM_CFG1+4 读取 */
    uint32_t nvram_cfg2 = b57_read_reg(B57_REG_NVM_CFG1 + 4);
    b57_iface.mac.bytes[4] = nvram_cfg2 & 0xFF;
    b57_iface.mac.bytes[5] = (nvram_cfg2 >> 8) & 0xFF;
}

/*
 * 通过 PHY 访问链路状态。
 * BCM57xx 使用 MDIO/MDC 接口访问外部 PHY 芯片。
 */
static void b57_check_link_status(void) {
    uint32_t status = b57_read_reg(B57_REG_MAC_STATUS);

    if (status & B57_MAC_STATUS_LINK_UP) {
        link_status = 1;
        b57_iface.flags |= IFF_RUNNING;
    } else {
        link_status = 0;
        b57_iface.flags &= ~IFF_RUNNING;
    }
}

/* ---- 设备初始化 ---- */
/*
 * b57_init - 初始化 BCM57xx 网卡
 *
 * 步骤:
 *   1. 读取 BAR0 映射 MMIO 内存空间
 *   2. 启用 PCI 总线主控和内存 I/O
 *   3. 执行软复位并等待完成
 *   4. 读取 MAC 地址
 *   5. 分配并初始化 TX/RX 描述符环
 *   6. 配置 DMA 引擎
 *   7. 注册中断处理程序
 *   8. 使能发送/接收引擎
 *   9. 强制启动链路协商
 */
void b57_init(uint8_t bus, uint8_t dev, uint8_t func) {
    /* 读取 BAR0 (MMIO 基地址), BCM57xx 使用 64KB MMIO 空间 */
    uint32_t bar0 = pci_read_config(bus, dev, func, 0x10);
    uint32_t phys_addr = bar0 & 0xFFFFFFF0;
    mmio_base = (volatile uint32_t *)vmm_map_physical(phys_addr, 0x20000);

    if (mmio_base == NULL) return;

    /* 启用 PCI 总线主控、内存空间和 I/O 空间 */
    uint32_t cmd = pci_read_config(bus, dev, func, 0x04) & 0xFFFF;
    pci_write_config(bus, dev, func, 0x04, cmd | 0x07);

    /* ---- 软复位设备 ---- */
    /* 设置 MAC_CTRL.RESET 位触发软复位 */
    b57_write_reg(B57_REG_MAC_CTRL, B57_MAC_CTRL_RESET);
    /* 等待复位完成 (RESET 位自动清除) */
    b57_wait_bits(B57_REG_MAC_CTRL, B57_MAC_CTRL_RESET, 0);
    if ((b57_read_reg(B57_REG_MAC_CTRL) & B57_MAC_CTRL_RESET)) {
        /* 复位超时，设备可能存在硬件问题 */
        return;
    }

    /* 清除 DMA 复位状态 */
    b57_write_reg(B57_REG_DMA_RCV_CTRL, B57_DMA_SRST);
    b57_wait_bits(B57_REG_DMA_RCV_CTRL, B57_DMA_SRST, 0);
    b57_write_reg(B57_REG_DMA_TX_CTRL, B57_DMA_SRST);
    b57_wait_bits(B57_REG_DMA_TX_CTRL, B57_DMA_SRST, 0);

    /* ---- 读取 MAC 地址 ---- */
    b57_read_mac();

    /*
     * 分配 TX 描述符环。
     * BCM57xx 的 TX 描述符包含: 标志长度 | VLAN标签 | 缓冲区地址(64位)
     * 每个 TX 描述符占用 16 字节。
     */
    tx_descs = (b57_tx_desc_t *)kmalloc(B57_NUM_TX_DESC * sizeof(b57_tx_desc_t));
    if (!tx_descs) return;
    memset(tx_descs, 0, B57_NUM_TX_DESC * sizeof(b57_tx_desc_t));

    /*
     * 分配 RX 描述符环 (Buffer Descriptors + Return Ring)。
     * BCM57xx 使用分离的 RX BD 环和返回环:
     *   - RX BD 环: 告诉 NIC 可用的接收缓冲区
     *   - 返回环: NIC 将已接收的数据包信息写回此处
     */
    rx_bds = (b57_rx_bd_t *)kmalloc(B57_NUM_RX_DESC * sizeof(b57_rx_bd_t));
    if (!rx_bds) return;
    memset(rx_bds, 0, B57_NUM_RX_DESC * sizeof(b57_rx_bd_t));

    rx_returns = (b57_rx_return_desc_t *)kmalloc(
        B57_NUM_RX_DESC * sizeof(b57_rx_return_desc_t));
    if (!rx_returns) return;
    memset(rx_returns, 0, B57_NUM_RX_DESC * sizeof(b57_rx_return_desc_t));

    /* 分配 TX/RX 数据缓冲区并初始化描述符 */
    uint32_t i;
    for (i = 0; i < B57_NUM_TX_DESC; i++) {
        tx_buffers[i] = kmalloc(B57_TX_BUF_SIZE);
        if (!tx_buffers[i]) continue;
        memset(tx_buffers[i], 0, B57_TX_BUF_SIZE);
        tx_descs[i].flags_len = 0;
        tx_descs[i].vlan_tag = 0;
        tx_descs[i].buf_addr_lo = (uint32_t)(uintptr_t)tx_buffers[i];
        tx_descs[i].buf_addr_hi = 0;
    }

    for (i = 0; i < B57_NUM_RX_DESC; i++) {
        rx_buffers[i] = kmalloc(B57_RX_BUF_SIZE);
        if (!rx_buffers[i]) continue;
        memset(rx_buffers[i], 0, B57_RX_BUF_SIZE);
        rx_bds[i].flags_len = B57_RX_BUF_SIZE;
        rx_bds[i].idx = i;
        rx_bds[i].addr_lo = (uint32_t)(uintptr_t)rx_buffers[i];
        rx_bds[i].addr_hi = 0;
    }

    tx_prod_idx = 0;
    rx_prod_idx = 0;
    rx_cons_idx = 0;

    /* ---- 配置 DMA 接收引擎 ---- */

    /* 设置 RX BD 环基地址 */
    b57_write_reg(B57_REG_RCV_BD_ADDR, (uint32_t)(uintptr_t)rx_bds);
    b57_write_reg(B57_REG_RCV_BD_IDX, 0);

    /* 设置返回环基地址 */
    b57_write_reg(B57_REG_RCV_RET_CON_IDX_0, (uint32_t)(uintptr_t)rx_returns);

    /* 设置 RX BD 替换阈值: 当可用 BD 数量低于此值时产生中断 */
    b57_write_reg(B57_REG_RCV_BD_REPL_THRESH, B57_NUM_RX_DESC / 4);

    /* 初始填充所有 RX BD */
    b57_write_reg(B57_REG_RCV_STD_PROD_IDX, B57_NUM_RX_DESC - 1);

    /* 配置 DMA 接收控制:
     * 使能 DMA 接收、设置命令结束标志、注意信号使能 */
    b57_write_reg(B57_REG_DMA_RCV_CTRL,
        B57_DMA_RX_ENABLE |
        B57_DMA_CMD_END |
        B57_DMA_ATTN_ENABLE);

    /* ---- 配置 DMA 发送引擎 ---- */

    /* 设置 TX BD 环基地址 */
    b57_write_reg(B57_REG_TX_BD_ADDR, (uint32_t)(uintptr_t)tx_descs);
    b57_write_reg(B57_REG_TX_BD_IDX, 0);

    /* 配置 TX 模式: 使能发送、允许短包填充 */
    b57_write_reg(B57_REG_TX_MODE,
        B57_DMA_TX_ENABLE |
        B57_DMA_CMD_END |
        B57_DMA_ATTN_ENABLE);

    /* 配置 TX 长度限制 (最大帧长度) */
    b57_write_reg(B57_REG_TX_LENGTHS, (1514 << 16) | 60);

    /* ---- 中断配置 ---- */

    /* 清除所有待处理的中断状态 */
    b57_write_reg(B57_REG_IRQSTAT_LO, 0xFFFFFFFF);
    b57_write_reg(B57_REG_IRQSTAT_HI, 0xFFFFFFFF);

    /* 先屏蔽所有中断 */
    b57_write_reg(B57_REG_IRQMASK_LO, 0);
    b57_write_reg(B57_REG_IRQMASK_HI, 0);

    /* 注册中断处理程序 */
    uint8_t irq_line = pci_read_config(bus, dev, func, 0x3C) & 0xFF;
    irq_register_handler(0x20 + irq_line, b57_irq_handler);

    /* 启用需要的中断:
     * - 链路状态变化 (LINKCHG)
     * - 接收完成 (RXDONE)
     * - 发送完成 (TXDONE)
     * - 主机错误 (HOSTERR) */
    b57_write_reg(B57_REG_IRQMASK_LO,
        B57_INT_LINKCHG |
        B57_INT_RXDONE |
        B57_INT_TXDONE |
        B57_INT_HOSTERR |
        B57_INT_RXMEMERR |
        B57_INT_TXMEMERR);

    /* 配置中断合并参数 (减少中断频率以提高性能) */
    b57_write_reg(B57_REG_IRQ_COAL, (50 << 16) | 25);  /* 25us/50us */

    /* ---- 启动 MAC 和链路 ---- */

    /* 使能 MAC 发送和接收 */
    b57_write_reg(B57_REG_MAC_CTRL,
        B57_MAC_CTRL_TX_EN |
        B57_MAC_CTRL_RX_EN |
        B57_MAC_CTRL_ALLMULTI);   /* 接受多播包 */

    /* 强制链路启动 */
    b57_check_link_status();
    link_status = 1;  /* 初始假设链路正常 */

    /* ---- 注册网络接口 ---- */
    strcpy(b57_iface.name, "b570");
    b57_iface.up = 1;
    b57_iface.mtu = 1500;
    b57_iface.flags = IFF_UP | IFF_BROADCAST | IFF_MULTICAST;
    b57_iface.send = b57_send;
    b57_iface.driver_data = (void *)0;
    net_register_interface(&b57_iface);

    b57_inited = 1;
}

/* ---- 发送数据包 ---- */
/*
 * b57_send - 发送一个网络数据包
 *
 * 将用户数据复制到 TX 缓冲区, 填写 TX 描述符,
 * 更新生产索引并通知硬件开始发送。
 *
 * 返回值: 成功时返回描述符索引, 失败时返回 -1
 */
int b57_send(net_interface_t *iface, const void *data, uint32_t len) {
    if (!b57_inited || len > B57_TX_BUF_SIZE) return -1;

    /* 复制数据到当前 TX 缓冲区 */
    memcpy(tx_buffers[tx_prod_idx], data, len);

    /* 填写 TX 描述符:
     * SOP: Start of Packet (包开始)
     * EOP: End of Packet (包结束)
     * INTR: 完成后请求中断
     * IP/TCP 校验和卸载标志 */
    tx_descs[tx_prod_idx].flags_len =
        (len & 0xFFFF) |          /* 低 16 位: 包长度 */
        B57_TX_FLAG_SOP |         /* 包开始 */
        B57_TX_FLAG_END |         /* 包结束 */
        B57_TX_FLAG_IP_CKSUM |    /* IP 校验和卸载 */
        B57_TX_FLAG_TCP_CKSUM;    /* TCP/UDP 校验和卸载 */
    tx_descs[tx_prod_idx].vlan_tag = 0;
    tx_descs[tx_prod_idx].buf_addr_lo =
        (uint32_t)(uintptr_t)tx_buffers[tx_prod_idx];
    tx_descs[tx_prod_idx].buf_addr_hi = 0;

    /* 更新 TX 生产索引 (通知硬件有新数据可发) */
    uint32_t old_idx = tx_prod_idx;
    tx_prod_idx = (tx_prod_idx + 1) % B57_NUM_TX_DESC;
    b57_write_reg(B57_REG_TX_BD_IDX, tx_prod_idx);

    /* 更新发送统计 */
    b57_iface.tx_packets++;
    b57_iface.tx_bytes += len;

    (void)iface;
    return (int)old_idx;
}

/* ---- 轮询接收数据包 ---- */
/*
 * b57_poll - 轮询检查是否有新接收到的数据包
 *
 * 遍历 RX 返回环, 将已完成的数据包交给协议栈处理。
 * 对于每个已完成的描述符:
 *   1. 检查错误标志
 *   2. 复制数据到网络缓冲区
 *   3. 调用 net_receive() 交给上层协议栈
 *   4. 归还 RX BD 给 NIC 以继续接收
 */
void b57_poll(void) {
    if (!b57_inited) return;

    while (rx_cons_idx != rx_prod_idx) {
        b57_rx_return_desc_t *ret = &rx_returns[rx_cons_idx];

        /* 检查描述符是否有效 (NIC 已填写) */
        if (!(ret->flags & B57_RX_FLAG_ERROR) && ret->len > 0) {
            /* 分配网络缓冲区 */
            net_buffer_t *buf = net_alloc_buffer();
            if (buf) {
                /* 从 RX 缓冲区复制数据 */
                uint16_t pkt_len = ret->len > B57_RX_BUF_SIZE ?
                                   B57_RX_BUF_SIZE : ret->len;
                memcpy(buf->data, rx_buffers[rx_cons_idx % B57_NUM_RX_DESC],
                       pkt_len);
                buf->len = pkt_len;
                buf->offset = 0;
                buf->iface = &b57_iface;

                /* 交给协议栈处理 */
                net_receive(buf);

                /* 更新接收统计 */
                b57_iface.rx_packets++;
                b57_iface.rx_bytes += pkt_len;
            } else {
                /* 接收缓冲区不足，丢弃包 */
            }
        } else {
            b57_iface.rx_errors++;
        }

        /* 归还此描述符给 NIC: 清零返回描述符 */
        memset(ret, 0, sizeof(b57_rx_return_desc_t));
        rx_cons_idx = (rx_cons_idx + 1) % B57_NUM_RX_DESC;
    }
}

/* ---- 中断处理程序 ---- */
/*
 * b57_irq_handler - BCM57xx 中断服务例程
 *
 * 根据中断类型执行相应操作:
 *   - RXDONE/RX 相关: 触发接收轮询
 *   - TXDONE: 发送完成 (无特殊处理, 统计更新在 send 时完成)
 *   - LINKCHG: 链路状态变化, 重新检测
 *   - HOSTERR: 主机总线错误, 打印日志
 */
void b57_irq_handler(regs_t *regs) {
    if (!b57_inited) return;

    /* 读取中断状态 */
    uint32_t lo_stat = b57_read_reg(B57_REG_IRQSTAT_LO);
    uint32_t hi_stat = b57_read_reg(B57_REG_IRQSTAT_HI);

    /* 处理接收相关中断 -> 轮询收取数据包 */
    if (lo_stat & (B57_INT_RXDONE | B57_INT_RXMEMERR)) {
        b57_poll();
    }

    /* 处理发送完成中断 */
    if (lo_stat & B57_INT_TXDONE) {
        /* TX 描述符写回中断, 无需特殊处理 */
        /* 统计已在 send() 中更新 */
    }

    /* 处理链路状态变化中断 */
    if (lo_stat & B57_INT_LINKCHG) {
        b57_check_link_status();
    }

    /* 处理主机错误中断 */
    if (lo_stat & B57_INT_HOSTERR) {
        uint32_t host_err = b57_read_reg(B57_REG_IRQHOST_ERR);
        (void)host_err;
        /* 主机错误通常表示总线问题, 可以尝试软复位恢复 */
    }

    /* 清除已处理的中断 (写 1 清除对应位) */
    b57_write_reg(B57_REG_IRQSTAT_LO, lo_stat);
    b57_write_reg(B57_REG_IRQSTAT_HI, hi_stat);

    (void)hi_stat;
    (void)regs;
}

/* ---- 链路状态查询 ---- */
/*
 * b57_link_up - 查询物理链路是否已建立
 *
 * 返回值: 1=链路已连接, 0=链路断开
 */
int b57_link_up(void) {
    if (!b57_inited) return 0;
    b57_check_link_status();
    return (int)link_status;
}

/* ---- PCI 探测入口 ---- */
/*
 * b57_probe - 扫描 PCI 总线查找 BCM57xx 设备
 *
 * 遍历所有 PCI 总线/设备/功能组合,
 * 匹配 Broadcom Vendor ID (0x14E4) 和已知设备 ID 列表。
 * 找到第一个匹配设备后调用 b57_init() 进行初始化。
 *
 * 返回值: 0=找到并初始化成功, -1=未找到设备
 */
int b57_probe(void) {
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

                /* 匹配 Broadcom Vendor ID 和 BCM57xx 系列设备 ID */
                if (vendor_id == B57_VENDOR_ID &&
                    (device_id == B57_DEV_5700 ||
                     device_id == B57_DEV_5701 ||
                     device_id == B57_DEV_5702 ||
                     device_id == B57_DEV_5703 ||
                     device_id == B57_DEV_5704 ||
                     device_id == B57_DEV_5750 ||
                     device_id == B57_DEV_5751 ||
                     device_id == B57_DEV_5752 ||
                     device_id == B57_DEV_5753 ||
                     device_id == B57_DEV_57780 ||
                     device_id == B57_DEV_57781 ||
                     device_id == B57_DEV_57782 ||
                     device_id == B57_DEV_57783 ||
                     device_id == B57_DEV_57784 ||
                     device_id == B57_DEV_57785 ||
                     device_id == B57_DEV_57786 ||
                     device_id == B57_DEV_57787 ||
                     device_id == B57_DEV_57788 ||
                     device_id == B57_DEV_57789 ||
                     device_id == B57_DEV_5780 ||
                     device_id == B57_DEV_5781 ||
                     device_id == B57_DEV_5782 ||
                     device_id == B57_DEV_5786 ||
                     device_id == B57_DEV_5787 ||
                     device_id == B57_DEV_5788 ||
                     device_id == B57_DEV_5789 ||
                     device_id == B57_DEV_5709 ||
                     device_id == B57_DEV_5714 ||
                     device_id == B57_DEV_5715 ||
                     device_id == B57_DEV_5716 ||
                     device_id == B57_DEV_5717 ||
                     device_id == B57_DEV_5718 ||
                     device_id == B57_DEV_5719 ||
                     device_id == B57_DEV_5720 ||
                     device_id == B57_DEV_5721 ||
                     device_id == B57_DEV_5722)) {
                    found = 1;
                    b57_init((uint8_t)bus, dev, func);
                }
            }
        }
    }

    return found ? 0 : -1;
}
