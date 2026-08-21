#ifndef MT76_USB_H
#define MT76_USB_H

#include "stdint.h"

/* MediaTek MT76xx series USB Wi-Fi adapter driver.
 *
 * Covers the USB variants of the MT76 family (MT7610U, MT7612U, MT7632U,
 * MT7662U, MT76x0u / MT76x2u). The driver focuses on the modern mt76
 * mac80211 driver-style architecture:
 *   - DMA rings (TX/RX) shared with USB bulk endpoints
 *   - Scatter-gather TSO/TXWI descriptors
 *   - Firmware load via USB bulk transfer
 *   - Rate control (hardware rate table)
 *   - 802.11n/ac aggregation via AMPDU
 *
 * Note: this is a "modeled" driver; on real hardware it depends on the
 * vendor firmware blob (mcu.bin). The driver loads the firmware via a
 * USB control transfer and switches the chip to "FW_RUNNING" mode.
 */

#define MT76_USB_VENDOR_ID_MEDIATEK     0x148C
#define MT76_USB_DEVICE_ID_MT7610U      0x761A
#define MT76_USB_DEVICE_ID_MT7612U      0x7612
#define MT76_USB_DEVICE_ID_MT7632U      0x7632
#define MT76_USB_DEVICE_ID_MT7662U      0x7662

/* Firmware interface constants */
#define MT76_FW_LOAD_CMD        0x01
#define MT76_FW_START_CMD       0x02
#define MT76_FW_RUNNING_STATE   0x03
#define MT76_FW_LOAD_TYPE_BIN   0x01

/* Registers over USB control pipe */
#define MT76_REG_REG_ID         0x0000
#define MT76_REG_CSR_VERSION    0x0001
#define MT76_REG_FCE_CTRL       0x0010
#define MT76_REG_WPDMA_GLO_CFG  0x0208
#define MT76_REG_WPDMA_RST_IDX  0x020C
#define MT76_REG_WPDMA_TX_BASE  0x0230
#define MT76_REG_WPDMA_RX_BASE  0x0238

/* DMA global config bits */
#define MT76_WPDMA_GLO_TX_EN     (1 << 0)
#define MT76_WPDMA_GLO_RX_EN     (1 << 1)
#define MT76_WPDMA_GLO_TX_DMA_BUSY (1 << 2)
#define MT76_WPDMA_GLO_RX_DMA_BUSY (1 << 3)

/* TX descriptor / TXWI fields. */
#define MT76_TX_RING_SIZE  256
#define MT76_RX_RING_SIZE  512
#define MT76_TXWI_SIZE     32
#define MT76_RXWI_SIZE     24

/* Channel definition. */
typedef struct {
    uint8_t band;          /* 0=2G, 1=5G */
    uint8_t channel;
    uint8_t bw;            /* 20 / 40 / 80 */
    uint8_t n_tx_streams;
    uint8_t n_rx_streams;
} mt76_channel_t;

typedef struct {
    uint8_t  mac[6];
    uint8_t  state;        /* 0=off, 1=loading, 2=ready, 3=err */
    uint32_t firmware_size;
    /* DMA addresses. */
    uint32_t tx_ring_paddr;
    uint32_t rx_ring_paddr;
    /* DMA virt addresses. */
    void    *tx_ring_virt;
    void    *rx_ring_virt;
    uint32_t tx_ring_count;
    uint32_t rx_ring_count;
    /* Channels. */
    mt76_channel_t channels_2g[14];
    mt76_channel_t channels_5g[200];
    uint8_t n_channels_2g;
    uint8_t n_channels_5g;
    /* Stats. */
    uint64_t tx_frames;
    uint64_t rx_frames;
    uint64_t tx_failures;
} mt76_state_t;

int mt76_usb_init(uint8_t usb_addr);
int mt76_usb_load_firmware(const void *fw_data, uint32_t fw_size);
int mt76_usb_scan(void);
int mt76_usb_connect(const char *ssid, uint8_t channel);
const mt76_state_t *mt76_usb_get_state(void);

#endif