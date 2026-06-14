#ifndef E1000E_H
#define E1000E_H

#include "net.h"
#include "stdint.h"
#include "kernel_types.h"

/* ---- PCI 设备 ID ---- */
#define E1000E_VENDOR_ID            0x8086
#define E1000E_DEV_ID_82574L        0x10D3  /* 82574L Gigabit Ethernet        */
#define E1000E_DEV_ID_82574LA       0x10D6  /* 82574LA                        */
#define E1000E_DEV_ID_ICH8_IGP_M    0x1049  /* ICH8 IGP M                     */
#define E1000E_DEV_ID_ICH8_IGP_AM   0x104A  /* ICH8 IGP AM                    */
#define E1000E_DEV_ID_ICH8_IFE      0x104B  /* ICH8 IFE                       */
#define E1000E_DEV_ID_ICH8_IFE_G    0x104C  /* ICH8 IFE G                     */
#define E1000E_DEV_ID_ICH8_IFE_GT   0x104D  /* ICH8 IFE GT                    */
#define E1000E_DEV_ID_ICH9_IGP_M    0x10BD  /* ICH9 IGP M                     */
#define E1000E_DEV_ID_ICH9_IGP_AM   0x10BF  /* ICH9 IGP AM                    */
#define E1000E_DEV_ID_ICH9_IFE      0x10C0  /* ICH9 IFE                       */
#define E1000E_DEV_ID_ICH9_IFE_G    0x10C2  /* ICH9 IFE G                     */
#define E1000E_DEV_ID_ICH9_IFE_GT   0x10C3  /* ICH9 IFE GT                    */
#define E1000E_DEV_ID_ICH10_R_BM    0x10CC  /* ICH10 R BM                     */
#define E1000E_DEV_ID_ICH10_D_BM    0x10CD  /* ICH10 D BM                     */
#define E1000E_DEV_ID_ICH10_R_BM_V  0x10CE  /* ICH10 R BM V                   */
#define E1000E_DEV_ID_ICH10_D_BM_V  0x10EF  /* ICH10 D BM V                   */

/* ---- E1000E 寄存器定义 ---- */
/* 控制与状态寄存器 */
#define E1000E_REG_CTRL             0x00000 /* Device Control                 */
#define E1000E_REG_STATUS           0x00008 /* Device Status                  */
#define E1000E_REG_EEC              0x00010 /* EEPROM/Flash Control           */
#define E1000E_REG_EERD             0x00014 /* EEPROM Read                    */
#define E1000E_REG_CTRL_EXT         0x00018 /* Extended Device Control        */
#define E1000E_REG_MDIC             0x00020 /* MDI Control                    */
#define E1000E_REG_FCAL             0x00028 /* Flow Control Address Low       */
#define E1000E_REG_FCAH             0x0002C /* Flow Control Address High      */
#define E1000E_REG_FCT              0x00030 /* Flow Control Type              */
#define E1000E_REG_VET              0x00038 /* VLAN EtherType                 */
#define E1000E_REG_ICR              0x000C0 /* Interrupt Cause Read           */
#define E1000E_REG_ITR              0x000C4 /* Interrupt Throttling           */
#define E1000E_REG_ICS              0x000C8 /* Interrupt Cause Set            */
#define E1000E_REG_IMS              0x000D0 /* Interrupt Mask Set             */
#define E1000E_REG_IMC              0x000D8 /* Interrupt Mask Clear           */
#define E1000E_REG_IAM              0x000E0 /* Interrupt Acknowledge Auto Mask*/

/* 接收控制 */
#define E1000E_REG_RCTL             0x00100 /* Receive Control                */
#define E1000E_REG_FCTTV            0x00170 /* Flow Control Transmit Timer    */
#define E1000E_REG_FCRTL            0x02160 /* Flow Control RX Threshold Low  */
#define E1000E_REG_FCRTH            0x02168 /* Flow Control RX Threshold High */
#define E1000E_REG_RDBAL            0x02800 /* RX Desc Base Address Low       */
#define E1000E_REG_RDBAH            0x02804 /* RX Desc Base Address High      */
#define E1000E_REG_RDLEN            0x02808 /* RX Descriptor Length           */
#define E1000E_REG_RDH              0x02810 /* RX Descriptor Head             */
#define E1000E_REG_RDT              0x02818 /* RX Descriptor Tail             */
#define E1000E_REG_RDTR             0x02820 /* RX Delay Timer                 */
#define E1000E_REG_RXDCTL           0x02828 /* RX Descriptor Control          */

/* 发送控制 */
#define E1000E_REG_TCTL             0x00400 /* Transmit Control               */
#define E1000E_REG_TIPG             0x00410 /* Transmit IPG                   */
#define E1000E_REG_TDBAL            0x03800 /* TX Desc Base Address Low       */
#define E1000E_REG_TDBAH            0x03804 /* TX Desc Base Address High      */
#define E1000E_REG_TDLEN            0x03808 /* TX Descriptor Length           */
#define E1000E_REG_TDH              0x03810 /* TX Descriptor Head             */
#define E1000E_REG_TDT              0x03818 /* TX Descriptor Tail             */
#define E1000E_REG_TIDV             0x03820 /* TX Interrupt Delay Value       */
#define E1000E_REG_TXDCTL           0x03828 /* TX Descriptor Control          */
#define E1000E_REG_TARC0            0x03840 /* TX Arbitration Count 0         */

/* 统计计数器 */
#define E1000E_REG_CRCERRS          0x04000 /* CRC Error Count                */
#define E1000E_REG_ALGNERRC         0x04004 /* Alignment Error Count          */
#define E1000E_REG_SYMERRC          0x04008 /* Symbol Error Count             */
#define E1000E_REG_RXERRC           0x0400C /* RX Error Count                 */
#define E1000E_REG_MPC              0x04010 /* Missed Packet Count            */
#define E1000E_REG_SCC              0x04014 /* Single Collision Count         */
#define E1000E_REG_ECOL             0x04018 /* Excessive Collision Count      */
#define E1000E_REG_MCC              0x0401C /* Multiple Collision Count       */
#define E1000E_REG_LATECOL          0x04020 /* Late Collision Count           */
#define E1000E_REG_COLC             0x04028 /* Collision Count                */
#define E1000E_REG_DC               0x04030 /* Defer Count                    */
#define E1000E_REG_TNCRS            0x04034 /* TX - No CRS                    */
#define E1000E_REG_SEC              0x04038 /* Sequence Error Count           */
#define E1000E_REG_CEXTERR          0x0403C /* Carrier Extension Error Count  */
#define E1000E_REG_RLEC             0x04040 /* Receive Length Error Count     */
#define E1000E_REG_XONRXC           0x04048 /* XON Received Count             */
#define E1000E_REG_XONTXC           0x0404C /* XON Transmitted Count          */
#define E1000E_REG_XOFFRXC          0x04050 /* XOFF Received Count            */
#define E1000E_REG_XOFFTXC          0x04054 /* XOFF Transmitted Count         */
#define E1000E_REG_FCRUC            0x04058 /* Flow Control RX Unsupported Ct */
#define E1000E_REG_PRC64            0x0405C /* Packets Received (64 bytes)    */
#define E1000E_REG_PRC127           0x04060 /* Packets Received (65-127)      */
#define E1000E_REG_PRC255           0x04064 /* Packets Received (128-255)     */
#define E1000E_REG_PRC511           0x04068 /* Packets Received (256-511)     */
#define E1000E_REG_PRC1023          0x0406C /* Packets Received (512-1023)    */
#define E1000E_REG_PRC1522          0x04070 /* Packets Received (1024-1522)   */
#define E1000E_REG_GPRC             0x04074 /* Good Packets Received Count    */
#define E1000E_REG_BPRC             0x04078 /* Broadcast Packets Received Ct   */
#define E1000E_REG_MPRC             0x0407C /* Multicast Packets Received Ct   */
#define E1000E_REG_GPTC             0x04080 /* Good Packets Transmitted Ct     */
#define E1000E_REG_GORCL            0x04088 /* Good Octets Received Count Low  */
#define E1000E_REG_GORCH            0x0408C /* Good Octets Received Count High */
#define E1000E_REG_GOTCL            0x04090 /* Good Octets Transmitted Ct Low  */
#define E1000E_REG_GOTCH            0x04094 /* Good Octets Transmitted Ct Hi   */
#define E1000E_REG_RNBC             0x040A0 /* Receive No Buffers Count        */
#define E1000E_REG_RUC              0x040A4 /* Receive Undersize Count         */
#define E1000E_REG_RFC              0x040A8 /* Receive Fragment Count          */
#define E1000E_REG_ROC              0x040AC /* Receive Oversize Count          */
#define E1000E_REG_RJC              0x040B0 /* Receive Jabber Count            */
#define E1000E_REG_MGTPRC           0x040B4 /* Management Packets RX Count     */
#define E1000E_REG_MGTPDC           0x040B8 /* Management Packets Drop Count   */
#define E1000E_REG_MGTPTC           0x040BC /* Management Packets TX Count     */
#define E1000E_REG_TORL             0x040C0 /* Total Octets Received Low       */
#define E1000E_REG_TORH             0x040C4 /* Total Octets Received High      */
#define E1000E_REG_TOTL             0x040C8 /* Total Octets Transmitted Low    */
#define E1000E_REG_TOTH             0x040CC /* Total Octets Transmitted High   */
#define E1000E_REG_TPR              0x040D0 /* Total Packets Received          */
#define E1000E_REG_TPT              0x040D4 /* Total Packets Transmitted       */
#define E1000E_REG_PTC64            0x040D8 /* Packets TX (64 bytes)           */
#define E1000E_REG_PTC127           0x040DC /* Packets TX (65-127)             */
#define E1000E_REG_PTC255           0x040E0 /* Packets TX (128-255)            */
#define E1000E_REG_PTC511           0x040E4 /* Packets TX (256-511)            */
#define E1000E_REG_PTC1023          0x040E8 /* Packets TX (512-1023)           */
#define E1000E_REG_PTC1522          0x040EC /* Packets TX (1024-1522)          */
#define E1000E_REG_MPTC             0x040F0 /* Multicast Packets TX Count      */
#define E1000E_REG_BPTC             0x040F4 /* Broadcast Packets TX Count      */
#define E1000E_REG_TSCTC            0x040F8 /* TCP Segmentation Context TX     */
#define E1000E_REG_TSCTFC           0x040FC /* TCP Segmentation Context TX Fail*/
#define E1000E_REG_IAC              0x04100 /* Interrupt Acknowledge Count     */
#define E1000E_REG_ICRXOC           0x04104 /* Interrupt Cause RX Overrun      */

/* 多播地址表 */
#define E1000E_REG_MTA              0x05200 /* Multicast Table Array (128 x 4) */

/* MAC 地址 (RAL/RAH) */
#define E1000E_REG_RAL(n)           (0x05400 + ((n) * 8))   /* RAL for n */
#define E1000E_REG_RAH(n)           (0x05404 + ((n) * 8))   /* RAH for n */

/* VLAN 过滤 */
#define E1000E_REG_VFTA             0x05600 /* VLAN Filter Table Array */

/* TSO / TCP Segmentation */
#define E1000E_REG_TARC             0x03840 /* TX Arbitration Count           */
#define E1000E_REG_MANC             0x05820 /* Management Control             */
#define E1000E_REG_MFUTP            0x05828 /* Management Flex UDP/TCP Ports  */

/* WoL / 电源管理 */
#define E1000E_REG_WUC              0x05800 /* Wake Up Control                */
#define E1000E_REG_WUFC             0x05808 /* Wake Up Filter Control         */
#define E1000E_REG_WUS              0x05810 /* Wake Up Status                 */
#define E1000E_REG_IPAV             0x05838 /* IP Address Valid               */
#define E1000E_REG_IP4AT            0x05840 /* IPv4 Address Table             */
#define E1000E_REG_IP6AT            0x05880 /* IPv6 Address Table             */
#define E1000E_REG_WUPL             0x05900 /* Wake Up Packet Length          */
#define E1000E_REG_WUPM             0x05A00 /* Wake Up Packet Memory          */

/* Advanced 寄存器 */
#define E1000E_REG_PBA              0x01000 /* Packet Buffer Allocation       */
#define E1000E_REG_EEMNGCTL         0x01010 /* EEPROM Management Control      */
#define E1000E_REG_EEMNGDATA        0x01014 /* EEPROM Management Data         */
#define E1000E_REG_MANC2H           0x05860 /* Management Control to Host      */

/* MSI-X 相关 */
#define E1000E_REG_MSIX_TABLE       0x00000 /* MSI-X Table offset in BAR3     */
#define E1000E_REG_MSIX_PBA         0x02000 /* MSI-X PBA offset               */

/* ---- CTRL 寄存器位定义 ---- */
#define E1000E_CTRL_FD              (1 << 0)  /* Full Duplex                   */
#define E1000E_CTRL_LRST            (1 << 3)  /* Link Reset                    */
#define E1000E_CTRL_ASDE            (1 << 5)  /* Auto-Speed Detection Enable   */
#define E1000E_CTRL_SLU             (1 << 6)  /* Set Link Up                   */
#define E1000E_CTRL_ILOS            (1 << 7)  /* Invert Loss of Signal         */
#define E1000E_CTRL_SPEED_MASK      (3 << 8)  /* Speed Select                  */
#define E1000E_CTRL_SPEED_10        (0 << 8)  /* 10 Mbps                       */
#define E1000E_CTRL_SPEED_100       (1 << 8)  /* 100 Mbps                      */
#define E1000E_CTRL_SPEED_1000      (2 << 8)  /* 1000 Mbps                     */
#define E1000E_CTRL_FRCSPD          (1 << 11) /* Force Speed                   */
#define E1000E_CTRL_FRCDPLX         (1 << 12) /* Force Duplex                  */
#define E1000E_CTRL_ADVD3WUC        (1 << 20) /* Advertise D3 Wake Up Capability*/
#define E1000E_CTRL_PHY_RST         (1 << 28) /* PHY Reset                     */
#define E1000E_CTRL_VME             (1 << 30) /* VLAN Mode Enable              */
#define E1000E_CTRL_RST             (1 << 31) /* Device Reset                  */

/* ---- STATUS 寄存器位定义 ---- */
#define E1000E_STATUS_FD            (1 << 0)  /* Full Duplex                   */
#define E1000E_STATUS_LU            (1 << 1)  /* Link Up                       */
#define E1000E_STATUS_TXOFF         (1 << 4)  /* Transmit Paused               */
#define E1000E_STATUS_SPEED_MASK    (3 << 6)  /* Speed                         */
#define E1000E_STATUS_SPEED_10      (0 << 6)  /* 10 Mbps                       */
#define E1000E_STATUS_SPEED_100     (1 << 6)  /* 100 Mbps                      */
#define E1000E_STATUS_SPEED_1000    (2 << 6)  /* 1000 Mbps                     */
#define E1000E_STATUS_ASDV_MASK     (3 << 8)  /* Auto-Speed Detect Value       */
#define E1000E_STATUS_PHYRA         (1 << 10) /* PHY Reset Asserted            */
#define E1000E_STATUS_GIO_ME        (1 << 19) /* GIO Master Enable             */

/* ---- CTRL_EXT 扩展控制位定义 ---- */
#define E1000E_CTRL_EXT_LPCD        (1 << 15) /* Link Partner Capability Disable*/
#define E1000E_CTRL_EXT_LINK_MODE_MASK (3 << 16)
#define E1000E_CTRL_EXT_LINK_MODE_GMII (0 << 16)
#define E1000E_CTRL_EXT_LINK_MODE_PCIE (1 << 16)
#define E1000E_CTRL_EXT_EIAME       (1 << 20) /* Extended Int Auto Mask Enable */
#define E1000E_CTRL_EXT_IAME        (1 << 27) /* Interrupt Acknowledge Auto Mask*/
#define E1000E_CTRL_EXT_PBA_CLR     (1 << 20) /* PBA Clear                     */
#define E1000E_CTRL_EXT_DRV_LOAD    (1 << 28) /* Driver Loaded                 */
#define E1000E_CTRL_EXT_PHYPDEN     (1 << 26) /* PHY Power Down Enable         */

/* ---- RCTL 位定义 ---- */
#define E1000E_RCTL_EN              (1 << 1)  /* Receiver Enable               */
#define E1000E_RCTL_SBP             (1 << 2)  /* Store Bad Packets             */
#define E1000E_RCTL_UPE             (1 << 3)  /* Unicast Promiscuous Enable    */
#define E1000E_RCTL_MPE             (1 << 4)  /* Multicast Promiscuous Enable  */
#define E1000E_RCTL_LPE             (1 << 5)  /* Long Packet Enable            */
#define E1000E_RCTL_LBM_MASK        (3 << 6)  /* Loopback Mode                 */
#define E1000E_RCTL_LBM_NONE        (0 << 6)  /* No Loopback                   */
#define E1000E_RCTL_LBM_PHY         (3 << 6)  /* PHY Loopback                  */
#define E1000E_RCTL_RDMTS_MASK      (3 << 8)  /* RX Descriptor Min Threshold   */
#define E1000E_RCTL_RDMTS_HALF      (0 << 8)  /* 1/2 of RDLEN                  */
#define E1000E_RCTL_RDMTS_QUARTER   (1 << 8)  /* 1/4 of RDLEN                  */
#define E1000E_RCTL_RDMTS_EIGHTH    (2 << 8)  /* 1/8 of RDLEN                  */
#define E1000E_RCTL_MO_MASK         (3 << 12) /* Multicast Offset              */
#define E1000E_RCTL_BAM             (1 << 15) /* Broadcast Accept Mode         */
#define E1000E_RCTL_BSIZE_MASK      (3 << 16) /* Buffer Size                   */
#define E1000E_RCTL_BSIZE_2048      (0 << 16) /* 2048 bytes                    */
#define E1000E_RCTL_BSIZE_4096      (1 << 16) /* 4096 bytes                    */
#define E1000E_RCTL_BSIZE_8192      (2 << 16) /* 8192 bytes                    */
#define E1000E_RCTL_BSIZE_16384     (3 << 16) /* 16384 bytes                   */
#define E1000E_RCTL_VFE             (1 << 18) /* VLAN Filter Enable            */
#define E1000E_RCTL_CFIEN           (1 << 19) /* CFI Enable                    */
#define E1000E_RCTL_CFI             (1 << 20) /* CFI bit value                 */
#define E1000E_RCTL_DPF             (1 << 22) /* Discard Pause Frames          */
#define E1000E_RCTL_PMCF            (1 << 23) /* Pass MAC Control Frames       */
#define E1000E_RCTL_BSEX_MASK       (3 << 25) /* Buffer Size Extension         */
#define E1000E_RCTL_SECRC           (1 << 26) /* Strip CRC                     */
#define E1000E_RCTL_FLEXBUF_MASK    (3 << 27) /* Flexible Buffer Size          */

/* ---- TCTL 位定义 ---- */
#define E1000E_TCTL_EN              (1 << 1)  /* Transmit Enable               */
#define E1000E_TCTL_PSP             (1 << 3)  /* Pad Short Packets             */
#define E1000E_TCTL_CT_MASK         (0xFF << 4)  /* Collision Threshold         */
#define E1000E_TCTL_CT_DEFAULT      (0x0F << 4) /* 15 - half duplex, 63 - full   */
#define E1000E_TCTL_COLD_MASK       (0x3FF << 12) /* Collision Distance          */
#define E1000E_TCTL_COLD_DEFAULT    (0x40 << 12) /* 64 for 1Gb                    */
#define E1000E_TCTL_SWXOFF          (1 << 22) /* Software XOFF                 */
#define E1000E_TCTL_RTLC            (1 << 24) /* Retransmit on Late Collision  */
#define E1000E_TCTL_MULR            (1 << 28) /* Multiple Request Support      */

/* ---- ICR / IMS / IMC 中断位定义 ---- */
#define E1000E_ICR_TXDW             (1 << 0)  /* TX Descriptor Written Back    */
#define E1000E_ICR_TXQE             (1 << 1)  /* TX Queue Empty                */
#define E1000E_ICR_LSC              (1 << 2)  /* Link Status Change            */
#define E1000E_ICR_RXSEQ            (1 << 3)  /* RX Sequence Error             */
#define E1000E_ICR_RXDMT0           (1 << 4)  /* RX Desc Min Threshold Hit     */
#define E1000E_ICR_RXO              (1 << 6)  /* RX Overrun                    */
#define E1000E_ICR_RXT0             (1 << 7)  /* RX Timer Interrupt            */
#define E1000E_ICR_MDAC             (1 << 19) /* MDI/O Access Complete         */
#define E1000E_ICR_RXCFG            (1 << 23) /* Receiving / Config Change     */
#define E1000E_ICR_GPI(n)           (1 << ((n) + 20)) /* General Purpose Int      */
#define E1000E_ICR_ECCER            (1 << 24) /* ECC Error                     */
#define E1000E_ICR_SRPD             (1 << 26) /* Small Receive Packet Detected */
#define E1000E_ICR_ACK              (1 << 29) /* Receive ACK frame             */

/* ---- TARC (TX Arbitration) 位定义 ---- */
#define E1000E_TARC_ENABLE          (1 << 10) /* Enable TX Arbitration         */

/* ---- MDIC 位定义 ---- */
#define E1000E_MDIC_DATA_MASK       0x0000FFFF  /* MDI Data                     */
#define E1000E_MDIC_REG_MASK        0x001F0000  /* MDI Register                  */
#define E1000E_MDIC_REG_SHIFT       16
#define E1000E_MDIC_PHY_MASK        0x03E00000  /* PHY Address                   */
#define E1000E_MDIC_PHY_SHIFT       21
#define E1000E_MDIC_OP_WRITE        (0x01 << 26) /* Write Operation              */
#define E1000E_MDIC_OP_READ         (0x02 << 26) /* Read Operation               */
#define E1000E_MDIC_READY           (1 << 28)  /* MDI Ready                     */
#define E1000E_MDIC_INT_EN          (1 << 29)  /* MDI Interrupt Enable          */
#define E1000E_MDIC_ERROR           (1 << 30)  /* MDI Error                     */

/* ---- PHY 寄存器 ---- */
#define E1000E_PHY_CTRL             0x00      /* PHY Control                   */
#define E1000E_PHY_STATUS           0x01      /* PHY Status                    */
#define E1000E_PHY_ID1              0x02      /* PHY Identifier 1              */
#define E1000E_PHY_ID2              0x03      /* PHY Identifier 2              */
#define E1000E_PHY_AUTONEG_ADV      0x04      /* Auto-Negotiation Advertisement*/
#define E1000E_PHY_LP_ABILITY       0x05      /* Link Partner Ability          */
#define E1000E_PHY_AUTONEG_EXP      0x06      /* Auto-Negotiation Expansion    */
#define E1000E_PHY_EXT_STATUS       0x0F      /* Extended PHY Status           */
#define E1000E_PHY_1000T_CTRL       0x09      /* 1000BASE-T Control            */
#define E1000E_PHY_1000T_STATUS     0x0A      /* 1000BASE-T Status             */

#define E1000E_PHY_CTRL_RESET       (1 << 15) /* PHY Reset                     */
#define E1000E_PHY_STATUS_LINK      (1 << 2)  /* Link Status                   */

/* ---- WoL / WUC 位定义 ---- */
#define E1000E_WUC_APME             (1 << 0)  /* APM Enable                    */
#define E1000E_WUC_PME_EN           (1 << 1)  /* PME Enable                    */
#define E1000E_WUC_PME_STATUS       (1 << 2)  /* PME Status                    */
#define E1000E_WUC_APMPME           (1 << 5)  /* APM PME Enable                */

#define E1000E_WUFC_LNKC            (1 << 0)  /* Link Status Change Wake       */
#define E1000E_WUFC_MAG             (1 << 1)  /* Magic Packet Wake             */
#define E1000E_WUFC_EX              (1 << 2)  /* Directed Exact Wake           */
#define E1000E_WUFC_MC              (1 << 3)  /* Directed Multicast Wake       */

/* ---- TX Descriptor 命令位 ---- */
#define E1000E_TX_CMD_EOP           (1 << 0)  /* End of Packet                 */
#define E1000E_TX_CMD_IFCS          (1 << 1)  /* Insert FCS                    */
#define E1000E_TX_CMD_IC            (1 << 2)  /* Insert Checksum               */
#define E1000E_TX_CMD_RS            (1 << 3)  /* Report Status                 */
#define E1000E_TX_CMD_RPS           (1 << 4)  /* Report Status Sent            */
#define E1000E_TX_CMD_DEXT          (1 << 5)  /* Descriptor Extension          */
#define E1000E_TX_CMD_VLE           (1 << 6)  /* VLAN Packet Enable            */
#define E1000E_TX_CMD_IDE           (1 << 7)  /* Interrupt Delay Enable        */

/* ---- TX Descriptor 状态位 ---- */
#define E1000E_TX_STATUS_DD         (1 << 0)  /* Descriptor Done               */

/* ---- RX Descriptor 状态位 ---- */
#define E1000E_RX_STATUS_DD         (1 << 0)  /* Descriptor Done               */
#define E1000E_RX_STATUS_EOP        (1 << 1)  /* End of Packet                 */
#define E1000E_RX_STATUS_IXSM       (1 << 2)  /* IP Checksum Verified          */
#define E1000E_RX_STATUS_VP         (1 << 3)  /* Packet is 802.1Q              */
#define E1000E_RX_STATUS_TCPCS      (1 << 5)  /* TCP Checksum Calculated       */
#define E1000E_RX_STATUS_IPCS       (1 << 6)  /* IP Checksum Calculated        */
#define E1000E_RX_STATUS_PIF        (1 << 7)  /* Passed Inexact Filter         */

/* ---- RX Descriptor 错误位 ---- */
#define E1000E_RX_ERROR_CE          (1 << 0)  /* CRC Error                     */
#define E1000E_RX_ERROR_SE          (1 << 1)  /* Symbol Error                  */
#define E1000E_RX_ERROR_SEQ         (1 << 2)  /* Sequence Error                */
#define E1000E_RX_ERROR_CXE         (1 << 4)  /* Carrier Extension Error       */
#define E1000E_RX_ERROR_TCPE        (1 << 5)  /* TCP/UDP Checksum Error        */
#define E1000E_RX_ERROR_IPE         (1 << 6)  /* IP Checksum Error             */
#define E1000E_RX_ERROR_RXE         (1 << 7)  /* RX Data Error                 */

/* ---- RSS 相关 ---- */
#define E1000E_REG_RETA             0x05C00 /* RSS Redirection Table         */
#define E1000E_REG_RSSRK            0x05C80 /* RSS Random Key                */
#define E1000E_REG_MRQC             0x05818 /* Multiple Receive Queues Cmd   */

#define E1000E_MRQC_RSS_ENABLE      0x00000002 /* RSS Enable                   */
#define E1000E_MRQC_RSS_FIELD_IPV4_TCP  0x00010000
#define E1000E_MRQC_RSS_FIELD_IPV4     0x00020000

/* ---- 描述符环大小 ---- */
#define E1000E_NUM_TX_DESC          256
#define E1000E_NUM_RX_DESC          256
#define E1000E_RX_BUF_SIZE          2048
#define E1000E_TX_BUF_SIZE          2048

/* ---- Legacy TX Descriptor 结构 (16 bytes) ---- */
typedef struct __attribute__((packed)) {
    uint64_t addr;       /* Buffer physical address             */
    uint16_t length;     /* Data length                         */
    uint8_t  cso;        /* Checksum Offset                     */
    uint8_t  cmd;        /* Command: EOP, IFCS, IC, RS, etc.    */
    uint8_t  status;     /* Descriptor Status: DD, etc.          */
    uint8_t  css;        /* Checksum Start                      */
    uint16_t special;    /* Special field / VLAN tag            */
} e1000e_tx_desc_t;

/* ---- Legacy RX Descriptor 结构 (16 bytes) --- */
typedef struct __attribute__((packed)) {
    uint64_t addr;       /* Buffer physical address             */
    uint16_t length;     /* Packet length                       */
    uint16_t checksum;   /* Hardware checksum value             */
    uint8_t  status;     /* Status: DD, EOP, IXSM, VP, etc.     */
    uint8_t  errors;     /* Error flags: CE, SE, IPE, TCPE, etc.*/
    uint16_t special;    /* VLAN tag or special field           */
} e1000e_rx_desc_t;

/* ---- TSO / TCP Segmentation Context Descriptor ---- */
typedef struct __attribute__((packed)) {
    uint64_t addr;       /* Header buffer physical address      */
    uint32_t tcp_opt;    /* TCP segmentation options            */
    uint32_t payload_len;/* MSS + payload length                */
} e1000e_tx_context_desc_t;

/* ---- 公共接口 ---- */
void e1000e_init(uint8_t bus, uint8_t dev, uint8_t func);
int  e1000e_send(net_interface_t *iface, const void *data, uint32_t len);
void e1000e_poll(void);
void e1000e_irq_handler(regs_t *regs);

/* ---- PCI 探测入口 ---- */
int e1000e_probe(void);

#endif /* E1000E_H */