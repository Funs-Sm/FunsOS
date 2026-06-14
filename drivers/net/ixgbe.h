#ifndef IXGBE_H
#define IXGBE_H

#include "net.h"
#include "stdint.h"
#include "kernel_types.h"

/* ---- PCI 设备 ID ---- */
#define IXGBE_VENDOR_ID             0x8086
#define IXGBE_DEV_ID_82599ES        0x10FB  /* 82599ES 10 Gigabit Ethernet    */
#define IXGBE_DEV_ID_82599EB        0x10F7  /* 82599EB (Dual Port)            */
#define IXGBE_DEV_ID_82599EN        0x10F8  /* 82599EN                        */
#define IXGBE_DEV_ID_82599_KX4      0x10F9  /* 82599 KR/KX4                   */
#define IXGBE_DEV_ID_82599_T3_LOM   0x151C  /* 82599 T3 LOM                   */
#define IXGBE_DEV_ID_82599_SFP      0x10FB  /* 82599 SFP+                     */

/* ---- IXGBE 寄存器定义 ---- */
/* 控制与状态寄存器 */
#define IXGBE_REG_CTRL              0x00000 /* Device Control                 */
#define IXGBE_REG_STATUS            0x00008 /* Device Status                  */
#define IXGBE_REG_EEC               0x00010 /* EEPROM Control                 */
#define IXGBE_REG_EERD              0x00014 /* EEPROM Read                    */
#define IXGBE_REG_CTRL_EXT          0x00018 /* Extended Device Control        */
#define IXGBE_REG_MDIO              0x00020 /* MDIO Control                   */
#define IXGBE_REG_LINKS             0x04200 /* Link Status                    */
#define IXGBE_REG_LINKS2            0x04300 /* Link Status 2                  */

/* 中断相关 */
#define IXGBE_REG_EICR              0x00800 /* Extended Interrupt Cause Read  */
#define IXGBE_REG_EICS              0x00808 /* Extended Interrupt Cause Set   */
#define IXGBE_REG_EIMS              0x00880 /* Extended Interrupt Mask Set    */
#define IXGBE_REG_EIMC              0x00888 /* Extended Interrupt Mask Clear  */
#define IXGBE_REG_EIAC              0x00810 /* Extended Interrupt Auto Clear  */
#define IXGBE_REG_EIAM              0x00890 /* Extended Interrupt Auto Mask   */
#define IXGBE_REG_EITR(n)           (0x00820 + ((n) * 4)) /* EITR for queue n */

/* 中断 Throttling */
#define IXGBE_REG_ITR               0x000C4

/* 接收控制 */
#define IXGBE_REG_RXCTRL            0x03000 /* RX Control                     */
#define IXGBE_REG_DROPEN            0x03004 /* Drop Enable                    */
#define IXGBE_REG_FCTRL             0x05080 /* Filter Control                 */
#define IXGBE_REG_HLREG0            0x04500 /* High/Low Watermark             */
#define IXGBE_REG_FCRTL_82599(n)    (0x03220 + ((n) * 4)) /* Flow Ctrl RX Threshold Low */
#define IXGBE_REG_FCRTH_82599(n)    (0x03260 + ((n) * 4)) /* Flow Ctrl RX Threshold High */

/* 每队列 RX 寄存器 */
#define IXGBE_REG_RDBAL(n)          (0x01000 + ((n) * 0x40)) /* RX Desc Base Low       */
#define IXGBE_REG_RDBAH(n)          (0x01004 + ((n) * 0x40)) /* RX Desc Base High      */
#define IXGBE_REG_RDLEN(n)          (0x01008 + ((n) * 0x40)) /* RX Desc Length         */
#define IXGBE_REG_RDH(n)            (0x01010 + ((n) * 0x40)) /* RX Desc Head           */
#define IXGBE_REG_RDT(n)            (0x01018 + ((n) * 0x40)) /* RX Desc Tail           */
#define IXGBE_REG_RXDCTL(n)         (0x01028 + ((n) * 0x40)) /* RX Desc Control        */
#define IXGBE_REG_SRRCTL(n)         (0x02100 + ((n) * 4))    /* Split RX Control       */
#define IXGBE_REG_DCA_RXCTRL(n)     (0x0100C + ((n) * 0x40)) /* DCA RX Control         */

/* 每队列 TX 寄存器 */
#define IXGBE_REG_TDBAL(n)          (0x06000 + ((n) * 0x40)) /* TX Desc Base Low       */
#define IXGBE_REG_TDBAH(n)          (0x06004 + ((n) * 0x40)) /* TX Desc Base High      */
#define IXGBE_REG_TDLEN(n)          (0x06008 + ((n) * 0x40)) /* TX Desc Length         */
#define IXGBE_REG_TDH(n)            (0x06010 + ((n) * 0x40)) /* TX Desc Head           */
#define IXGBE_REG_TDT(n)            (0x06018 + ((n) * 0x40)) /* TX Desc Tail           */
#define IXGBE_REG_TXDCTL(n)         (0x06028 + ((n) * 0x40)) /* TX Desc Control        */
#define IXGBE_REG_TXCTRL(n)         (0x0600C + ((n) * 0x40)) /* TX Control             */
#define IXGBE_REG_DCA_TXCTRL(n)     (0x06014 + ((n) * 0x40)) /* DCA TX Control         */

/* 全局 TX 寄存器 */
#define IXGBE_REG_DTXMXSZRQ          0x08100 /* TX Max Queue Size               */
#define IXGBE_REG_RTTDCS             0x04900 /* RX Transmit Traffic Data Class  */
#define IXGBE_REG_RTTPCS             0x04930 /* RX Transmit Traffic Prior Class */
#define IXGBE_REG_RTTDT1C            0x04908 /* RX Transmit Traffic Data T1 Cnt  */
#define IXGBE_REG_RTTDT1S            0x04904 /* RX Transmit Traffic Data T1 Size */

/* MAC 地址 */
#define IXGBE_REG_RAL(n)            (0x0A200 + ((n) * 8))  /* Receive Addr Low      */
#define IXGBE_REG_RAH(n)            (0x0A204 + ((n) * 8))  /* Receive Addr High     */

/* VLAN 相关 */
#define IXGBE_REG_VLNCTRL           0x05000 /* VLAN Control                   */
#define IXGBE_REG_VET               0x05080 /* VLAN EtherType                 */

/* 统计寄存器 */
#define IXGBE_REG_CRCERRS           0x04000 /* CRC Error Count                */
#define IXGBE_REG_ILLERRC           0x04004 /* Illegal Byte Error Count       */
#define IXGBE_REG_ERRBC             0x04008 /* Error Byte Count               */
#define IXGBE_REG_MPC               0x04010 /* Missed Packet Count            */
#define IXGBE_REG_MLFC              0x04034 /* MAC Local Fault Count          */
#define IXGBE_REG_MRFC              0x04038 /* MAC Remote Fault Count         */
#define IXGBE_REG_RLEC              0x04040 /* Receive Length Error Count     */
#define IXGBE_REG_LXONTXC           0x04048 /* Link XON TX Count              */
#define IXGBE_REG_LXONRXC           0x0404C /* Link XON RX Count              */
#define IXGBE_REG_LXOFFTXC          0x04050 /* Link XOFF TX Count             */
#define IXGBE_REG_LXOFFRXC          0x04054 /* Link XOFF RX Count             */
#define IXGBE_REG_PRC64             0x0405C /* Pkts Received (64 bytes)       */
#define IXGBE_REG_PRC127            0x04060 /* Pkts Received (65-127)         */
#define IXGBE_REG_PRC255            0x04064 /* Pkts Received (128-255)        */
#define IXGBE_REG_PRC511            0x04068 /* Pkts Received (256-511)        */
#define IXGBE_REG_PRC1023           0x0406C /* Pkts Received (512-1023)       */
#define IXGBE_REG_PRC1522           0x04070 /* Pkts Received (1024-1522)      */
#define IXGBE_REG_GPRC              0x04074 /* Good Packets Received Count    */
#define IXGBE_REG_BPRC              0x04078 /* Broadcast Pkts Received        */
#define IXGBE_REG_MPRC              0x0407C /* Multicast Pkts Received        */
#define IXGBE_REG_GPTC              0x04080 /* Good Packets Transmitted Count */
#define IXGBE_REG_GORCL             0x04088 /* Good Octets Received Low       */
#define IXGBE_REG_GORCH             0x0408C /* Good Octets Received High      */
#define IXGBE_REG_GOTCL             0x04090 /* Good Octets TX Low             */
#define IXGBE_REG_GOTCH             0x04094 /* Good Octets TX High            */
#define IXGBE_REG_RNBC              0x040A0 /* Receive No Buffers Count       */
#define IXGBE_REG_RUC               0x040A4 /* Receive Undersize Count        */
#define IXGBE_REG_RFC               0x040A8 /* Receive Fragment Count         */
#define IXGBE_REG_ROC               0x040AC /* Receive Oversize Count         */
#define IXGBE_REG_RJC               0x040B0 /* Receive Jabber Count           */
#define IXGBE_REG_TORL              0x040C0 /* Total Octets Received Low      */
#define IXGBE_REG_TORH              0x040C4 /* Total Octets Received High     */
#define IXGBE_REG_TOTL              0x040C8 /* Total Octets TX Low            */
#define IXGBE_REG_TOTH              0x040CC /* Total Octets TX High           */
#define IXGBE_REG_TPR               0x040D0 /* Total Packets Received         */
#define IXGBE_REG_TPT               0x040D4 /* Total Packets Transmitted      */
#define IXGBE_REG_PTC64             0x040D8 /* Pkts TX (64 bytes)             */
#define IXGBE_REG_PTC127            0x040DC /* Pkts TX (65-127)               */
#define IXGBE_REG_PTC255            0x040E0 /* Pkts TX (128-255)              */
#define IXGBE_REG_PTC511            0x040E4 /* Pkts TX (256-511)              */
#define IXGBE_REG_PTC1023           0x040E8 /* Pkts TX (512-1023)             */
#define IXGBE_REG_PTC1522           0x040EC /* Pkts TX (1024-1522)            */
#define IXGBE_REG_MPTC              0x040F0 /* Multicast Pkts TX              */
#define IXGBE_REG_BPTC              0x040F4 /* Broadcast Pkts TX              */
#define IXGBE_REG_XEC               0x04120 /* Xsum Errors Count              */
#define IXGBE_REG_RQSMR(n)          (0x02300 + ((n) * 4))  /* RX Queue Stat Map      */
#define IXGBE_REG_TQSMR(n)          (0x07300 + ((n) * 4))  /* TX Queue Stat Map      */

/* RSS 相关 */
#define IXGBE_REG_MRQC              0x05818 /* Multiple Receive Queues Command */
#define IXGBE_REG_RETA(n)           (0x05C00 + ((n) * 4))  /* RSS Redirection Table  */
#define IXGBE_REG_RSSRK(n)          (0x05C80 + ((n) * 4))  /* RSS Random Key         */
#define IXGBE_REG_PFVFRE            0x05C50 /* PF/VF RSS Rule Enable          */

/* 流导向 (Flow Director / ATR) */
#define IXGBE_REG_FDIRCTRL          0x0EE00 /* Flow Director Control          */
#define IXGBE_REG_FDIRCMD           0x0EE04 /* Flow Director Command          */
#define IXGBE_REG_FDIRLEN           0x0EE08 /* Flow Director Length           */
#define IXGBE_REG_FDIRHASH          0x0EE0C /* Flow Director Hash             */
#define IXGBE_REG_FDIRSMPL          0x0EE20 /* Flow Director Sample           */

/* DCB (Data Center Bridging) */
#define IXGBE_REG_RTRPCS            0x04940 /* RX Transmit Traffic Prior Class*/
#define IXGBE_REG_RTRUP2TC          0x04D00 /* RX User Priority to TC         */
#define IXGBE_REG_RTTUP2TC          0x0C800 /* TX User Priority to TC         */
#define IXGBE_REG_RXPBTHRESH(n)     (0x04950 + ((n) * 4))  /* RX PB Threshold       */
#define IXGBE_REG_RXFIFOSIZE        0x0C840 /* RX FIFO Size                   */
#define IXGBE_REG_RXPBSIZE(n)       (0x04900 + ((n) * 4))  /* RX Packet Buffer Size */
#define IXGBE_REG_TXPBSIZE(n)       (0x0CC00 + ((n) * 4))  /* TX Packet Buffer Size */

/* 电源管理 */
#define IXGBE_REG_WUC               0x05800 /* Wake Up Control                */
#define IXGBE_REG_WUFC              0x05808 /* Wake Up Filter Control         */
#define IXGBE_REG_WUS               0x05810 /* Wake Up Status                 */

/* MSI-X 相关 */
#define IXGBE_REG_MSIX_TABLE        0x00000 /* MSI-X Table offset in BAR3     */
#define IXGBE_REG_MSIX_PBA          0x02000 /* MSI-X PBA offset               */
#define IXGBE_REG_IVAR              0x00900 /* Interrupt Vector Allocation    */
#define IXGBE_REG_IVAR_MISC         0x00A00 /* Misc Interrupt Vector          */

/* ---- CTRL 寄存器位定义 ---- */
#define IXGBE_CTRL_LRST             (1 << 3)  /* Link Reset                    */
#define IXGBE_CTRL_SLU              (1 << 6)  /* Set Link Up                   */
#define IXGBE_CTRL_RST              (1 << 26) /* Device Reset                  */
#define IXGBE_CTRL_VME              (1 << 30) /* VLAN Mode Enable              */
#define IXGBE_CTRL_TFCE             (1 << 27) /* TX Flow Control Enable        */
#define IXGBE_CTRL_RFCE             (1 << 28) /* RX Flow Control Enable        */

/* ---- CTRL_EXT 扩展控制位 ---- */
#define IXGBE_CTRL_EXT_NS_DIS       (1 << 8)  /* No Snoop Disable              */
#define IXGBE_CTRL_EXT_RO_DIS       (1 << 9)  /* Relaxed Ordering Disable      */
#define IXGBE_CTRL_EXT_DRV_LOAD     (1 << 28) /* Driver Loaded                 */

/* ---- LINKS 寄存器位定义 ---- */
#define IXGBE_LINKS_UP              (1 << 30) /* Link Up                       */
#define IXGBE_LINKS_SPEED_MASK      (0xF << 8)
#define IXGBE_LINKS_SPEED_100M      (1 << 8)
#define IXGBE_LINKS_SPEED_1G        (2 << 8)
#define IXGBE_LINKS_SPEED_10G       (4 << 8)

/* ---- EIMS / EICR 中断位 ---- */
#define IXGBE_EICR_LSC              (1 << 2)  /* Link Status Change            */
#define IXGBE_EICR_RX_QUEUE(n)      (1 << (n)) /* RX Queue n                   */
#define IXGBE_EICR_TX_QUEUE(n)      (1 << ((n) + 8)) /* TX Queue n              */
#define IXGBE_EICR_OTHER            (1 << 7)  /* Other interrupt               */
#define IXGBE_EICR_GPI(n)           (1 << ((n) + 16))
#define IXGBE_EICR_MDIO             (1 << 20) /* MDIO Done                      */

/* ---- RXDCTL / TXDCTL ---- */
#define IXGBE_RXDCTL_ENABLE         (1 << 25) /* RX Queue Enable               */
#define IXGBE_TXDCTL_ENABLE         (1 << 25) /* TX Queue Enable               */

/* ---- SRRCTL ---- */
#define IXGBE_SRRCTL_BSIZE_PACKET   0x00000000 /* Receive into packet buffer    */
#define IXGBE_SRRCTL_BSIZE_HDRS     0x00000001 /* Receive into header buffer    */
#define IXGBE_SRRCTL_DESCTYPE_ADV_ONCE 0x00000000 /* Adv desc one buffer          */
#define IXGBE_SRRCTL_DESCTYPE_ADV_SPLIT 0x00000004 /* Adv desc split buffer       */
#define IXGBE_SRRCTL_DESCTYPE_LEGACY    0x00000008 /* Legacy descriptor           */
#define IXGBE_SRRCTL_DROP_EN        (1 << 28) /* Drop Enable                   */

/* ---- RXCTRL ---- */
#define IXGBE_RXCTRL_RXEN           (1 << 0)  /* RX Enable                     */
#define IXGBE_RXCTRL_DMBYPS         (1 << 2)  /* Descriptor Monitor Bypass     */

/* ---- FCRTL / FCRTH ---- */
#define IXGBE_FCRTL_XONE           0x01000000 /* XON Enable                    */

/* ---- MRQC ---- */
#define IXGBE_MRQC_RSS_ENABLE       0x00000001 /* RSS Enable                    */
#define IXGBE_MRQC_RSS_FIELD_IPV4      0x00010000
#define IXGBE_MRQC_RSS_FIELD_IPV4_TCP  0x00020000
#define IXGBE_MRQC_RSS_FIELD_IPV6      0x00100000
#define IXGBE_MRQC_RSS_FIELD_IPV6_TCP  0x00200000
#define IXGBE_MRQC_MULTIPLE_RSS    0x00000002

/* ---- VLNCTRL ---- */
#define IXGBE_VLNCTRL_VFE           (1 << 30) /* VLAN Filter Enable            */
#define IXGBE_VLNCTRL_VME           (1 << 31) /* VLAN Mode Enable              */

/* ---- FDIRCTRL ---- */
#define IXGBE_FDIRCTRL_INIT_DONE    (1 << 0)  /* Init Done                     */
#define IXGBE_FDIRCTRL_PERFECT_MATCH (1 << 2)/* Perfect Match Filter          */
#define IXGBE_FDIRCTRL_DROP_NO_MATCH (1 << 4)/* Drop if no match              */
#define IXGBE_FDIRCTRL_FLEX         (1 << 5)  /* Flexible Payload              */
#define IXGBE_FDIRCTRL_REPORT_STATUS (1 << 6)/* Report Status                 */
#define IXGBE_FDIRCTRL_PBALLOC(n)   ((n) << 8)

/* ---- 每队列描述符数 ---- */
#define IXGBE_NUM_QUEUES            4         /* Number of TX/RX queue pairs    */
#define IXGBE_NUM_TX_DESC           512       /* TX descriptors per queue       */
#define IXGBE_NUM_RX_DESC           512       /* RX descriptors per queue       */
#define IXGBE_RX_BUF_SIZE           2048      /* Default RX buffer              */
#define IXGBE_JUMBO_BUF_SIZE        9216      /* Jumbo frame buffer             */
#define IXGBE_TX_BUF_SIZE           9216      /* TX buffer for jumbo            */

/* ---- 高级 TX Descriptor (16 bytes) ---- */
typedef struct __attribute__((packed)) {
    uint64_t addr;       /* Buffer physical address              */
    uint64_t reserved;   /* Reserved                             */
} ixgbe_tx_desc_t;

/* ---- 高级 RX Descriptor (16 bytes) ---- */
typedef struct __attribute__((packed)) {
    uint64_t addr;       /* Buffer physical address              */
    uint64_t reserved;   /* Reserved                             */
} ixgbe_rx_desc_t;

/* ---- TX Descriptor 写回 (主机读取) ---- */
typedef struct __attribute__((packed)) {
    uint32_t rss;        /* RSS hash                             */
    uint16_t length;     /* Descriptor byte count                */
    uint8_t  vlan;       /* VLAN header                          */
    uint8_t  hlen_flags; /* Header length + flags                */
    uint8_t  status;     /* DD, EOP, etc.                        */
    uint8_t  errors;     /* Error flags                          */
    uint16_t special;    /* Special field                        */
} ixgbe_tx_wb_t;

/* ---- RX Descriptor 写回 (主机读取) ---- */
typedef struct __attribute__((packed)) {
    uint32_t rss;        /* RSS hash                             */
    uint16_t length;     /* Packet length                        */
    uint16_t vlan;       /* VLAN tag                             */
    uint8_t  hlen_flags; /* Header length + flags                */
    uint8_t  status;     /* DD, EOP, IXSM, VP, etc.              */
    uint8_t  errors;     /* Error flags                          */
    uint16_t special;    /* Special field                        */
} ixgbe_rx_wb_t;

/* ---- TX 写回状态位 ---- */
#define IXGBE_TX_WB_DD              (1 << 0)  /* Descriptor Done               */
#define IXGBE_TX_WB_EOP             (1 << 1)  /* End of Packet                 */

/* ---- RX 写回状态位 ---- */
#define IXGBE_RX_WB_DD              (1 << 0)  /* Descriptor Done               */
#define IXGBE_RX_WB_EOP             (1 << 1)  /* End of Packet                 */
#define IXGBE_RX_WB_VP              (1 << 3)  /* VLAN Packet                   */
#define IXGBE_RX_WB_IXSM            (1 << 4)  /* IP Checksum Valid             */
#define IXGBE_RX_WB_TCPCS           (1 << 5)  /* TCP Checksum Valid            */
#define IXGBE_RX_WB_IPCS            (1 << 6)  /* IP Checksum Valid             */
#define IXGBE_RX_WB_PIF             (1 << 7)  /* Passed Inexact Filter         */

/* ---- RX 错误位 ---- */
#define IXGBE_RX_ERROR_CE           (1 << 0)  /* CRC Error                     */
#define IXGBE_RX_ERROR_SE           (1 << 1)  /* Symbol Error                  */
#define IXGBE_RX_ERROR_PE           (1 << 2)  /* Packet Error                  */
#define IXGBE_RX_ERROR_IPE          (1 << 3)  /* IP Checksum Error             */
#define IXGBE_RX_ERROR_TCPE         (1 << 4)  /* TCP/UDP Checksum Error        */

/* ---- TX 选项位 (写在 addr 字段的高位) ---- */
#define IXGBE_TX_DESC_CMD_RS        (1ULL << 32) /* Report Status              */
#define IXGBE_TX_DESC_CMD_IFCS      (1ULL << 33) /* Insert FCS                 */
#define IXGBE_TX_DESC_CMD_TSE       (1ULL << 34) /* TCP Segmentation Enable    */
#define IXGBE_TX_DESC_CMD_IC        (1ULL << 35) /* Insert Checksum            */
#define IXGBE_TX_DESC_CMD_EOP       (1ULL << 36) /* End of Packet              */
#define IXGBE_TX_DESC_CMD_VLE       (1ULL << 37) /* VLAN Enable                */
#define IXGBE_TX_OPT_RS             (1ULL << 32) /* Report Status              */
#define IXGBE_TX_OPT_IFCS           (1ULL << 33) /* Insert FCS                 */
#define IXGBE_TX_OPT_TSE            (1ULL << 34) /* TCP Segmentation Enable    */
#define IXGBE_TX_OPT_IC             (1ULL << 35) /* Insert Checksum            */
#define IXGBE_TX_OPT_EOP            (1ULL << 36) /* End of Packet              */
#define IXGBE_TX_OPT_VLE            (1ULL << 37) /* VLAN Enable                */

/* ---- 公共接口 ---- */
void ixgbe_init(uint8_t bus, uint8_t dev, uint8_t func);
int  ixgbe_send(net_interface_t *iface, const void *data, uint32_t len);
void ixgbe_poll(void);
void ixgbe_irq_handler(regs_t *regs);

/* ---- PCI 探测入口 ---- */
int ixgbe_probe(void);

#endif /* IXGBE_H */