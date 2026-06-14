#ifndef CONNECTX3_H
#define CONNECTX3_H

#include "net.h"
#include "stdint.h"
#include "kernel_types.h"

/* ---- PCI 设备 ID ---- */
#define CX3_VENDOR_ID        0x15B3
#define CX3_DEVICE_ID        0x1007   /* ConnectX-3      */
#define CX3_PRO_DEVICE_ID    0x1003   /* ConnectX-3 Pro  */

/* ---- 关键寄存器偏移 (相对于 MMIO base) ---- */
/* 命令接口 */
#define CX3_REG_HCR          0x000080 /* 命令寄存器 (Command Interface) */
#define CX3_REG_HCR_OP       0x000080 /* 命令操作码 */
#define CX3_REG_HCR_GO       0x000080 /* 命令 GO 位 */

/* 初始化段 */
#define CX3_REG_FW_VER       0x000000 /* 固件版本 */
#define CX3_REG_HW_VER       0x000004 /* 硬件版本 */
#define CX3_REG_CMD_IF_REV   0x000008 /* 命令接口版本 */

/* 命令寄存器位定义 */
#define CX3_HCR_GO_BIT       (1 << 31)
#define CX3_HCR_OPCODE_SHIFT 24
#define CX3_HCR_OPCODE_MASK  0x1F

/* 命令操作码 */
#define CX3_CMD_QUERY_FW     0x004
#define CX3_CMD_INIT_HCA     0x002
#define CX3_CMD_SW2HW_MPT    0x00D
#define CX3_CMD_HW2SW_MPT    0x00F
#define CX3_CMD_SW2HW_EQ     0x013
#define CX3_CMD_SW2HW_CQ     0x016
#define CX3_CMD_RST2INIT_QP  0x019
#define CX3_CMD_INIT2RTR_QP  0x01A
#define CX3_CMD_RTR2RTS_QP   0x01B
#define CX3_CMD_2RST_QP      0x01E

/* 事件队列 (EQ) 寄存器 */
#define CX3_REG_EQE_SIZE     0x000020
#define CX3_REG_EQ_DOORBELL  0x080018

/* 完成队列 (CQ) 寄存器 */
#define CX3_REG_CQ_DOORBELL  0x080100

/* 发送队列 (SQ) 寄存器 */
#define CX3_REG_SQ_DOORBELL  0x080108

/* 接收队列 (RQ) 寄存器 */
#define CX3_REG_RQ_DOORBELL  0x080110

/* ---- 描述符环大小 ---- */
#define CX3_NUM_TX_DESC    256
#define CX3_NUM_RX_DESC    256
#define CX3_RX_BUF_SIZE    2048
#define CX3_TX_BUF_SIZE    2048

/* ---- TX 描述符结构 (Send WQE 简化版, 64 bytes) ---- */
typedef struct {
    uint32_t ctrl[4];      /* 控制字段 */
    uint64_t addr;         /* 数据缓冲区地址 */
    uint32_t lkey;         /* 本地密钥 */
    uint32_t byte_count;   /* 数据长度 */
    uint32_t reserved[6];  /* 保留 */
} cx3_tx_desc_t;

/* ---- RX 描述符结构 (Receive WQE 简化版, 64 bytes) ---- */
typedef struct {
    uint32_t ctrl[4];      /* 控制字段 */
    uint64_t addr;         /* 数据缓冲区地址 */
    uint32_t lkey;         /* 本地密钥 */
    uint32_t byte_count;   /* 数据长度 */
    uint32_t reserved[6];  /* 保留 */
} cx3_rx_desc_t;

/* ---- 完成队列条目 ---- */
typedef struct {
    uint32_t vendor_err_syndrome;
    uint32_t qpn;
    uint32_t byte_cnt;
    uint16_t wqe_counter;
    uint8_t  opcode;
    uint8_t  owner;
} cx3_cqe_t;

/* ---- 公共接口 ---- */
void cx3_init(uint8_t bus, uint8_t dev, uint8_t func);
int cx3_send(net_interface_t *iface, const void *data, uint32_t len);
void cx3_poll(void);
void cx3_irq_handler(regs_t *regs);

/* ---- PCI 探测入口 ---- */
int connectx3_probe(void);

#endif /* CONNECTX3_H */
