/*
 * dm9000.c - Davicom DM9000 快速以太网控制器驱动
 *
 * 供嵌入式系统/QEMU 使用, 非 PCI 设备 (ISA 总线或内存映射)。
 * 使用内部 SRAM 进行数据包收/发, PHY 通过 MDIO 访问。
 *
 * 功能支持:
 * - 设备签名检测 (VID/PID 寄存器验证)
 * - 包 TX/RX 通过内部 SRAM (MWCMD/MRCMD)
 * - PHY 访问和自动协商 (通过 EPAR/EPDRL/EPDRH)
 * - MAC 地址从 EEPROM 读取
 * - 链路状态监控
 * - Loopback 测试
 * - 基本电源管理 (WoL Magic Packet)
 */

#include "dm9000.h"
#include "kheap.h"
#include "string.h"
#include "irq.h"
#include "klog.h"
#include "net.h"

/* ---- 私有状态 ---- */
static uint16_t io_base = DM9000_DEFAULT_IO_BASE; /* I/O base port            */
static uint8_t  dm9000_irq = 0;                   /* IRQ line                  */

static uint8_t  tx_buf[DM9000_TX_BUF_SIZE];        /* 发送缓冲区 (SRAM 之前)    */
static uint8_t  rx_buf[DM9000_RX_BUF_SIZE];        /* 接收缓冲区 (从 SRAM 读)   */

static net_interface_t *dm9000_iface = NULL;
static int             dm9000_inited = 0;
static uint32_t        dm9000_chip_id = 0;         /* 芯片 ID                  */
static uint8_t         dm9000_chip_rev = 0;        /* 芯片版本                 */

/* ---- 寄存器访问辅助函数 ---- */
/* DM9000 使用 INDEX 端口 + DATA 端口 间接访问寄存区域 */
static inline uint8_t dm9000_ioreg_read(uint8_t reg) {
	/* 写 INDEX 端口 */
	*(volatile uint8_t *)(uintptr_t)(io_base + DM9000_REG_INDEX_PORT) = reg;
	/* 读 DATA 端口 */
	return *(volatile uint8_t *)(uintptr_t)(io_base + DM9000_REG_DATA_PORT);
}

static inline void dm9000_ioreg_write(uint8_t reg, uint8_t val) {
	*(volatile uint8_t *)(uintptr_t)(io_base + DM9000_REG_INDEX_PORT) = reg;
	*(volatile uint8_t *)(uintptr_t)(io_base + DM9000_REG_DATA_PORT) = val;
}

/* ---- 内部 SRAM 访问 ---- */
/* 写数据到 TX SRAM (通过 MWCMD - 写入后自动递增地址) */
static void dm9000_sram_write(const uint8_t *data, uint16_t len) {
	uint16_t i;
	/* 设置 MWCMD 命令: 从 TX SRAM 起始地址写入 */
	dm9000_ioreg_write(DM9000_REG_MWCMD, 0x00);
	for (i = 0; i < len; i++) {
		dm9000_ioreg_write(DM9000_REG_MWCMD, data[i]);
	}
}

/* 从 RX SRAM 读取数据 (通过 MRCMD - 读取后自动递增地址) */
static void dm9000_sram_read(uint8_t *data, uint16_t len) {
	uint16_t i;
	/* 设置 MRCMD 命令: 从 RX SRAM 起始地址读取 */
	*(volatile uint8_t *)(uintptr_t)(io_base + DM9000_REG_INDEX_PORT) = DM9000_REG_MRCMD;
	for (i = 0; i < len; i++) {
		data[i] = *(volatile uint8_t *)(uintptr_t)(io_base + DM9000_REG_DATA_PORT);
	}
}

/* ---- PHY 访问 (通过 EPAR/EPDRL/EPDRH) ---- */
static int dm9000_phy_read(uint8_t reg, uint16_t *val) {
	/* 设置 PHY 地址和寄存器地址 */
	dm9000_ioreg_write(DM9000_REG_EPAR,
		(uint8_t)(0x40 | reg));  /* PHY addr = 0x01 (bit 6), reg addr (bits 0-4) */

	/* 设置 EPCR: 发起 PHY 读取 */
	dm9000_ioreg_write(DM9000_REG_EPCR,
		DM9000_EPCR_EPOS | DM9000_EPCR_ERPRR);

	/* 等待操作完成 (EPCR bit 0 清零) */
	uint32_t timeout = 100000;
	while ((dm9000_ioreg_read(DM9000_REG_EPCR) & DM9000_EPCR_ERRE) && timeout--)
		;

	/* 清除操作位 */
	dm9000_ioreg_write(DM9000_REG_EPCR, DM9000_EPCR_EPOS);

	/* 读取数据: 低字节 + 高字节 */
	uint8_t lo = dm9000_ioreg_read(DM9000_REG_EPDRL);
	uint8_t hi = dm9000_ioreg_read(DM9000_REG_EPDRH);
	*val = ((uint16_t)hi << 8) | lo;

	return 0;
}

static int dm9000_phy_write(uint8_t reg, uint16_t val) {
	/* 设置 PHY 地址和寄存器地址 */
	dm9000_ioreg_write(DM9000_REG_EPAR,
		(uint8_t)(0x40 | reg));

	/* 写数据 */
	dm9000_ioreg_write(DM9000_REG_EPDRL, (uint8_t)(val & 0xFF));
	dm9000_ioreg_write(DM9000_REG_EPDRH, (uint8_t)((val >> 8) & 0xFF));

	/* 设置 EPCR: 发起 PHY 写操作 */
	dm9000_ioreg_write(DM9000_REG_EPCR,
		DM9000_EPCR_EPOS | DM9000_EPCR_ERPRW);

	/* 等待操作完成 */
	uint32_t timeout = 100000;
	while ((dm9000_ioreg_read(DM9000_REG_EPCR) & DM9000_EPCR_ERRE) && timeout--)
		;

	/* 清除操作位 */
	dm9000_ioreg_write(DM9000_REG_EPCR, DM9000_EPCR_EPOS);

	return 0;
}

/* ---- 设备签名检测 ---- */
static int dm9000_detect_chip(void) {
	/* 读取 Vendor ID (0x28-0x29) 和 Product ID (0x2A-0x2B) */
	uint8_t vid_l = dm9000_ioreg_read(DM9000_REG_VIDL);
	uint8_t vid_h = dm9000_ioreg_read(DM9000_REG_VIDH);
	uint8_t pid_l = dm9000_ioreg_read(DM9000_REG_PIDL);
	uint8_t pid_h = dm9000_ioreg_read(DM9000_REG_PIDH);

	dm9000_chip_id = ((uint32_t)vid_h << 24) | ((uint32_t)vid_l << 16) |
	                 ((uint32_t)pid_h << 8)  | (uint32_t)pid_l;

	dm9000_chip_rev = dm9000_ioreg_read(DM9000_REG_CHIPR);

	/* 验证芯片 ID */
	if (dm9000_chip_id != DM9000_CHIP_ID_DM9000 &&
	    dm9000_chip_id != DM9000_CHIP_ID_DM9000A &&
	    dm9000_chip_id != DM9000_CHIP_ID_DM9000B &&
	    dm9000_chip_id != DM9000_CHIP_ID_DM9000C &&
	    dm9000_chip_id != DM9000_CHIP_ID_DM9000EP) {
		klog_err("dm9000: Unknown chip ID 0x%08x", dm9000_chip_id);
		return -1;
	}

	klog_info("dm9000: Chip detected: ID=0x%08x, Rev=0x%02x",
	          dm9000_chip_id, dm9000_chip_rev);
	return 0;
}

/* ---- MAC 地址读取 (从 EEPROM) ---- */
static void dm9000_read_mac(void) {
	uint8_t i;
	uint8_t mac[6];
	for (i = 0; i < 6; i++) {
		mac[i] = dm9000_ioreg_read(DM9000_REG_PAR + i);
	}
	klog_info("dm9000: MAC address: %02x:%02x:%02x:%02x:%02x:%02x",
	          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* ---- PHY 自动协商 ---- */
static void dm9000_phy_autoneg(void) {
	uint16_t bmcr;

	/* 读取 PHY 控制寄存器 */
	if (dm9000_phy_read(DM9000_PHY_BMCR, &bmcr) != 0) {
		klog_err("dm9000: Failed to read PHY BMCR");
		return;
	}

	/* 设置: 100Mbps, Full Duplex, 自动协商启用, 重启自动协商 */
	bmcr |= DM9000_PHY_BMCR_AUTONEG_EN |
	        DM9000_PHY_BMCR_RESTART_AN;

	dm9000_phy_write(DM9000_PHY_BMCR, bmcr);
	klog_info("dm9000: PHY auto-negotiation started");

	/* 等待自动协商完成 */
	uint32_t timeout = 500000;
	uint16_t bmsr;
	do {
		dm9000_phy_read(DM9000_PHY_BMSR, &bmsr);
	} while (!(bmsr & DM9000_PHY_BMSR_AUTONEG_OK) && --timeout);

	if (timeout == 0) {
		klog_warn("dm9000: PHY auto-negotiation timeout");
	}
}

/* ---- 链路状态监控 ---- */
static void dm9000_check_link(void) {
	if (!dm9000_iface) return;
	uint8_t nsr = dm9000_ioreg_read(DM9000_REG_NSR);

	if (nsr & DM9000_NSR_LINKST) {
		dm9000_iface->flags |= IFF_RUNNING;
		const char *speed = (nsr & DM9000_NSR_SPEED) ? "100" : "10";
		klog_info("dm9000: Link up at %s Mbps", speed);
	} else {
		dm9000_iface->flags &= ~(uint32_t)(IFF_RUNNING);
		klog_warn("dm9000: Link down");
	}
}

/* ---- 发送数据包 ---- */
static int dm9000_send_pkt(net_interface_t *iface, const void *data, uint32_t len) {
	if (!dm9000_inited || len > DM9000_TX_BUF_SIZE) return -1;

	/* 复制数据到发送缓冲区 */
	memcpy(tx_buf, data, len);

	/* 写数据到 TX SRAM */
	dm9000_sram_write(tx_buf, (uint16_t)len);

	/* 设置 TX 包长度 */
	dm9000_ioreg_write(DM9000_REG_TXPLL, (uint8_t)(len & 0xFF));
	dm9000_ioreg_write(DM9000_REG_TXPLH, (uint8_t)((len >> 8) & 0xFF));

	/* 触发发送: 设置 TCR.TXREQ 位 */
	dm9000_ioreg_write(DM9000_REG_TCR,
		dm9000_ioreg_read(DM9000_REG_TCR) | DM9000_TCR_TXREQ);

	/* 等待 TX 完成 (NSR.TX1END 或 NSR.TX2END) */
	uint32_t timeout = 100000;
	uint8_t nsr;
	do {
		nsr = dm9000_ioreg_read(DM9000_REG_NSR);
	} while (!(nsr & (DM9000_NSR_TX1END | DM9000_NSR_TX2END)) && --timeout);
	if (timeout == 0) {
		klog_warn("dm9000: TX timeout");
		if (dm9000_iface) dm9000_iface->tx_errors++;
		return -1;
	}

	if (dm9000_iface) {
		dm9000_iface->tx_packets++;
		dm9000_iface->tx_bytes += len;
	}

	/* 清除 TX 完成标志 */
	dm9000_ioreg_write(DM9000_REG_NSR,
		nsr | DM9000_NSR_TX1END | DM9000_NSR_TX2END);

	(void)iface;
	return (int)len;
}

/* ---- 轮询接收数据包 ---- */
void dm9000_poll(void) {
	if (!dm9000_inited) return;

	uint8_t isr = dm9000_ioreg_read(DM9000_REG_ISR);

	/* 检查是否有接收数据 (PRS) */
	if (!(isr & DM9000_ISR_PRS)) {
		return;
	}

	/* 检查 NSR.RXRDY */
	uint8_t nsr = dm9000_ioreg_read(DM9000_REG_NSR);

	while (nsr & DM9000_NSR_RXRDY) {
		/* 从 RX SRAM 读取数据包:
		 * DM9000 在 RX SRAM 中存储的包格式:
		 * +0: RSR (1 byte) - Receive Status
		 * +1: 包长度低字节
		 * +2: 包长度高字节
		 * +3: 数据开始
		 */
		uint8_t rx_hdr[4];
		dm9000_sram_read(rx_hdr, 4);

		uint8_t  rsr     = rx_hdr[0];
		uint16_t pkt_len = ((uint16_t)rx_hdr[2] << 8) | rx_hdr[1];

		if (rsr & DM9000_RSR_RXOK) {
			/* 读取包数据 */
			if (pkt_len > DM9000_RX_BUF_SIZE) {
				klog_warn("dm9000: Packet too large (%d bytes), dropping", pkt_len);
			} else {
				dm9000_sram_read(rx_buf, pkt_len);

				net_buffer_t *buf = net_alloc_buffer();
				if (buf) {
					memcpy(buf->data, rx_buf, pkt_len);
					buf->len    = pkt_len;
					buf->offset = 0;
					buf->iface  = dm9000_iface;

					if (rsr & (DM9000_RSR_CE | DM9000_RSR_AE)) {
						if (dm9000_iface) dm9000_iface->rx_errors++;
						net_free_buffer(buf);
					} else {
						if (dm9000_iface) {
							dm9000_iface->rx_packets++;
							dm9000_iface->rx_bytes += pkt_len;
						}
						net_receive(buf);
					}
				}
			}
		} else {
			if (dm9000_iface) dm9000_iface->rx_errors++;
		}

		/* 刷新 RX SRAM 读取指针: MRCMDX 读取不递增 */
		*(volatile uint8_t *)(uintptr_t)(io_base + DM9000_REG_INDEX_PORT) = DM9000_REG_MRCMDX;
		(void)*(volatile uint8_t *)(uintptr_t)(io_base + DM9000_REG_DATA_PORT);

		nsr = dm9000_ioreg_read(DM9000_REG_NSR);
	}

	/* 清除 ISR PRS 位 */
	dm9000_ioreg_write(DM9000_REG_ISR, DM9000_ISR_PRS);
}

/* ---- 中断处理 ---- */
void dm9000_irq_handler(regs_t *regs) {
	if (!dm9000_inited) return;

	uint8_t isr = dm9000_ioreg_read(DM9000_REG_ISR);

	if (isr & DM9000_ISR_PRS) {
		dm9000_poll();
	}
	if (isr & DM9000_ISR_PTS) {
		/* TX 完成 */
	}
	if (isr & DM9000_ISR_LNKCHG) {
		dm9000_check_link();
	}

	/* 清除中断状态 */
	dm9000_ioreg_write(DM9000_REG_ISR, isr);

	(void)regs;
}

/* ---- 初始化 ---- */
void dm9000_init(uint16_t base, uint8_t irq) {
	io_base    = base;
	dm9000_irq = irq;

	/* ---- 复位设备 ---- */
	dm9000_ioreg_write(DM9000_REG_NCR,
		dm9000_ioreg_read(DM9000_REG_NCR) | DM9000_NCR_RST);
	uint32_t timeout = 100000;
	while ((dm9000_ioreg_read(DM9000_REG_NCR) & DM9000_NCR_RST) && timeout--)
		;
	if (timeout == 0) {
		klog_err("dm9000: Reset timeout at I/O base 0x%x", io_base);
		return;
	}

	/* ---- 检测芯片 ---- */
	if (dm9000_detect_chip() != 0) {
		return;
	}

	/* ---- 分配网络接口 ---- */
	dm9000_iface = kmalloc(sizeof(net_interface_t));
	if (!dm9000_iface) {
		klog_err("dm9000: Failed to allocate interface");
		return;
	}
	memset(dm9000_iface, 0, sizeof(net_interface_t));

	/* ---- 读取 MAC 地址 ---- */
	{
		uint8_t i;
		for (i = 0; i < 6; i++) {
			dm9000_iface->mac.bytes[i] = dm9000_ioreg_read(DM9000_REG_PAR + i);
		}
	}
	klog_info("dm9000: MAC address: %02x:%02x:%02x:%02x:%02x:%02x",
	          dm9000_iface->mac.bytes[0], dm9000_iface->mac.bytes[1],
	          dm9000_iface->mac.bytes[2], dm9000_iface->mac.bytes[3],
	          dm9000_iface->mac.bytes[4], dm9000_iface->mac.bytes[5]);

	/* ---- 配置网络控制寄存器 ---- */
	/* NCR: 正常模式, 全双工, 内部 PHY */
	uint8_t ncr = DM9000_NCR_LBK_NORMAL | DM9000_NCR_FDX;
	dm9000_ioreg_write(DM9000_REG_NCR, ncr);

	/* ---- 配置接收控制 ---- */
	/* RCR: RX 启用, 丢弃长包, 丢弃 CRC 错误包, 禁用看门狗 */
	uint8_t rcr = DM9000_RCR_RXEN |
	              DM9000_RCR_DIS_LONG |
	              DM9000_RCR_DIS_CRC |
	              DM9000_RCR_WTDIS;
	dm9000_ioreg_write(DM9000_REG_RCR, rcr);

	/* ---- 配置发送控制 ---- */
	/* TCR: IP 校验和卸载 + TCP/UDP 校验和卸载 + 填充短包 */
	uint8_t tcr = DM9000_TCR_IPCRC |
	              DM9000_TCR_TCPCRC |
	              DM9000_TCR_PAD_EN;
	dm9000_ioreg_write(DM9000_REG_TCR, tcr);

	/* ---- 配置流控制 ---- */
	dm9000_ioreg_write(DM9000_REG_FCR, 0x29);  /* 默认流控制设置 */

	/* ---- 配置中断 ---- */
	/* 清除所有待处理中断 */
	dm9000_ioreg_write(DM9000_REG_ISR, 0xFF);

	/* 启用中断: PRS (包接收), PTS (包发送), LNKCHG (链路变化) */
	dm9000_ioreg_write(DM9000_REG_IMR,
		DM9000_IMR_PRM | DM9000_IMR_PTM | DM9000_IMR_PRM6);

	/* 注册中断处理程序 */
	if (dm9000_irq != 0) {
		irq_register_handler(dm9000_irq, dm9000_irq_handler);
	}

	/* ---- PHY 自动协商 ---- */
	dm9000_phy_autoneg();

	/* ---- 电源管理 (WoL) ---- */
	dm9000_ioreg_write(DM9000_REG_WCR,
		DM9000_WCR_MAGICEN | DM9000_WCR_LINKEN |
		DM9000_WCR_WAKEEN | DM9000_WCR_SAMPLEEN);

	/* ---- 检查链路 ---- */
	dm9000_check_link();

	/* ---- 注册网络接口 ---- */
	strcpy(dm9000_iface->name, "dm0");
	dm9000_iface->up          = 1;
	dm9000_iface->mtu         = 1500;
	dm9000_iface->flags       = IFF_UP | IFF_BROADCAST | IFF_RUNNING | IFF_MULTICAST;
	dm9000_iface->send        = dm9000_send_pkt;
	dm9000_iface->driver_data = NULL;
	dm9000_iface->tx_packets  = 0;
	dm9000_iface->rx_packets  = 0;
	dm9000_iface->tx_bytes    = 0;
	dm9000_iface->rx_bytes    = 0;
	dm9000_iface->tx_errors   = 0;
	dm9000_iface->rx_errors   = 0;
	net_register_interface(dm9000_iface);

	dm9000_inited = 1;
	klog_info("dm9000: Driver initialized, iface=%s", dm9000_iface->name);
}

/* ---- 探测入口 (非 PCI, 通过默认 I/O 基地址) ---- */
int dm9000_probe(void) {
	int found = 0;

	/* 尝试默认 I/O 基地址: 0x300, 0x310, 0x320, ..., 0x370 */
	uint16_t bases[] = { 0x300, 0x310, 0x320, 0x330, 0x340, 0x350, 0x360, 0x370, 0 };
	uint8_t irqs[]   = { 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0 };

	int b;
	for (b = 0; bases[b] != 0 && !found; b++) {
		io_base = bases[b];

		/* 读取 Vendor ID 以验证芯片存在 */
		uint8_t vid_l = dm9000_ioreg_read(DM9000_REG_VIDL);
		uint8_t vid_h = dm9000_ioreg_read(DM9000_REG_VIDH);

		uint32_t chip_id = ((uint32_t)vid_h << 24) | ((uint32_t)vid_l << 16);

		if (chip_id == 0x0A460000 || chip_id == 0x0B460000 ||
		    chip_id == 0x0C460000 || chip_id == 0x0E460000) {
			found = 1;
			klog_info("dm9000: Found chip at I/O base 0x%x", io_base);
			dm9000_init(io_base, irqs[b]);
		}
	}

	return found ? 0 : -1;
}