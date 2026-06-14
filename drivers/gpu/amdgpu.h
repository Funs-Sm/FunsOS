#ifndef AMDGPU_H
#define AMDGPU_H
#include "stdint.h"

/* ================================================================ */
/*  AMD GPU 寄存器定义                                               */
/* ================================================================ */

#define AMDGPU_MMIO_BASE    0x0000
#define AMDGPU_FB_BASE      0x10000000

/* ---- AtomBIOS 寄存器 ---- */
#define AMDGPU_ATOMBIOS_ROM_BASE           0x000F0000
#define AMDGPU_ATOMBIOS_ROM_SIZE           0x00010000
#define AMDGPU_ATOMBIOS_SIGNATURE_OFFSET   0x0000
#define AMDGPU_ATOMBIOS_TABLE_OFFSET       0x0048
#define AMDGPU_ATOMBIOS_CLOCK_TABLE        0x0060
#define AMDGPU_ATOMBIOS_CONNECTOR_TABLE    0x0070
#define AMDGPU_ATOMBIOS_GPIO_I2C_INFO      0x0080
#define AMDGPU_ATOMBIOS_VOLTAGE_TABLE      0x0090
#define AMDGPU_ATOMBIOS_POWERPLAY_INFO     0x00A0

/* ---- 显示连接器寄存器 ---- */
#define AMDGPU_DC_HPD_CONTROL              0x6E10
#define AMDGPU_DC_HPD_STATUS               0x6E11
#define AMDGPU_DC_DP_CONFIG                0x6E20
#define AMDGPU_DC_DP_LINK_TRAINING         0x6E21
#define AMDGPU_DC_DP_LANE_COUNT            0x6E22
#define AMDGPU_DC_DP_LINK_RATE             0x6E23
#define AMDGPU_DC_HDMI_CONFIG              0x6E30
#define AMDGPU_DC_HDMI_INFOFRAME           0x6E31
#define AMDGPU_DC_HDMI_AUDIO               0x6E32
#define AMDGPU_DC_DVI_CONFIG               0x6E40
#define AMDGPU_DC_I2C_CONTROL              0x6E50
#define AMDGPU_DC_I2C_DATA                 0x6E51
#define AMDGPU_DC_I2C_STATUS               0x6E52
#define AMDGPU_DC_EDID_BASE                0x6E60
#define AMDGPU_DC_EDID_SEGMENT             0x6E61

/* ---- 多显示器 ---- */
#define AMDGPU_DC_CRTC0_CONTROL            0x6E00
#define AMDGPU_DC_CRTC1_CONTROL            0x6E80
#define AMDGPU_DC_CRTC2_CONTROL            0x6F00
#define AMDGPU_DC_CRTC3_CONTROL            0x6F80
#define AMDGPU_DC_CRTC_OFFSET              0x0080
#define AMDGPU_DC_CRTC_BASE(x)             (0x6E00 + ((x) * 0x80))

/* ---- GPU 环形缓冲区 ---- */
#define AMDGPU_RING_GFX_BASE               0x6800
#define AMDGPU_RING_GFX_SIZE               0x6801
#define AMDGPU_RING_GFX_RPTR               0x6802
#define AMDGPU_RING_GFX_WPTR               0x6803
#define AMDGPU_RING_GFX_DOORBELL           0x6818
#define AMDGPU_RING_GFX_DOORBELL_OFFSET    0x6819
#define AMDGPU_RING_GFX_IB_BASE            0x681A
#define AMDGPU_RING_GFX_IB_SIZE            0x681B
#define AMDGPU_RING_COMPUTE_BASE           0x6820
#define AMDGPU_RING_COMPUTE_SIZE           0x6821
#define AMDGPU_RING_COMPUTE_RPTR           0x6822
#define AMDGPU_RING_COMPUTE_WPTR           0x6823
#define AMDGPU_RING_SDMA0_BASE             0x6830
#define AMDGPU_RING_SDMA1_BASE             0x6838

/* ---- 2D 加速引擎寄存器 ---- */
#define AMDGPU_2D_SRC_ADDR                 0x7000
#define AMDGPU_2D_DST_ADDR                 0x7001
#define AMDGPU_2D_SRC_PITCH                0x7002
#define AMDGPU_2D_DST_PITCH                0x7003
#define AMDGPU_2D_SRC_X                    0x7004
#define AMDGPU_2D_SRC_Y                    0x7005
#define AMDGPU_2D_DST_X                    0x7006
#define AMDGPU_2D_DST_Y                    0x7007
#define AMDGPU_2D_WIDTH                    0x7008
#define AMDGPU_2D_HEIGHT                   0x7009
#define AMDGPU_2D_FILL_COLOR               0x700A
#define AMDGPU_2D_ROP3                     0x700B
#define AMDGPU_2D_COMMAND                  0x700C
#define AMDGPU_2D_STATUS                   0x700D
#define AMDGPU_2D_LINE_X0                  0x7010
#define AMDGPU_2D_LINE_Y0                  0x7011
#define AMDGPU_2D_LINE_X1                  0x7012
#define AMDGPU_2D_LINE_Y1                  0x7013
#define AMDGPU_2D_BRESENHAM_CTL            0x7014

/* 2D 引擎命令 */
#define AMDGPU_2D_CMD_NOP                  0x00000000
#define AMDGPU_2D_CMD_BITBLT               0x00000001
#define AMDGPU_2D_CMD_SOLID_FILL           0x00000002
#define AMDGPU_2D_CMD_LINE_DRAW            0x00000003
#define AMDGPU_2D_CMD_CLEAR                0x00000004
#define AMDGPU_2D_CMD_STRETCH_BLT          0x00000005

/* ROP3 操作码 */
#define AMDGPU_ROP3_SRCCOPY                0xCC
#define AMDGPU_ROP3_SRCPAINT               0xEE
#define AMDGPU_ROP3_SRCAND                 0x88
#define AMDGPU_ROP3_SRCINVERT              0x66
#define AMDGPU_ROP3_SRCERASE               0x44
#define AMDGPU_ROP3_PATCOPY                0xF0
#define AMDGPU_ROP3_BLACKNESS              0x00
#define AMDGPU_ROP3_WHITENESS              0xFF

/* ---- VRAM 带宽优化寄存器 ---- */
#define AMDGPU_VRAM_TILING_CFG             0x7020
#define AMDGPU_VRAM_SURFACE_ALIGN          0x7021
#define AMDGPU_VRAM_BANDWIDTH_CTL          0x7022
#define AMDGPU_VRAM_MC_CONFIG              0x7023
#define AMDGPU_VRAM_DCC_CTL                0x7024
#define AMDGPU_VRAM_CMASK_ADDR             0x7025
#define AMDGPU_VRAM_FMASK_ADDR             0x7026
#define AMDGPU_VRAM_TILE_CONFIG            0x7027
#define AMDGPU_VRAM_MACROTILE_MODE         0x7028
#define AMDGPU_VRAM_MICROTILE_MODE         0x7029

/* 表面对齐模式 */
#define AMDGPU_SURF_ALIGN_LINEAR           0x00000000
#define AMDGPU_SURF_ALIGN_256B             0x00000100
#define AMDGPU_SURF_ALIGN_4KB              0x00001000
#define AMDGPU_SURF_ALIGN_64KB             0x00010000

/* ---- PowerPlay 时钟/电压控制寄存器 ---- */
#define AMDGPU_PP_SMU_BASE                 0x7C00
#define AMDGPU_PP_SMU_CMD                  0x7C00
#define AMDGPU_PP_SMU_RESP                 0x7C01
#define AMDGPU_PP_SMU_ARG                  0x7C02
#define AMDGPU_PP_SMU_FW_VERSION           0x7C03
#define AMDGPU_PP_SCLK_DPM_TABLE           0x7C10
#define AMDGPU_PP_MCLK_DPM_TABLE           0x7C11
#define AMDGPU_PP_SCLK_CURRENT             0x7C12
#define AMDGPU_PP_MCLK_CURRENT             0x7C13
#define AMDGPU_PP_VCORE_CURRENT            0x7C14
#define AMDGPU_PP_GPU_LOAD                 0x7C15
#define AMDGPU_PP_DPM_STATE                0x7C16
#define AMDGPU_PP_PSTATE_MIN               0x7C20
#define AMDGPU_PP_PSTATE_MAX               0x7C21
#define AMDGPU_PP_PSTATE_CURRENT           0x7C22
#define AMDGPU_PP_FAN_SPEED                0x7C30
#define AMDGPU_PP_FAN_PWM                  0x7C31
#define AMDGPU_PP_THERMAL_CTL              0x7C40

/* SMU 消息 */
#define AMDGPU_SMU_MSG_GET_CLK_TABLE       0x01
#define AMDGPU_SMU_MSG_SET_CLK_FREQ        0x02
#define AMDGPU_SMU_MSG_SET_VOLTAGE         0x03
#define AMDGPU_SMU_MSG_GET_TEMPERATURE     0x04
#define AMDGPU_SMU_MSG_GET_GPU_LOAD        0x05
#define AMDGPU_SMU_MSG_ENTER_DPM           0x06
#define AMDGPU_SMU_MSG_EXIT_DPM            0x07
#define AMDGPU_SMU_MSG_SET_FAN_SPEED       0x08
#define AMDGPU_SMU_MSG_GET_POWER_CONSUMP   0x09
#define AMDGPU_SMU_MSG_THROTTLE_CTL        0x0A
#define AMDGPU_SMU_MSG_GET_VRAM_USAGE      0x0B

/* DPM 状态 */
#define AMDGPU_DPM_STATE_BOOT              0x00
#define AMDGPU_DPM_STATE_PERFORMANCE       0x01
#define AMDGPU_DPM_STATE_BATTERY           0x02
#define AMDGPU_DPM_STATE_BALANCED          0x03
#define AMDGPU_DPM_STATE_CUSTOM            0x04

/* ---- 温度传感器寄存器 ---- */
#define AMDGPU_THERMAL_TEMP_EDGE           0x7C50
#define AMDGPU_THERMAL_TEMP_JUNCTION       0x7C51
#define AMDGPU_THERMAL_TEMP_MEM            0x7C52
#define AMDGPU_THERMAL_TEMP_VRM            0x7C53
#define AMDGPU_THERMAL_THROTTLE_TEMP       0x7C54
#define AMDGPU_THERMAL_CRITICAL_TEMP       0x7C55
#define AMDGPU_THERMAL_STATUS              0x7C56
#define AMDGPU_THERMAL_EMERGENCY_TEMP      0x7C57

/* 热节流阈值 */
#define AMDGPU_THERMAL_THROTTLE_DEFAULT    85000U   /* 85°C (millidegrees) */
#define AMDGPU_THERMAL_CRITICAL_DEFAULT    95000U   /* 95°C */
#define AMDGPU_THERMAL_EMERGENCY_DEFAULT   105000U  /* 105°C */

/* ---- DMA 引擎寄存器 ---- */
#define AMDGPU_SDMA0_BASE_ADDR             0x7400
#define AMDGPU_SDMA0_RB_BASE               0x7400
#define AMDGPU_SDMA0_RB_RPTR               0x7401
#define AMDGPU_SDMA0_RB_WPTR               0x7402
#define AMDGPU_SDMA0_RB_CNTL               0x7403
#define AMDGPU_SDMA0_IB_BASE               0x7404
#define AMDGPU_SDMA0_IB_SIZE               0x7405
#define AMDGPU_SDMA0_STATUS                0x7406
#define AMDGPU_SDMA0_COPY_CNTL             0x7408
#define AMDGPU_SDMA0_SRC_ADDR              0x7409
#define AMDGPU_SDMA0_SRC_ADDR_HI           0x740A
#define AMDGPU_SDMA0_DST_ADDR              0x740B
#define AMDGPU_SDMA0_DST_ADDR_HI           0x740C
#define AMDGPU_SDMA0_COPY_SIZE             0x740D
#define AMDGPU_SDMA0_FENCE_ADDR            0x740E
#define AMDGPU_SDMA0_FENCE_DATA            0x740F

/* SDMA 拷贝命令 */
#define AMDGPU_SDMA_PKT_COPY_LINEAR        0x01
#define AMDGPU_SDMA_PKT_COPY_LINEAR_SUBWIN 0x02
#define AMDGPU_SDMA_PKT_COPY_TILED         0x03
#define AMDGPU_SDMA_PKT_FENCE              0x04
#define AMDGPU_SDMA_PKT_TRAP               0x05
#define AMDGPU_SDMA_PKT_NOP                0x06
#define AMDGPU_SDMA_PKT_WRITE_DATA         0x07

/* ---- 中断寄存器 ---- */
#define AMDGPU_IH_RB_BASE                  0x7E00
#define AMDGPU_IH_RB_WPTR                  0x7E01
#define AMDGPU_IH_RB_RPTR                  0x7E02
#define AMDGPU_IH_RB_CNTL                  0x7E03
#define AMDGPU_IH_STATUS                   0x7E04
#define AMDGPU_IH_DOORBELL_RPTR            0x7E05
#define AMDGPU_IH_INT_VECTOR               0x7E10
#define AMDGPU_IH_INT_ACK                  0x7E11
#define AMDGPU_IH_INT_MASK                 0x7E12

/* 中断源 */
#define AMDGPU_IH_SRCID_VSYNC              0x0001
#define AMDGPU_IH_SRCID_PAGE_FLIP          0x0002
#define AMDGPU_IH_SRCID_CMD_COMPLETE       0x0003
#define AMDGPU_IH_SRCID_SDMA_TRAP          0x0004
#define AMDGPU_IH_SRCID_THERMAL_TRIP       0x0005
#define AMDGPU_IH_SRCID_HPD                0x0010
#define AMDGPU_IH_SRCID_HPD1               0x0011
#define AMDGPU_IH_SRCID_HPD2               0x0012
#define AMDGPU_IH_SRCID_HPD3               0x0013
#define AMDGPU_IH_SRCID_GPU_FAULT          0x0020
#define AMDGPU_IH_SRCID_VCE_TRAP           0x0030
#define AMDGPU_IH_SRCID_UVD_TRAP           0x0031
#define AMDGPU_IH_SRCID_VCN_TRAP           0x0032

/* ---- 原有寄存器 ---- */
#define AMDGPU_GRPH_ENABLE         0x6800
#define AMDGPU_GRPH_CONTROL        0x6801
#define AMDGPU_GRPH_SWAP_CNTL      0x6802
#define AMDGPU_GRPH_PRIMARY_SURFACE_ADDRESS  0x6804
#define AMDGPU_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH  0x6805
#define AMDGPU_GRPH_PITCH          0x6808
#define AMDGPU_GRPH_X_START        0x680C
#define AMDGPU_GRPH_Y_START        0x680D
#define AMDGPU_GRPH_X_END          0x680E
#define AMDGPU_GRPH_Y_END          0x680F
#define AMDGPU_GRPH_UPDATE         0x6810

#define AMDGPU_DC_CRTC_CONTROL     0x6E00
#define AMDGPU_DC_CRTC_STATUS      0x6E01
#define AMDGPU_DC_TIMING_H_TOTAL   0x6E02
#define AMDGPU_DC_TIMING_V_TOTAL   0x6E03
#define AMDGPU_DC_TIMING_H_SYNC    0x6E04
#define AMDGPU_DC_TIMING_V_SYNC    0x6E05

#define AMDGPU_PM_CNTL             0x6F00
#define AMDGPU_PM_STATUS           0x6F01

/* ================================================================ */
/*  数据类型定义                                                     */
/* ================================================================ */

/* ---- 连接器类型 ---- */
#define AMDGPU_CONNECTOR_NONE              0
#define AMDGPU_CONNECTOR_VGA               1
#define AMDGPU_CONNECTOR_DVI_I             2
#define AMDGPU_CONNECTOR_DVI_D             3
#define AMDGPU_CONNECTOR_DVI_A             4
#define AMDGPU_CONNECTOR_COMPOSITE         5
#define AMDGPU_CONNECTOR_SVIDEO            6
#define AMDGPU_CONNECTOR_LVDS              7
#define AMDGPU_CONNECTOR_COMPONENT         8
#define AMDGPU_CONNECTOR_9PIN_DIN          9
#define AMDGPU_CONNECTOR_HDMI_A            10
#define AMDGPU_CONNECTOR_HDMI_B            11
#define AMDGPU_CONNECTOR_DisplayPort       12
#define AMDGPU_CONNECTOR_eDP               13
#define AMDGPU_CONNECTOR_USB_C             14

/* ---- 连接器状态 ---- */
typedef struct {
    uint32_t type;
    uint32_t hpd_pin;
    uint32_t i2c_channel;
    uint32_t dpcd_capable;
    uint32_t hdmi_capable;
    uint32_t dvi_dual_link;
    uint8_t  plugged;
    uint8_t  edid_valid;
    uint8_t  edid[256];
    uint32_t max_pixel_clock;
    uint32_t max_hres;
    uint32_t max_vres;
    uint32_t max_refresh;
    /* EDID 解析结果 */
    char     monitor_name[14];
    uint32_t pref_width;
    uint32_t pref_height;
    uint32_t pref_refresh;
} amdgpu_connector_t;

/* ---- 显示模式 (CRTC) ---- */
typedef struct {
    uint32_t crtc_id;
    uint32_t width;
    uint32_t height;
    uint32_t refresh;
    uint32_t h_total;
    uint32_t h_sync_start;
    uint32_t h_sync_end;
    uint32_t v_total;
    uint32_t v_sync_start;
    uint32_t v_sync_end;
    uint32_t fb_offset;
    uint32_t connector_id;
    uint32_t enabled;
    uint32_t bpp;
    uint32_t pitch;
    uint32_t fb_base;
} amdgpu_crtc_t;

/* ---- AtomBIOS 解析结构 ---- */
typedef struct __attribute__((packed)) {
    uint8_t  signature[2];           /* 0x55 0xAA */
    uint8_t  size;                   /* ROM size in 512-byte blocks */
    uint8_t  init_vector[4];
    uint8_t  reserved[16];
    uint16_t pci_device_offset;
    uint16_t master_data_table;
    uint32_t function_ptr;
} atom_bios_header_t;

typedef struct __attribute__((packed)) {
    uint16_t table_offset;
    uint16_t table_size;
    uint8_t  table_format_revision;
    uint8_t  table_content_revision;
} atom_master_table_entry_t;

typedef struct {
    uint8_t  valid;
    char     gpu_name[32];
    uint32_t gpu_chip_id;
    uint32_t vram_size;
    uint32_t vram_type;
    uint32_t max_engine_clock;
    uint32_t max_memory_clock;
    uint32_t core_count;
    uint32_t cu_count;
    uint32_t rop_count;
    uint32_t tmu_count;
    uint32_t pci_device_id;
    uint32_t pci_vendor_id;
    uint32_t pci_revision_id;
    /* 时钟表 */
    uint32_t sclk_table[16];
    uint32_t sclk_count;
    uint32_t mclk_table[16];
    uint32_t mclk_count;
    /* 电压表 */
    uint32_t voltage_table[16];
    uint32_t voltage_count;
    /* 连接器 */
    uint32_t connector_count;
    uint32_t connector_types[8];
} atom_bios_info_t;

/* ---- GPU 环形缓冲区 ---- */
#define AMDGPU_RING_QUEUE_SIZE      4096    /* 条目数 */
#define AMDGPU_RING_ENTRY_SIZE      16      /* 字节 */
#define AMDGPU_IB_MAX_SIZE          65536   /* 间接缓冲区最大大小 */

typedef struct __attribute__((packed)) {
    uint32_t header;                /* 类型 + 计数 */
    uint32_t data[3];               /* 参数 */
} amdgpu_ring_entry_t;

typedef struct {
    uint32_t *base;
    uint32_t  size;
    uint32_t  rptr;
    uint32_t  wptr;
    uint32_t  doorbell_offset;
    uint32_t *doorbell_ptr;
    uint32_t  align_mask;
    uint32_t  ready;
} amdgpu_ring_t;

typedef struct {
    uint32_t *cpu_addr;
    uint32_t  gpu_addr;
    uint32_t  size;
    uint32_t  used;
} amdgpu_ib_t;

/* ---- 2D 加速引擎 ---- */
typedef struct {
    uint32_t *command_buffer;
    uint32_t  command_buffer_size;
    uint32_t  command_count;
    uint32_t  op_count;
    uint32_t  bytes_copied;
    uint32_t  pixels_filled;
    uint32_t  lines_drawn;
    uint32_t  busy;
    uint32_t  initialized;
} amdgpu_2d_engine_t;

/* ---- VRAM 带宽优化 ---- */
typedef struct {
    uint32_t tiling_enabled;
    uint32_t tile_mode;           /* 0=linear, 1=256B, 2=4KB, 3=64KB, 4=mixed */
    uint32_t macro_tile_mode;
    uint32_t micro_tile_mode;
    uint32_t surface_alignment;
    uint32_t dcc_enabled;         /* Delta Color Compression */
    uint32_t cmask_base;
    uint32_t fmask_base;
    uint32_t channel_count;
    uint32_t bank_count;
    uint32_t bank_width;
    uint32_t bank_height;
    uint32_t num_banks;
    uint32_t optimized_bytes_saved;
    uint32_t bandwidth_optimized;
} amdgpu_vram_config_t;

/* ---- PowerPlay 电源管理 ---- */
typedef struct __attribute__((packed)) {
    uint32_t sclk;              /* 引擎时钟 (kHz) */
    uint32_t mclk;              /* 显存时钟 (kHz) */
    uint32_t vcore;             /* 核心电压 (mV) */
    uint32_t fan_speed_pwm;     /* 风扇 PWM */
    uint32_t power_limit;       /* 功耗限制 (mW) */
} amdgpu_pstate_t;

typedef struct {
    amdgpu_pstate_t pstates[8];
    uint32_t        pstate_count;
    uint32_t        current_pstate;
    uint32_t        dpm_enabled;        /* 动态电源管理 */
    uint32_t        gpu_load;           /* GPU 负载百分比 (0-100) */
    uint32_t        gpu_power;          /* 功耗 (mW) */
    uint32_t        fan_speed;          /* 风扇转速 (RPM) */
    uint32_t        voltage_table[16];
    uint32_t        voltage_count;
    uint32_t        clock_policy;       /* 0=battery, 1=balanced, 2=performance */
    uint32_t        initialized;
} amdgpu_powerplay_t;

/* ---- 温度监控 ---- */
typedef struct {
    uint32_t temp_edge;               /* GPU 边缘温度 (millidegrees C) */
    uint32_t temp_junction;           /* 结温 (millidegrees C) */
    uint32_t temp_mem;                /* 显存温度 */
    uint32_t temp_vrm;                /* VRM 温度 */
    uint32_t throttle_temp;           /* 节流温度阈值 */
    uint32_t critical_temp;           /* 临界温度阈值 */
    uint32_t emergency_temp;          /* 紧急关机温度 */
    uint32_t throttling_active;       /* 是否正在节流 */
    uint32_t throttle_level;          /* 节流级别 0=none, 1=light, 2=moderate, 3=heavy */
    uint32_t overheat_count;          /* 过热事件计数 */
    uint32_t last_reading_ms;         /* 最近一次读数时间 */
    uint32_t initialized;
} amdgpu_thermal_t;

/* ---- DMA 异步传输 ---- */
typedef struct __attribute__((packed)) {
    uint32_t src_addr_lo;
    uint32_t src_addr_hi;
    uint32_t dst_addr_lo;
    uint32_t dst_addr_hi;
    uint32_t copy_size;
    uint32_t fence_addr_lo;
    uint32_t fence_addr_hi;
    uint32_t fence_value;
    uint32_t flags;             /* bit0=interrupt, bit1=wait, bit2=linear */
} amdgpu_dma_cmd_t;

typedef struct {
    amdgpu_ring_t     ring;           /* SDMA 环形缓冲区 */
    amdgpu_dma_cmd_t *pending;        /* 待处理队列 */
    uint32_t          pending_count;
    uint32_t          pending_max;
    uint32_t          bytes_transferred;
    uint32_t          transfer_count;
    uint32_t          fence_value;
    uint32_t          initialized;
    uint32_t          busy;
} amdgpu_dma_engine_t;

/* ---- 中断处理 ---- */
typedef struct __attribute__((packed)) {
    uint32_t src_id;            /* 中断源 ID */
    uint32_t src_data;          /* 中断源数据 */
    uint32_t ring_id;           /* 关联环形缓冲区 ID */
    uint32_t vmid;              /* VM 上下文 */
    uint64_t timestamp;         /* 中断时间戳 */
} amdgpu_iv_entry_t;

/* 中断环缓冲区 */
#define AMDGPU_IH_RING_SIZE     256

typedef struct {
    amdgpu_iv_entry_t ring[AMDGPU_IH_RING_SIZE];
    uint32_t          rptr;
    uint32_t          wptr;
    uint32_t          enabled;
    uint32_t          interrupt_count;
    uint32_t          vsync_count;
    uint32_t          page_flip_count;
    uint32_t          cmd_complete_count;
    uint32_t          sdma_trap_count;
    uint32_t          thermal_trip_count;
    uint32_t          hpd_count;
    uint32_t          gpu_fault_count;
} amdgpu_ih_t;

/* ---- 扩展设备结构 ---- */
typedef struct amdgpu_device {
    uint32_t mmio_base;
    uint32_t fb_base;
    uint32_t fb_size;
    uint32_t *framebuffer;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t engine_clock;
    uint32_t memory_clock;
    uint32_t initialized;
    uint32_t pci_bus;
    uint32_t pci_slot;
    uint32_t pci_func;
    /* 新增字段 */
    atom_bios_info_t    atombios;
    amdgpu_connector_t  connectors[8];
    uint32_t            connector_count;
    amdgpu_crtc_t       crtc[4];           /* 最多4个显示控制器 */
    uint32_t            crtc_count;
    amdgpu_ring_t       gfx_ring;          /* 图形环形缓冲区 */
    amdgpu_ring_t       compute_ring;      /* 计算环形缓冲区 */
    amdgpu_2d_engine_t  engine_2d;         /* 2D 加速引擎 */
    amdgpu_vram_config_t vram_config;      /* VRAM 带宽配置 */
    amdgpu_powerplay_t  powerplay;         /* PowerPlay 电源管理 */
    amdgpu_thermal_t    thermal;           /* 温度监控 */
    amdgpu_dma_engine_t sdma0;             /* SDMA 引擎 0 */
    amdgpu_dma_engine_t sdma1;             /* SDMA 引擎 1 */
    amdgpu_ih_t         ih;                /* 中断处理器 */
} amdgpu_device_t;

/* ================================================================ */
/*  AMD GPU 驱动 API                                                 */
/* ================================================================ */

/* 基本初始化 */
int amdgpu_init(void);
int amdgpu_probe(void);
void amdgpu_shutdown(void);

/* 帧缓冲区 */
int amdgpu_get_framebuffer(amdgpu_device_t **dev);
void amdgpu_fill_rect(uint32_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void amdgpu_blit(uint32_t *dst, uint32_t *src, uint32_t w, uint32_t h, uint32_t dst_pitch, uint32_t src_pitch);
void amdgpu_set_mode(uint32_t width, uint32_t height, uint32_t bpp);
void amdgpu_power_save(void);
void amdgpu_power_full(void);
void amdgpu_get_info(char *buf, int bufsize);

/* ---- AtomBIOS ---- */
int  amdgpu_atombios_parse(amdgpu_device_t *dev);
int  amdgpu_atombios_get_clocks(amdgpu_device_t *dev);
int  amdgpu_atombios_get_connectors(amdgpu_device_t *dev);
const atom_bios_info_t *amdgpu_atombios_get_info(void);

/* ---- 显示输出 ---- */
int  amdgpu_connector_detect(amdgpu_device_t *dev);
int  amdgpu_edid_read(amdgpu_device_t *dev, uint32_t connector_idx);
int  amdgpu_dp_link_train(amdgpu_device_t *dev, uint32_t connector_idx);
int  amdgpu_hdmi_configure(amdgpu_device_t *dev, uint32_t connector_idx);
int  amdgpu_dvi_configure(amdgpu_device_t *dev, uint32_t connector_idx);
int  amdgpu_connector_set_mode(amdgpu_device_t *dev, uint32_t connector_idx,
                                uint32_t width, uint32_t height, uint32_t bpp, uint32_t refresh);

/* ---- 环形缓冲区 ---- */
int  amdgpu_ring_init(amdgpu_ring_t *ring, uint32_t size_kb);
int  amdgpu_ring_submit(amdgpu_ring_t *ring, const uint32_t *entries, uint32_t count);
int  amdgpu_ring_wait(amdgpu_ring_t *ring, uint32_t timeout_ms);
void amdgpu_ring_destroy(amdgpu_ring_t *ring);
int  amdgpu_ib_create(amdgpu_ib_t *ib, uint32_t size);
int  amdgpu_ib_submit(amdgpu_ring_t *ring, amdgpu_ib_t *ib);
void amdgpu_ring_doorbell(amdgpu_ring_t *ring);

/* ---- 2D 加速引擎 ---- */
int  amdgpu_2d_init(amdgpu_2d_engine_t *engine);
int  amdgpu_2d_bitblt(amdgpu_2d_engine_t *engine,
                      uint32_t dst_addr, uint32_t dst_pitch, uint32_t dst_x, uint32_t dst_y,
                      uint32_t src_addr, uint32_t src_pitch, uint32_t src_x, uint32_t src_y,
                      uint32_t w, uint32_t h, uint8_t rop);
int  amdgpu_2d_solid_fill(amdgpu_2d_engine_t *engine,
                          uint32_t dst_addr, uint32_t dst_pitch,
                          uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                          uint32_t color);
int  amdgpu_2d_line_draw(amdgpu_2d_engine_t *engine,
                         uint32_t dst_addr, uint32_t dst_pitch,
                         int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                         uint32_t color, uint32_t thickness);
int  amdgpu_2d_clear(amdgpu_2d_engine_t *engine,
                     uint32_t dst_addr, uint32_t dst_pitch,
                     uint32_t w, uint32_t h, uint32_t color);
void amdgpu_2d_shutdown(amdgpu_2d_engine_t *engine);

/* ---- VRAM 带宽优化 ---- */
int  amdgpu_vram_config_init(amdgpu_device_t *dev);
int  amdgpu_vram_set_tiling(amdgpu_device_t *dev, uint32_t mode);
int  amdgpu_vram_enable_dcc(amdgpu_device_t *dev, int enable);
int  amdgpu_vram_calc_alignment(uint32_t width, uint32_t height, uint32_t bpp, uint32_t *pitch, uint32_t *size);
int  amdgpu_vram_get_bandwidth(amdgpu_device_t *dev, uint32_t *read_bw, uint32_t *write_bw);

/* ---- PowerPlay 时钟/电压控制 ---- */
int  amdgpu_pp_init(amdgpu_device_t *dev);
int  amdgpu_pp_set_clock(uint32_t sclk_khz, uint32_t mclk_khz);
int  amdgpu_pp_set_voltage(uint32_t vcore_mv);
int  amdgpu_pp_set_fan_speed(uint32_t pwm);
int  amdgpu_pp_get_gpu_load(uint32_t *load_pct);
int  amdgpu_pp_get_power(uint32_t *power_mw);
int  amdgpu_pp_set_policy(uint32_t policy);
int  amdgpu_pp_get_current_pstate(amdgpu_pstate_t *pstate);
int  amdgpu_pp_dpm_enable(int enable);

/* ---- 多显示器支持 ---- */
int  amdgpu_multihead_init(amdgpu_device_t *dev);
int  amdgpu_multihead_add_crtc(amdgpu_device_t *dev, uint32_t connector_idx,
                                uint32_t width, uint32_t height, uint32_t bpp, uint32_t refresh);
int  amdgpu_multihead_remove_crtc(amdgpu_device_t *dev, uint32_t crtc_id);
int  amdgpu_multihead_get_crtc_count(amdgpu_device_t *dev);
void amdgpu_multihead_scan_displays(amdgpu_device_t *dev);
int  amdgpu_multihead_set_primary(amdgpu_device_t *dev, uint32_t crtc_id);

/* ---- 温度监控 ---- */
int  amdgpu_thermal_init(amdgpu_device_t *dev);
int  amdgpu_thermal_read(amdgpu_device_t *dev);
int  amdgpu_thermal_get_temp(uint32_t *edge, uint32_t *junction, uint32_t *mem, uint32_t *vrm);
int  amdgpu_thermal_get_throttle(uint32_t *level);
int  amdgpu_thermal_set_thresholds(uint32_t throttle, uint32_t critical);
void amdgpu_thermal_check(amdgpu_device_t *dev);

/* ---- DMA 引擎 ---- */
int  amdgpu_dma_init(amdgpu_dma_engine_t *dma, uint32_t ring_size);
int  amdgpu_dma_copy(amdgpu_dma_engine_t *dma,
                     uint32_t src_addr, uint32_t dst_addr,
                     uint32_t size, int use_interrupt);
int  amdgpu_dma_copy_async(amdgpu_dma_engine_t *dma,
                           uint32_t src_addr, uint32_t dst_addr, uint32_t size);
int  amdgpu_dma_wait(amdgpu_dma_engine_t *dma, uint32_t timeout_ms);
int  amdgpu_dma_fence_signal(amdgpu_dma_engine_t *dma);
int  amdgpu_dma_fence_wait(amdgpu_dma_engine_t *dma, uint32_t fence_value, uint32_t timeout_ms);
void amdgpu_dma_shutdown(amdgpu_dma_engine_t *dma);

/* ---- 中断处理 ---- */
int  amdgpu_ih_init(amdgpu_device_t *dev);
int  amdgpu_ih_enable(amdgpu_device_t *dev);
int  amdgpu_ih_disable(amdgpu_device_t *dev);
int  amdgpu_ih_process(amdgpu_device_t *dev);
void amdgpu_ih_register_handler(uint32_t src_id, void (*handler)(const amdgpu_iv_entry_t*));
void amdgpu_ih_unregister_handler(uint32_t src_id);
int  amdgpu_ih_wait_vsync(uint32_t crtc_id, uint32_t timeout_ms);
int  amdgpu_ih_wait_page_flip(uint32_t crtc_id, uint32_t timeout_ms);

#endif