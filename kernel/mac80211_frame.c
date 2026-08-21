/* mac80211_frame.c - 802.11 frame parser and constructors.
 *
 * Implements decoding of the 802.11 MAC header plus a small set of
 * frame constructors commonly needed by a STA: probe req, beacon,
 * auth/deauth, assoc req, data.
 */

#include "mac80211_frame.h"
#include "string.h"

#define HT_CONTROL_FIELD_PRESENT (1 << 15)

uint8_t mac80211_get_type(uint16_t fc) {
    return (uint8_t)((fc & MAC80211_FCTL_TYPE_MASK) >> 2);
}
uint8_t mac80211_get_subtype(uint16_t fc) {
    return (uint8_t)((fc & MAC80211_FCTL_SUBTYPE_MASK) >> 4);
}

int mac80211_is_protected(uint16_t fc) {
    return (fc & MAC80211_FCTL_PROTECTED) ? 1 : 0;
}

const char *mac80211_type_name(uint16_t fc) {
    switch (mac80211_get_type(fc)) {
        case MAC80211_TYPE_MGMT: return "mgmt";
        case MAC80211_TYPE_CTRL: return "ctrl";
        case MAC80211_TYPE_DATA: return "data";
        case MAC80211_TYPE_EXT:  return "ext";
    }
    return "unknown";
}

const char *mac80211_subtype_name(uint16_t fc) {
    switch (mac80211_get_type(fc)) {
        case MAC80211_TYPE_MGMT: switch (mac80211_get_subtype(fc)) {
            case MAC80211_SUBTYPE_ASSOC_REQ: return "assoc_req";
            case MAC80211_SUBTYPE_ASSOC_RESP: return "assoc_resp";
            case MAC80211_SUBTYPE_REASSOC_REQ: return "reassoc_req";
            case MAC80211_SUBTYPE_REASSOC_RESP: return "reassoc_resp";
            case MAC80211_SUBTYPE_PROBE_REQ: return "probe_req";
            case MAC80211_SUBTYPE_PROBE_RESP: return "probe_resp";
            case MAC80211_SUBTYPE_BEACON: return "beacon";
            case MAC80211_SUBTYPE_DISASSOC: return "disassoc";
            case MAC80211_SUBTYPE_AUTH: return "auth";
            case MAC80211_SUBTYPE_DEAUTH: return "deauth";
            case MAC80211_SUBTYPE_ACTION: return "action";
        } break;
        case MAC80211_TYPE_DATA: switch (mac80211_get_subtype(fc)) {
            case MAC80211_SUBTYPE_DATA:     return "data";
            case MAC80211_SUBTYPE_NULL:     return "null";
            case MAC80211_SUBTYPE_QOS_DATA: return "qos_data";
            case MAC80211_SUBTYPE_QOS_NULL: return "qos_null";
        } break;
        case MAC80211_TYPE_CTRL: return "ctrl";
        case MAC80211_TYPE_EXT:  return "ext";
    }
    return "unknown";
}

int mac80211_parse(const uint8_t *buf, uint32_t len,
                    mac80211_decoded_t *out)
{
    if (!buf || !out || len < 24) return -1;
    memset(out, 0, sizeof(*out));
    out->fc = (uint16_t)(buf[0] | (buf[1] << 8));
    out->duration = (uint16_t)(buf[2] | (buf[3] << 8));
    memcpy(out->addr1, buf + 4, 6);
    memcpy(out->addr2, buf + 10, 6);
    memcpy(out->addr3, buf + 16, 6);
    out->seq_ctrl = (uint16_t)(buf[22] | (buf[23] << 8));

    int tods = (out->fc & MAC80211_FCTL_TODS) ? 1 : 0;
    int fromds = (out->fc & MAC80211_FCTL_FROMDS) ? 1 : 0;
    uint32_t hdr_len = 24;
    if (tods && fromds) {
        if (len < 30) return -1;
        memcpy(out->addr4, buf + 24, 6);
        out->has_addr4 = 1;
        hdr_len += 6;
    }

    /* QoS data subtype carries a 2-byte QoS Control. */
    uint8_t type = mac80211_get_type(out->fc);
    uint8_t subtype = mac80211_get_subtype(out->fc);
    if (type == MAC80211_TYPE_DATA && (subtype & 0x80)) {
        if (len < hdr_len + 2) return -1;
        out->qos_ctrl = (uint16_t)(buf[hdr_len] | (buf[hdr_len + 1] << 8));
        out->has_qos = 1;
        hdr_len += 2;
        /* HT control is present when QoS bit 7 of qos_ctrl is set. */
        if (out->qos_ctrl & HT_CONTROL_FIELD_PRESENT) {
            if (len < hdr_len + 4) return -1;
            out->ht_ctrl = (uint32_t)buf[hdr_len]
                         | ((uint32_t)buf[hdr_len + 1] << 8)
                         | ((uint32_t)buf[hdr_len + 2] << 16)
                         | ((uint32_t)buf[hdr_len + 3] << 24);
            out->has_ht = 1;
            hdr_len += 4;
        }
    }
    out->body = buf + hdr_len;
    out->body_len = len - hdr_len;
    return 0;
}

/* ---- Constructors ---- */

static void put_le16(uint8_t *out, uint16_t v) {
    out[0] = (uint8_t)v; out[1] = (uint8_t)(v >> 8);
}
static void put_mac(uint8_t *out, const uint8_t *mac) {
    memcpy(out, mac, 6);
}

/* Common IE builders. */
static uint32_t put_ssid_ie(uint8_t *out, const uint8_t *ssid) {
    uint32_t len = 0;
    while (ssid[len] && len < MAC80211_SSID_MAX_LEN) len++;
    out[0] = 0; /* SSID element id */
    out[1] = (uint8_t)len;
    memcpy(out + 2, ssid, len);
    return len + 2;
}

static uint32_t put_dsss_param(uint8_t *out, uint8_t channel) {
    out[0] = 3; /* DSSS Parameter Set id */
    out[1] = 1;
    out[2] = channel;
    return 3;
}

uint32_t mac80211_build_beacon(const uint8_t *src, const uint8_t *bssid,
                                uint16_t seq_ctrl, const uint8_t *ssid,
                                uint8_t channel, uint8_t *out)
{
    if (!out) return 0;
    /* MAC header (24 bytes). */
    put_le16(out, MAC80211_TYPE_MGMT | MAC80211_SUBTYPE_BEACON);
    put_le16(out + 2, 0);
    put_mac(out + 4, bssid);   /* DA = BSSID (broadcast in real use) */
    put_mac(out + 10, src);    /* SA */
    put_mac(out + 16, bssid);  /* BSSID */
    put_le16(out + 22, seq_ctrl);
    uint32_t off = 24;
    /* Timestamp (8 bytes) */
    memset(out + off, 0, 8);
    off += 8;
    /* Beacon interval (2 bytes) */
    put_le16(out + off, 100);
    off += 2;
    /* Capability (2 bytes): 0x21 = ESS, Short Preamble */
    put_le16(out + off, 0x21);
    off += 2;
    /* Tagged IEs: SSID, DSSS Param */
    off += put_ssid_ie(out + off, ssid);
    off += put_dsss_param(out + off, channel);
    return off;
}

uint32_t mac80211_build_probe_req(const uint8_t *src, const uint8_t *ssid,
                                   uint8_t *out)
{
    if (!out) return 0;
    put_le16(out, MAC80211_TYPE_MGMT | MAC80211_SUBTYPE_PROBE_REQ);
    put_le16(out + 2, 0);
    memset(out + 4, 0xFF, 6);     /* DA broadcast */
    put_mac(out + 10, src);
    memset(out + 16, 0xFF, 6);    /* BSSID broadcast */
    put_le16(out + 22, 0);
    uint32_t off = 24;
    off += put_ssid_ie(out + off, ssid);
    return off;
}

uint32_t mac80211_build_auth(const uint8_t *src, const uint8_t *dst,
                              uint16_t seq, uint16_t alg, uint16_t status,
                              uint8_t *out)
{
    if (!out) return 0;
    put_le16(out, MAC80211_TYPE_MGMT | MAC80211_SUBTYPE_AUTH);
    put_le16(out + 2, 0);
    put_mac(out + 4, dst);
    put_mac(out + 10, src);
    put_mac(out + 16, dst);
    put_le16(out + 22, seq);
    uint32_t off = 24;
    put_le16(out + off, alg); off += 2;
    put_le16(out + off, seq); off += 2;
    put_le16(out + off, status); off += 2;
    return off;
}

uint32_t mac80211_build_deauth(const uint8_t *src, const uint8_t *dst,
                                uint16_t reason, uint8_t *out)
{
    if (!out) return 0;
    put_le16(out, MAC80211_TYPE_MGMT | MAC80211_SUBTYPE_DEAUTH);
    put_le16(out + 2, 0);
    put_mac(out + 4, dst);
    put_mac(out + 10, src);
    put_mac(out + 16, dst);
    put_le16(out + 22, 0);
    uint32_t off = 24;
    put_le16(out + off, reason);
    off += 2;
    return off;
}

uint32_t mac80211_build_assoc_req(const uint8_t *src, const uint8_t *bssid,
                                   uint16_t seq, uint8_t *out)
{
    if (!out) return 0;
    put_le16(out, MAC80211_TYPE_MGMT | MAC80211_SUBTYPE_ASSOC_REQ);
    put_le16(out + 2, 0);
    put_mac(out + 4, bssid);
    put_mac(out + 10, src);
    put_mac(out + 16, bssid);
    put_le16(out + 22, seq);
    uint32_t off = 24;
    /* Capability: ESS */
    put_le16(out + off, 0x21);
    off += 2;
    /* Listen interval */
    put_le16(out + off, 100);
    off += 2;
    /* SSID IE */
    off += put_ssid_ie(out + off, (const uint8_t *)"");
    return off;
}

uint32_t mac80211_build_data(const uint8_t *src, const uint8_t *dst,
                              const uint8_t *bssid, uint16_t seq,
                              uint8_t qos, const uint8_t *payload,
                              uint16_t payload_len, uint8_t *out)
{
    if (!out) return 0;
    uint16_t fc = MAC80211_TYPE_DATA | MAC80211_SUBTYPE_DATA;
    if (qos) fc |= MAC80211_SUBTYPE_QOS_DATA;
    /* FromDS=0 ToDS=1 typical infra. */
    fc |= MAC80211_FCTL_TODS;
    put_le16(out, fc);
    put_le16(out + 2, 0);
    put_mac(out + 4, dst);
    put_mac(out + 10, src);
    put_mac(out + 16, bssid);
    put_le16(out + 22, seq);
    uint32_t off = 24;
    if (qos) {
        put_le16(out + off, 0);  /* QoS control: tid=0 */
        off += 2;
    }
    if (payload && payload_len) memcpy(out + off, payload, payload_len);
    off += payload_len;
    return off;
}