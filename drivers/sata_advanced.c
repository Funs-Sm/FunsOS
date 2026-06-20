/*
 * sata_advanced.c - SATA/AHCI 高级驱动实现
 *
 * 功能:
 * - AHCI 1.3 规范兼容的 SATA 主机控制器驱动
 * - NCQ (Native Command Queuing) 支持: 最多 32 条排队命令
 * - LBA48 寻址: 支持超过 2TB 的硬盘
 * - SMART 监控: 读取属性/阈值/自检
 * - 热插拔检测: 设备插入/移除回调
 * - 链路电源管理: Partial/Slumber 状态
 *
 * AHCI 命令流程:
 * 1. 构造 Command Table (包含 FIS + Physical Region Descriptors)
 * 2. 在 Command List 中分配一个命令槽位 (Command Header)
 * 3. 设置 PxCI (Command Issue) 触发硬件执行
 * 4. 轮询 PxIS 或等待中断确认完成
 */

#include "sata_advanced.h"
#include "klog.h"
#include "string.h"

/* ---- 全局控制器实例 ---- */
static ahci_controller_t g_ahci_ctrl;

/* ---- AHCI 命令表结构定义 ----
 * 每个 AHCI 端口需要:
 *   - Command List: 每个槽位的 Command Header (16 字节)
 *   - Command Table: 包含 FIS 和 PRDT
 *   - Received FIS Area: 存储设备返回的 FIS
 *
 * Command Header 格式 (16 字节):
 *   DW0: bits [4:0] = PRDTL (PRDT length)
 *        bit 6 = BIST (Built-In Self Test)
 *        bit 7 = RSP (Reset PMP)
 *        bits [15:8] = PMP (Port Multiplier Port)
 *   DW1: bits [9:0] = DB (Data Byte Count / PRDT byte count)
 *   DW2-3: Command Table Base Address (CTBA), 128 字节对齐
 *
 * FIS (Frame Information Structure) 类型:
 *   - Register Host-to-Device FIS (0x27): 发送 ATA 命令
 *   - DMA Setup FIS (0x41): DMA 传输设置
 *   - Data FIS (0x46): 数据传输
 *   - PIO Setup FIS (0x5F): PIO 传输设置
 *   - D2H Register FIS (0x34): Device 到 Host 的状态报告
 *
 * PRDT (Physical Region Descriptor Table) 条目:
 *   每个 PRDT 描述一段物理连续的内存区域:
 *   DW0-1: Data Base Address (DBA), 4 字节对齐
 *   DW2: bits [21:0] =DBC (Data Byte Count), 最大 4MB
 *        bit 31 = Interrupt on Completion (IOC)
 */

/* ---- 内部寄存器访问辅助 ---- */
static inline uint32_t ahci_read_global(ahci_controller_t *ctrl, uint32_t offset)
{
    return ctrl->mmio_base[offset / 4];
}

static inline void ahci_write_global(ahci_controller_t *ctrl, uint32_t offset, uint32_t val)
{
    ctrl->mmio_base[offset / 4] = val;
}

static inline uint32_t ahci_read_port(ahci_port_t *port, uint32_t offset)
{
    volatile uint32_t *base = (volatile uint32_t *)port->base;
    return base[offset / 4];
}

static inline void ahci_write_port(ahci_port_t *port, uint32_t offset, uint32_t val)
{
    volatile uint32_t *base = (volatile uint32_t *)port->base;
    base[offset / 4] = val;
}

/* ---- 等待端口就绪 ----
 * 检查 PxCMD 的 CR (Command List Running) 和 FR (FIS Running) 位。
 * 端口操作前必须确保这两个位都为 0。 */
static int ahci_wait_port_ready(ahci_port_t *port, uint32_t timeout_ms)
{
    while (timeout_ms--) {
        uint32_t cmd = ahci_read_port(port, AHCI_PxCMD);
        if (!(cmd & (AHCI_PxCMD_CR | AHCI_PxCMD_FR))) {
            return 0;  /* 就绪 */
        }
        /* 短暂延时 (~1ms) */
        for (volatile int i = 0; i < 1000; i++)
            __asm__ __volatile__("nop");
    }
    klog_err("sata: Port %u not ready after wait", port->port_num);
    return -1;
}

/*
 * 从 Identify 数据中提取 ASCII 字符串。
 * ATA 使用 16-bit word 存储 ASCII (每个字存两个字符, 高字节在前),
 * 需要字节交换并添加 null 终止符。
 */
static void extract_ascii_string(uint16_t *src, char *dst, int max_words)
{
    int i, pos = 0;
    for (i = 0; i < max_words && pos < max_words * 2 - 1; i++) {
        dst[pos++] = (char)(src[i] >> 8);     /* 高字节 */
        dst[pos++] = (char)(src[i] & 0xFF);    /* 低字节 */
    }
    /* 去除尾部空格 */
    while (pos > 0 && dst[pos - 1] == ' ') {
        pos--;
    }
    dst[pos] = '\0';
}

/*
 * 解析 ATA IDENTIFY DEVICE 返回的数据结构。
 * 提取型号、序列号、固件版本、容量等关键信息。
 */
static void parse_identify_data(ahci_port_t *port)
{
    ata_identify_t *id = &port->identify;

    /* 提取模型号 (words 27-46, 20 words -> 40 chars) */
    extract_ascii_string(id->model_no, port->model, 20);

    /* 提取序列号 (words 10-19, 10 words -> 20 chars) */
    extract_ascii_string(id->serial_no, port->serial, 10);

    /* 提取固件版本 (words 23-26, 4 words -> 8 chars) */
    extract_ascii_string(id->firmware_rev, port->firmware, 4);

    /* LBA48 容量 (words 100-103, 64-bit) */
    port->max_lba48 = id->lba48_sectors;

    /* LBA28 容量 (words 60-61, 32-bit) */
    port->max_lba = ((uint64_t)id->capacity_sectors[1] << 16) |
                    id->capacity_sectors[0];

    /* 扇区大小 (通常 512 字节, 4K 扇区在 word 106 中标识) */
    port->sector_size = 512;
    if (id->features_support & (1 << 14)) {
        /* 支持 4K 扇区 */
        if (id->features_enable & (1 << 14)) {
            port->sector_size = 4096;
        }
    }

    /* 检查 SMART 支持 (word 87, bit 0) */
    port->smart_supported = (id->smart_commands & 0x0001) ? 1 : 0;

    /* 检查 NCQ 支持 (word 76, bit 8) */
    if (id->features_support & (1 << 8)) {
        port->ncq_enabled = 1;
        /* Queue depth: word 75, bits [4:0] + 1 */
        port->queue_depth = (uint8_t)((id->features_default >> 0) & 0x1F) + 1;
        if (port->queue_depth > 32) port->queue_depth = 32;
    }

    klog_info("sata: Port %u device: %s SN=%s FW=%s LBA48=%llu sectors (%llu MB)",
              port->port_num,
              port->model, port->serial, port->firmware,
              (unsigned long long)port->max_lba48,
              (unsigned long long)(port->max_lba48 * port->sector_size / (1024 * 1024)));
}


/*
 * ================================================================
 * 公共 API 实现
 * ================================================================
 */

/*
 * 初始化 AHCI 控制器。
 *
 * 步骤:
 * 1. 读取全局寄存器获取控制器参数 (CAP, PI, VERS)
 * 2. 使能 AHCI 模式 (GHC.AE = 1)
 * 3. 执行 BIOS/OS Handoff (如果支持)
 * 4. 复位主机控制器
 * 5. 配置中断使能
 *
 * mmio_base: AHCI 控制器的 MMIO 物理基址 (来自 PCI BAR5)
 */
int sata_init(volatile uint32_t *mmio_base)
{
    ahci_controller_t *ctrl = &g_ahci_ctrl;
    memset(ctrl, 0, sizeof(ahci_controller_t));

    ctrl->mmio_base = mmio_base;

    /* ---- Step 1: 读取基本能力 ---- */
    ctrl->cap  = ahci_read_global(ctrl, AHCI_CAP);
    ctrl->cap2 = ahci_read_global(ctrl, AHCI_CAP2);
    ctrl->ghc  = ahci_read_global(ctrl, AHCI_GHC);
    ctrl->ports_implemented = ahci_read_global(ctrl, AHCI_PI);

    /* AHCI 版本 (BCD 编码): 0x10000 = 1.0, 0x10100 = 1.01, etc. */
    ctrl->version = ahci_read_global(ctrl, AHCI_VS);

    /* 解析 CAP 寄存器 */
    ctrl->ncs = (ctrl->cap >> AHCI_CAP_NCS_SHIFT) & 0x1F;  /* Number of Command Slots */
    ctrl->nports = (ctrl->cap >> AHCI_CAP_NP_SHIFT) & 0x1F; /* Number of Ports */
    ctrl->has_ncq = (ctrl->cap & AHCI_CAP_NCQ) ? 1 : 0;

    /* 初始化每个端口的基地址指针 */
    for (uint32_t i = 0; i < ctrl->nports && i < 32; i++) {
        ctrl->ports[i].port_num = (uint8_t)i;
        ctrl->ports[i].base = (void *)((uintptr_t)mmio_base + 0x100 + i * 0x80);
        ctrl->ports[i].cmd_slot_count = ctrl->ncs;
        ctrl->ports[i].sector_size = 512;
    }

    /* ---- Step 2: BIOS/OS Handoff ----
     * 如果 BOHC 寄存器存在且 OS Ownership 位被设置,
     * 说明 BIOS 还持有控制权, 需要请求移交。 */
    if (ctrl->cap2 & (1 << 0)) {  /* BOH supported */
        uint32_t bohc = ahci_read_global(ctrl, AHCI_BOHC);
        if (bohc & (1 << 0)) {  /* BIOS Busy */
            /* 设置 OOS (OS Owned Semaphore) 请求控制权移交 */
            bohc |= (1 << 1);
            ahci_write_global(ctrl, AHCI_BOHC, bohc);

            /* 等待 BIOS 清除 BBO (BIOS Owns Semaphore) */
            uint32_t timeout = 100000;
            while (timeout--) {
                bohc = ahci_read_global(ctrl, AHCI_BOHC);
                if (!(bohc & (1 << 0))) break;  /* BIOS Busy cleared */
                for (volatile int i = 0; i < 100; i++)
                    __asm__ __volatile__("nop");
            }
            if (timeout == 0) {
                klog_warn("sata: BIOS handoff timed out, forcing takeover");
                /* 强制清除 BB (BIOS ownership) */
                ahci_write_global(ctrl, AHCI_BOHC, (1 << 1));
            }
        }
    }

    /* ---- Step 3: 使能 AHCI 模式 ----
     * GHC.AE (AHCI Enable) 必须在访问 AHCI 特有寄存器之前设置 */
    ctrl->ghc |= AHCI_GHC_AE;
    ahci_write_global(ctrl, AHCI_GHC, ctrl->ghc);

    /* ---- Step 4: 全局复位 ---- */
    ctrl->ghc |= AHCI_GHC_HR;  /* Host Controller Reset */
    ahci_write_global(ctrl, AHCI_GHC, ctrl->ghc);

    /* 等待 HR 自动清除 */
    uint32_t timeout = 1000000;
    while (timeout--) {
        ctrl->ghc = ahci_read_global(ctrl, AHCI_GHC);
        if (!(ctrl->ghc & AHCI_GHC_HR)) break;
        for (volatile int i = 0; i < 10; i++)
            __asm__ __volatile__("nop");
    }

    if (timeout == 0) {
        klog_err("sata: Global reset failed");
        return -1;
    }

    /* 重新使能 AE (复位会清除) */
    ctrl->ghc |= AHCI_GHC_AE | AHCI_GHC_IE;
    ahci_write_global(ctrl, AHCI_GHC, ctrl->ghc);

    /* 清除所有待处理的全局中断状态 */
    ahci_write_global(ctrl, AHCI_IS, 0xFFFFFFFF);

    klog_info("sata: AHCI controller initialized, ver=0x%04X ports=%u slots=%u "
              "NCQ=%d",
              ctrl->version, ctrl->nports, ctrl->ncs, ctrl->has_ncq);

    return 0;
}

/*
 * 扫描所有已实现的端口, 检测连接的设备。
 *
 * 对每个端口:
 * 1. 检查 SStatus.DET (Detection) 判断是否有设备连接
 * 2. 启动端口 (PxCMD.ST = 1, FRE = 1)
 * 3. 等待 BSY/DREQ/DRDY 信号稳定
 * 4. 发送 IDENTIFY DEVICE 命令读取设备信息
 * 5. 配置 NCQ 和 SMART (如果支持)
 */
int sata_scan_ports(ahci_controller_t *ctrl)
{
    if (!ctrl || !ctrl->mmio_base) return -1;

    uint32_t found_devices = 0;

    for (uint32_t p = 0; p < ctrl->nports && p < 32; p++) {
        ahci_port_t *port = &ctrl->ports[p];

        /* 检查该端口是否被 PI 标记为实现 */
        if (!(ctrl->ports_implemented & (1U << p))) continue;

        /* ---- 读取 SStatus (SATA Status Register) ----
         * SStatus 布局:
         * bits [3:0]: Device Detection (DET)
         *   0=No device, 1=Device present but no communication,
         *   2=Device present and communication established,
         *   3=Phy offline
         * bits [11:8]: Interface Power Management State (IPM)
         * bits [19:12]: Current Interface Speed (SPD):
         *   0=None, 1=Gen1(1.5Gb/s), 2=Gen2(3Gb/s), 3=Gen3(6Gb/s) */
        uint32_t sstatus = ahci_read_port(port, AhCI_PxSSTS);
        uint8_t det = (uint8_t)(sstatus & 0x0F);

        if (det != 0x03) {
            /* 无设备或通信未建立 */
            port->present = 0;
            port->active = 0;
            continue;
        }

        port->present = 1;
        port->sstatus = sstatus;

        /* 提取链路速度 */
        port->link_speed = (uint8_t)((sstatus >> 4) & 0x0F);

        /* ---- 启动端口 ---- */
        /* 先确保端口处于停止状态 */
        uint32_t cmd = ahci_read_port(port, AHCI_PxCMD);
        cmd &= ~(AHCI_PxCMD_ST | AHCI_PxCMD_FRE);  /* Stop + Disable FIS RX */
        ahci_write_port(port, AHCI_PxCMD, cmd);

        /* 等待 CR 和 FR 清零 */
        if (ahci_wait_port_ready(port, 5000) != 0) {
            klog_warn("sata: Port %u failed to stop before start", p);
            continue;
        }

        /* 分配命令列表和 FIS 区域 (简化版: 使用静态缓冲区) */
        static uint8_t cmd_list_mem[32][1024];   /* 每个端口 1KB 命令列表 */
        static uint8_t fis_mem[32][256];          /* 每个端口 256B FIS 区域 */
        port->cmd_list = cmd_list_mem[p];
        port->fis_base = fis_mem[p];

        memset((void *)port->cmd_list, 0, 1024);
        memset((void *)port->fis_base, 0, 256);

        /* 设置 CLB (Command List Base) 和 FB (FIS Base) */
        ahci_write_port(port, AHCI_PxCLB, (uint32_t)(uintptr_t)port->cmd_list);
        ahci_write_port(port, AHCI_PxFB,  (uint32_t)(uintptr_t)port->fis_base);

        /* 清除所有端口错误状态 */
        ahci_write_port(port, AhCI_PxSERR, 0xFFFFFFFF);
        ahci_write_port(port, AHCI_PxIS, 0xFFFFFFFF);

        /* 使能 FIS 接收 */
        cmd = ahci_read_port(port, AHCI_PxCMD);
        cmd |= AHCI_PxCMD_FRE;
        ahci_write_port(port, AHCI_PxCMD, cmd);

        /* 短暂延时确保 FRE 生效 */
        for (volatile int i = 0; i < 10000; i++)
            __asm__ __volatile__("nop");

        /* 启动端口 */
        cmd |= AHCI_PxCMD_ST;
        ahci_write_port(port, AHCI_PxCMD, cmd);

        /* 等待端口启动完成 (ST 保持为 1) */
        int timeout = 10000;
        while (timeout--) {
            cmd = ahci_read_port(port, AHCI_PxCMD);
            if (cmd & AHCI_PxCMD_ST) break;
            for (volatile int j = 0; j < 100; j++)
                __asm__ __volatile__("nop");
        }

        port->active = 1;

        /* ---- 发送 IDENTIFY DEVICE ---- */
        if (sata_identify(port) == 0) {
            found_devices++;

            /* 如果支持 SMART, 默认启用 */
            if (port->smart_supported) {
                sata_smart_enable(port);
            }
        } else {
            klog_warn("sata: Port %u identify failed", p);
        }
    }

    klog_info("sata: Port scan complete, %u devices found", found_devices);
    return (int)found_devices;
}

/*
 * 发送 ATA 命令到指定端口的核心函数。
 *
 * 构造完整的 AHCI 命令结构:
 * 1. Command Header (在 Command List 中)
 * 2. Register FIS (Host-to-Device FIS)
 * 3. Physical Region Descriptor(s) (指向数据缓冲区)
 *
 * command: ATA 命令码 (如 0xEC = IDENTIFY, 0x25 = READ_DMA_EXT)
 * features: Features 寄存器值
 * lba: 起始逻辑块地址 (LBA48 使用低 48 位)
 * count: 扇区数量
 * buffer: 数据缓冲区指针
 * is_write: 1=写操作, 0=读操作
 * use_ncq: 1=使用 NCQ FPDMA 命令格式
 *
 * 返回: 0 成功, -1 失败
 */
static int sata_issue_command(ahci_port_t *port, uint8_t command,
                               uint8_t features, uint64_t lba, uint16_t count,
                               void *buffer, int is_write, int use_ncq)
{
    if (!port || !port->active) return -1;

    /* 使用命令槽位 0 (简化实现) */
    const int slot = 0;
    uint32_t *cmd_header = (uint32_t *)((uintptr_t)port->cmd_list + slot * 32);

    /* ---- 构造 Command Table ----
     * Command Table 紧跟在 Command List 之后 (或通过 CTBA 指定)
     * 这里将 Command Table 放在命令列表之后 */
    static uint8_t cmd_table_mem[32][256];  /* 每个端口 256B 命令表 */
    uint8_t *cmd_table = cmd_table_mem[port->port_num];
    memset(cmd_table, 0, 256);

    /* ---- 构建 Register Host-to-Device FIS (40 字节, offset 0) ----
     * FIS Type = 0x27 (Register H2D)
     * 对于 NCQ (FPDMA), FIS 格式略有不同:
     * - Feature/LBA 字段含义改变
     * - Sector Count 变成 Tag 字段
     * - Command码使用 READ/WRITE FDMA EXT (0x60/0x61) */
    uint8_t *fis = cmd_table;
    fis[0] = 0x27;       /* FIS type: Register H2D */
    fis[1] = 0x80;       /* bit 7: C=1 (command), bits [6:0]: PM Port (0) */

    if (use_ncq) {
        /* NCQ FIS 格式 (FPDMA):
         * Features = 0 (NCQ 不使用标准 feature)
         * LBA Low/Mid/High = LBA [7:0], [15:8], [23:16]
         * LBA 4/5 = LBA [31:24], [39:32]
         * Device = LBA [47:40] | bit 6 (LBA mode)
         * Sector Count = FUA|AA|Tag (tag in lower 5 bits) */
        fis[2] = 0;                    /* Features (unused for NCQ) */
        fis[3] = (uint8_t)(lba & 0xFF);           /* LBA Low */
        fis[4] = (uint8_t)((lba >> 8) & 0xFF);     /* LBA Mid */
        fis[5] = (uint8_t)((lba >> 16) & 0xFF);    /* LBA High */
        fis[6] = 0x40;                   /* Device: LBA mode (bit 6) */
        fis[7] = (uint8_t)(count & 0xFF);           /* Sector Count (used as tag for NCQ) */
        fis[8] = (uint8_t)((lba >> 24) & 0xFF);    /* LBA 4 */
        fis[9] = (uint8_t)((lba >> 32) & 0xFF);    /* LBA 5 */
        fis[10] = (uint8_t)((lba >> 40) & 0xFF);   /* LBA 6 */
        fis[11] = 0;                     /* Features (high) */
        fis[12] = (uint8_t)(count >> 8);             /* Sector Count (high) */
        fis[13] = command;              /* Command: READ/WRITE FDMA EXT */
    } else {
        /* 标准 LBA48 FIS 格式:
         * Features/LBA/Sector Count 各有高 8 位扩展寄存器 */
        fis[2] = features;              /* Features */
        fis[3] = (uint8_t)(lba & 0xFF);           /* LBA Low */
        fis[4] = (uint8_t)((lba >> 8) & 0xFF);     /* LBA Mid */
        fis[5] = (uint8_t)((lba >> 16) & 0xFF);    /* LBA High */
        fis[6] = 0x40;                   /* Device: LBA mode */
        fis[7] = (uint8_t)(count & 0xFF);           /* Sector Count */
        fis[8] = (uint8_t)((lba >> 24) & 0xFF);    /* LBA 4 */
        fis[9] = (uint8_t)((lba >> 32) & 0xFF);    /* LBA 5 */
        fis[10] = (uint8_t)((lba >> 40) & 0xFF);   /* LBA 6 */
        fis[11] = 0;                     /* Features (high) */
        fis[12] = (uint8_t)(count >> 8);             /* Sector Count (high) */
        fis[13] = command;              /* Command */
    }

    /* ---- 构造 PRDT (Physical Region Descriptor Table) ----
     * offset 0x80 (128 bytes) in Command Table
     * 每个 PRDT 条目描述一个物理连续的数据区域 */
    uint32_t *prdt = (uint32_t *)(cmd_table + 0x80);
    uint32_t data_len = (uint32_t)count * port->sector_size;

    prdt[0] = (uint32_t)(uintptr_t)buffer;      /* DBA (Data Base Address) low */
    prdt[1] = 0;                                 /* DBA high (32-bit only here) */
    prdt[2] = (data_len - 1) & 0x3FFFFF;        /* DBC (Data Byte Count) - 1 */
    prdt[3] = (1U << 31);                        /* IOC (Interrupt On Completion) */

    /* ---- 构造 Command Header (在 Command List 中) ----
     * DW0: CFL (Command FIS Length) in bits [4:0]
         *       Number of PRDT entries in bits [15:11]
         *       W (Write) in bit 6
         *       BIST/RSP/PMP bits
     * DW1: PRDT Byte Count (total bytes of PRDT)
     * DW2-3: CTBA (Command Table Base Address) */
    cmd_header[0] = (5 << 0) |   /* CFL = 5 (80 bytes / 4 = 20 DWORDs) */
                     (1 << 16) |  /* PRDTL = 1 (one PRDT entry) */
                     (is_write ? (1 << 6) : 0);  /* W bit */
    cmd_header[1] = 16;  /* PRDBC (PRDT Byte Count): 1 entry × 16 bytes */
    cmd_header[2] = (uint32_t)(uintptr_t)cmd_table;  /* CTBA low */
    cmd_header[3] = 0;                                     /* CTBA high */

    /* ---- 触发命令执行 ----
     * 向 PxCI (Command Issue) 写入对应槽位掩码 */
    __asm__ __volatile__("" ::: "memory");  /* 内存屏障 */
    ahci_write_port(port, AHCI_PxCI, (1U << slot));

    /* ---- 等待命令完成 ----
     * 轮询 PxIS (Port Interrupt Status) 的 DHRE (Device to Host Register FIS)
     * 或 DPS (Descriptor Processed) 位
     * 同时检查 TFD (Task File Data) 的 BSY (Busy) 和 ERR (Error) 位 */
    uint32_t timeout = 30000;  /* 30 秒超时 (对于大块读写) */
    while (timeout--) {
        uint32_t pxis = ahci_read_port(port, AHCI_PxIS);
        if (pxis & (1 << 0)) {  /* DHRE or task completed */
            break;
        }

        /* 检查 Task File Data 错误 */
        uint32_t tfd = ahci_read_port(port, AHCI_PxTFD);
        if (tfd & (1 << 0)) {  /* ERR bit set */
            klog_err("sata: Port %u command error, TFD=0x%08X",
                     port->port_num, tfd);
            port->errors++;
            /* 清除错误状态 */
            ahci_write_port(port, AhCI_PxSERR, 0xFFFFFFFF);
            ahci_write_port(port, AHCI_PxIS, 0xFFFFFFFF);
            return -1;
        }

        /* 短暂延时 (~1ms) */
        for (volatile int i = 0; i < 1000; i++)
            __asm__ __volatile__("nop");
    }

    if (timeout == 0) {
        klog_err("sata: Port %u command timeout", port->port_num);
        port->errors++;
        return -1;
    }

    /* 清除中断状态 */
    ahci_write_port(port, AHCI_PxIS, 0xFFFFFFFF);

    /* 更新统计 */
    if (is_write) {
        port->write_ops++;
        port->bytes_written += data_len;
    } else {
        port->read_ops++;
        port->bytes_read += data_len;
    }

    return 0;
}

/*
 * 读取扇区 (LBA48 模式)。
 *
 * 使用 READ DMA EXT 命令 (0x25) 通过 DMA 方式从磁盘读取数据。
 * 支持 48 位 LBA 寻址, 可访问最大 128 PB 地址空间。
 */
int sata_read_sectors(ahci_port_t *port, uint64_t lba, uint16_t count, void *buffer)
{
    if (!port || !port->present || !buffer || count == 0) return -1;

    return sata_issue_command(port, ATA_CMD_READ_DMA_EXT, 0, lba, count,
                              buffer, 0, 0);  /* read=0, no ncq */
}

/*
 * 写入扇区 (LBA48 模式)。
 *
 * 使用 WRITE DMA EXT 命令 (0x35) 通过 DMA 方式写入数据到磁盘。
 */
int sata_write_sectors(ahci_port_t *port, uint64_t lba, uint16_t count,
                       const void *buffer)
{
    if (!port || !port->present || !buffer || count == 0) return -1;

    return sata_issue_command(port, ATA_CMD_WRITE_DMA_EXT, 0, lba, count,
                              (void *)buffer, 1, 0);  /* write=1, no ncq */
}

/*
 * NCQ 异步读操作。
 *
 * 使用 READ FPDMA QUEUED 命令 (0x60) 进行排队读取。
 * tag 参数指定使用的 NCQ 标签 (0 ~ queue_depth-1)。
 * 调用后立即返回, 需要通过 sata_ncq_wait() 等待完成。
 */
int sata_ncq_read(ahci_port_t *port, uint64_t lba, uint16_t count,
                  void *buffer, uint8_t tag)
{
    if (!port || !port->present || !port->ncq_enabled) return -1;
    if (tag >= port->queue_depth) return -1;

    /* 记录 NCQ 标签状态 */
    ncq_tag_t *ncq = &port->ncq_tags[tag];
    ncq->tag = tag;
    ncq->active = 1;
    ncq->lba = lba;
    ncq->sector_count = count;
    ncq->direction = 0;  /* read */
    ncq->buffer = buffer;

    /* 更新 SActive 影子寄存器 */
    port->sactive |= (1U << tag);

    /* 发送 NCQ 读命令 */
    int ret = sata_issue_command(port, ATA_CMD_READ_NCQ, 0, lba, count,
                                  buffer, 0, 1);  /* read, use_ncq=1 */

    if (ret != 0) {
        ncq->active = 0;
        port->sactive &= ~(1U << tag);
    }

    return ret;
}

/*
 * NCQ 异步写操作。
 *
 * 使用 WRITE FPDMA QUEUED 命令 (0x61) 进行排队写入。
 */
int sata_ncq_write(ahci_port_t *port, uint64_t lba, uint16_t count,
                   const void *buffer, uint8_t tag)
{
    if (!port || !port->present || !port->ncq_enabled) return -1;
    if (tag >= port->queue_depth) return -1;

    ncq_tag_t *ncq = &port->ncq_tags[tag];
    ncq->tag = tag;
    ncq->active = 1;
    ncq->lba = lba;
    ncq->sector_count = count;
    ncq->direction = 1;  /* write */
    ncq->buffer = (void *)buffer;

    port->sactive |= (1U << tag);

    int ret = sata_issue_command(port, ATA_CMD_WRITE_NCQ, 0, lba, count,
                                  (void *)buffer, 1, 1);  /* write, use_ncq=1 */

    if (ret != 0) {
        ncq->active = 0;
        port->sactive &= ~(1U << tag);
    }

    return ret;
}

/*
 * 等待 NCQ 命令完成。
 *
 * 轮询 SActive 寄存器检查指定标签是否已完成。
 * timeout_ms: 超时时间 (毫秒)
 */
int sata_ncq_wait(ahci_port_t *port, uint8_t tag, uint32_t timeout_ms)
{
    if (!port || tag >= port->queue_depth) return -1;
    if (!port->ncq_tags[tag].active) return 0;  /* 已经完成或未激活 */

    while (timeout_ms--) {
        /* 读取 SActive 寄存器, 检查对应 tag 是否仍活跃 */
        uint32_t sact = ahci_read_port(port, AhCI_PxSACT);
        if (!(sact & (1U << tag))) {
            /* 该 tag 已完成 */
            port->ncq_tags[tag].active = 0;
            port->sactive &= ~(1U << tag);
            return 0;
        }

        /* 检查错误 */
        uint32_t tfd = ahci_read_port(port, AHCI_PxTFD);
        if (tfd & (1 << 0)) {
            klog_err("sata: NCQ tag %u error, TFD=0x%08X", tag, tfd);
            port->ncq_tags[tag].active = 0;
            port->sactive &= ~(1U << tag);
            port->errors++;
            return -1;
        }

        for (volatile int i = 0; i < 1000; i++)
            __asm__ __volatile__("nop");
    }

    klog_err("sata: NCQ tag %u timeout", tag);
    port->ncq_tags[tag].active = 0;
    port->sactive &= ~(1U << tag);
    return -1;
}

/*
 * ATA IDENTIFY DEVICE 命令。
 *
 * 读取设备的完整标识信息 (512 字节),
 * 包括型号、序列号、固件版本、支持的特性集、容量等。
 */
int sata_identify(ahci_port_t *port)
{
    if (!port || !port->present) return -1;

    /* IDENTIFY DEVICE 命令使用 PIO 方式, 数据大小固定为 512 字节 (256 words) */
    static ata_identify_t id_buffer;

    int ret = sata_issue_command(port, ATA_CMD_IDENTIFY, 0, 0, 0,
                                  &id_buffer, 0, 0);
    if (ret != 0) {
        klog_err("sata: Port %u IDENTIFY failed", port->port_num);
        return -1;
    }

    /* 复制 Identify 数据到端口上下文 */
    memcpy(&port->identify, &id_buffer, sizeof(ata_identify_t));

    /* 解析关键信息 */
    parse_identify_data(port);

    return 0;
}

/*
 * 启用 SMART 监控功能。
 *
 * 发送 SMART ENABLE OPERATIONS 子命令。
 * 启用后可以定期读取 SMART 属性来预测磁盘故障。
 */
int sata_smart_enable(ahci_port_t *port)
{
    if (!port || !port->present || !port->smart_supported) return -1;

    /* SMART 命令格式:
     * Command = 0xB0 (SMART)
     * Features = 0xD8 (SMART Enable Operations)
     * LBA/Count = 0 (不使用) */
    int ret = sata_issue_command(port, ATA_CMD_SMART, ATA_SMART_ENABLE,
                                  0, 0, (void *)0, 0, 0);
    if (ret == 0) {
        port->smart_enabled = 1;
        klog_info("sata: Port %u SMART monitoring enabled", port->port_num);
    }
    return ret;
}

/*
 * 读取 SMART DATA LOG。
 *
 * 返回 30 个 SMART 属性的当前值和原始值。
 * 这些属性用于判断磁盘健康状态。
 */
int sata_smart_read_data(ahci_port_t *port, smart_data_t *out)
{
    if (!port || !port->present || !port->smart_enabled) return -1;
    if (!out) return -1;

    /* SMART READ DATA 返回 512 字节的 SMART 数据结构:
     * Offset 0: Version (2 bytes)
     * Offset 2: SMART Attributes (30 entries × 12 bytes each = 360 bytes)
     * Offset 362: Offline status (1 byte)
     * Offset 363: Self-test status (1 byte)
     * Offset 364: Total time (2 bytes) */
    static uint8_t smart_raw[512];
    memset(smart_raw, 0, sizeof(smart_raw));

    int ret = sata_issue_command(port, ATA_CMD_SMART, ATA_SMART_READ_DATA,
                                  0x00C24F00ULL, 0x01,  /* LBA 特殊值 */
                                  smart_raw, 0, 0);
    if (ret != 0) return -1;

    /* 解析 SMART 数据结构 */
    memset(out, 0, sizeof(smart_data_t));
    out->version = *(uint16_t *)&smart_raw[0];
    out->offline_status = smart_raw[362];
    out->self_test_status = smart_raw[363];
    out->total_time = *(uint16_t *)&smart_raw[364];

    /* 解析 30 个 SMART 属性 (每项 12 字节):
     * [0]: Attribute ID
     * [1-2]: Flags (threshold/pre-failure/online/etc)
     * [3]: Current Normalized Value
     * [4]: Worst Normalized Value
     * [5-8]: Raw Value (vendor specific encoding, 6 bytes packed into 8) */
    for (int i = 0; i < 30; i++) {
        uint8_t *attr = &smart_raw[2 + i * 12];
        out->attributes[i].id            = attr[0];
        out->attributes[i].flags         = *(uint16_t *)(attr + 1);
        out->attributes[i].current_value = attr[3];
        out->attributes[i].worst_value   = attr[4];
        /* Raw value: 6 字节, 通常编码为 48-bit 整数或自定义格式 */
        out->attributes[i].raw_value =
            ((uint32_t)attr[8] << 24) |
            ((uint32_t)attr[7] << 16) |
            ((uint32_t)attr[6] << 8)  |
            (attr[5]);
    }

    return 0;
}

/*
 * 读取 SMART 阈值表。
 *
 * 每个属性都有一个阈值, 当标准化值低于阈值时表示可能故障。
 */
int sata_smart_read_thresholds(ahci_port_t *port, smart_attribute_t attrs[30])
{
    if (!port || !port->present || !port->smart_enabled || !attrs) return -1;

    /* SMART READ THRESHOLDS 返回类似结构的阈值数据 */
    static uint8_t thresh_raw[512];
    memset(thresh_raw, 0, sizeof(thresh_raw));

    int ret = sata_issue_command(port, ATA_CMD_SMART, ATA_SMART_READ_THRESHOLDS,
                                  0x00C24FD1ULL, 0x01,
                                  thresh_raw, 0, 0);
    if (ret != 0) return -1;

    /* 解析阈值 (每项 12 字节, 但只有 ID+Threshold 有效) */
    for (int i = 0; i < 30; i++) {
        uint8_t *t = &thresh_raw[2 + i * 12];
        attrs[i].id = t[0];
        attrs[i].threshold = t[3];  /* Threshold value */
    }

    return 0;
}

/*
 * 执行 SMART Short Self-Test。
 *
 * 快速自检通常耗时不超过 2 分钟。
 * 结果可以通过 SMART READ DATA 的 self_test_status 字段查询。
 */
int sata_smart_self_test_short(ahci_port_t *port)
{
    if (!port || !port->present || !port->smart_enabled) return -1;

    /* SMART EXECUTE OFF-LINE IMMEDIATE:
     * Subcommand = 0x00 (Short test) */
    int ret = sata_issue_command(port, ATA_CMD_SMART, 0xD0,
                                  0x00, 0x01,  /* Short test, sub-captive */
                                  (void *)0, 0, 0);

    if (ret == 0) {
        klog_info("sata: Port %u SMART short self-test started", port->port_num);
    }
    return ret;
}

/*
 * 执行 SMART Extended Self-Test (Long)。
 *
 * 全面扫描整个盘面, 可能耗时数十分钟到数小时。
 */
int sata_smart_self_test_long(ahci_port_t *port)
{
    if (!port || !port->present || !port->smart_enabled) return -1;

    /* Subcommand = 0x02 (Extended/Long test) */
    int ret = sata_issue_command(port, ATA_CMD_SMART, 0xD0,
                                  0x02, 0x01,
                                  (void *)0, 0, 0);

    if (ret == 0) {
        klog_info("sata: Port %u SMART extended self-test started", port->port_num);
    }
    return ret;
}

/*
 * FLUSH CACHE 命令。
 *
 * 将磁盘写缓存中的所有脏数据刷写到介质上。
 * 对保证文件系统一致性至关重要。
 */
int sata_flush(ahci_port_t *port)
{
    if (!port || !port->present) return -1;

    /* FLUSH CACHE EXTENDED (0xEA) 用于 LBA48 兼容 */
    int ret = sata_issue_command(port, ATA_CMD_FLUSH_CACHE_EXT, 0, 0, 0,
                                  (void *)0, 0, 0);

    if (ret == 0) {
        klog_debug("sata: Port %u cache flushed", port->port_num);
    }
    return ret;
}

/*
 * 热插拔检测扫描。
 *
 * 遍历所有端口, 比较 SStatus.DET 与上次记录的状态:
 * - DET 从 0/1 变为 3: 设备插入 → 调用 insert_cb
 * - DET 从 3 变为 0/1: 设备移除 → 调用 remove_cb
 *
 * 返回检测到的变化事件数。
 */
int sata_check_hotplug(ahci_controller_t *ctrl)
{
    if (!ctrl) return -1;

    int events = 0;

    for (uint32_t p = 0; p < ctrl->nports && p < 32; p++) {
        ahci_port_t *port = &ctrl->ports[p];
        if (!(ctrl->ports_implemented & (1U << p))) continue;

        uint32_t new_sstatus = ahci_read_port(port, AhCI_PxSSTS);
        uint8_t new_det = (uint8_t)(new_sstatus & 0x0F);
        uint8_t old_det = (uint8_t)(port->sstatus & 0x0F);

        if (new_det >= 2 && old_det < 2) {
            /* 设备插入事件 */
            events++;
            port->sstatus = new_sstatus;
            port->present = 1;
            klog_info("sata: Hot-plug INSERT on port %u", p);

            if (port->on_hotplug_insert) {
                port->on_hotplug_insert(port);
            }

            /* 尝试重新初始化端口 */
            sata_scan_ports(ctrl);
        } else if (new_det < 2 && old_det >= 2) {
            /* 设备移除事件 */
            events++;
            port->sstatus = new_sstatus;
            port->present = 0;
            port->active = 0;
            klog_info("sata: Hot-plug REMOVE on port %u", p);

            if (port->on_hotplug_remove) {
                port->on_hotplug_remove(port);
            }
        }
    }

    return events;
}

/*
 * 注册热插拔回调函数。
 *
 * insert_cb: 设备插入时调用
 * remove_cb: 设备移除时调用
 */
void sata_set_hotplug_callback(ahci_port_t *port,
                                void (*insert_cb)(ahci_port_t*),
                                void (*remove_cb)(ahci_port_t*))
{
    if (!port) return;
    port->on_hotplug_insert = insert_cb;
    port->on_hotplug_remove = remove_cb;
    port->hotplug_capable = 1;
}

/*
 * 设置链路电源管理级别。
 *
 * level:
 *   0 = 无链路电源管理 (始终全速运行)
 *   1 = Partial (部分节能, 恢复快 ~10us)
 *   2 = Slumber (深度睡眠, 恢复慢 ~10ms)
 */
int sata_set_link_pm(ahci_port_t *port, uint8_t level)
{
    if (!port || !port->active || level > 2) return -1;

    /* 通过 SControl 寄存器的 IPM 字段设置
     * SControl bits [11:8]: Interface Power Management State
     * 0 = No PM, 1 = Partial, 2 = Slumber */
    uint32_t sctl = ahci_read_port(port, AhCI_PxSCTL);
    sctl = (sctl & ~(0x0F << 8)) | ((uint32_t)level << 8);
    ahci_write_port(port, AhCI_PxSCTL, sctl);

    klog_info("sata: Port %u link PM set to %s",
              port->port_num,
              level == 0 ? "None" : level == 1 ? "Partial" : "Slumber");
    return 0;
}

/*
 * 控制 Device Sleep (DevSleep) 功能。
 *
 * DevSleep 是 SATA 3.2 引入的超低功耗模式,
 * 功耗低于 Slumber, 但恢复时间较长。
 */
int sata_set_dev_sleep(ahci_port_t *port, int enable)
{
    if (!port || !port->active) return -1;

    /* DEVSLP 寄存器控制 */
    uint32_t devslp = ahci_read_port(port, AHCI_PxDEVSLP);
    if (enable) {
        devslp |= (1 << 1);  /* DSE (DevSleep Enable) */
    } else {
        devslp &= ~(1 << 1);
    }
    ahci_write_port(port, AHCI_PxDEVSLP, devslp);

    klog_info("sata: Port %u DevSleep %s", port->port_num,
              enable ? "enabled" : "disabled");
    return 0;
}

/*
 * 打印端口详细信息 (调试用途)。
 */
void sata_print_port_info(ahci_port_t *port)
{
    if (!port) return;

    klog_info("=== SATA Port %u Info ===", port->port_num);
    klog_info("  Present:    %s", port->present ? "Yes" : "No");
    klog_info("  Active:     %s", port->active ? "Yes" : "No");

    if (port->present) {
        klog_info("  Model:      %s", port->model);
        klog_info("  Serial:     %s", port->serial);
        klog_info("  Firmware:   %s", port->firmware);
        klog_info("  Link Speed: Gen%u", port->link_speed);
        klog_info("  Sector Size: %u bytes", port->sector_size);
        klog_info("  Max LBA48:  %llu sectors (%llu GB)",
                  (unsigned long long)port->max_lba48,
                  (unsigned long long)(port->max_lba48 * port->sector_size /
                                       (1000ULL * 1000 * 1000)));
        klog_info("  NCQ:        %s (depth=%u)",
                  port->ncq_enabled ? "Enabled" : "Disabled",
                  port->queue_depth);
        klog_info("  SMART:      %s%s",
                  port->smart_supported ? "Supported" : "N/A",
                  port->smart_enabled ? ", Enabled" : "");
        klog_info("  Read Ops:    %llu", (unsigned long long)port->read_ops);
        klog_info("  Write Ops:   %llu", (unsigned long long)port->write_ops);
        klog_info("  Bytes Read:  %llu MB",
                  (unsigned long long)(port->bytes_read / (1024 * 1024)));
        klog_info("  Bytes Write: %llu MB",
                  (unsigned long long)(port->bytes_written / (1024 * 1024)));
        klog_info("  Errors:     %u", port->errors);
    }
}

/*
 * 获取控制器级别的统计汇总。
 */
void sata_get_stats(ahci_controller_t *ctrl, sata_stats_t *out)
{
    if (!ctrl || !out) return;

    memset(out, 0, sizeof(sata_stats_t));

    for (uint32_t i = 0; i < ctrl->nports && i < 32; i++) {
        ahci_port_t *port = &ctrl->ports[i];
        out->total_ports++;
        if (port->active) out->active_ports++;
        out->total_reads += port->read_ops;
        out->total_writes += port->write_ops;
        out->total_bytes_read += port->bytes_read;
        out->total_bytes_written += port->bytes_written;
        out->errors += port->errors;
    }
}
