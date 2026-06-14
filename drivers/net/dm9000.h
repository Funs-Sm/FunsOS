#ifndef DM9000_H
#define DM9000_H

#include "net.h"
#include "stdint.h"
#include "kernel_types.h"

/* ---- DM9000 设备基地址 (ISA/MMIO) ---- */
#define DM9000_DEFAULT_IO_BASE      0x300   /* Default I/O base address       */
#define DM9000_DEFAULT_MEM_BASE     0xE0000000 /* Default memory-mapped base  */

/* ---- DM9000 寄存器定义 ---- */
/* 通过 INDEX 端口 + DATA 端口访问 */
#define DM9000_REG_INDEX_PORT       0x00    /* INDEX port offset (I/O base+0) */
#define DM9000_REG_DATA_PORT        0x04    /* DATA port offset (I/O base+4)  */

/* 索引寄存器 */
#define DM9000_REG_NCR              0x00    /* Network Control Register       */
#define DM9000_REG_NSR              0x01    /* Network Status Register        */
#define DM9000_REG_TCR              0x02    /* TX Control Register            */
#define DM9000_REG_TSR_I            0x03    /* TX Status Register I           */
#define DM9000_REG_TSR_II           0x04    /* TX Status Register II          */
#define DM9000_REG_RCR              0x05    /* RX Control Register            */
#define DM9000_REG_RSR              0x06    /* RX Status Register             */
#define DM9000_REG_ROCR             0x07    /* Receive Overflow Counter       */
#define DM9000_REG_BPTR             0x08    /* Back Pressure Threshold        */
#define DM9000_REG_FCTR             0x09    /* Flow Control Threshold         */
#define DM9000_REG_FCR              0x0A    /* RX/TX Flow Control Register    */
#define DM9000_REG_EPCR             0x0B    /* EEPROM & PHY Control Register  */
#define DM9000_REG_EPAR             0x0C    /* EEPROM & PHY Address Register  */
#define DM9000_REG_EPDRL            0x0D    /* EEPROM & PHY Data Low          */
#define DM9000_REG_EPDRH            0x0E    /* EEPROM & PHY Data High         */
#define DM9000_REG_WCR              0x0F    /* Wake Up Control Register       */

#define DM9000_REG_PAR              0x10    /* Physical Address 0-1           */
#define DM9000_REG_PAR2             0x12    /* Physical Address 2-3           */
#define DM9000_REG_PAR4             0x14    /* Physical Address 4-5           */
#define DM9000_REG_MAR              0x16    /* Multicast Address 0-7          */

#define DM9000_REG_GPCR             0x1E    /* General Purpose Control        */
#define DM9000_REG_GPR              0x1F    /* General Purpose Register       */
#define DM9000_REG_TRPAL            0x22    /* TX SRAM Read Pointer Low       */
#define DM9000_REG_TRPAH            0x23    /* TX SRAM Read Pointer High      */
#define DM9000_REG_RWPAL            0x24    /* RX SRAM Write Pointer Low      */
#define DM9000_REG_RWPAH            0x25    /* RX SRAM Write Pointer High      */

#define DM9000_REG_VIDL             0x28    /* Vendor ID Low                  */
#define DM9000_REG_VIDH             0x29    /* Vendor ID High                 */
#define DM9000_REG_PIDL             0x2A    /* Product ID Low                 */
#define DM9000_REG_PIDH             0x2B    /* Product ID High                */
#define DM9000_REG_CHIPR            0x2C    /* Chip Revision                  */

#define DM9000_REG_TCR2             0x2D    /* TX Control Register 2          */
#define DM9000_REG_OCR              0x2E    /* Operation Control Register     */
#define DM9000_REG_SMCR             0x2F    /* Special Mode Control Register  */
#define DM9000_REG_ETXCSR           0x30    /* Early TX Control/Status        */
#define DM9000_REG_TXCRC            0x31    /* TX Checksum Counter            */
#define DM9000_REG_TCSCR            0x32    /* TX Control/Status              */
#define DM9000_REG_RCSCSR           0x34    /* RX Control/Status              */
#define DM9000_REG_MRCMDX           0xF0    /* Memory Read Cmd w/o Addr Inc   */
#define DM9000_REG_MRCMD            0xF2    /* Memory Read Cmd w/ Addr Inc    */
#define DM9000_REG_MRRL             0xF4    /* Memory Read Lower Byte         */
#define DM9000_REG_MRRH             0xF5    /* Memory Read Higher Byte        */
#define DM9000_REG_MWCMDX           0xF6    /* Memory Write Cmd w/o Addr Inc  */
#define DM9000_REG_MWCMD            0xF8    /* Memory Write Cmd w/ Addr Inc   */
#define DM9000_REG_MWRL             0xFA    /* Memory Write Lower Byte        */
#define DM9000_REG_MWRH             0xFB    /* Memory Write Higher Byte       */
#define DM9000_REG_TXPLL            0xFC    /* TX Packet Length Low           */
#define DM9000_REG_TXPLH            0xFD    /* TX Packet Length High          */
#define DM9000_REG_ISR              0xFE    /* Interrupt Status Register      */
#define DM9000_REG_IMR              0xFF    /* Interrupt Mask Register        */

/* ---- NCR 网络控制位 ---- */
#define DM9000_NCR_RST              (1 << 0)  /* Software Reset                 */
#define DM9000_NCR_LBK_MASK         (3 << 1)  /* Loopback Mode                  */
#define DM9000_NCR_LBK_NORMAL       (0 << 1)  /* Normal                         */
#define DM9000_NCR_LBK_INTERNAL     (1 << 1)  /* Internal Loopback              */
#define DM9000_NCR_LBK_EXTERNAL     (2 << 1)  /* External Loopback              */
#define DM9000_NCR_FDX              (1 << 3)  /* Full Duplex                    */
#define DM9000_NCR_WAKEEN           (1 << 6)  /* Wake Up Event Enable           */
#define DM9000_NCR_EXT_PHY          (1 << 7)  /* External PHY Select            */

/* ---- NSR 网络状态位 ---- */
#define DM9000_NSR_RXRDY            (1 << 0)  /* RX Ready                       */
#define DM9000_NSR_TX1END           (1 << 2)  /* TX Packet 1 End                */
#define DM9000_NSR_TX2END           (1 << 3)  /* TX Packet 2 End                */
#define DM9000_NSR_WAKEST           (1 << 5)  /* Wake Up Event Status           */
#define DM9000_NSR_LINKST           (1 << 6)  /* Link Status                    */
#define DM9000_NSR_SPEED            (1 << 7)  /* Speed: 0=10Mbps, 1=100Mbps     */

/* ---- TCR TX 控制位 ---- */
#define DM9000_TCR_TXREQ            (1 << 0)  /* TX Request                     */
#define DM9000_TCR_TX1EXC           (1 << 2)  /* TX1 Excessive Collision        */
#define DM9000_TCR_TX2EXC           (1 << 3)  /* TX2 Excessive Collision        */
#define DM9000_TCR_TJPO             (1 << 4)  /* TX Jabber Packet Time Out      */
#define DM9000_TCR_IPCRC            (1 << 5)  /* IP Checksum Offload Enable     */
#define DM9000_TCR_TCPCRC           (1 << 6)  /* TCP/UDP Checksum Offload Enable */
#define DM9000_TCR_PAD_EN           (1 << 7)  /* Pad Short Packet Enable        */

/* ---- TSR_I TX 状态位 ---- */
#define DM9000_TSR_I_TX1OV          (1 << 0)  /* TX1 Overflow                   */
#define DM9000_TSR_I_TX1COL         (1 << 1)  /* TX1 Collision                  */
#define DM9000_TSR_I_TX1EC          (1 << 2)  /* TX1 Excessive Collision        */
#define DM9000_TSR_I_TX1NC          (1 << 3)  /* TX1 No Carrier                 */
#define DM9000_TSR_I_TX1LC          (1 << 4)  /* TX1 Loss of Carrier            */
#define DM9000_TSR_I_TX1NCAR        (1 << 5)  /* TX1 No Carrier Asserted        */
#define DM9000_TSR_I_TX1LCAR        (1 << 6)  /* TX1 Late Collision Asserted    */
#define DM9000_TSR_I_TX1PD          (1 << 7)  /* TX1 Packet Dropped             */

/* ---- TSR_II TX 状态位 ---- */
#define DM9000_TSR_II_TX2OV         (1 << 0)  /* TX2 Overflow                   */
#define DM9000_TSR_II_TX2COL        (1 << 1)  /* TX2 Collision                  */
#define DM9000_TSR_II_TX2EC         (1 << 2)  /* TX2 Excessive Collision        */
#define DM9000_TSR_II_TX2NC         (1 << 3)  /* TX2 No Carrier                 */
#define DM9000_TSR_II_TX2LC         (1 << 4)  /* TX2 Loss of Carrier            */
#define DM9000_TSR_II_TX2NCAR       (1 << 5)  /* TX2 No Carrier Asserted        */
#define DM9000_TSR_II_TX2LCAR       (1 << 6)  /* TX2 Late Collision Asserted    */
#define DM9000_TSR_II_TX2PD         (1 << 7)  /* TX2 Packet Dropped             */

/* ---- RCR RX 控制位 ---- */
#define DM9000_RCR_RXEN             (1 << 0)  /* RX Enable                      */
#define DM9000_RCR_PRMSC            (1 << 1)  /* Promiscuous Mode               */
#define DM9000_RCR_RXALL            (1 << 3)  /* Pass All Multicast             */
#define DM9000_RCR_DIS_LONG         (1 << 4)  /* Discard Long Packets           */
#define DM9000_RCR_DIS_CRC          (1 << 5)  /* Discard CRC Error Packets      */
#define DM9000_RCR_WTDIS            (1 << 6)  /* Watchdog Timer Disable         */
#define DM9000_RCR_LONG_PKT_EN      (1 << 7)  /* Long Packet Enable             */

/* ---- RSR RX 状态位 ---- */
#define DM9000_RSR_RXOK             (1 << 0)  /* RX Packet OK                   */
#define DM9000_RSR_RF               (1 << 1)  /* Runt Frame                     */
#define DM9000_RSR_MF               (1 << 2)  /* Multicast Frame                */
#define DM9000_RSR_LCS              (1 << 3)  /* Late Collision Seen            */
#define DM9000_RSR_RWTO             (1 << 4)  /* Receive Watchdog Time Out      */
#define DM9000_RSR_PLE              (1 << 5)  /* Physical Layer Error           */
#define DM9000_RSR_AE               (1 << 6)  /* Alignment Error                */
#define DM9000_RSR_CE               (1 << 7)  /* CRC Error                      */

/* ---- ISR 中断状态位 ---- */
#define DM9000_ISR_PRS              (1 << 0)  /* Packet Received                */
#define DM9000_ISR_PTS              (1 << 1)  /* Packet Transmitted             */
#define DM9000_ISR_ROS              (1 << 2)  /* Receive Overflow               */
#define DM9000_ISR_ROOS             (1 << 3)  /* Receive Overflow Overflow      */
#define DM9000_ISR_PTC              (1 << 4)  /* Packet Transmit Collision      */
#define DM9000_ISR_FBE              (1 << 5)  /* FIFO Bus Error                 */
#define DM9000_ISR_LNKCHG           (1 << 6)  /* Link Status Change             */
#define DM9000_ISR_UDRUN            (1 << 7)  /* Underrun                       */

/* ---- IMR 中断屏蔽位 ---- */
#define DM9000_IMR_PRM              (1 << 0)  /* Packet Received Mask           */
#define DM9000_IMR_PTM              (1 << 1)  /* Packet Transmitted Mask        */
#define DM9000_IMR_PRM2             (1 << 2)  /* Receive Overflow Mask          */
#define DM9000_IMR_PRM3             (1 << 3)  /* ROOS Mask                      */
#define DM9000_IMR_PTM4             (1 << 4)  /* Packet Tx Collision Mask       */
#define DM9000_IMR_PRM5             (1 << 5)  /* FIFO Bus Error Mask            */
#define DM9000_IMR_PRM6             (1 << 6)  /* Link Change Mask               */
#define DM9000_IMR_PRM7             (1 << 7)  /* Underrun Mask                  */

/* ---- WCR 唤醒控制位 ---- */
#define DM9000_WCR_MAGICST          (1 << 0)  /* Magic Packet Status            */
#define DM9000_WCR_WAKEEN           (1 << 1)  /* Wake Up Event Enable           */
#define DM9000_WCR_LINKST           (1 << 2)  /* Link Status Change Wake        */
#define DM9000_WCR_SAMPLEEN         (1 << 3)  /* Sample Enable                  */
#define DM9000_WCR_MAGICEN          (1 << 4)  /* Magic Packet Wake Enable       */
#define DM9000_WCR_LINKEN           (1 << 5)  /* Link Change Wake Enable        */

/* ---- EPCR (EEPROM/PHY 控制) ---- */
#define DM9000_EPCR_ERRE            (1 << 0)  /* EEPROM Read/Write Enable       */
#define DM9000_EPCR_ERPRW           (1 << 1)  /* EEPROM Read/Write Command      */
#define DM9000_EPCR_ERPRR           (1 << 2)  /* EEPROM Read Command            */
#define DM9000_EPCR_EPOS            (1 << 3)  /* EEPROM/PHY Operate Select      */
#define DM9000_EPCR_WEP             (1 << 4)  /* Write Erase Protect            */
#define DM9000_EPCR_REEP            (1 << 5)  /* Reload EEPROM                  */

/* ---- PHY 寄存器 (通过 EPAR/EPDRL/EPDRH 访问) ---- */
#define DM9000_PHY_BMCR             0x00    /* Basic Mode Control Register    */
#define DM9000_PHY_BMSR             0x01    /* Basic Mode Status Register     */
#define DM9000_PHY_ID1              0x02    /* PHY ID 1                       */
#define DM9000_PHY_ID2              0x03    /* PHY ID 2                       */
#define DM9000_PHY_ANAR             0x04    /* Auto-Negotiation Advertise     */
#define DM9000_PHY_ANLPAR           0x05    /* Auto-Negotiation Link Partner  */
#define DM9000_PHY_ANER             0x06    /* Auto-Negotiation Expansion     */
#define DM9000_PHY_DSCR             0x10    /* DAVICOM Specified Config       */
#define DM9000_PHY_DSCSR            0x11    /* DAVICOM Specified Config/Status*/

#define DM9000_PHY_BMCR_RESET       (1 << 15)
#define DM9000_PHY_BMCR_LOOPBACK    (1 << 14)
#define DM9000_PHY_BMCR_100MB       (1 << 13)
#define DM9000_PHY_BMCR_AUTONEG_EN  (1 << 12)
#define DM9000_PHY_BMCR_PD          (1 << 11)
#define DM9000_PHY_BMCR_ISOLATE     (1 << 10)
#define DM9000_PHY_BMCR_RESTART_AN  (1 << 9)
#define DM9000_PHY_BMCR_FDX         (1 << 8)

#define DM9000_PHY_BMSR_100B_T4     (1 << 15)
#define DM9000_PHY_BMSR_100B_TX_FDX (1 << 14)
#define DM9000_PHY_BMSR_100B_TX_HDX (1 << 13)
#define DM9000_PHY_BMSR_10B_T_FDX   (1 << 12)
#define DM9000_PHY_BMSR_10B_T_HDX   (1 << 11)
#define DM9000_PHY_BMSR_MF_PS       (1 << 6)
#define DM9000_PHY_BMSR_AUTONEG_OK  (1 << 5)
#define DM9000_PHY_BMSR_RF          (1 << 4)
#define DM9000_PHY_BMSR_AUTONEG_ABLE (1 << 3)
#define DM9000_PHY_BMSR_LINK        (1 << 2)
#define DM9000_PHY_BMSR_JD          (1 << 1)
#define DM9000_PHY_BMSR_EXT_CAP     (1 << 0)

/* ---- 芯片 ID 值 ---- */
#define DM9000_CHIP_ID_DM9000       0x90000A46  /* DM9000A/B/EP                 */
#define DM9000_CHIP_ID_DM9000A      0x90000A46
#define DM9000_CHIP_ID_DM9000B      0x90000B46
#define DM9000_CHIP_ID_DM9000C      0x90000C46
#define DM9000_CHIP_ID_DM9000EP     0x90000E46

/* ---- SRAM 大小 ---- */
#define DM9000_TX_SRAM_SIZE         0x0800  /* 2KB TX SRAM                    */
#define DM9000_RX_SRAM_SIZE         0x3000  /* 12KB RX SRAM                   */
#define DM9000_TX_BUF_SIZE          2048
#define DM9000_RX_BUF_SIZE          2048

/* ---- 公共接口 ---- */
void dm9000_init(uint16_t io_base, uint8_t irq);
int  dm9000_send(net_interface_t *iface, const void *data, uint32_t len);
void dm9000_poll(void);
void dm9000_irq_handler(regs_t *regs);

/* ---- 探测入口 ---- */
int dm9000_probe(void);

#endif /* DM9000_H */