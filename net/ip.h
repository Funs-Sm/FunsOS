#ifndef IP_H
#define IP_H

#include "net.h"
#include "stdint.h"

#define IP_PROTO_ICMP     1
#define IP_PROTO_TCP      6
#define IP_PROTO_UDP     17
#define IP_PROTO_UDP_LITE 136

#define IP_FLAG_DF  0x4000
#define IP_FLAG_MF  0x2000
#define IP_FLAG_EVIL 0x8000   /* RFC 3514 evil bit (parsed but ignored) */

#define IP_FRAG_TIMEOUT_MS  30000U
#define IP_REASM_MAX         32
#define IP_DEFAULT_TTL       64

typedef struct __attribute__((packed)) {
    uint8_t version_ihl;
    uint8_t tos;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    ipv4_addr_t src_ip;
    ipv4_addr_t dst_ip;
} ip_header_t;

typedef struct {
    net_interface_t *iface;
    ipv4_addr_t gateway;
} ip_route_t;

typedef struct {
    /* Reassembly cache statistics (RFC 815 / 791) */
    uint32_t packets_sent;
    uint32_t packets_rcvd;
    uint32_t fragments_sent;
    uint32_t fragments_rcvd;
    uint32_t reassembled;
    uint32_t dropped;
    uint32_t no_route;
    uint32_t checksum_errors;
    uint32_t ttl_expired;
} ip_stats_t;

void ip_init(void);
int ip_send(net_interface_t *iface, ipv4_addr_t dst, uint8_t proto, const void *payload, uint32_t len);
int ip_send_with_ttl(net_interface_t *iface, ipv4_addr_t dst, uint8_t proto, const void *payload, uint32_t len, uint8_t ttl, uint8_t tos);
void ip_receive(net_buffer_t *buf);
uint16_t ip_checksum(const void *data, uint32_t len);
ip_route_t ip_route_lookup(ipv4_addr_t dst);
int ip_fragment_send(net_interface_t *iface, ipv4_addr_t dst, uint8_t proto, const void *payload, uint32_t len, uint8_t ttl, uint8_t tos, uint16_t ident);
void ip_reassemble_tick(uint32_t now_ms);
const ip_stats_t *ip_get_stats(void);

/* Path MTU cache (RFC 1191).  Each entry caches the smallest MTU
 * we have observed for a given destination and expires after 10
 * minutes (RFC 1191 §6).  The PMTU is consulted by ip_send_with_ttl
 * so that large datagrams are fragmented along the way rather than
 * being dropped at the first hop. */
#define IP_PMTU_MAX_ENTRIES 32
#define IP_PMTU_DEFAULT     1500
#define IP_PMTU_TIMEOUT_MS  (10 * 60 * 1000U)

int  ip_pmtu_get(ipv4_addr_t dst);
void ip_pmtu_update(ipv4_addr_t dst, uint32_t mtu);
void ip_pmtu_age(uint32_t now_ms);
void ip_pmtu_clear(void);

/* IP options (RFC 791 §3.1).  parse_options() walks the option
 * region of a received header and dispatches recognised options to
 * the supplied handler.  build_options() composes an option region
 * for outgoing datagrams (currently used to honour the Record Route
 * option for diagnostic tools). */
#define IP_OPT_EOOL   0
#define IP_OPT_NOP    1
#define IP_OPT_RR     7   /* Record Route                          */
#define IP_OPT_TS     68  /* Timestamp                             */
#define IP_OPT_LSRR   131 /* Loose Source & Record Route           */
#define IP_OPT_SSRR   137 /* Strict Source & Record Route          */

typedef int (*ip_opt_handler_t)(uint8_t kind, const uint8_t *data, uint8_t len, void *ctx);

int  ip_parse_options(const uint8_t *options, uint32_t len, ip_opt_handler_t h, void *ctx);
int  ip_build_record_route(const ipv4_addr_t *addrs, uint8_t count, uint8_t *out, uint32_t max);
int  ip_options_length(const uint8_t *options, uint32_t len);

/* ---- QoS / Packet Scheduling ---- */

/* Queue disciplines */
#define QDISC_PFIFO_FAST    0    /* 3-band priority FIFO */
#define QDISC_PFIFO         1    /* simple FIFO */
#define QDISC_HTB           2    /* Hierarchical Token Bucket (simplified) */
#define QDISC_FQ_CODEL      3    /* Fair Queue + CoDel (simplified) */

/* Priority bands for pfifo_fast (derived from TOS/DSCP) */
#define QOS_BAND_LO    0
#define QOS_BAND_MID   1
#define QOS_BAND_HI    2
#define QOS_BAND_MAX   3

/* DSCP to priority band mapping (simplified) */
#define DSCP_CLASS_CS0  0
#define DSCP_CLASS_CS1  8
#define DSCP_CLASS_AF11 10
#define DSCP_CLASS_AF12 12
#define DSCP_CLASS_AF13 14
#define DSCP_CLASS_CS2  16
#define DSCP_CLASS_AF21 18
#define DSCP_CLASS_AF22 20
#define DSCP_CLASS_AF23 22
#define DSCP_CLASS_CS3  24
#define DSCP_CLASS_AF31 26
#define DSCP_CLASS_AF32 28
#define DSCP_CLASS_AF33 30
#define DSCP_CLASS_CS4  32
#define DSCP_CLASS_AF41 34
#define DSCP_CLASS_AF42 36
#define DSCP_CLASS_AF43 38
#define DSCP_CLASS_CS5  40
#define DSCP_CLASS_EF   46
#define DSCP_CLASS_CS6  48
#define DSCP_CLASS_CS7  56

/* Token bucket filter parameters */
typedef struct {
    uint32_t rate;           /* bytes per second */
    uint32_t burst;          /* max burst size in bytes */
    uint32_t tokens;         /* current token count */
    uint32_t last_update_ms;  /* last token replenish time */
} tbf_params_t;

/* QoS queue statistics */
typedef struct {
    uint32_t packets_queued[QOS_BAND_MAX];
    uint32_t packets_dequeued[QOS_BAND_MAX];
    uint32_t packets_dropped[QOS_BAND_MAX];
    uint32_t bytes_queued[QOS_BAND_MAX];
    uint32_t bytes_dequeued[QOS_BAND_MAX];
    uint32_t high_prio_steals;    /* times hi band starved others */
} qos_stats_t;

int  qos_init(void);
int  qos_set_discipline(uint8_t disc);
uint8_t qos_get_discipline(void);
int  qos_enqueue(net_buffer_t *buf, uint8_t tos);
net_buffer_t *qos_dequeue(net_interface_t *iface);
int  qos_dscp_to_band(uint8_t dscp);
int  qos_set_rate_limit(uint32_t rate_bps, uint32_t burst_bytes);
void qos_tick(uint32_t now_ms);
const qos_stats_t *qos_get_stats(void);

/* ---- IPsec (AH / ESP basics) ---- */

#define IPSEC_PROTO_AH   51
#define IPSEC_PROTO_ESP  50

/* Security Association */
typedef struct {
    uint32_t    spi;               /* Security Parameter Index */
    ipv4_addr_t dst;
    uint8_t     proto;             /* IPSEC_PROTO_AH or IPSEC_PROTO_ESP */
    uint8_t     mode;              /* 0 = transport, 1 = tunnel */
    uint8_t     enc_alg;           /* 0 = none, 1 = AES-CBC, 2 = 3DES */
    uint8_t     auth_alg;          /* 0 = none, 1 = HMAC-SHA1, 2 = HMAC-SHA256 */
    uint8_t     enc_key[32];       /* encryption key */
    uint8_t     enc_key_len;
    uint8_t     auth_key[32];      /* authentication key */
    uint8_t     auth_key_len;
    uint32_t    lifetime_soft;     /* soft lifetime (bytes) */
    uint32_t    lifetime_hard;     /* hard lifetime (bytes) */
    uint32_t    bytes_processed;
    uint32_t    packets_processed;
    uint8_t     used;
} ipsec_sa_t;

/* AH header (RFC 4302) */
typedef struct __attribute__((packed)) {
    uint8_t  next_header;
    uint8_t  payload_len;
    uint16_t reserved;
    uint32_t spi;
    uint32_t sequence;
    /* followed by ICV (Integrity Check Value) */
} ah_header_t;

/* ESP header (RFC 4303) */
typedef struct __attribute__((packed)) {
    uint32_t spi;
    uint32_t sequence;
    /* followed by payload, padding, pad_len, next_header */
} esp_header_t;

/* ESP trailer */
typedef struct __attribute__((packed)) {
    uint8_t  pad_len;
    uint8_t  next_header;
    /* followed by ICV */
} esp_trailer_t;

#define IPSEC_SA_MAX     32
#define IPSEC_SPD_MAX    64

/* Security Policy Database entry */
typedef struct {
    uint8_t     used;
    uint8_t     action;            /* 0 = bypass, 1 = protect, 2 = discard */
    ipv4_addr_t src;
    ipv4_addr_t src_mask;
    ipv4_addr_t dst;
    ipv4_addr_t dst_mask;
    uint8_t     proto;
    uint16_t    sport;
    uint16_t    dport;
    uint32_t    spi_out;           /* SPI to use for outgoing */
    uint32_t    spi_in;            /* SPI to use for incoming */
    uint32_t    counter;
} ipsec_spd_entry_t;

typedef struct {
    uint32_t sa_created;
    uint32_t sa_deleted;
    uint32_t sa_expired;
    uint32_t pkts_encrypted;
    uint32_t pkts_decrypted;
    uint32_t pkts_auth_ok;
    uint32_t pkts_auth_fail;
    uint32_t pkts_discarded;
    uint32_t pkts_bypassed;
} ipsec_stats_t;

int  ipsec_sa_add(uint32_t spi, ipv4_addr_t dst, uint8_t proto, uint8_t mode,
                   uint8_t enc_alg, const uint8_t *enc_key, uint8_t enc_key_len,
                   uint8_t auth_alg, const uint8_t *auth_key, uint8_t auth_key_len);
int  ipsec_sa_del(uint32_t spi, ipv4_addr_t dst);
ipsec_sa_t *ipsec_sa_lookup(uint32_t spi, ipv4_addr_t dst, uint8_t proto);
void ipsec_sa_tick(uint32_t now_ms);
void ipsec_sa_flush(void);

int  ipsec_spd_add(uint8_t action, ipv4_addr_t src, ipv4_addr_t src_mask,
                    ipv4_addr_t dst, ipv4_addr_t dst_mask, uint8_t proto,
                    uint16_t sport, uint16_t dport, uint32_t spi_in, uint32_t spi_out);
int  ipsec_spd_del(uint32_t idx);
ipsec_spd_entry_t *ipsec_spd_lookup(net_buffer_t *buf, int dir);
int  ipsec_spd_apply(net_buffer_t *buf, int dir);

/* Output processing */
int  ipsec_output(net_buffer_t *buf);
/* Input processing */
int  ipsec_input(net_buffer_t *buf);

void ipsec_init(void);
const ipsec_stats_t *ipsec_get_stats(void);

#endif
