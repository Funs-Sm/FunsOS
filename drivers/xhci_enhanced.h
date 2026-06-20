/*
 * xhci_enhanced.h - USB 3.0 XHCI SuperSpeed 增强驱动
 *
 * 在基础 XHCI 驱动之上增加 USB 3.0 SuperSpeed 支持:
 * - SuperSpeed (5Gbps) 端口管理
 * - 完整的 TRB 环形队列管理
 * - USB 3.0 链路状态控制 (U0/U1/U2/U3)
 * - SuperSpeed Endpoint Companion Descriptor
 * - 增强的设备上下文管理
 * - 支持最多 256 个设备插槽和 32 个中断器
 */

#ifndef XHCI_ENHANCED_H
#define XHCI_ENHANCED_H

#include "stdint.h"
#include "stddef.h"
#include "string.h"

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

/* USB 3.0 SuperSpeed 特性 */
#define USB_SPEED_SUPER   5   /* 5 Gbps */
#define USB_SPEED_HIGH    3   /* 480 Mbps */
#define USB_SPEED_FULL    2   /* 12 Mbps */
#define USB_SPEED_LOW     1   /* 1.5 Mbps */

/* XHCI 寄存器 (SuperSpeed 相关) */
#define XHCI_USBCMD       0x0000
#define XHCI_USBSTS       0x0004
#define XHCI_PAGESIZE     0x0008
#define XHCI_DNCTRL       0x0314
#define XHCI_CAPLENGTH    0x0000  /* Cap register (8-bit) */
#define XHCI_HCIVERSION   0x0002  /* 16-bit BCD version */
#define XHCI_HCSPARAMS1   0x0004  /* Structural Params 1 */
#define XHCI_HCSPARAMS2   0x0008  /* Structural Params 2 */
#define XHCI_HCSPARAMS3   0x000C  /* Structural Params 3 */
#define XHCI_HCCPARAMS    0x0010  /* Capability Params */
#define XHCI_DBOFF        0x0014  /* Doorbell Offset */
#define XHCI_RTSOFF       0x0018  /* Runtime Register Space Offset */
#define XHCI_OPREG_BASE   0x0020  /* Operational registers base (CAPLENGTH) */

/* XHCI Port Status bits (USB 3.0) */
#define PORTSC_CCS        (1 << 0)  /* Current Connect Status */
#define PORTSC_PED        (1 << 1)  /* Port Enable/Disable Complete */
#define PORTSC_OCA        (1 << 3)  /* Over-current Active */
#define PORTSC_PR         (1 << 4)  /* Port Reset */
#define PORTSC_PLS_SHIFT  5         /* Port Link State shift */
#define PORTSC_PLS_MASK   (0xF << 5)
#define PORTSC_PP         (1 << 9)  /* Port Power */
#define PORTSC_SPEED_SHIFT 10       /* Port Speed shift */
#define PORTSC_SPEED_MASK (0xF << 10)

/* USB 3.0 Link States */
#define PLS_U0            0   /* U0 - Fully operational */
#define PLS_U1            1   /* U1 - Low power idle */
#define PLS_U2            2   /* U2 - Suspended */
#define PLS_U3            3   /* U3 - Suspend */
#define PLS_DISABLED      4   /* Disabled */
#define PLS_RxDetect      5   /* Rx.Detect */
#define PLS_Inactive      6   /* Inactive */
#define PLS_Polling       7   /* Polling */
#define PLS_Recovery      8   /* Recovery */
#define PLS_HOT_RESET     9   /* Hot Reset */
#define PLS_COMP_MODE     10  /* Compliance Mode */
#define PLS_LOOPBACK      11  /* Loopback */

/* SuperSpeed Endpoint Companion Descriptor */
typedef struct ss_ep_companion {
    uint8_t  bMaxBurst;           /* Maximum number of bursts */
    uint8_t  bmAttributes;        /* Sync type + Mult */
    uint16_t wBytesPerInterval;   /* Bytes per service interval */
} ss_ep_companion_t;

/* XHCI 设备上下文 */
typedef struct xhci_dev_ctx {
    uint64_t input_ctx;           /* Input context DMA addr */
    uint64_t output_ctx;          /* Output context DMA addr */
    uint32_t slot_id;             /* Slot ID assigned by HC */
    uint8_t  speed;               /* Device speed */
    uint8_t  port_id;             /* Root hub port number */
    uint8_t  ctx_entries;         /* Context entries */
    uint32_t route_string;        /* Route string for TT */
    uint8_t  hub_addr;            /* Hub address (if TT) */
    uint8_t  hub_port;            /* Hub port (if TT) */
} xhci_dev_ctx_t;

/* Transfer Request Block (TRB)
 * 每个 TRB 占 16 字节, 用于命令/传输/事件描述。
 * 环形队列通过 Cycle Bit 区分有效/无效条目。 */
typedef struct xhci_trb {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
} __attribute__((packed)) xhci_trb_t;

/* TRB Types */
#define TRB_NORMAL          1
#define TRB_SETUP_STAGE     2
#define TRB_DATA_STAGE      3
#define TRB_STATUS_STAGE    4
#define TRB_LINK            6
#define TRB_EVENT_DATA      7
#define TRB_NOOP            8
#define TRB_ENABLE_SLOT     9
#define TRB_DISABLE_SLOT    10
#define TRB_ADDRESS_DEVICE  11
#define TRB_CONFIGURE_EP   12
#define TRB_EVAL_CONTEXT   13
#define TRB_RESET_EP       15
#define TRB_STOP_EP        16
#define TRB_SET_TR_DEQ     17
#define TRB_RESET_DEVICE   17
#define TRB_GET_PORT_BANDWIDTH 21
#define TRB_FORCE_HEADER    22
#define TRB_NOOP_CMD        23

/* Event TRB types */
#define TRB_TRANSFER              32  /* Transfer Event */
#define TRB_COMMAND_COMPLETION    33  /* Command Completion Event */
#define TRB_PORT_STATUS_CHANGE    34  /* Port Status Change Event */

/* Event TRB completion codes */
#define CC_SUCCESS         1
#define CC_TRB_ERROR       3
#define CC_STALL_ERROR     6
#define CC_RESOURCE_ERROR  7
#define CC_BANDWIDTH_ERR   8
#define CC_SHORT_PACKET    13
#define CC_UNDEFINED       0
#define CC_INVALID         255

/* XHCI 增强驱动上下文 */
typedef struct xhci_enhanced {
    volatile uint32_t *cap_regs;   /* Capability registers base */
    volatile uint32_t *op_regs;    /* Operational registers base */
    volatile uint32_t *rt_regs;    /* Runtime registers base */
    volatile uint32_t *db_regs;    /* Doorbell registers base */

    uint32_t cap_length;           /* Capability register length */
    uint16_t hci_version;          /* BCD version */
    uint32_t hcs_params1;          /* HC Structural Params 1 */
    uint32_t hcs_params2;          /* HC Structural Params 2 */
    uint32_t hcs_params3;          /* HC Structural Params 3 */
    uint32_t hcc_params;           /* HC Capability Params */
    uint32_t db_off;               /* Doorbell offset */
    uint32_t rtsoff;               /* Runtime reg offset */

    /* Derived values */
    uint32_t max_slots;            /* Max Device Slots */
    uint32_t max_intrs;            /* Max Interrupters */
    uint32_t max_ports;            /* Max Ports */
    uint32_t max_scratchpad_bufs;  /* Max Scratchpad Buffers */

    /* DMA pools */
    uint64_t dcbaa_ptr;            /* Device Context Base Address Array */
    uint64_t cmd_ring_ptr;         /* Command Ring pointer */
    uint64_t event_ring_ptr;       /* Event Ring pointer */
    uint64_t scratchpad_bufs[16];  /* Scratchpad buffers */

    /* Port management */
    uint32_t usb2_port_count;      /* USB 2.0 port count */
    uint32_t usb3_port_count;      /* USB 3.0 port count */

    /* Device management */
    xhci_dev_ctx_t devices[256];   /* Slot contexts */

    /* Statistics */
    uint32_t transfers_completed;
    uint32_t transfers_failed;
    uint32_t devices_connected;
    uint32_t super_speed_devices;
} xhci_enhanced_t;

/* 初始化XHCI增强驱动 */
int xhci_enhanced_init(uint32_t mmio_base);

/* 启动XHCI主机控制器 */
int xhci_enhanced_start(xhci_enhanced_t *hc);

/* 停止XHCI主机控制器 */
void xhci_enhanced_stop(xhci_enhanced_t *hc);

/* 重置主机控制器 */
int xhci_enhanced_reset(xhci_enhanced_t *hc);

/* 端口管理 */
int xhci_enhanced_reset_port(xhci_enhanced_t *hc, uint8_t port_id);
int xhci_enhanced_get_port_speed(xhci_enhanced_t *hc, uint8_t port_id);
int xhci_enhanced_set_port_power(xhci_enhanced_t *hc, uint8_t port_id, int power_on);

/* 设备插槽分配 */
int xhci_enhanced_enable_slot(xhci_enhanced_t *hc, uint32_t *slot_id);
int xhci_enhanced_address_device(xhci_enhanced_t *hc, uint32_t slot_id, uint8_t port_id, uint8_t speed);
int xhci_enhanced_configure_endpoint(xhci_enhanced_t *hc, uint32_t slot_id, void *input_ctx);

/* 数据传输 */
int xhci_enhanced_bulk_transfer(xhci_enhanced_t *hc, uint32_t slot_id, uint8_t ep_id,
                                 void *data, uint32_t length, int direction);
int xhci_enhanced_control_transfer(xhci_enhanced_t *hc, uint32_t slot_id,
                                    uint8_t request_type, uint8_t request,
                                    uint16_t value, uint16_t index,
                                    void *data, uint32_t length);

/* 中断处理 */
void xhci_enhanced_handle_interrupt(xhci_enhanced_t *hc);

/* 调试输出 */
void xhci_enhanced_dump_caps(xhci_enhanced_t *hc);
void xhci_enhanced_dump_ports(xhci_enhanced_t *hc);

#endif /* XHCI_ENHANCED_H */
