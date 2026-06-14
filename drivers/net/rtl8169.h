#ifndef RTL8169_H
#define RTL8169_H

#include "net.h"
#include "stdint.h"
#include "kernel_types.h"

/* ---- PCI 设备 ID ---- */
#define RTL8169_VENDOR_ID    0x10EC
#define RTL8169_DEVICE_ID    0x8169
#define RTL8168_DEVICE_ID    0x8168
#define RTL8167_DEVICE_ID    0x8167

/* ---- 关键寄存器偏移 (相对于 MMIO base) ---- */
#define RTL8169_REG_TNPDS    0x20   /* Tx Normal Priority Desc Low  */
#define RTL8169_REG_TNPPS    0x24   /* Tx Normal Priority Desc High */
#define RTL8169_REG_RDSAR    0xE4   /* Rx Desc Start Address        */
#define RTL8169_REG_CMD      0x37   /* Command Register             */
#define RTL8169_REG_IMR      0x3C   /* Interrupt Mask Register      */
#define RTL8169_REG_ISR      0x3E   /* Interrupt Status Register    */
#define RTL8169_REG_TCR      0x40   /* Tx Configuration Register    */
#define RTL8169_REG_RCR      0x44   /* Rx Configuration Register    */

/* ---- MAC 地址寄存器 (IDR0-IDR5) ---- */
#define RTL8169_REG_IDR0     0x00

/* ---- CMD 寄存器位定义 ---- */
#define RTL8169_CMD_RST       (1 << 4)  /* Reset          */
#define RTL8169_CMD_RE        (1 << 2)  /* Rx Enable      */
#define RTL8169_CMD_TE        (1 << 1)  /* Tx Enable      */

/* ---- ISR 中断位 ---- */
#define RTL8169_ISR_ROK       (1 << 1)  /* RX OK          */
#define RTL8169_ISR_TOK       (1 << 2)  /* TX OK          */
#define RTL8169_ISR_RER       (1 << 1)  /* RX Error       */
#define RTL8169_ISR_TER       (1 << 2)  /* TX Error       */

/* ---- RCR 接收配置位 ---- */
#define RTL8169_RCR_ACCEPT_PHYS_MATCH  (1 << 3)
#define RTL8169_RCR_ACCEPT_MULTICAST   (1 << 2)
#define RTL8169_RCR_ACCEPT_BROADCAST   (1 << 0)
#define RTL8169_RCR_WRAP              (1 << 7)

/* ---- TCR 发送配置 ---- */
#define RTL8169_TCR_MAX_DMA_2048     (6 << 8)

/* ---- TX Descriptor 状态/选项位 ---- */
#define RTL8169_TX_OWN       (1 << 31) /* 描述符所有权: 0=host, 1=nic */
#define RTL8169_TX_FS        (1 << 29) /* First Segment */
#define RTL8169_TX_LS        (1 << 28) /* Last Segment  */
#define RTL8169_TX_EOR       (1 << 27) /* End of Ring   */
#define RTL8169_TX_LGSEN     (1 << 26) /* Large Send    */

/* ---- RX Descriptor 状态位 ---- */
#define RTL8169_RX_OWN       (1 << 31) /* 描述符所有权: 0=host, 1=nic */
#define RTL8169_RX_RES       (1 << 30) /* Receive OK    */
#define RTL8169_RX_EOR       (1 << 25) /* End of Ring   */

/* 描述符环大小 */
#define RTL8169_NUM_TX_DESC  256
#define RTL8169_NUM_RX_DESC  256
#define RTL8169_RX_BUF_SIZE  2048
#define RTL8169_TX_BUF_SIZE  2048

/* TX Descriptor 结构 (16 bytes) */
typedef struct {
    uint64_t addr;       /* buffer 物理地址           */
    uint32_t opts;       /* ownership, packet size, flags */
    uint32_t reserved;
} rtl8169_tx_desc_t;

/* RX Descriptor 结构 (16 bytes) */
typedef struct {
    uint64_t addr;       /* buffer 物理地址           */
    uint32_t opts;       /* status, error flags, packet size */
    uint32_t reserved;
} rtl8169_rx_desc_t;

/* ---- 公共接口 ---- */
void rtl8169_init(uint8_t bus, uint8_t dev, uint8_t func);
int rtl8169_send(net_interface_t *iface, const void *data, uint32_t len);
void rtl8169_poll(void);
void rtl8169_irq_handler(regs_t *regs);

/* ---- PCI 探测入口 ---- */
int rtl8169_probe(void);

#endif /* RTL8169_H */
