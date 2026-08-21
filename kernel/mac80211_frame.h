#ifndef MAC80211_FRAME_H
#define MAC80211_FRAME_H

#include "stdint.h"

/* IEEE 802.11 frame parser.
 *
 * Decodes the MAC header for mgmt / ctrl / data frames, including
 * - Frame Control fields (Type / Subtype / ToDS / FromDS / ...)
 * - Sequence Control
 * - Address 1..4 (semantics depend on To/From DS)
 * - QoS Control (for QoS data)
 * - HT / VHT Control
 * - HT/VHT Capable signaling
 *
 * Also provides encoders for the frames the kernel needs to emit
 * (auth, deauth, assoc req/resp, probe req/resp, data, action).
 */

#define MAC80211_FCTL_VER_MASK     0x0003
#define MAC80211_FCTL_TYPE_MASK    0x000C
#define MAC80211_FCTL_SUBTYPE_MASK 0x00F0
#define MAC80211_FCTL_TODS         0x0100
#define MAC80211_FCTL_FROMDS       0x0200
#define MAC80211_FCTL_MOREFRAG     0x0400
#define MAC80211_FCTL_RETRY        0x0800
#define MAC80211_FCTL_PWRMGT       0x1000
#define MAC80211_FCTL_MOREDATA     0x2000
#define MAC80211_FCTL_PROTECTED    0x4000
#define MAC80211_FCTL_ORDER        0x8000

#define MAC80211_TYPE_MGMT         0x00
#define MAC80211_TYPE_CTRL         0x04
#define MAC80211_TYPE_DATA         0x08
#define MAC80211_TYPE_EXT          0x0C

#define MAC80211_SUBTYPE_ASSOC_REQ    0x00
#define MAC80211_SUBTYPE_ASSOC_RESP   0x10
#define MAC80211_SUBTYPE_REASSOC_REQ  0x20
#define MAC80211_SUBTYPE_REASSOC_RESP 0x30
#define MAC80211_SUBTYPE_PROBE_REQ    0x40
#define MAC80211_SUBTYPE_PROBE_RESP   0x50
#define MAC80211_SUBTYPE_BEACON       0x80
#define MAC80211_SUBTYPE_DISASSOC     0xA0
#define MAC80211_SUBTYPE_AUTH         0xB0
#define MAC80211_SUBTYPE_DEAUTH       0xC0
#define MAC80211_SUBTYPE_ACTION       0xD0

#define MAC80211_SUBTYPE_DATA         0x00
#define MAC80211_SUBTYPE_NULL         0x40
#define MAC80211_SUBTYPE_QOS_DATA     0x80
#define MAC80211_SUBTYPE_QOS_NULL     0xC0

#define MAC80211_MAX_FRAME_LEN  4096
#define MAC80211_MAC_LEN       6
#define MAC80211_SSID_MAX_LEN  32

#define MAC80211_PROT_NONE     0
#define MAC80211_PROT_WEP      1
#define MAC80211_PROT_TKIP     2
#define MAC80211_PROT_CCMP     3
#define MAC80211_PROT_GCMP     4

typedef struct __attribute__((packed)) {
    uint16_t frame_control;
    uint16_t duration_id;
    uint8_t  addr1[6];
    uint8_t  addr2[6];
    uint8_t  addr3[6];
    uint16_t seq_ctrl;
    uint8_t  addr4[6];      /* optional, To+FromDS only */
} mac80211_hdr_t;

typedef struct {
    /* Decoded MAC header. */
    uint16_t fc;
    uint16_t duration;
    uint8_t  addr1[6];
    uint8_t  addr2[6];
    uint8_t  addr3[6];
    uint8_t  addr4[6];
    uint16_t seq_ctrl;
    uint8_t  has_addr4;
    /* Decoded QoS control (only for QoS data). */
    uint16_t qos_ctrl;
    uint8_t  has_qos;
    /* Decoded HT control (for HT variants). */
    uint32_t ht_ctrl;
    uint8_t  has_ht;
    /* Frame body + length. */
    const uint8_t *body;
    uint32_t body_len;
    /* Aggregate type. */
    uint8_t  is_aggregate;
} mac80211_decoded_t;

/* Parsing. */
int mac80211_parse(const uint8_t *buf, uint32_t len,
                   mac80211_decoded_t *out);

/* Constructors. */
uint32_t mac80211_build_beacon(const uint8_t *src, const uint8_t *bssid,
                               uint16_t seq_ctrl, const uint8_t *ssid,
                               uint8_t channel, uint8_t *out);
uint32_t mac80211_build_probe_req(const uint8_t *src,
                                   const uint8_t *ssid,
                                   uint8_t *out);
uint32_t mac80211_build_auth(const uint8_t *src, const uint8_t *dst,
                              uint16_t seq, uint16_t alg, uint16_t status,
                              uint8_t *out);
uint32_t mac80211_build_deauth(const uint8_t *src, const uint8_t *dst,
                                uint16_t reason, uint8_t *out);
uint32_t mac80211_build_assoc_req(const uint8_t *src, const uint8_t *bssid,
                                   uint16_t seq, uint8_t *out);
uint32_t mac80211_build_data(const uint8_t *src, const uint8_t *dst,
                              const uint8_t *bssid, uint16_t seq,
                              uint8_t qos, const uint8_t *payload,
                              uint16_t payload_len, uint8_t *out);

const char *mac80211_type_name(uint16_t fc);
const char *mac80211_subtype_name(uint16_t fc);
int mac80211_is_protected(uint16_t fc);
uint8_t mac80211_get_type(uint16_t fc);
uint8_t mac80211_get_subtype(uint16_t fc);

#endif