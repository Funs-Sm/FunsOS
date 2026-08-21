#ifndef RTL8822BE_H
#define RTL8822BE_H

#include "stdint.h"

/* Realtek RTL8822BE 802.11ac 2x2 Wi-Fi + BT combo card driver.
 *
 * PCIe device, vendor 0x10EC, device 0xB822 (RTL8822BE) / 0xC822 (RTL8822BE-VS).
 * Implements the firmware command/response protocol used by the in-tree
 * Realtek driver and provides a simplified netdev integration with 802.11
 * management frame handling.
 *
 * This driver implements:
 *   - PCIe config probe and BAR2 (firmware) + BAR0 (MAC) setup
 *   - H2C (host-to-card) mailbox command ring and firmware download
 *   - 802.11 frame TX/RX queues (ring-based DMA descriptors)
 *   - Basic SCAN / CONNECT / DISCONNECT commands
 *   - Regulatory domain and channel list management
 *
 * Reference: RTL8822B hardware specification (vendor proprietary).
 */

#define RTL8822BE_VENDOR_ID    0x10EC
#define RTL8822BE_DEVICE_ID    0xB822

/* BAR0 - MAC register space (32KB) */
#define RTL8822BE_BAR0_SIZE    0x8000
#define RTL8822BE_BAR0_MASK    (RTL8822BE_BAR0_SIZE - 1)

/* BAR2 - Firmware DMA window (typically 1MB) */
#define RTL8822BE_BAR2_SIZE    (1024 * 1024)

/* Selected MAC registers */
#define RTL8822BE_REG_SYS_FUNC_EN        0x0002
#define RTL8822BE_REG_MAC_CR             0x0100
#define RTL8822BE_REG_MAC_TX_TSF_L       0x0510
#define RTL8822BE_REG_MAC_RX_TSF_L       0x0518
#define RTL8822BE_REG_MAC_BCN_CTRL        0x0550
#define RTL8822BE_REG_MAC_RXDMA_AGG      0x0408
#define RTL8822BE_REG_MAC_TXDMA_AGG      0x0414
#define RTL8822BE_REG_TXDESC_BASE        0x0300
#define RTL8822BE_REG_RXDESC_BASE        0x0310
#define RTL8822BE_REG_H2C_CMD_REG        0x0370  /* H2C mailbox doorbell */
#define RTL8822BE_REG_C2H_INT            0x0371  /* C2H interrupt doorbell */

/* H2C command classes */
#define H2C_CMD_FW_DOWNLOAD      0x00
#define H2C_CMD_INIT             0x01
#define H2C_CMD_SCAN             0x02
#define H2C_CMD_JOIN             0x03
#define H2C_CMD_LEAVE            0x04
#define H2C_CMD_TX_REPORT        0x05
#define H2C_CMD_SET_PWR_MODE     0x20

/* PHY parameters / rates */
#define RTL8822BE_MAX_TX_RATE_HT_MCS9 0x19
#define RTL8822BE_BAND_2GHZ  0x0
#define RTL8822BE_BAND_5GHZ  0x1

/* TX/RX ring sizes (must be power of two) */
#define RTL8822BE_TX_RING_SIZE  128
#define RTL8822BE_RX_RING_SIZE  128

/* Maximum firmware chunk size for H2C */
#define RTL8822BE_FW_CHUNK_SIZE  196

/* 802.11 SSID and rates */
#define RTL8822BE_SSID_MAX_LEN    32
#define RTL8822BE_MAX_RATES       12
#define RTL8822BE_MAX_CHANNELS_2G 14
#define RTL8822BE_MAX_CHANNELS_5G 200

/* Descriptor format (simplified for documentation; full struct is vendor
 * proprietary and uses reserved fields). */
typedef struct __attribute__((packed)) {
    uint32_t ctrl0;        /* TX/RX control + flags */
    uint32_t ctrl1;        /* buffer size, ring info */
    uint32_t addr_lo;
    uint32_t addr_hi;
    uint32_t ctrl2;        /* TX-specific: rate, retry limit */
    uint32_t ctrl3;        /* RX-specific: PHY info */
} rtl8822be_desc_t;

typedef struct {
    uint32_t desc_base;
    uint32_t desc_paddr;   /* DMA address */
    uint32_t desc_count;
    uint32_t desc_head;
    uint32_t desc_tail;
    void    *buf_virt;
    uint32_t buf_paddr;
    uint32_t buf_size;
    int      in_use;
} rtl8822be_ring_t;

typedef struct {
    /* Per-channel configuration */
    uint8_t  band;             /* RTL8822BE_BAND_* */
    uint8_t  channel;
    uint8_t  bw;               /* 20 / 40 / 80 MHz */
    int8_t   tx_power_dbm;
    /* 802.11 rates supported (units of 500 kbps). */
    uint8_t  rates[RTL8822BE_MAX_RATES];
    uint8_t  rate_count;
} rtl8822be_channel_t;

typedef struct {
    /* Device identification. */
    uint8_t  mac[6];
    uint32_t firmware_version;
    int      firmware_loaded;
    /* Active connection. */
    char     ssid[RTL8822BE_SSID_MAX_LEN + 1];
    uint8_t  bssid[6];
    uint8_t  channel;
    int      connected;
    /* Channels. */
    rtl8822be_channel_t channels_2g[RTL8822BE_MAX_CHANNELS_2G];
    rtl8822be_channel_t channels_5g[RTL8822BE_MAX_CHANNELS_5G];
    uint8_t  n_channels_2g;
    uint8_t  n_channels_5g;
    /* Rings. */
    rtl8822be_ring_t tx_ring;
    rtl8822be_ring_t rx_ring;
    /* Stats. */
    uint64_t tx_frames;
    uint64_t rx_frames;
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    uint64_t tx_errors;
    uint64_t rx_errors;
} rtl8822be_state_t;

/* Public API */
int  rtl8822be_init(uint8_t bus, uint8_t dev, uint8_t func);
int  rtl8822be_scan(void);
int  rtl8822be_connect(const char *ssid, uint8_t channel);
int  rtl8822be_disconnect(void);
int  rtl8822be_get_mac(uint8_t *mac);
const rtl8822be_state_t *rtl8822be_get_state(void);

#endif