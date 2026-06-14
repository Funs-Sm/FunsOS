#ifndef I219_H
#define I219_H

#include "net.h"
#include "stdint.h"
#include "kernel_types.h"

/* ---- PCI 设备 ID ---- */
#define I219_VENDOR_ID       0x8086
#define I219V_DEVICE_ID      0x15B8   /* I219-V    */
#define I219LM_DEVICE_ID     0x15B7   /* I219-LM   */
#define I219V2_DEVICE_ID     0x15D6   /* I219-V2   */
#define I219LM2_DEVICE_ID    0x15D7   /* I219-LM2  */

/* ---- 关键寄存器偏移 (相对于 MMIO base) ---- */
/* 控制与状态寄存器 */
#define I219_REG_CTRL        0x0000  /* Device Control Register      */
#define I219_REG_STATUS      0x0008  /* Device Status Register       */
#define I219_REG_EEC         0x0010  /* EEPROM/Flash Control Reg     */
#define I219_REG_EERD        0x0014  /* EEPROM Read                  */

/* 中断相关 */
#define I219_REG_ICR         0x00C0  /* Interrupt Cause Read         */
#define I219_REG_IMS         0x00D0  /* Interrupt Mask Set           */
#define I219_REG_IMC         0x00D8  /* Interrupt Mask Clear         */

/* 接收控制 */
#define I219_REG_RCTL        0x0100  /* RX Control                   */
#define I219_REG_FCTTV       0x0240  /* Flow Control Timer Value     */
#define I219_REG_FCRTL       0x0214  /* Flow Control RX Threshold Low*/
#define I219_REG_FCRTH       0x0218  /* Flow Control RX Threshold Hi */

/* 发送控制 */
#define I219_REG_TCTL        0x0400  /* TX Control                   */
#define I219_REG_TIPG        0x0410  /* TX Inter Packet Gap          */

/* RX 描述符环 */
#define I219_REG_RDBAL       0x2800  /* RX Desc Base Address Low     */
#define I219_REG_RDBAH       0x2804  /* RX Desc Base Address High    */
#define I219_REG_RDLEN       0x2808  /* RX Descriptor Length         */
#define I219_REG_RDH         0x2810  /* RX Descriptor Head           */
#define I219_REG_RDT         0x2818  /* RX Descriptor Tail           */
#define I219_REG_RXDCTL      0x028C  /* RX Descriptor Control        */

/* TX 描述符环 */
#define I219_REG_TDBAL       0x3800  /* TX Desc Base Address Low     */
#define I219_REG_TDBAH       0x3804  /* TX Desc Base Address High    */
#define I219_REG_TDLEN       0x3808  /* TX Descriptor Length         */
#define I219_REG_TDH         0x3810  /* TX Descriptor Head           */
#define I219_REG_TDT         0x3818  /* TX Descriptor Tail           */
#define I219_REG_TXDCTL      0x038C  /* TX Descriptor Control        */

/* MAC 地址 (RAL/RAH) */
#define I219_REG_RAL         0x5400  /* Receive Address Low          */
#define I219_REG_RAH         0x5404  /* Receive Address High         */

/* PHY 控制寄存器 (I219 特有) */
#define I219_REG_PHY_CTRL    0x5F00  /* PHY Control Register         */
#define I219_REG_FEXTNVM6    0x0010  /* Extended NVM Control 6       */

/* ---- CTRL 寄存器位定义 ---- */
#define I219_CTRL_RST        (1 << 31) /* SW Reset (Full)            */
#define I219_CTRL_LRST       (1 << 3)  /* Link Reset                 */
#define I219_CTRL_ASDE       (1 << 5)  /* Auto-Speed Detection Ena   */
#define I219_CTRL_SLU        (1 << 6)  /* Set Link Up                */
#define I219_CTRL_FRCSPD     (1 << 11) /* Force Speed               */
#define I219_CTRL_FRCDPLX    (1 << 12) /* Force Duplex              */
#define I219_CTRL_VME        (1 << 30) /* VLAN Mode Enable          */
#define I219_CTRL_PHY_RST    (1 << 28) /* PHY Reset                 */

/* ---- RCTL 位定义 ---- */
#define I219_RCTL_EN         (1 << 1)  /* Receiver Enable            */
#define I219_RCTL_BAM        (1 << 15) /* Broadcast Accept Mode     */
#define I219_RCTL_UPE        (1 << 3)  /* Unicast Promiscuous Enable */
#define I219_RCTL_MPE        (1 << 4)  /* Multicast Promiscuous Enable */
#define I219_RCTL_LBM_NONE   (0 << 6)  /* No Loopback                */
#define I219_RCTL_SECRC      (1 << 26) /* Strip CRC                 */
#define I219_RCTL_BSIZE_2048 (0 << 16) /* Buffer Size = 2048         */
#define I219_RCTL_BSIZE_4096 (1 << 16)
#define I219_RCTL_BSIZE_8192 (2 << 16)
#define I219_RCTL_BSIZE_16384 (3 << 16)

/* ---- TCTL 位定义 ---- */
#define I219_TCTL_EN         (1 << 1)  /* Transmitter Enable         */
#define I219_TCTL_PSP        (1 << 3)  /* Pad Short Packets          */
#define I219_TCTL_CT_MASK    (0xFF << 4) /* Collision Threshold      */
#define I219_TCTL_COLD_MASK  (0x3FF << 12) /* Collision Distance    */

/* ---- ICR / IMS 中断位 ---- */
#define I219_ICR_TXDW        (1 << 0)  /* TX Descriptor Written Back */
#define I219_ICR_TXQE        (1 << 1)  /* TX Queue Empty             */
#define I219_ICR_LSC         (1 << 2)  /* Link Status Change         */
#define I219_ICR_RXSEQ       (1 << 3)  /* RX Sequence Error          */
#define I219_ICR_RXDMT0      (1 << 4)  /* RX Desc Min Threshold Hit   */
#define I219_ICR_RXO         (1 << 6)  /* RX Overflow                */
#define I219_ICR_RXT0        (1 << 7)  /* RX Timer Interrupt         */
#define I219_ICR_MDAC        (1 << 19) /* MDIO Access Complete       */
#define I219_ICR_RXCFG       (1 << 23) /* Receiving / Config         */
#define I219_ICR_GPI(x)      (1 << ((x)+20)) /* General Purpose Int  */
#define I219_ICR_ALL         0xFFFFFFFF

/* ---- TX/RX 描述符状态位 ---- */
#define I219_TXDESC_STATUS_DD  0x01  /* Descriptor Done             */
#define I219_RXDESC_STATUS_DD  0x01  /* Descriptor Done             */
#define I219_RXDESC_STATUS_EOP 0x02  /* End of Packet               */

/* ---- 描述符环大小 ---- */
#define I219_NUM_TX_DESC    256
#define I219_NUM_RX_DESC    256
#define I219_RX_BUF_SIZE    2048
#define I219_TX_BUF_SIZE    2048

/* ---- TX Descriptor 结构 (与 E1000/I225 相同, 16 bytes) ---- */
typedef struct {
    uint64_t addr;       /* buffer 物理地址               */
    uint16_t length;     /* 包长度                       */
    uint16_t cso;        /* Checksum Offset               */
    uint8_t cmd;         /* Command bits (RS, IC, etc.)   */
    uint8_t status;      /* 状态标志                     */
    uint8_t css;         /* Checksum Start                */
    uint16_t special;    /* Special field / VLAN tag      */
} i219_tx_desc_t;

/* ---- RX Descriptor 结构 (与 E1000/I225 相同, 16 bytes) ---- */
typedef struct {
    uint64_t addr;       /* buffer 物理地址               */
    uint16_t length;     /* 包长度                       */
    uint16_t checksum;   /* 校验和                       */
    uint8_t status;      /* 状态标志 (DD, EOP, etc.)      */
    uint8_t errors;      /* 错误标志                     */
    uint16_t special;    /* 特殊字段 / VLAN 标签          */
} i219_rx_desc_t;

/* ---- 公共接口 ---- */
void i219_init(uint8_t bus, uint8_t dev, uint8_t func);
int i219_send(net_interface_t *iface, const void *data, uint32_t len);
void i219_poll(void);
void i219_irq_handler(regs_t *regs);

/* ---- PCI 探测入口 ---- */
int i219_probe(void);

#endif /* I219_H */
