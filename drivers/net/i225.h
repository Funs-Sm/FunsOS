#ifndef I225_H
#define I225_H

#include "net.h"
#include "stdint.h"
#include "kernel_types.h"

/* ---- PCI 设备 ID ---- */
#define I225_VENDOR_ID       0x8086
#define I225V_DEVICE_ID      0x15F3   /* I225-V   */
#define I225LM_DEVICE_ID     0x15F0   /* I225-LM  */
#define I225IT_DEVICE_ID     0x15F1   /* I225-IT  */

/* ---- 关键寄存器偏移 (相对于 MMIO base) ---- */
/* 控制与状态寄存器 */
#define I225_REG_CTRL        0x0000  /* Device Control Register      */
#define I225_REG_STATUS      0x0008  /* Device Status Register       */
#define I225_REG_EEC         0x0010  /* EEPROM/Flash Control Reg     */
#define I225_REG_EERD        0x0014  /* EEPROM Read                  */

/* 中断相关 */
#define I225_REG_ICR         0x00C0  /* Interrupt Cause Read         */
#define I225_REG_IMS         0x00D0  /* Interrupt Mask Set           */
#define I225_REG_IMC         0x00D8  /* Interrupt Mask Clear         */

/* 接收控制 */
#define I225_REG_RCTL        0x0100  /* RX Control                   */
#define I225_REG_FCTTV       0x0240  /* Flow Control Timer Value     */
#define I225_REG_FCRTL       0x0214  /* Flow Control RX Threshold Low*/
#define I225_REG_FCRTH       0x0218  /* Flow Control RX Threshold Hi */

/* 发送控制 */
#define I225_REG_TCTL        0x0400  /* TX Control                   */
#define I225_REG_TIPG        0x0410  /* TX Inter Packet Gap          */

/* RX 描述符环 */
#define I225_REG_RDBAL       0x2800  /* RX Desc Base Address Low     */
#define I225_REG_RDBAH       0x2804  /* RX Desc Base Address High    */
#define I225_REG_RDLEN       0x2808  /* RX Descriptor Length         */
#define I225_REG_RDH         0x2810  /* RX Descriptor Head           */
#define I225_REG_RDT         0x2818  /* RX Descriptor Tail           */
#define I225_REG_RXDCTL      0x028C  /* RX Descriptor Control        */

/* TX 描述符环 */
#define I225_REG_TDBAL       0x3800  /* TX Desc Base Address Low     */
#define I225_REG_TDBAH       0x3804  /* TX Desc Base Address High    */
#define I225_REG_TDLEN       0x3808  /* TX Descriptor Length         */
#define I225_REG_TDH         0x3810  /* TX Descriptor Head           */
#define I225_REG_TDT         0x3818  /* TX Descriptor Tail           */
#define I225_REG_TXDCTL      0x038C  /* TX Descriptor Control        */

/* MAC 地址 (RAL/RAH) */
#define I225_REG_RAL         0x5400  /* Receive Address Low          */
#define I225_REG_RAH         0x5404  /* Receive Address High         */

/* ---- CTRL 寄存器位定义 ---- */
#define I225_CTRL_RST        (1 << 31) /* SW Reset (Full)            */
#define I225_CTRL_LRST       (1 << 3)  /* Link Reset                 */
#define I225_CTRL_ASDE       (1 << 5)  /* Auto-Speed Detection Ena   */
#define I225_CTRL_SLU        (1 << 6)  /* Set Link Up                */
#define I225_CTRL_FRCSPD     (1 << 11) /* Force Speed               */
#define I225_CTRL_FRCDPLX    (1 << 12) /* Force Duplex              */
#define I225_CTRL_VME        (1 << 30) /* VLAN Mode Enable          */

/* ---- RCTL 位定义 ---- */
#define I225_RCTL_EN         (1 << 1)  /* Receiver Enable            */
#define I225_RCTL_BAM        (1 << 15) /* Broadcast Accept Mode     */
#define I225_RCTL_UPE        (1 << 3)  /* Unicast Promiscuous Enable */
#define I225_RCTL_MPE        (1 << 4)  /* Multicast Promiscuous Enable */
#define I225_RCTL_LBM_NONE   (0 << 6)  /* No Loopback                */
#define I225_RCTL_SECRC      (1 << 26) /* Strip CRC                 */
#define I225_RCTL_BSIZE_2048 (0 << 16) /* Buffer Size = 2048         */
#define I225_RCTL_BSIZE_4096 (1 << 16)
#define I225_RCTL_BSIZE_8192 (2 << 16)
#define I225_RCTL_BSIZE_16384 (3 << 16)

/* ---- TCTL 位定义 ---- */
#define I225_TCTL_EN         (1 << 1)  /* Transmitter Enable         */
#define I225_TCTL_PSP        (1 << 3)  /* Pad Short Packets          */
#define I225_TCTL_CT_MASK    (0xFF << 4) /* Collision Threshold      */
#define I225_TCTL_COLD_MASK  (0x3FF << 12) /* Collision Distance    */

/* ---- ICR / IMS 中断位 ---- */
#define I225_ICR_TXDW        (1 << 0)  /* TX Descriptor Written Back */
#define I225_ICR_TXQE        (1 << 1)  /* TX Queue Empty             */
#define I225_ICR_LSC         (1 << 2)  /* Link Status Change         */
#define I225_ICR_RXSEQ       (1 << 3)  /* RX Sequence Error          */
#define I225_ICR_RXDMT0      (1 << 4)  /* RX Desc Min Threshold Hit   */
#define I225_ICR_RXO         (1 << 6)  /* RX Overflow                */
#define I225_ICR_RXT0        (1 << 7)  /* RX Timer Interrupt         */
#define I225_ICR_MDAC        (1 << 19) /* MDIO Access Complete       */
#define I225_ICR_RXCFG       (1 << 23) /* Receiving / Config         */
#define I225_ICR_GPI(x)      (1 << ((x)+20)) /* General Purpose Int  */
#define I225_ICR_ALL         0xFFFFFFFF

/* ---- TX/RX 描述符状态位 ---- */
#define I225_TXDESC_STATUS_DD  0x01  /* Descriptor Done             */
#define I225_RXDESC_STATUS_DD  0x01  /* Descriptor Done             */
#define I225_RXDESC_STATUS_EOP 0x02  /* End of Packet               */

/* ---- 描述符环大小 ---- */
#define I225_NUM_TX_DESC    256
#define I225_NUM_RX_DESC    256
#define I225_RX_BUF_SIZE    2048
#define I225_TX_BUF_SIZE    2048

/* ---- TX Descriptor 结构 (与 E1000 相同, 16 bytes) ---- */
typedef struct {
    uint64_t addr;       /* buffer 物理地址               */
    uint16_t length;     /* 包长度                       */
    uint16_t cso;        /* Checksum Offset               */
    uint8_t cmd;         /* Command bits (RS, IC, etc.)   */
    uint8_t status;      /* 状态标志                     */
    uint8_t css;         /* Checksum Start                */
    uint16_t special;    /* Special field / VLAN tag      */
} i225_tx_desc_t;

/* ---- RX Descriptor 结构 (与 E1000 相同, 16 bytes) ---- */
typedef struct {
    uint64_t addr;       /* buffer 物理地址               */
    uint16_t length;     /* 包长度                       */
    uint16_t checksum;   /* 校验和                       */
    uint8_t status;      /* 状态标志 (DD, EOP, etc.)      */
    uint8_t errors;      /* 错误标志                     */
    uint16_t special;    /* 特殊字段 / VLAN 标签          */
} i225_rx_desc_t;

/* ---- 公共接口 ---- */
void i225_init(uint8_t bus, uint8_t dev, uint8_t func);
int i225_send(net_interface_t *iface, const void *data, uint32_t len);
void i225_poll(void);
void i225_irq_handler(regs_t *regs);

/* ---- PCI 探测入口 ---- */
int i225_probe(void);

#endif /* I225_H */
