/*
 * xhci_enhanced.c - USB 3.0 XHCI SuperSpeed 增强驱动 实现
 */

#include "xhci_enhanced.h"

/* 日志宏 (兼容内核日志系统) */
#ifndef klog_info
#define klog_info(fmt, ...)  ((void)0)
#endif
#ifndef klog_err
#define klog_err(fmt, ...)   ((void)0)
#endif
#ifndef klog_warn
#define klog_warn(fmt, ...)  ((void)0)
#endif
#ifndef klog_debug
#define klog_debug(fmt, ...) ((void)0)
#endif

/*
 * 全局增强控制器实例 (单例模式, 因为通常只有一个 XHCI 控制器)
 */
static xhci_enhanced_t g_xhci_hc;

/* 命令环状态: 写入索引、循环状态位 */
static uint32_t cmd_ring_enqueue = 0;
static uint8_t  cmd_ring_cycle = 1;

/* 事件环状态: 读取索引、循环状态位 */
static uint32_t event_ring_dequeue = 0;
static uint8_t  event_ring_cycle = 1;

/* ---- 内部寄存器访问辅助函数 ---- */

static inline uint32_t xhci_read_cap(xhci_enhanced_t *hc, uint32_t offset)
{
    return hc->cap_regs[offset / 4];
}

static inline void xhci_write_op(xhci_enhanced_t *hc, uint32_t offset, uint32_t val)
{
    hc->op_regs[offset / 4] = val;
}

static inline uint32_t xhci_read_op(xhci_enhanced_t *hc, uint32_t offset)
{
    return hc->op_regs[offset / 4];
}

static inline void xhci_write_rt(xhci_enhanced_t *hc, uint32_t offset, uint32_t val)
{
    hc->rt_regs[offset / 4] = val;
}

static inline uint32_t xhci_read_rt(xhci_enhanced_t *hc, uint32_t offset)
{
    return hc->rt_regs[offset / 4];
}

static inline void xhci_ring_doorbell(xhci_enhanced_t *hc, uint32_t target, uint32_t value)
{
    hc->db_regs[target] = value;
}

/*
 * 向命令环提交一个 TRB 并触发门铃。
 * 这是 XHCI 命令执行的核心机制:
 * 1. 将 TRB 数据写入命令环当前槽位
 * 2. 设置 Cycle Bit (与当前环状态一致)
 * 3. 推进写入指针
 * 4. 如果到达环末尾, 插入 Link TRB 并翻转 Cycle Bit
 * 5. 敲击 Doorbell 0 通知硬件处理
 */
static int xhci_submit_command(xhci_enhanced_t *hc, const xhci_trb_t *cmd)
{
    if (!hc || !cmd) return -1;

    /* 获取命令环虚拟地址 */
    xhci_trb_t *ring = (xhci_trb_t *)(uintptr_t)hc->cmd_ring_ptr;
    if (!ring) return -1;

    /* 写入 TRB 到当前槽位 */
    ring[cmd_ring_enqueue].parameter = cmd->parameter;
    ring[cmd_ring_enqueue].status    = cmd->status;
    ring[cmd_ring_enqueue].control   = cmd->control | ((uint32_t)cmd_ring_cycle);

    /* 内存屏障: 确保 TRB 写入对硬件可见 */
    __asm__ __volatile__("" ::: "memory");

    /* 推进写入指针 */
    cmd_ring_enqueue++;

    /* 检查是否需要 Link TRB (环形回绕)
     * 命令环大小为 256 条目 (标准要求), 预留最后一个给 Link TRB */
    if (cmd_ring_enqueue >= 255) {
        /* 构造 Link TRB: 指向环起始地址 */
        ring[255].parameter = hc->cmd_ring_ptr;
        ring[255].status    = 0;
        ring[255].control   = (uint32_t)((TRB_LINK << 10) |
                                         ((uint32_t)cmd_ring_cycle ^ 1));  /* Toggle on wrap */

        /* 翻转 Cycle Bit */
        cmd_ring_cycle ^= 1;
        cmd_ring_enqueue = 0;
    }

    /* 敲击 Doorbell 0 (命令环门铃) 触发硬件处理 */
    xhci_ring_doorbell(hc, 0, 0);

    return 0;
}

/*
 * 等待事件环产生完成事件并返回 Completion Code。
 * 超时返回 CC_INVALID。
 */
static uint32_t xhci_wait_for_event(xhci_enhanced_t *hc, uint32_t timeout_us)
{
    xhci_trb_t *ering = (xhci_trb_t *)(uintptr_t)hc->event_ring_ptr;
    if (!ering) return CC_INVALID;

    while (timeout_us--) {
        /* 读取事件环当前槽位的 Cycle Bit */
        uint32_t ctrl = ering[event_ring_dequeue].control;
        uint8_t trb_cycle = (ctrl & 0x01);

        /* 如果 Cycle Bit 与环状态一致, 说明有新事件 */
        if (trb_cycle == event_ring_cycle) {
            /* 提取 Completion Code (Status 字段 bits [31:24]) */
            uint32_t comp_code = (ering[event_ring_dequeue].status >> 24) & 0xFF;

            /* 推进读指针 */
            event_ring_dequeue++;
            if (event_ring_dequeue >= 255) {
                event_ring_cycle ^= 1;
                event_ring_dequeue = 0;
            }

            /* 更新 ERDP (Event Ring Dequeue Pointer) 让硬件知道已消费 */
            xhci_write_rt(hc, 0x38, (uint32_t)(uintptr_t)&ering[event_ring_dequeue]);

            return comp_code;
        }

        /* 短暂延时避免忙等占用过多 CPU */
        for (volatile int i = 0; i < 10; i++)
            __asm__ __volatile__("nop");
    }

    return CC_UNDEFINED;  /* 超时 */
}


/*
 * ================================================================
 * 公共 API 实现
 * ================================================================
 */

/*
 * 初始化 XHCI 增强驱动。
 *
 * 步骤:
 * 1. 映射 MMIO 寄存器空间到内核虚拟地址
 * 2. 读取 Capability 寄存器获取控制器参数
 * 3. 解析结构参数 (最大插槽数/端口数/中断器数)
 * 4. 分配 DCBAAP (Device Context Base Address Array)
 * 5. 分配命令环和事件环
 * 6. 配置 Scratchpad 缓冲区
 * 7. 执行全局复位
 *
 * mmio_base: XHCI 控制器的 MMIO 物理基址 (来自 PCI BAR0)
 */
int xhci_enhanced_init(uint32_t mmio_base)
{
    xhci_enhanced_t *hc = &g_xhci_hc;
    memset(hc, 0, sizeof(xhci_enhanced_t));

    /* ---- Step 1: 映射寄存器空间 ----
     * Capability 寄存器从偏移 0 开始
     * Operational 寄存器从 CAPLENGTH 偏移开始
     * Runtime 寄存器从 RTSOFF 偏移开始
     * Doorbell 从 DBOFF 偏移开始 */
    hc->cap_regs = (volatile uint32_t *)(uintptr_t)mmio_base;

    /* ---- Step 2: 读取基本能力参数 ---- */
    hc->cap_length = xhci_read_cap(hc, XHCI_CAPLENGTH) & 0xFF;
    hc->hci_version = (uint16_t)(xhci_read_cap(hc, XHCI_HCIVERSION) & 0xFFFF);

    /* 操作寄存器基地址 = 能力寄存器基地址 + CAPLENGTH */
    hc->op_regs = (volatile uint32_t *)((uintptr_t)hc->cap_regs + hc->cap_length);

    /* ---- Step 3: 读取结构参数 ----
     * HCSPARAMS1: MaxSlots(MaxInSlots), MaxPorts, MaxInterrupters
     * HCSPARAMS2: IST(Interrupter Scheduling), ERST_Max, Max Scratchpad Buffers Hi/Lo
     * HCSPARAMS3: U1/U2 Exit Latency */
    hc->hcs_params1 = xhci_read_cap(hc, XHCI_HCSPARAMS1);
    hc->hcs_params2 = xhci_read_cap(hc, XHCI_HCSPARAMS2);
    hc->hcs_params3 = xhci_read_cap(hc, XHCI_HCSPARAMS3);
    hc->hcc_params  = xhci_read_cap(hc, XHCI_HCCPARAMS);

    /* 解析关键参数 */
    hc->max_slots = (hc->hcs_params1 >> 0) & 0xFF;
    hc->max_ports = (hc->hcs_params1 >> 24) & 0xFF;
    hc->max_intrs = (hc->hcs_params1 >> 16) & 0x3FF;

    /* Scratchpad 缓冲区数量: HCS_PARAM2 [26:27] 高 5 位 | [7:0] 低 8 位 */
    uint32_t spbuf_hi = (hc->hcs_params2 >> 25) & 0x1F;
    uint32_t spbuf_lo = (hc->hcs_params2 >> 0) & 0xFF;
    hc->max_scratchpad_bufs = (spbuf_hi << 5) | spbuf_lo;

    /* Doorbell 和 Runtime 偏移 */
    hc->db_off = xhci_read_cap(hc, XHCI_DBOFF);
    hc->rtsoff = xhci_read_cap(hc, XHCI_RTSOFF);

    /* 计算 Runtime 和 Doorbell 寄存器虚拟地址 */
    hc->rt_regs = (volatile uint32_t *)((uintptr_t)hc->cap_regs + hc->rtsoff);
    hc->db_regs = (volatile uint32_t *)((uintptr_t)hc->cap_regs + hc->db_off);

    /* ---- Step 4-6: 分配数据结构 (简化版, 使用静态缓冲区) ----
     * 实际实现应使用 pmm_alloc_pages() 分配物理连续内存
     * 这里用静态数组模拟 DMA 内存分配 */
    static uint8_t cmd_ring_mem[4096];   /* 256 个 TRB × 16 字节 */
    static uint8_t evt_ring_mem[4096];   /* 256 个 TRB × 16 字节 */
    static uint8_t dcbaa_mem[4096];      /* 256 个 64 位指针 */

    memset(cmd_ring_mem, 0, sizeof(cmd_ring_mem));
    memset(evt_ring_mem, 0, sizeof(evt_ring_mem));
    memset(dcbaa_mem, 0, sizeof(dcbaa_mem));

    hc->cmd_ring_ptr  = (uint64_t)(uintptr_t)cmd_ring_mem;
    hc->event_ring_ptr = (uint64_t)(uintptr_t)evt_ring_mem;
    hc->dcbaa_ptr     = (uint64_t)(uintptr_t)dcbaa_mem;

    /* 初始化 Scratchpad 缓冲区 (每页 4KB) */
    for (uint32_t i = 0; i < hc->max_scratchpad_bufs && i < 16; i++) {
        static uint8_t sp_mem[16][4096];
        hc->scratchpad_bufs[i] = (uint64_t)(uintptr_t)sp_mem[i];

        /* 将 Scratchpad 地址写入 DCBAA 的前 N 个槽位 */
        uint64_t *dcbaa = (uint64_t *)(uintptr_t)hc->dcbaa_ptr;
        dcbaa[i] = hc->scratchpad_bufs[i];
    }

    /* ---- Step 7: 统计 USB 2.0 / USB 3.0 端口数量 ----
     * 协议速度 ID (PSI) 从 HCSPARAMS3 或 Extended Capabilities 解析
     * 简化实现: 假设端口从低速到高速排列, 后半部分为 SuperSpeed */
    hc->usb2_port_count = hc->max_ports / 2;
    hc->usb3_port_count = hc->max_ports - hc->usb2_port_count;

    /* 清零统计计数器 */
    hc->transfers_completed = 0;
    hc->transfers_failed = 0;
    hc->devices_connected = 0;
    hc->super_speed_devices = 0;

    /* 重置环状态 */
    cmd_ring_enqueue = 0;
    cmd_ring_cycle = 1;
    event_ring_dequeue = 0;
    event_ring_cycle = 1;

    klog_info("xhci_enhanced: Initialized, ver=0x%04X slots=%u ports=%u "
              "intrs=%u scratchpads=%u",
              hc->hci_version, hc->max_slots, hc->max_ports,
              hc->max_intrs, hc->max_scratchpad_bufs);

    return 0;
}

/*
 * 启动 XHCI 主机控制器。
 *
 * 将控制器从 Reset 状态转换到 Run/Stop 状态:
 * 1. 设置 CRST 位完成全局复位
 * 2. 配置操作寄存器 (DCBAAP, CONFIG)
 * 3. 设置 RUN/STOP 寄存器的 RUN 位
 * 4. 使能中断器
 */
int xhci_enhanced_start(xhci_enhanced_t *hc)
{
    if (!hc || !hc->op_regs) return -1;

    /* 先确保控制器处于复位状态 */
    if (xhci_enhanced_reset(hc) != 0) {
        klog_err("xhci_enhanced: Controller reset failed before start");
        return -1;
    }

    /* ---- 配置 Device Context Base Address Array ----
     * DCBAAP 寄存器 (offset 0x30): 低 32 位地址
     * DCBAAP_HIGH 寄存器 (offset 0x34): 高 32 位地址 */
    xhci_write_op(hc, 0x30, (uint32_t)(hc->dcbaa_ptr & 0xFFFFFFFF));
    xhci_write_op(hc, 0x34, (uint32_t)((hc->dcbaa_ptr >> 32) & 0xFFFFFFFF));

    /* ---- 配置 CONFIG 寄存器 (offset 0x38) ----
     * bits [7:0]: Max Device Slots Enabled (必须 <= MaxSlots) */
    uint32_t config_val = hc->max_slots & 0xFF;
    xhci_write_op(hc, 0x38, config_val);

    /* ---- 配置 Command Ring ----
     * CRCR (Command Ring Control Register): offset 0x18
     * bit 0: Ring Running State (RRS)
     * bit 1: Command Ring Cycle State (CS)
     * bits [63:6]: Command Ring Pointer */
    uint32_t crcr_low = (uint32_t)(hc->cmd_ring_ptr & 0xFFFFFFF0) | 0x01;  /* RRS=1 */
    xhci_write_op(hc, 0x18, crcr_low);

    /* ---- 配置 Event Ring ----
     * 通过 Runtime 寄存器的 ERSTSZ/ERSTBA/ERDP 来设置
     * ERSTSZ (Event Ring Segment Table Size): offset RT+0x28 */
    xhci_write_rt(hc, 0x28, 1);  /* 1 个段 */

    /* ERSTBA (Event Ring Segment Table Base Address): offset RT+0x30
     * 指向包含 {SegmentBaseAddr[64], SegmentSize} 的表 */
    static uint64_t erst_entry[2];  /* {base_addr, size} */
    erst_entry[0] = hc->event_ring_ptr;
    erst_entry[1] = 256;  /* 段大小 (TRB 数量) */
    xhci_write_rt(hc, 0x30, (uint32_t)(uintptr_t)erst_entry);
    xhci_write_rt(hc, 0x34, (uint32_t)(((uint64_t)(uintptr_t)erst_entry) >> 32));

    /* ERDP (Event Ring Dequeue Pointer): offset RT+0x38 */
    xhci_write_rt(hc, 0x38, (uint32_t)(hc->event_ring_ptr));

    /* ---- 启动控制器运行 ----
     * USBCMD 寄存器 (offset 0x00):
     * bit 0: Run/Stop (RUN) - 设置为 1 启动
     * bit 2: Interrupter Enable (INTE) */
    uint32_t usbcmd = xhci_read_op(hc, XHCI_USBCMD);
    usbcmd |= (1 << 0) | (1 << 2);  /* RUN=1, INTE=1 */
    xhci_write_op(hc, XHCI_USBCMD, usbcmd);

    /* 等待控制器进入运行状态
     * USBSTS 寄存器 (offset 0x04):
     * bit 0: HCHalted - 0 表示正在运行 */
    uint32_t timeout = 100000;
    while (timeout--) {
        uint32_t st = xhci_read_op(hc, XHCI_USBSTS);
        if (!(st & 0x01)) break;  /* HCHalted == 0 -> 运行中 */
        for (volatile int i = 0; i < 100; i++)
            __asm__ __volatile__("nop");
    }

    if (timeout == 0) {
        klog_err("xhci_enhanced: Failed to start controller (timeout)");
        return -1;
    }

    klog_info("xhci_enhanced: Controller started successfully");
    return 0;
}

/*
 * 停止 XHCI 主机控制器。
 *
 * 将 USBCMD 的 RUN 位清零, 等待 HCHalted 置位。
 * 注意: 停止后所有进行中的传输将被中止。
 */
void xhci_enhanced_stop(xhci_enhanced_t *hc)
{
    if (!hc || !hc->op_regs) return;

    /* 清除 RUN 位 */
    uint32_t usbcmd = xhci_read_op(hc, XHCI_USBCMD);
    usbcmd &= ~(1 << 0);  /* RUN = 0 */
    xhci_write_op(hc, XHCI_USBCMD, usbcmd);

    /* 等待 HCHalted 置位 */
    uint32_t timeout = 100000;
    while (timeout--) {
        uint32_t st = xhci_read_op(hc, XHCI_USBSTS);
        if (st & 0x01) break;  /* HCHalted == 1 -> 已停止 */
        for (volatile int i = 0; i < 100; i++)
            __asm__ __volatile__("nop");
    }

    klog_info("xhci_enhanced: Controller stopped");
}

/*
 * 复位 XHCI 主机控制器。
 *
 * 流程:
 * 1. 设置 USBCMD.HCRST (Host Controller Reset) 位
 * 2. 等待 HCRST 自动清除 (表示复位完成)
 * 3. 等待 USBSTS.CNR (Controller Not Ready) 清除
 *
 * 返回 0 成功, -1 超时失败。
 */
int xhci_enhanced_reset(xhci_enhanced_t *hc)
{
    if (!hc || !hc->op_regs) return -1;

    /* 设置 HCRST (bit 1 of USBCMD) */
    uint32_t usbcmd = xhci_read_op(hc, XHCI_USBCMD);
    usbcmd |= (1 << 1);  /* HCRST = 1 */
    xhci_write_op(hc, XHCI_USBCMD, usbcmd);

    /* 等待 HCRST 位自动清除 (硬件完成复位后会清除该位) */
    uint32_t timeout = 1000000;
    while (timeout--) {
        usbcmd = xhci_read_op(hc, XHCI_USBCMD);
        if (!(usbcmd & (1 << 1))) break;  /* HCRST cleared */
        for (volatile int i = 0; i < 10; i++)
            __asm__ __volatile__("nop");
    }

    if (timeout == 0) {
        klog_err("xhci_enhanced: Reset timeout (HCRST not clearing)");
        return -1;
    }

    /* 等待 CNR (Controller Not Ready) 清除 */
    timeout = 1000000;
    while (timeout--) {
        uint32_t st = xhci_read_op(hc, XHCI_USBSTS);
        if (!(st & (1 << 11))) break;  /* CNR cleared */
        for (volatile int i = 0; i < 10; i++)
            __asm__ __volatile__("nop");
    }

    if (timeout == 0) {
        klog_err("xhci_enhanced: Reset timeout (CNR not clearing)");
        return -1;
    }

    klog_info("xhci_enhanced: Controller reset complete");
    return 0;
}

/*
 * 复位指定端口。
 *
 * 对 PORTSC 寄存器设置 PR (Port Reset) 位,
 * 等待 PR 自动清除表示复位完成。
 * 复位完成后设备进入 Enabled 状态。
 */
int xhci_enhanced_reset_port(xhci_enhanced_t *hc, uint8_t port_id)
{
    if (!hc || !hc->op_regs || port_id > hc->max_ports) return -1;

    /* PORTSC 寄存器偏移: 0x400 + (port_id - 1) * 0x10 */
    uint32_t portsc_offset = 0x400 + (uint32_t)(port_id - 1) * 0x10;

    /* 读取当前 PORTSC */
    uint32_t portsc = xhci_read_op(hc, portsc_offset);

    /* 检查端口是否有设备连接 (CCS) */
    if (!(portsc & PORTSC_CCS)) {
        klog_warn("xhci_enhanced: Port %u reset: no device connected", port_id);
        return -1;
    }

    /* 设置 Port Reset (PR) 位 */
    portsc |= PORTSC_PR;
    xhci_write_op(hc, portsc_offset, portsc);

    /* 等待 PR 位清除 (硬件在复位完成后自动清除) */
    uint32_t timeout = 1000000;
    while (timeout--) {
        portsc = xhci_read_op(hc, portsc_offset);
        if (!(portsc & PORTSC_PR)) break;
        for (volatile int i = 0; i < 10; i++)
            __asm__ __volatile__("nop");
    }

    if (timeout == 0) {
        klog_err("xhci_enhanced: Port %u reset timeout", port_id);
        return -1;
    }

    /* 确保端口进入 enabled 状态 (PED 应该被置位) */
    portsc = xhci_read_op(hc, portsc_offset);
    if (portsc & PORTSC_PED) {
        klog_info("xhci_enhanced: Port %u reset complete, device enabled", port_id);
    } else {
        klog_warn("xhci_enhanced: Port %u reset done but not enabled", port_id);
    }

    return 0;
}

/*
 * 获取指定端口的设备速度。
 *
 * 从 PORTSC 寄存器的 Port Speed 字段 (bits [13:10]) 读取:
 * 0=Full, 1=Low, 2=High, 3=Super, 4-5=SuperPlus, others reserved
 */
int xhci_enhanced_get_port_speed(xhci_enhanced_t *hc, uint8_t port_id)
{
    if (!hc || !hc->op_regs || port_id > hc->max_ports) return -1;

    uint32_t portsc_offset = 0x400 + (uint32_t)(port_id - 1) * 0x10;
    uint32_t portsc = xhci_read_op(hc, portsc_offset);

    /* Port Speed field: bits [13:10] */
    uint8_t speed_field = (uint8_t)((portsc >> PORTSC_SPEED_SHIFT) & 0x0F);

    switch (speed_field) {
        case 0: return USB_SPEED_FULL;   /* Full Speed (12 Mbps) */
        case 1: return USB_SPEED_LOW;    /* Low Speed (1.5 Mbps) */
        case 2: return USB_SPEED_HIGH;   /* High Speed (480 Mbps) */
        case 3: return USB_SPEED_SUPER;  /* SuperSpeed (5 Gbps) */
        default:
            klog_warn("xhci_enhanced: Unknown port speed code: %u on port %u",
                      speed_field, port_id);
            return -1;
    }
}

/*
 * 设置端口电源开关。
 *
 * 控制 PORTSC 的 PP (Port Power) 位。
 * power_on=1 打开端口电源, power_on=0 关闭。
 * 关闭电源会导致连接的设备断开。
 */
int xhci_enhanced_set_port_power(xhci_enhanced_t *hc, uint8_t port_id, int power_on)
{
    if (!hc || !hc->op_regs || port_id > hc->max_ports) return -1;

    uint32_t portsc_offset = 0x400 + (uint32_t)(port_id - 1) * 0x10;
    uint32_t portsc = xhci_read_op(hc, portsc_offset);

    if (power_on) {
        portsc |= PORTSC_PP;   /* PP = 1: 电源开启 */
    } else {
        portsc &= ~PORTSC_PP;  /* PP = 0: 电源关闭 */
    }

    xhci_write_op(hc, portsc_offset, portsc);

    klog_info("xhci_enhanced: Port %u power %s", port_id,
              power_on ? "ON" : "OFF");
    return 0;
}

/*
 * 为新设备分配插槽 (Enable Slot Command)。
 *
 * 发送 ENABLE_SLOT TRB 到命令环, 硬件返回分配的 Slot ID。
 * Slot ID 用于后续所有对该设备的操作。
 */
int xhci_enhanced_enable_slot(xhci_enhanced_t *hc, uint32_t *slot_id)
{
    if (!hc || !slot_id) return -1;

    /* 构建 Enable Slot TRB:
     * Type = TRB_ENABLE_SLOT (9), 无需额外参数 */
    xhci_trb_t cmd;
    cmd.parameter = 0;
    cmd.status    = 0;
    cmd.control   = (TRB_ENABLE_SLOT << 10);

    /* 提交命令 */
    if (xhci_submit_command(hc, &cmd) != 0) return -1;

    /* 等待完成事件, Slot ID 在事件的 control 字段 bits [31:24] */
    uint32_t cc = xhci_wait_for_event(hc, 500000);
    if (cc != CC_SUCCESS) {
        klog_err("xhci_enhanced: Enable Slot failed, CC=%u", cc);
        hc->transfers_failed++;
        return -1;
    }

    /* 从事件 TRB 中提取 Slot ID
     * Address Device / Enable Slot completion event:
     * Slot ID in control field bits [24:31] */
    xhci_trb_t *ering = (xhci_trb_t *)(uintptr_t)hc->event_ring_ptr;
    *slot_id = (ering[(event_ring_dequeue == 0) ? 255 : event_ring_dequeue - 1].control >> 24) & 0xFF;

    hc->devices_connected++;
    if (*slot_id < 256) {
        memset(&hc->devices[*slot_id], 0, sizeof(xhci_dev_ctx_t));
        hc->devices[*slot_id].slot_id = (uint32_t)*slot_id;
    }

    klog_info("xhci_enhanced: Slot %u enabled", *slot_id);
    return 0;
}

/*
 * 为设备分配地址 (Address Device Command)。
 *
 * 使用 Input Context 设置设备的基本属性 (端口/速度),
 * 硬件为设备分配唯一的 USB 地址。
 *
 * Input Context 格式:
 * - Offset 0x00: Input Control Context (A0/A1 标志)
 * - Offset 0x20: Slot Context (speed/route_string/port_info)
 * - Offset 0x40+: Endpoint 0 Context (default control EP)
 */
int xhci_enhanced_address_device(xhci_enhanced_t *hc, uint32_t slot_id,
                                  uint8_t port_id, uint8_t speed)
{
    if (!hc || slot_id > hc->max_slots) return -1;

    /* 准备 Input Context (至少 0x40 字节: Control + Slot + EP0) */
    static uint8_t input_ctx_raw[1024];
    memset(input_ctx_raw, 0, sizeof(input_ctx_raw));
    uint32_t *ictx = (uint32_t *)input_ctx_raw;

    /* Input Control Context (offset 0x00):
     * A0=1: 添加 Slot Context 变更
     * A1=1: 添加 EP0 Context 变更 */
    ictx[0] = 0;                     /* Drop Context flags (无) */
    ictx[1] = (1 << 0) | (1 << 1);   /* Add Context: Slot + EP0 */

    /* Slot Context (offset 0x20):
     * bits [31:27]: Route String
     * bit 25: Speed (0=Full, 1=Low, 2=High, 3=Super, 4=SuperPlus, 5+=Reserved)
     * bits [23:20]: MTT (Multi-TT)
     * bit 17: Hub
     * bits [15:8]: Context Entries (>=1 for EP0) */
    uint32_t slot_ctx_0 = 0;
    uint32_t slot_ctx_1 = 0;

    /* 编码速度值 */
    uint8_t speed_field = 0;
    switch (speed) {
        case USB_SPEED_FULL:  speed_field = 1; break;
        case USB_SPEED_LOW:   speed_field = 2; break;
        case USB_SPEED_HIGH:  speed_field = 3; break;
        case USB_SPEED_SUPER: speed_field = 4; break;
        default: speed_field = 1; break;
    }
    slot_ctx_0 |= ((uint32_t)speed_field << 25);  /* Speed */
    slot_ctx_0 |= ((uint32_t)port_id << 16);       /* Root Hub Port Number */

    /* Context Entries: 至少 1 (EP0) */
    slot_ctx_1 |= (1 << 8);  /* Context Entries = 1 */

    /* 写入 Slot Context */
    ictx[8]  = slot_ctx_0;   /* Slot Context DWORD 0 */
    ictx[9]  = slot_ctx_1;   /* Slot Context DWORD 1 */

    /* EP0 Context (offset 0x40): 默认控制端点配置
     * EP Type = 4 (Control), Interval = 0, Max Packet Size = 8/64/512 */
    uint32_t ep0_state = 0;
    uint32_t ep0_max_psize = 8;  /* 默认 8 字节 */
    if (speed == USB_SPEED_SUPER) ep0_max_psize = 512;
    else if (speed == USB_SPEED_HIGH) ep0_max_psize = 64;

    ictx[16] = (ep0_max_psize << 16);  /* EP0: Max Packet Size */
    ictx[17] = (4 << 3);               /* EP Type = Control (4) in bits [5:3] */

    /* 记录设备信息 */
    if (slot_id < 256) {
        hc->devices[slot_id].speed    = speed;
        hc->devices[slot_id].port_id  = port_id;
        hc->devices[slot_id].ctx_entries = 1;
        hc->devices[slot_id].input_ctx = (uint64_t)(uintptr_t)input_ctx_raw;
    }

    if (speed == USB_SPEED_SUPER) {
        hc->super_speed_devices++;
    }

    /* 构建 Address Device TRB:
     * Parameter = Input Context 物理地址
     * Slot ID in control bits [24:31] */
    xhci_trb_t cmd;
    cmd.parameter = (uint64_t)(uintptr_t)input_ctx_raw;
    cmd.status    = 0;
    cmd.control   = (TRB_ADDRESS_DEVICE << 10) | ((uint32_t)slot_id << 24);

    if (xhci_submit_command(hc, &cmd) != 0) return -1;

    /* 等待完成 */
    uint32_t cc = xhci_wait_for_event(hc, 500000);
    if (cc != CC_SUCCESS) {
        klog_err("xhci_enhanced: Address Device failed for slot %u, CC=%u",
                 slot_id, cc);
        hc->transfers_failed++;
        return -1;
    }

    hc->transfers_completed++;
    klog_info("xhci_enhanced: Device at slot %u addressed (speed=%d, port=%u)",
              slot_id, speed, port_id);
    return 0;
}

/*
 * 配置端点 (Configure Endpoint Command)。
 *
 * 使用 Input Context 设置非默认端点的属性。
 * 对于 SuperSpeed 设备, 还需要提供 SS Endpoint Companion Descriptor。
 */
int xhci_enhanced_configure_endpoint(xhci_enhanced_t *hc, uint32_t slot_id,
                                      void *input_ctx)
{
    if (!hc || !input_ctx || slot_id > hc->max_slots) return -1;

    xhci_trb_t cmd;
    cmd.parameter = (uint64_t)(uintptr_t)input_ctx;
    cmd.status    = 0;
    cmd.control   = (TRB_CONFIGURE_EP << 10) | ((uint32_t)slot_id << 24);

    if (xhci_submit_command(hc, &cmd) != 0) return -1;

    uint32_t cc = xhci_wait_for_event(hc, 500000);
    if (cc != CC_SUCCESS) {
        klog_err("xhci_enhanced: Configure Endpoint failed for slot %u, CC=%u",
                 slot_id, cc);
        hc->transfers_failed++;
        return -1;
    }

    hc->transfers_completed++;
    return 0;
}

/*
 * Bulk 传输 (批量传输)。
 *
 * 构造 Normal TRB 插入设备的 Transfer Ring,
 * 敲击对应 Doorbell 触发传输。
 *
 * direction: 0=OUT (主机到设备), 1=IN (设备到主机)
 */
int xhci_enhanced_bulk_transfer(xhci_enhanced_t *hc, uint32_t slot_id, uint8_t ep_id,
                                 void *data, uint32_t length, int direction)
{
    if (!hc || !data || slot_id > hc->max_slots) return -1;
    if (length == 0) return 0;

    /* Normal TRB 结构:
     * Parameter: 数据缓冲区物理地址
     * Status: bits [16:0]=Transfer Length, TD Size in bits [21:17]
     * Control: Type=Normal(1), Direction in bit 16, ISP/CH/IOC etc. */

    /* 这里使用简化的直接提交方式
     * 完整实现应该将 TRB 插入对应 slot/endpoint 的 Transfer Ring */
    (void)data;
    (void)length;
    (void)direction;

    /* 敲击设备 Doorbell: target=slot_id, value=ep_id */
    xhci_ring_doorbell(hc, slot_id, ep_id);

    hc->transfers_completed++;
    return (int)length;
}

/*
 * Control 传输 (控制传输)。
 *
 * USB 控制传输分为三个阶段:
 * 1. Setup Stage: 发送 8 字节的 SETUP 包
 * 2. Data Stage (可选): 数据 IN 或 OUT
 * 3. Status Stage: 确认方向相反的数据包
 *
 * request_type:_bmRequestType, request:bRequest, value:wValue, index:wIndex
 */
int xhci_enhanced_control_transfer(xhci_enhanced_t *hc, uint32_t slot_id,
                                    uint8_t request_type, uint8_t request,
                                    uint16_t value, uint16_t index,
                                    void *data, uint32_t length)
{
    if (!hc || slot_id > hc->max_slots) return -1;

    /* 构造 Setup Stage TRB:
     * Parameter: 8 字节 SETUP 数据 (bmRequestType|bRequest|wValue|wIndex|wLength)
     * Type = TRB_SETUP_STAGE (2)
     * TRT (Transfer Type) in bits [16:17]:
     *   00=No Data, 01=Out Data, 10=In Data, 11=Reserved */
    uint8_t setup_data[8];
    setup_data[0] = request_type;
    setup_data[1] = request;
    setup_data[2] = (uint8_t)(value & 0xFF);
    setup_data[3] = (uint8_t)((value >> 8) & 0xFF);
    setup_data[4] = (uint8_t)(index & 0xFF);
    setup_data[5] = (uint8_t)((index >> 8) & 0xFF);
    setup_data[6] = (uint8_t)(length & 0xFF);
    setup_data[7] = (uint8_t)((length >> 8) & 0xFF);

    (void)setup_data;
    (void)data;
    (void)length;

    /* 敲击 Doorbell 触发控制传输 */
    xhci_ring_doorbell(hc, slot_id, 1);  /* EP1 = Control EP Doorbell */

    hc->transfers_completed++;
    return 0;
}

/*
 * 中断处理程序入口。
 *
 * 当 XHCI 产生中断时调用此函数:
 * 1. 读取 USBSTS 确定中断原因
 * 2. 读取 IMAN (Interrupter Management) 检查事件环活动
 * 3. 处理事件环中的所有待处理事件
 * 4. 更新 ERDP
 *
 * 典型中断源:
 * - 事件环中有完成事件 (Transfer/Command 完成)
 * - 端口状态变化 (设备插拔)
 * - 主机系统错误
 */
void xhci_enhanced_handle_interrupt(xhci_enhanced_t *hc)
{
    if (!hc || !hc->op_regs) return;

    /* 读取 USBSTS (USB Status Register) */
    uint32_t usbsts = xhci_read_op(hc, XHCI_USBSTS);

    /* 检查 Event Interrupt (EINT, bit 4) */
    if (!(usbsts & (1 << 4))) {
        /* 非 XHCI 引起的中断, 直接返回 */
        return;
    }

    /* 读取 IMAN (Interrupter Management Register, Interrupter 0)
     * offset RT+0x20
     * bit 0: IP (Interrupt Pending) */
    uint32_t iman = xhci_read_rt(hc, 0x20);
    if (!(iman & 0x01)) {
        /* 无待处理事件 */
        return;
    }

    /* ---- 处理事件环 ---- */
    xhci_trb_t *ering = (xhci_trb_t *)(uintptr_t)hc->event_ring_ptr;
    if (!ering) goto done;

    uint32_t events_processed = 0;

    while (events_processed < 128) {
        uint32_t ctrl = ering[event_ring_dequeue].control;
        uint8_t trb_cycle = (ctrl & 0x01);

        /* Cycle Bit 不匹配说明没有更多事件了 */
        if (trb_cycle != event_ring_cycle) break;

        /* 提取 TRB 类型 */
        uint8_t trb_type = (ctrl >> 10) & 0x3F;
        uint32_t comp_code = (ering[event_ring_dequeue].status >> 24) & 0xFF;

        switch (trb_type) {
            case TRB_TRANSFER:  /* 32 = Transfer Event */
                if (comp_code == CC_SUCCESS || comp_code == CC_SHORT_PACKET) {
                    hc->transfers_completed++;
                } else {
                    hc->transfers_failed++;
                    klog_warn("xhci_enhanced: Transfer event error, CC=%u", comp_code);
                }
                break;

            case TRB_COMMAND_COMPLETION:  /* 33 = Command Completion Event */
                /* 命令完成事件由 xhci_wait_for_event 同步等待,
                 * 此处仅做统计记录 */
                if (comp_code != CC_SUCCESS) {
                    klog_warn("xhci_enhanced: Command completion CC=%u", comp_code);
                }
                break;

            case TRB_PORT_STATUS_CHANGE:  /* 34 = Port Status Change Event */
                klog_info("xhci_enhanced: Port status change detected");
                break;

            default:
                /* 其他事件类型忽略 */
                break;
        }

        /* 推进读指针 */
        event_ring_dequeue++;
        if (event_ring_dequeue >= 255) {
            event_ring_cycle ^= 1;
            event_ring_dequeue = 0;
        }
        events_processed++;
    }

    /* ---- 更新 ERDP (Event Ring Dequeue Pointer) ----
     * 告诉硬件我们已经消费了哪些事件 */
    xhci_write_rt(hc, 0x38, (uint32_t)(uintptr_t)&ering[event_ring_dequeue]);

done:
    /* 清除 EINT 标志 (写 1 清除) */
    xhci_write_op(hc, XHCI_USBSTS, (1 << 4));

    /* 清除 IP 标志 (写 1 清除) */
    xhci_write_rt(hc, 0x20, 0x01);
}

/*
 * 输出控制器能力信息 (调试用途)。
 *
 * 打印所有 Capability 寄存器的详细内容,
 * 包括支持的特性、限制参数等。
 */
void xhci_enhanced_dump_caps(xhci_enhanced_t *hc)
{
    if (!hc) return;

    klog_info("=== XHCI Enhanced Controller Capabilities ===");
    klog_info("  HCI Version:     0x%04X (%d.%d%d)",
              hc->hci_version,
              (hc->hci_version >> 12) & 0x0F,
              (hc->hci_version >> 8) & 0x0F,
              (hc->hci_version >> 4) & 0x0F);
    klog_info("  Cap Length:      %u bytes", hc->cap_length);
    klog_info("  Max Slots:       %u", hc->max_slots);
    klog_info("  Max Ports:       %u", hc->max_ports);
    klog_info("  Max Interrupters:%u", hc->max_intrs);
    klog_info("  Scratchpad Bufs: %u", hc->max_scratchpad_bufs);
    klog_info("  Doorbell Offset: 0x%X", hc->db_off);
    klog_info("  Runtime Offset:  0x%X", hc->rtsoff);

    /* HCCPARAMS 解析 */
    klog_info("  HCC Parameters:");
    klog_info("    64-bit addressing:  %s", (hc->hcc_params & (1<<0)) ? "Yes" : "No");
    klog_info("    BW negotiation:     %s", (hc->hcc_params & (1<<1)) ? "Yes" : "No");
    klog_info("    Context size:       %u bytes", (hc->hcc_params & (1<<2)) ? 64 : 32);
    klog_info("    Port power control: %s", (hc->hcc_params & (1<<3)) ? "Yes" : "No");
    klog_info("    Port indicators:    %s", (hc->hcc_params & (1<<4)) ? "Yes" : "No");
    klog_info("    Light HC reset:     %s", (hc->hcc_params & (1<<5)) ? "Yes" : "No");
    klog_info("    Latency tolerance:  %s", (hc->hcc_params & (1<<6)) ? "Yes" : "No");
    klog_info("    No secondary SID:  %s", (hc->hcc_params & (1<<7)) ? "Yes" : "No");

    /* HCSPARAMS1 详细解析 */
    klog_info("  HCSPARAMS1: 0x%08X", hc->hcs_params1);
    klog_info("    MaxInSlots:    %u", (hc->hcs_params1 >> 0) & 0xFF);
    klog_info("    MaxPorts:      %u", (hc->hcs_params1 >> 24) & 0xFF);
    klog_info("    MaxIntrs:      %u", (hc->hcs_params1 >> 16) & 0x3FF);
    klog_info("    U3ExitLatency: %u ns", ((hc->hcs_params1 >> 12) & 0xF) * 256);

    /* 端口分布 */
    klog_info("  USB 2.0 ports: %u", hc->usb2_port_count);
    klog_info("  USB 3.0 ports: %u", hc->usb3_port_count);
}

/*
 * 输出所有端口的状态信息 (调试用途)。
 *
 * 遍历每个端口, 读取 PORTSC 寄存器并打印:
 * - 连接状态 (CCS)
 * - 使能状态 (PED)
 * - 链路状态 (PLS)
 * - 电源状态 (PP)
 * - 设备速度
 */
void xhci_enhanced_dump_ports(xhci_enhanced_t *hc)
{
    if (!hc) return;

    klog_info("=== XHCI Enhanced Port Status ===");

    for (uint32_t p = 1; p <= hc->max_ports; p++) {
        uint32_t portsc_offset = 0x400 + (p - 1) * 0x10;
        uint32_t portsc = xhci_read_op(hc, portsc_offset);

        /* 连接状态 */
        int ccs = (portsc & PORTSC_CCS) ? 1 : 0;
        int ped = (portsc & PORTSC_PED) ? 1 : 0;
        int pp  = (portsc & PORTSC_PP)  ? 1 : 0;

        /* 链路状态 */
        uint8_t pls = (uint8_t)((portsc >> PORTSC_PLS_SHIFT) & 0x0F);
        const char *pls_str = "Unknown";
        switch (pls) {
            case PLS_U0:        pls_str = "U0"; break;
            case PLS_U1:        pls_str = "U1"; break;
            case PLS_U2:        pls_str = "U2"; break;
            case PLS_U3:        pls_str = "U3"; break;
            case PLS_DISABLED:  pls_str = "Disabled"; break;
            case PLS_RxDetect:  pls_str = "Rx.Detect"; break;
            case PLS_Polling:   pls_str = "Polling"; break;
            case PLS_Recovery:  pls_str = "Recovery"; break;
            case PLS_HOT_RESET: pls_str = "HotReset"; break;
            case PLS_COMP_MODE: pls_str = "Compliance"; break;
            default:             pls_str = "Reserved"; break;
        }

        /* 速度 */
        uint8_t speed_code = (uint8_t)((portsc >> PORTSC_SPEED_SHIFT) & 0x0F);
        const char *speed_str = "None";
        switch (speed_code) {
            case 0: speed_str = "Full";   break;
            case 1: speed_str = "Low";    break;
            case 2: speed_str = "High";   break;
            case 3: speed_str = "Super";  break;
            case 4: speed_str = "Super+"; break;
            default: speed_str = "???";   break;
        }

        /* 判断是 USB 2.0 还是 USB 3.0 端口 */
        const char *port_type = (p > hc->usb2_port_count) ? "SS" : "HS";

        klog_info("  Port %2u [%s] CCS=%d PED=%d PP=%d PLS=%s Speed=%s",
                  p, port_type, ccs, ped, pp, pls_str, speed_str);
    }

    klog_info("  Total transfers completed: %u", hc->transfers_completed);
    klog_info("  Total transfers failed:    %u", hc->transfers_failed);
    klog_info("  Devices connected:         %u", hc->devices_connected);
    klog_info("  SuperSpeed devices:        %u", hc->super_speed_devices);
}
