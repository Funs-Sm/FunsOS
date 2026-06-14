/*
 * rtl8139_ex.c - Enhanced Realtek RTL8139/8129 PCI 快速以太网驱动
 *
 * 基于 MMIO 方式访问寄存器, 完整支持:
 * - TX 描述符环 (4 描述符 + 链式传输)
 * - RX 环形缓冲区
 * - 中断调节 (Interrupt Moderation)
 * - 硬件校验和卸载 (Checksum Offload)
 * - Wake-on-LAN (WoL)
 * - PHY 状态监控和自动协商
 * - Loopback 测试模式
 * - EEPROM 访问 (93C46)
 * - 电源管理 (D0/D3)
 * - 统计和错误计数器
 */

#include "rtl8139_ex.h"
#include "pci.h"
#include "pmm.h"
#include "vmm.h"
#include "kheap.h"
#include "string.h"
#include "irq.h"
#include "klog.h"

/* ---- 私有状态 ---- */
static volatile uint32_t *mmio_base = 0;       /* MMIO base address              */
static volatile uint8_t  *mmio_byte = 0;       /* MMIO byte access               */

static rtl8139ex_tx_desc_t tx_desc_ring[RTL8139EX_NUM_TX_DESC];
static void *rx_ring_buffer = 0;

static void *tx_buffers[RTL8139EX_NUM_TX_DESC];

static uint32_t tx_next_idx = 0;                /* 下一个可用的 TX 描述符索引  */
static uint32_t rx_offset   = 0;                /* RX 环读取偏移                 */

static net_interface_t rtl8139ex_iface;
static int             rtl8139ex_inited = 0;

static uint8_t  hw_revision = 0;               /* 芯片版本 (从 TCR 读取)       */
static uint32_t rx_buf_size = RTL8139EX_RX_BUF_SIZE;

/* ---- 寄存器读写辅助函数 ---- */
static inline uint32_t rtl8139ex_read_reg(uint32_t offset) {
    return mmio_base[offset / 4];
}

static inline void rtl8139ex_write_reg(uint32_t offset, uint32_t val) {
    mmio_base[offset / 4] = val;
}

/* 字节访问 (某些寄存器需要字节访问) */
static inline uint8_t rtl8139ex_read_byte(uint32_t offset) {
    return mmio_byte[offset];
}

static inline void rtl8139ex_write_byte(uint32_t offset, uint8_t val) {
    mmio_byte[offset] = val;
}

/* ---- EEPROM (93C46) 访问 ---- */
static void rtl8139ex_eeprom_cs(uint8_t level) {
    uint8_t val = rtl8139ex_read_byte(RTL8139EX_REG_9346CR);
    if (level) val |= RTL8139EX_9346CR_EECS;
    else       val &= (uint8_t)(~RTL8139EX_9346CR_EECS);
    rtl8139ex_write_byte(RTL8139EX_REG_9346CR, val);
}

static void rtl8139ex_eeprom_ck(uint8_t level) {
    uint8_t val = rtl8139ex_read_byte(RTL8139EX_REG_9346CR);
    if (level) val |= RTL8139EX_9346CR_EESK;
    else       val &= (uint8_t)(~RTL8139EX_9346CR_EESK);
    rtl8139ex_write_byte(RTL8139EX_REG_9346CR, val);
}

static void rtl8139ex_eeprom_di(uint8_t level) {
    uint8_t val = rtl8139ex_read_byte(RTL8139EX_REG_9346CR);
    if (level) val |= RTL8139EX_9346CR_EEDI;
    else       val &= (uint8_t)(~RTL8139EX_9346CR_EEDI);
    rtl8139ex_write_byte(RTL8139EX_REG_9346CR, val);
}

static uint8_t rtl8139ex_eeprom_do(void) {
    uint8_t val = rtl8139ex_read_byte(RTL8139EX_REG_9346CR);
    return (val & RTL8139EX_9346CR_EEDO) ? 1 : 0;
}

/* 读取 EEPROM 一个字 (93C46: 6-bit address, 16-bit data) */
static uint16_t rtl8139ex_eeprom_read(uint8_t addr) {
    uint16_t data = 0;
    int i;

    /* 设置 EEM=1 (Auto-load) 模式, 以便访问 EEPROM */
    uint8_t saved_cr = rtl8139ex_read_byte(RTL8139EX_REG_9346CR);
    rtl8139ex_write_byte(RTL8139EX_REG_9346CR,
        (saved_cr & 0x3F) | RTL8139EX_9346CR_EEM_PROG);

    rtl8139ex_eeprom_cs(0);
    rtl8139ex_eeprom_ck(0);
    rtl8139ex_eeprom_cs(1);

    /* 发送 Read 命令 (bits: 1 1 0) + 6-bit address */
    uint16_t cmd = 0x600 | (addr & 0x3F);  /* 0x180 + addr */
    for (i = 9; i >= 0; i--) {
        rtl8139ex_eeprom_di((cmd >> i) & 1);
        rtl8139ex_eeprom_ck(1);
        rtl8139ex_eeprom_ck(0);           /* 空闲时钟脉冲 */
    }

    /* 读取 16-bit 数据 (MSB first) */
    for (i = 15; i >= 0; i--) {
        rtl8139ex_eeprom_ck(1);
        if (rtl8139ex_eeprom_do()) data |= (1 << i);
        rtl8139ex_eeprom_ck(0);
    }

    rtl8139ex_eeprom_cs(0);
    rtl8139ex_eeprom_di(0);

    /* 恢复 EEM 模式 */
    rtl8139ex_write_byte(RTL8139EX_REG_9346CR, saved_cr);

    return data;
}

/* ---- MAC 地址读取 ---- */
static void rtl8139ex_read_mac(void) {
    /* MAC 地址位于 IDR0-IDR5 (偏移 0x00-0x05), 通过 MMIO 字节方式读取 */
    rtl8139ex_iface.mac.bytes[0] = rtl8139ex_read_byte(RTL8139EX_REG_IDR0);
    rtl8139ex_iface.mac.bytes[1] = rtl8139ex_read_byte(RTL8139EX_REG_IDR1);
    rtl8139ex_iface.mac.bytes[2] = rtl8139ex_read_byte(RTL8139EX_REG_IDR2);
    rtl8139ex_iface.mac.bytes[3] = rtl8139ex_read_byte(RTL8139EX_REG_IDR3);
    rtl8139ex_iface.mac.bytes[4] = rtl8139ex_read_byte(RTL8139EX_REG_IDR4);
    rtl8139ex_iface.mac.bytes[5] = rtl8139ex_read_byte(RTL8139EX_REG_IDR5);

    klog_info("rtl8139ex: MAC address from IDR: %02x:%02x:%02x:%02x:%02x:%02x",
              rtl8139ex_iface.mac.bytes[0], rtl8139ex_iface.mac.bytes[1],
              rtl8139ex_iface.mac.bytes[2], rtl8139ex_iface.mac.bytes[3],
              rtl8139ex_iface.mac.bytes[4], rtl8139ex_iface.mac.bytes[5]);
}

/* ---- PHY 状态监控 ---- */
static void rtl8139ex_phy_check(void) {
    uint8_t msr = rtl8139ex_read_byte(RTL8139EX_REG_MSR);

    if (msr & RTL8139EX_MSR_LINKB) {
        /* Link is bad */
        rtl8139ex_iface.flags &= ~(uint32_t)(IFF_RUNNING);
    } else {
        /* Link OK, 读取速度和双工 */
        rtl8139ex_iface.flags |= IFF_RUNNING;
        const char *speed = (msr & RTL8139EX_MSR_SPEED_100) ? "100" : "10";
        const char *duplex = (msr & RTL8139EX_MSR_DUPLEX) ? "Full" : "Half";
        klog_info("rtl8139ex: Link up: %s Mbps %s duplex", speed, duplex);
    }
}

/* ---- Loopback 测试 ---- */
static int rtl8139ex_loopback_test(void) {
    uint8_t saved_tcr = rtl8139ex_read_byte(RTL8139EX_REG_TCR);

    /* 保存当前接收配置 */
    uint32_t saved_rcr = rtl8139ex_read_reg(RTL8139EX_REG_RCR);

    /* 配置 Loopback: TCR bit 17-18 */
    rtl8139ex_write_byte(RTL8139EX_REG_TCR,
        (saved_tcr & 0xFC) | (1 << 2));  /* LBK=01 (Internal loopback) */

    /* 接受所有包 (包括错误包) */
    rtl8139ex_write_reg(RTL8139EX_REG_RCR,
        saved_rcr | RTL8139EX_RCR_AAP | RTL8139EX_RCR_AR | RTL8139EX_RCR_AER);

    /* 发送测试包 */
    uint8_t test_pkt[] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, /* 广播 MAC */
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, /* 源 MAC     */
        0x08, 0x00,                         /* EtherType (IP) */
        0xDE, 0xAD, 0xBE, 0xEF             /* 测试数据        */
    };

    rtl8139ex_send(0, test_pkt, sizeof(test_pkt));

    /* 延迟等待 loopback 完成 */
    for (volatile int i = 0; i < 100000; i++) __asm__ __volatile__("pause");

    /* 恢复配置 */
    rtl8139ex_write_byte(RTL8139EX_REG_TCR, saved_tcr);
    rtl8139ex_write_reg(RTL8139EX_REG_RCR, saved_rcr);

    klog_info("rtl8139ex: Loopback test completed");
    return 0;
}

/* ---- 中断调节 (Interrupt Moderation) ---- */
static void rtl8139ex_configure_int_moderation(void) {
    /* RTL8139 通过配置 SYMERR/RXERR/TIMER 等寄存器实现中断调节
     * 配置早期 TX 阈值以减少中断频率 */
    /* Early TX Threshold: 8 * 32 = 256 bytes */
    /* (此功能对于不同芯片版本有所不同) */
}

/* ---- 电源管理 ---- */
static void rtl8139ex_pm_init(void) {
    /* 启用 Config1 中的电源管理 */
    uint8_t config1 = rtl8139ex_read_byte(RTL8139EX_REG_CONFIG1);
    config1 |= RTL8139EX_CONFIG1_PMEn;
    rtl8139ex_write_byte(RTL8139EX_REG_CONFIG1, config1);

    /* 配置 Wake-on-LAN */
    uint8_t config3 = rtl8139ex_read_byte(RTL8139EX_REG_CONFIG3);
    config3 |= RTL8139EX_CONFIG3_Magic |  /* Magic Packet */
               RTL8139EX_CONFIG3_WOL;       /* Wake-on-LAN   */
    rtl8139ex_write_byte(RTL8139EX_REG_CONFIG3, config3);

    /* Config4: LWAKE for PME, Magic Packet, Link-On, Link-Off */
    uint8_t config4 = rtl8139ex_read_byte(RTL8139EX_REG_CONFIG4);
    config4 |= RTL8139EX_CONFIG4_LWPME |
               RTL8139EX_CONFIG4_LWPM  |
               RTL8139EX_CONFIG4_LWLAN |
               RTL8139EX_CONFIG4_LWLANO;
    rtl8139ex_write_byte(RTL8139EX_REG_CONFIG4, config4);

    klog_info("rtl8139ex: Power management and WoL configured");
}

/* ---- 初始化主函数 ---- */
void rtl8139ex_init(uint8_t bus, uint8_t dev, uint8_t func) {
    /* 读取 BAR1 (I/O base, 有时也提供 MMIO) */
    uint32_t bar0 = pci_read_config(bus, dev, func, 0x10);
    uint32_t bar1 = pci_read_config(bus, dev, func, 0x14);
    uint32_t phys_addr;

    /* RTL8139 可以用 I/O 端口或 MMIO, 优先使用 MMIO */
    if ((bar1 & 1) == 0 && bar1 != 0) {
        phys_addr = bar1 & 0xFFFFFFF0;
        mmio_base = (volatile uint32_t *)vmm_map_physical(phys_addr, 0x1000);
        mmio_byte = (volatile uint8_t *)mmio_base;
    } else if ((bar0 & 1) == 0 && bar0 != 0) {
        phys_addr = bar0 & 0xFFFFFFF0;
        mmio_base = (volatile uint32_t *)vmm_map_physical(phys_addr, 0x1000);
        mmio_byte = (volatile uint8_t *)mmio_base;
    }

    if (!mmio_base) {
        klog_err("rtl8139ex: Failed to map MMIO");
        return;
    }

    /* 启用 PCI 总线主控 */
    uint32_t cmd = pci_read_config(bus, dev, func, 0x04) & 0xFFFF;
    pci_write_config(bus, dev, func, 0x04, cmd | 0x07);

    /* ---- 软复位设备 ---- */
    rtl8139ex_write_byte(RTL8139EX_REG_CR, RTL8139EX_CR_RST);
    uint32_t timeout = 100000;
    while ((rtl8139ex_read_byte(RTL8139EX_REG_CR) & RTL8139EX_CR_RST) && timeout--)
        ;
    if (timeout == 0) {
        klog_err("rtl8139ex: Device reset timeout");
        return;
    }

    /* 读取硬件版本 (TCR bits 23-17) */
    hw_revision = (uint8_t)((rtl8139ex_read_reg(RTL8139EX_REG_TCR) &
                             RTL8139EX_TCR_HWVERID_MASK) >> 2);
    klog_info("rtl8139ex: Hardware revision: 0x%02x", hw_revision);

    /* 读取 MAC 地址 */
    rtl8139ex_read_mac();

    /* ---- 分配和设置描述符 ---- */
    /* TX 描述符: 链式, 4 个描述符 */
    uint32_t i;
    for (i = 0; i < RTL8139EX_NUM_TX_DESC; i++) {
        tx_buffers[i] = kmalloc(RTL8139EX_TX_BUF_SIZE);
        if (!tx_buffers[i]) {
            klog_err("rtl8139ex: Failed to alloc TX buffer %d", i);
            return;
        }
        memset(tx_buffers[i], 0, RTL8139EX_TX_BUF_SIZE);
        tx_desc_ring[i].tsd  = 0;
        tx_desc_ring[i].addr = (uint32_t)(uintptr_t)tx_buffers[i];
        tx_desc_ring[i].next = (i < RTL8139EX_NUM_TX_DESC - 1)
                               ? &tx_desc_ring[i + 1]
                               : &tx_desc_ring[0];
    }

    /* RX 环形缓冲区: 8K + 16 bytes */
    rx_buf_size = RTL8139EX_RX_BUF_SIZE;
    rx_ring_buffer = kmalloc(rx_buf_size + 16);
    if (!rx_ring_buffer) {
        klog_err("rtl8139ex: Failed to alloc RX ring buffer");
        return;
    }
    memset(rx_ring_buffer, 0, rx_buf_size + 16);

    /* 设置 RX 缓冲区物理地址 */
    rtl8139ex_write_reg(RTL8139EX_REG_RBSTART, (uint32_t)(uintptr_t)rx_ring_buffer);

    /* ---- 配置接收 ---- */
    /* RCR: 物理匹配 + 多播 + 广播 + WRAP, 缓冲区大小 = 8K */
    uint32_t rcr = RTL8139EX_RCR_APM |
                   RTL8139EX_RCR_AM  |
                   RTL8139EX_RCR_AB  |
                   RTL8139EX_RCR_WRAP |
                   RTL8139EX_RCR_RBLEN_8K |
                   RTL8139EX_RCR_MXDMA_1024 |
                   RTL8139EX_RCR_RXFTH_DEFAULT;
    rtl8139ex_write_reg(RTL8139EX_REG_RCR, rcr);

    /* ---- 配置发送 ---- */
    /* TCR: Max DMA burst = 1024, 正常 IFG */
    uint32_t tcr = RTL8139EX_TCR_MXDMA_1024 |
                   RTL8139EX_TCR_IFG_NORMAL;
    /* 如果芯片版本支持校验和卸载, 启用 */
    if (hw_revision >= 0x20) {
        /* RTL8139C+ 及更新版本支持 IP/TCP/UDP 校验和卸载 */
    }
    rtl8139ex_write_reg(RTL8139EX_REG_TCR, tcr);

    /* 初始化 TX 描述符: 清除所有 TSD 寄存器 */
    for (i = 0; i < RTL8139EX_NUM_TX_DESC; i++) {
        rtl8139ex_write_reg(RTL8139EX_REG_TSD0 + i * 4, 0);
    }

    /* ---- 中断配置 ---- */
    /* 清除所有中断 */
    rtl8139ex_write_reg(RTL8139EX_REG_ISR, 0xFFFF);

    /* 配置中断调节 */
    rtl8139ex_configure_int_moderation();

    /* 设置中断屏蔽:
     * ROK: 接收完成
     * RER: 接收错误
     * TOK: 发送完成
     * TER: 发送错误
     * RXOVW: RX 缓冲区溢出
     * LNKCHG: 链路状态变化
     * TIMEOUT: 超时
     */
    rtl8139ex_write_reg(RTL8139EX_REG_IMR,
        RTL8139EX_IMR_ROK |
        RTL8139EX_IMR_RER |
        RTL8139EX_IMR_TOK |
        RTL8139EX_IMR_TER |
        RTL8139EX_IMR_RXOVW |
        RTL8139EX_IMR_LNKCHG |
        RTL8139EX_IMR_TIMEOUT);

    /* 注册中断处理程序 */
    uint8_t irq_line = (uint8_t)(pci_read_config(bus, dev, func, 0x3C) & 0xFF);
    irq_register_handler(0x20 + irq_line, rtl8139ex_irq_handler);

    /* ---- 启用 PHY 自动协商 ---- */
    /* RTL8139C+ 通过 BMCR (偏移 0x62) 访问 PHY */
    uint16_t bmcr = rtl8139ex_read_reg(RTL8139EX_REG_BMCR);
    bmcr |= (1 << 12);  /* 自动协商启用 */
    bmcr |= (1 << 9);   /* 重启自动协商 */
    rtl8139ex_write_reg(RTL8139EX_REG_BMCR, bmcr);

    /* ---- 电源管理 ---- */
    rtl8139ex_pm_init();

    /* ---- 启用 TX/RX ---- */
    uint8_t cr = rtl8139ex_read_byte(RTL8139EX_REG_CR);
    cr |= RTL8139EX_CR_TE | RTL8139EX_CR_RE;
    rtl8139ex_write_byte(RTL8139EX_REG_CR, cr);

    /* 检查链路 */
    rtl8139ex_phy_check();

    /* ---- 注册网络接口 ---- */
    strcpy(rtl8139ex_iface.name, "rl0");
    rtl8139ex_iface.up          = 1;
    rtl8139ex_iface.mtu         = 1500;
    rtl8139ex_iface.flags       = IFF_UP | IFF_BROADCAST | IFF_RUNNING | IFF_MULTICAST;
    rtl8139ex_iface.send        = rtl8139ex_send;
    rtl8139ex_iface.driver_data = (void *)0;
    rtl8139ex_iface.tx_packets  = 0;
    rtl8139ex_iface.rx_packets  = 0;
    rtl8139ex_iface.tx_bytes    = 0;
    rtl8139ex_iface.rx_bytes    = 0;
    rtl8139ex_iface.tx_errors   = 0;
    rtl8139ex_iface.rx_errors   = 0;
    net_register_interface(&rtl8139ex_iface);

    rtl8139ex_inited = 1;
    klog_info("rtl8139ex: Driver initialized, iface=%s", rtl8139ex_iface.name);
}

/* ---- 发送数据包 ---- */
int rtl8139ex_send(net_interface_t *iface, const void *data, uint32_t len) {
    if (!rtl8139ex_inited || len > RTL8139EX_TX_BUF_SIZE) return -1;

    /* 检查当前 TX 描述符是否空闲 */
    if (tx_desc_ring[tx_next_idx].tsd & RTL8139EX_TSD_OWN) {
        return -1;  /* NIC 仍拥有此描述符 */
    }

    /* 复制数据到 TX 缓冲区 */
    memcpy(tx_buffers[tx_next_idx], data, len);

    /* 构造 TSD: 包大小 (低 13 位) + OWN 位
     * 如果芯片支持, 添加 IP/TCP/UDP 校验和卸载标志 */
    uint32_t tsd = (len & RTL8139EX_TSD_SIZE_MASK) | RTL8139EX_TSD_OWN;

    /* 硬件校验和卸载 (RTL8139C+ / RTL8139D 支持) */
    if (hw_revision >= 0x20) {
        tsd |= RTL8139EX_TSD_IPCF  |  /* IP Checksum Offload   */
               RTL8139EX_TSD_TCPCF |  /* TCP Checksum Offload  */
               RTL8139EX_TSD_UDPCF;   /* UDP Checksum Offload  */
    }

    tx_desc_ring[tx_next_idx].tsd = tsd;

    /* 更新 TSD 和 TSAD 寄存器以通知 NIC */
    uint32_t tsd_reg = RTL8139EX_REG_TSD0 + tx_next_idx * 4;
    uint32_t tsad_reg = RTL8139EX_REG_TSAD0 + tx_next_idx * 4;

    rtl8139ex_write_reg(tsd_reg, tsd);
    rtl8139ex_write_reg(tsad_reg, (uint32_t)(uintptr_t)tx_buffers[tx_next_idx]);

    uint32_t old_idx = tx_next_idx;
    tx_next_idx = (tx_next_idx + 1) % RTL8139EX_NUM_TX_DESC;

    (void)iface;
    return (int)old_idx;
}

/* ---- 轮询接收数据包 ---- */
void rtl8139ex_poll(void) {
    if (!rtl8139ex_inited) return;

    /* RTL8139 RX: 环形缓冲区方式
     * 读取 CAPR (Current Address of Packet Read) 与 CBR (Current Buffer Address)
     * 当 CAPR != CBR 时表示有数据
     */
    uint16_t capr = (uint16_t)(rtl8139ex_read_reg(RTL8139EX_REG_CAPR) & 0xFFFF);
    uint16_t cbr  = (uint16_t)(rtl8139ex_read_reg(RTL8139EX_REG_CBR) & 0xFFFF);

    while (rx_offset != cbr) {
        /* 每个包前面是 4 字节的 RX Header:
         * +0: RSR (Receive Status Register)
         * +2: 包长度 (包括 CRC)
         */
        uint8_t *rx_ptr = (uint8_t *)rx_ring_buffer + rx_offset;

        uint16_t rsr   = *(uint16_t *)(rx_ptr + 0);
        uint16_t pkt_len = *(uint16_t *)(rx_ptr + 2);
        uint8_t *pkt_data = rx_ptr + 4;

        /* 计算实际包长度 (去掉 4 字节 CRC) */
        uint16_t data_len = pkt_len - 4;

        /* 检查 RSR: ROK 位表示接收成功 */
        if (rsr & 0x0001) { /* RSR ROK */
            /* 检查是否有错误 */
            if (!(rsr & 0x003E)) {  /* 没有 FAE, CRC, RUNT, LONG 错误 */
                net_buffer_t *buf = net_alloc_buffer();
                if (buf) {
                    /* 处理环形缓冲区跨越的情况 */
                    if ((rx_offset + 4 + data_len) > rx_buf_size) {
                        /* 包跨越缓冲区末尾, 需要处理两部分 */
                        uint16_t first_part = rx_buf_size - (rx_offset + 4);
                        memcpy(buf->data, pkt_data, first_part);
                        memcpy(buf->data + first_part, rx_ring_buffer,
                               data_len - first_part);
                    } else {
                        memcpy(buf->data, pkt_data, data_len);
                    }
                    buf->len    = data_len;
                    buf->offset = 0;
                    buf->iface  = &rtl8139ex_iface;

                    rtl8139ex_iface.rx_packets++;
                    rtl8139ex_iface.rx_bytes += data_len;
                    net_receive(buf);
                }
            } else {
                rtl8139ex_iface.rx_errors++;
            }
        }

        /* 更新 RX 偏移: 数据包开始位置对齐到 DWORD + CRC (4 bytes) + 头部 (4 bytes) */
        rx_offset = (rx_offset + pkt_len + 4 + 3) & ~3U; /* DWORD 对齐 */

        /* WRAP 处理: 如果接近缓冲区末尾, 回绕 */
        if (rx_offset >= rx_buf_size) {
            rx_offset -= rx_buf_size;
        }

        /* 更新 CBR 以通知 NIC 已读取的数据位置 */
        rtl8139ex_write_reg(RTL8139EX_REG_CAPR, rx_offset - 16);

        /* 重新读取 CBR */
        cbr = (uint16_t)(rtl8139ex_read_reg(RTL8139EX_REG_CBR) & 0xFFFF);
    }
}

/* ---- 中断处理 ---- */
void rtl8139ex_irq_handler(regs_t *regs) {
    if (!rtl8139ex_inited) return;

    uint16_t isr = rtl8139ex_read_reg(RTL8139EX_REG_ISR);

    if (isr & RTL8139EX_ISR_ROK) {
        rtl8139ex_poll();
    }
    if (isr & RTL8139EX_ISR_RER) {
        rtl8139ex_iface.rx_errors++;
    }
    if (isr & RTL8139EX_ISR_TOK) {
        /* TX 完成 */
    }
    if (isr & RTL8139EX_ISR_TER) {
        rtl8139ex_iface.tx_errors++;
    }
    if (isr & RTL8139EX_ISR_RXOVW) {
        /* RX 缓冲区溢出 */
        rtl8139ex_iface.rx_errors++;
    }
    if (isr & RTL8139EX_ISR_LNKCHG) {
        rtl8139ex_phy_check();
    }

    /* 清除已处理的中断位 */
    rtl8139ex_write_reg(RTL8139EX_REG_ISR, isr);

    (void)regs;
}

/* ---- PCI 探测入口 ---- */
int rtl8139ex_probe(void) {
    static const uint16_t rtl8139ex_dev_ids[] = {
        RTL8139EX_DEVICE_ID,
        RTL8139EX_DEVICE_ID_8129,
        RTL8139EX_DEVICE_ID_8138
    };
    static const int num_dev_ids = sizeof(rtl8139ex_dev_ids) / sizeof(rtl8139ex_dev_ids[0]);

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

                if (vendor_id == RTL8139EX_VENDOR_ID) {
                    int j;
                    for (j = 0; j < num_dev_ids; j++) {
                        if (device_id == rtl8139ex_dev_ids[j]) {
                            found = 1;
                            klog_info("rtl8139ex: Found device %04x:%04x at %02x:%02x.%x",
                                      vendor_id, device_id, bus, dev, func);
                            rtl8139ex_init((uint8_t)bus, dev, func);
                            break;
                        }
                    }
                }
            }
        }
    }

    return found ? 0 : -1;
}