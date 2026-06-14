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

#endif
