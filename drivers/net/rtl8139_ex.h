#ifndef RTL8139_EX_H
#define RTL8139_EX_H

#include "net.h"
#include "stdint.h"
#include "kernel_types.h"

/* ---- PCI 设备 ID ---- */
#define RTL8139EX_VENDOR_ID         0x10EC
#define RTL8139EX_DEVICE_ID         0x8139
#define RTL8139EX_DEVICE_ID_8129    0x8129
#define RTL8139EX_DEVICE_ID_8138    0x8138

/* ---- 寄存器定义 (相对于 MMIO base / IO base) ---- */
/* 标准寄存器 */
#define RTL8139EX_REG_IDR0          0x00    /* MAC Address 0                  */
#define RTL8139EX_REG_IDR1          0x01    /* MAC Address 1                  */
#define RTL8139EX_REG_IDR2          0x02    /* MAC Address 2                  */
#define RTL8139EX_REG_IDR3          0x03    /* MAC Address 3                  */
#define RTL8139EX_REG_IDR4          0x04    /* MAC Address 4                  */
#define RTL8139EX_REG_IDR5          0x05    /* MAC Address 5                  */
#define RTL8139EX_REG_MAR0          0x08    /* Multicast Address 0            */
#define RTL8139EX_REG_MAR1          0x09    /* Multicast Address 1            */
#define RTL8139EX_REG_MAR2          0x0A    /* Multicast Address 2            */
#define RTL8139EX_REG_MAR3          0x0B    /* Multicast Address 3            */
#define RTL8139EX_REG_MAR4          0x0C    /* Multicast Address 4            */
#define RTL8139EX_REG_MAR5          0x0D    /* Multicast Address 5            */
#define RTL8139EX_REG_MAR6          0x0E    /* Multicast Address 6            */
#define RTL8139EX_REG_MAR7          0x0F    /* Multicast Address 7            */

#define RTL8139EX_REG_TSD0          0x10    /* TX Status Descriptor 0         */
#define RTL8139EX_REG_TSD1          0x14    /* TX Status Descriptor 1         */
#define RTL8139EX_REG_TSD2          0x18    /* TX Status Descriptor 2         */
#define RTL8139EX_REG_TSD3          0x1C    /* TX Status Descriptor 3         */
#define RTL8139EX_REG_TSAD0         0x20    /* TX Start Address 0             */
#define RTL8139EX_REG_TSAD1         0x24    /* TX Start Address 1             */
#define RTL8139EX_REG_TSAD2         0x28    /* TX Start Address 2             */
#define RTL8139EX_REG_TSAD3         0x2C    /* TX Start Address 3             */

#define RTL8139EX_REG_RBSTART       0x30    /* RX Ring Buffer Start Address   */
#define RTL8139EX_REG_CR            0x37    /* Command Register               */
#define RTL8139EX_REG_CAPR          0x38    /* Current Address of Packet Read */
#define RTL8139EX_REG_CBR           0x3A    /* Current Buffer Address         */
#define RTL8139EX_REG_IMR           0x3C    /* Interrupt Mask Register        */
#define RTL8139EX_REG_ISR           0x3E    /* Interrupt Status Register      */
#define RTL8139EX_REG_TCR           0x40    /* Transmit Configuration         */
#define RTL8139EX_REG_RCR           0x44    /* Receive Configuration          */
#define RTL8139EX_REG_TCTR          0x48    /* 82 Counter                     */
#define RTL8139EX_REG_MPC           0x4C    /* Missed Packet Counter          */

/* 扩展/未文档化寄存器 */
#define RTL8139EX_REG_9346CR        0x50    /* 93C46 Command Register (EEPROM) */
#define RTL8139EX_REG_CONFIG0       0x51    /* Configuration Register 0        */
#define RTL8139EX_REG_CONFIG1       0x52    /* Configuration Register 1        */
#define RTL8139EX_REG_CONFIG2       0x53    /* Configuration Register 2        */
#define RTL8139EX_REG_CONFIG3       0x54    /* Configuration Register 3        */
#define RTL8139EX_REG_CONFIG4       0x55    /* Configuration Register 4        */
#define RTL8139EX_REG_MSR           0x58    /* Media Status Register           */
#define RTL8139EX_REG_BMCR          0x62    /* Basic Mode Control Register     */
#define RTL8139EX_REG_BMSR          0x64    /* Basic Mode Status Register      */
#define RTL8139EX_REG_ANAR          0x66    /* Auto-Negotiation Advertise      */
#define RTL8139EX_REG_ANLPAR        0x68    /* Auto-Negotiation Link Partner   */
#define RTL8139EX_REG_ANER          0x6A    /* Auto-Negotiation Expansion      */
#define RTL8139EX_REG_DIS           0x6C    /* Disconnect Counter              */
#define RTL8139EX_REG_FCSC          0x6E    /* False Carrier Sense Counter     */
#define RTL8139EX_REG_NWAYTR        0x70    /* N-Way Test Register             */
#define RTL8139EX_REG_RX_ERCNT      0x72    /* RX Error Counter                */
#define RTL8139EX_REG_CSCR          0x74    /* Carrier Sense Configuration     */

/* 电源管理寄存器 */
#define RTL8139EX_REG_PM_CAP        0x50    /* Power Mgmt Capability (PCI PM)  */
#define RTL8139EX_REG_PM_CSR        0x54    /* Power Mgmt Control/Status       */
#define RTL8139EX_REG_WAKEUP        0x60    /* Wake Up Control                 */

/* ---- CR 命令寄存器位定义 ---- */
#define RTL8139EX_CR_RST             (1 << 4)  /* Software Reset                 */
#define RTL8139EX_CR_RE              (1 << 3)  /* Receiver Enable                */
#define RTL8139EX_CR_TE              (1 << 2)  /* Transmitter Enable             */
#define RTL8139EX_CR_BUFE            (1 << 0)  /* Buffer Empty                   */

/* ---- ISR 中断状态位 ---- */
#define RTL8139EX_ISR_ROK            (1 << 0)  /* Receive OK                     */
#define RTL8139EX_ISR_RER            (1 << 1)  /* Receive Error                  */
#define RTL8139EX_ISR_TOK            (1 << 2)  /* Transmit OK                    */
#define RTL8139EX_ISR_TER            (1 << 3)  /* Transmit Error                 */
#define RTL8139EX_ISR_RXOVW          (1 << 4)  /* RX Buffer Overflow             */
#define RTL8139EX_ISR_PUN            (1 << 5)  /* Packet Underrun                */
#define RTL8139EX_ISR_FOVW           (1 << 6)  /* RX FIFO Overflow               */
#define RTL8139EX_ISR_LNKCHG         (1 << 13) /* Link Change                    */
#define RTL8139EX_ISR_TIMEOUT        (1 << 14) /* Time Out                       */
#define RTL8139EX_ISR_SERR           (1 << 15) /* System Error                   */

/* ---- IMR 中断屏蔽位 ---- */
#define RTL8139EX_IMR_ROK           (1 << 0)  /* Receive OK                     */
#define RTL8139EX_IMR_RER           (1 << 1)  /* Receive Error                  */
#define RTL8139EX_IMR_TOK           (1 << 2)  /* Transmit OK                    */
#define RTL8139EX_IMR_TER           (1 << 3)  /* Transmit Error                 */
#define RTL8139EX_IMR_RXOVW         (1 << 4)  /* RX Buffer Overflow             */
#define RTL8139EX_IMR_PUN           (1 << 5)  /* Packet Underrun                */
#define RTL8139EX_IMR_FOVW          (1 << 6)  /* RX FIFO Overflow               */
#define RTL8139EX_IMR_LNKCHG        (1 << 13) /* Link Change                    */
#define RTL8139EX_IMR_TIMEOUT       (1 << 14) /* Time Out                       */

/* ---- TSD 发送状态/描述符位 ---- */
#define RTL8139EX_TSD_SIZE_MASK     0x00001FFF  /* Packet Size                    */
#define RTL8139EX_TSD_OWN           (1 << 13) /* NIC Owns Descriptor             */
#define RTL8139EX_TSD_TUN           (1 << 14) /* TX FIFO Underrun               */
#define RTL8139EX_TSD_TOK           (1 << 15) /* TX OK                           */
#define RTL8139EX_TSD_ERTXTH        (1 << 16) /* Early TX Threshold             */
#define RTL8139EX_TSD_CRS           (1 << 17) /* Carrier Sense Lost             */
#define RTL8139EX_TSD_OWC           (1 << 18) /* Out of Window Collision        */
#define RTL8139EX_TSD_TABT          (1 << 19) /* TX Abort                       */
#define RTL8139EX_TSD_CDH           (1 << 20) /* CD Heart Beat                  */
#define RTL8139EX_TSD_NCC           (1 << 21) /* No Carrier                     */
#define RTL8139EX_TSD_IPCF          (1 << 24) /* IP Checksum Offload            */
#define RTL8139EX_TSD_UDPCF         (1 << 25) /* UDP Checksum Offload           */
#define RTL8139EX_TSD_TCPCF         (1 << 26) /* TCP Checksum Offload           */
#define RTL8139EX_TSD_LGSEN         (1 << 27) /* Large Send Enable              */

/* ---- RCR 接收配置位 ---- */
#define RTL8139EX_RCR_AAP           (1 << 0)  /* Accept All Packets             */
#define RTL8139EX_RCR_APM           (1 << 1)  /* Accept Physical Match          */
#define RTL8139EX_RCR_AM            (1 << 2)  /* Accept Multicast               */
#define RTL8139EX_RCR_AB            (1 << 3)  /* Accept Broadcast               */
#define RTL8139EX_RCR_AR            (1 << 4)  /* Accept Runt                    */
#define RTL8139EX_RCR_AER           (1 << 5)  /* Accept Error                   */
#define RTL8139EX_RCR_WRAP          (1 << 7)  /* Wrap (Ring Buffer)             */
#define RTL8139EX_RCR_RXFTH_MASK    (7 << 13) /* RX FIFO Threshold              */
#define RTL8139EX_RCR_RXFTH_DEFAULT (1 << 13) /* Default: 16 bytes              */
#define RTL8139EX_RCR_RBLEN_MASK    (3 << 11) /* RX Buffer Length               */
#define RTL8139EX_RCR_RBLEN_8K      (0 << 11) /* 8K + 16 bytes                  */
#define RTL8139EX_RCR_RBLEN_16K     (1 << 11) /* 16K + 16 bytes                 */
#define RTL8139EX_RCR_RBLEN_32K     (2 << 11) /* 32K + 16 bytes                 */
#define RTL8139EX_RCR_RBLEN_64K     (3 << 11) /* 64K + 16 bytes                 */
#define RTL8139EX_RCR_MXDMA_MASK    (7 << 8)  /* Max DMA Burst                  */
#define RTL8139EX_RCR_MXDMA_16      (0 << 8)  /* 16 bytes                       */
#define RTL8139EX_RCR_MXDMA_32      (1 << 8)  /* 32 bytes                       */
#define RTL8139EX_RCR_MXDMA_64      (2 << 8)  /* 64 bytes                       */
#define RTL8139EX_RCR_MXDMA_128     (3 << 8)  /* 128 bytes                      */
#define RTL8139EX_RCR_MXDMA_256     (4 << 8)  /* 256 bytes                      */
#define RTL8139EX_RCR_MXDMA_512     (5 << 8)  /* 512 bytes                      */
#define RTL8139EX_RCR_MXDMA_1024    (6 << 8)  /* 1024 bytes                     */
#define RTL8139EX_RCR_MXDMA_UNLIMITED (7 << 8) /* Unlimited                   */

/* ---- TCR 发送配置位 ---- */
#define RTL8139EX_TCR_MXDMA_MASK    (7 << 8)  /* Max DMA Burst                  */
#define RTL8139EX_TCR_MXDMA_16      (0 << 8)
#define RTL8139EX_TCR_MXDMA_32      (1 << 8)
#define RTL8139EX_TCR_MXDMA_64      (2 << 8)
#define RTL8139EX_TCR_MXDMA_128     (3 << 8)
#define RTL8139EX_TCR_MXDMA_256     (4 << 8)
#define RTL8139EX_TCR_MXDMA_512     (5 << 8)
#define RTL8139EX_TCR_MXDMA_1024    (6 << 8)
#define RTL8139EX_TCR_MXDMA_2048    (7 << 8)
#define RTL8139EX_TCR_IFG_MASK      (3 << 24) /* Interframe Gap                 */
#define RTL8139EX_TCR_IFG_NORMAL    (0 << 24) /* Normal (96 bit times)          */
#define RTL8139EX_TCR_IFG_EXTRA     (1 << 24) /* Extra (128 bit times)          */
#define RTL8139EX_TCR_HWVERID_MASK  (0x7C << 2) /* Hardware Version ID           */
#define RTL8139EX_TCR_TXRR          (1 << 27) /* TX Retry                       */
#define RTL8139EX_TCR_LBK_MASK      (3 << 17) /* Loopback Test                  */

/* ---- 93C46 (EEPROM) 命令寄存器 ---- */
#define RTL8139EX_9346CR_EEM_MASK   (3 << 6)  /* EEPROM Mode                    */
#define RTL8139EX_9346CR_EEM_NORMAL (0 << 6)  /* Normal (no access)             */
#define RTL8139EX_9346CR_EEM_AUTO   (1 << 6)  /* Auto-load                      */
#define RTL8139EX_9346CR_EEM_PROG   (2 << 6)  /* Programming mode               */
#define RTL8139EX_9346CR_EEM_CFG    (3 << 6)  /* Configuration mode             */
#define RTL8139EX_9346CR_EECS       (1 << 3)  /* EEPROM Chip Select             */
#define RTL8139EX_9346CR_EESK       (1 << 2)  /* EEPROM Serial Clock            */
#define RTL8139EX_9346CR_EEDI       (1 << 1)  /* EEPROM Data In                 */
#define RTL8139EX_9346CR_EEDO       (1 << 0)  /* EEPROM Data Out                */

/* ---- CONFIG1 配置位 ---- */
#define RTL8139EX_CONFIG1_SLEEP     (1 << 0)  /* Sleep Mode                     */
#define RTL8139EX_CONFIG1_VPD       (1 << 1)  /* Vital Product Data Enabled     */
#define RTL8139EX_CONFIG1_PMEn      (1 << 5)  /* Power Management Enable        */
#define RTL8139EX_CONFIG1_PMEN      (1 << 6)  /* PM Enable                      */
#define RTL8139EX_CONFIG1_LWACT     (1 << 7)  /* Link Wake Action               */

/* ---- CONFIG3 配置位 ---- */
#define RTL8139EX_CONFIG3_FBtBEn    (1 << 0)  /* Fast Back-to-Back Enable       */
#define RTL8139EX_CONFIG3_CardB_En  (1 << 1)  /* CardBus Enable                 */
#define RTL8139EX_CONFIG3_CLKRUN_En (1 << 2)  /* CLKRUN Enable                  */
#define RTL8139EX_CONFIG3_FuncRegEn (1 << 3)  /* Function Register Enable       */
#define RTL8139EX_CONFIG3_LinkUp    (1 << 4)  /* Link Up                        */
#define RTL8139EX_CONFIG3_Magic     (1 << 5)  /* Magic Packet                   */
#define RTL8139EX_CONFIG3_WOL       (1 << 6)  /* Wake on LAN                    */

/* ---- CONFIG4 配置位 ---- */
#define RTL8139EX_CONFIG4_LWPME     (1 << 0)  /* LWAKE for PME                  */
#define RTL8139EX_CONFIG4_LWPTN     (1 << 1)  /* LWAKE for Pattern              */
#define RTL8139EX_CONFIG4_LWPM      (1 << 2)  /* LWAKE for Magic Packet         */
#define RTL8139EX_CONFIG4_LWPTNO    (1 << 3)  /* LWAKE for Pattern (not)        */
#define RTL8139EX_CONFIG4_LWLAN     (1 << 4)  /* LWAKE for Link-On              */
#define RTL8139EX_CONFIG4_LWLANO    (1 << 5)  /* LWAKE for Link-Off             */

/* ---- MSR 媒体状态位 ---- */
#define RTL8139EX_MSR_RXPF          (1 << 0)  /* RX Pause Flag                  */
#define RTL8139EX_MSR_TXPF          (1 << 1)  /* TX Pause Flag                  */
#define RTL8139EX_MSR_LINKB         (1 << 2)  /* Link Status Bad                */
#define RTL8139EX_MSR_SPEED_10      (1 << 3)  /* Speed: 10 Mbps                 */
#define RTL8139EX_MSR_SPEED_100     (1 << 4)  /* Speed: 100 Mbps                */
#define RTL8139EX_MSR_TXFCE         (1 << 5)  /* TX Flow Control Enable         */
#define RTL8139EX_MSR_RXFCE         (1 << 6)  /* RX Flow Control Enable         */
#define RTL8139EX_MSR_DUPLEX        (1 << 7)  /* Duplex Mode: 1=Full            */

/* ---- 描述符环大小 ---- */
#define RTL8139EX_NUM_TX_DESC       4         /* 4 TX descriptors w/ chaining   */
#define RTL8139EX_RX_BUF_SIZE       8192      /* 8K RX ring buffer + 16 bytes   */
#define RTL8139EX_TX_BUF_SIZE       2048

/* ---- TX Descriptor 结构 ---- */
typedef struct {
    uint32_t tsd;         /* TX Status Descriptor (packet options)  */
    uint32_t addr;        /* Packet physical address                */
    void    *next;        /* Chaining: next descriptor             */
} rtl8139ex_tx_desc_t;

/* ---- 公共接口 ---- */
void rtl8139ex_init(uint8_t bus, uint8_t dev, uint8_t func);
int  rtl8139ex_send(net_interface_t *iface, const void *data, uint32_t len);
void rtl8139ex_poll(void);
void rtl8139ex_irq_handler(regs_t *regs);

/* ---- PCI 探测入口 ---- */
int rtl8139ex_probe(void);

#endif /* RTL8139_EX_H */