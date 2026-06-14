#ifndef FW_H
#define FW_H

#include "net.h"
#include "netfilter.h"
#include "stdint.h"

/* ------------------------------------------------------------------ */
/*  Firewall subsystem: stateful inspection, NAT, rate limiting      */
/* ------------------------------------------------------------------ */

/* Connection state for a 5-tuple flow. */
typedef enum {
    FW_FLOW_NEW        = 0,
    FW_FLOW_ESTABLISHED = 1,
    FW_FLOW_RELATED     = 2,
    FW_FLOW_INVALID     = 3
} fw_flow_state_t;

typedef enum {
    FW_PROTO_ANY = 0,
    FW_PROTO_TCP = 6,
    FW_PROTO_UDP = 17,
    FW_PROTO_ICMP = 1
} fw_proto_t;

typedef struct fw_conn {
    uint32_t hash;             /* hash of the 5-tuple                  */
    ipv4_addr_t src_ip;
    ipv4_addr_t dst_ip;
    uint16_t    src_port;
    uint16_t    dst_port;
    uint8_t     proto;
    uint8_t     state;         /* fw_flow_state_t                      */
    uint8_t     tcp_flags;     /* last observed TCP flags              */
    uint8_t     direction;     /* 0 = orig, 1 = reply                  */

    /* NAT mappings (host byte order).  reply_* is the original
     * tuple before translation. */
    ipv4_addr_t reply_src_ip;
    ipv4_addr_t reply_dst_ip;
    uint16_t    reply_src_port;
    uint16_t    reply_dst_port;
    uint8_t     nat_type;      /* 0 = none, 1 = SNAT, 2 = DNAT         */

    uint32_t    packets;
    uint32_t    bytes;
    uint32_t    last_seen_ms;
    uint32_t    timeout_ms;
    struct fw_conn *next;
} fw_conn_t;

/* NAT rule (very simple -- matches on the original tuple). */
typedef enum {
    FW_NAT_SNAT = 1,
    FW_NAT_DNAT = 2,
    FW_NAT_MASQUERADE = 3
} fw_nat_type_t;

typedef struct fw_nat_rule {
    uint8_t      used;
    uint8_t      type;          /* fw_nat_type_t                       */
    uint8_t      proto;         /* 0 = any, else IPPROTO_*             */
    ipv4_addr_t  orig_src;
    ipv4_addr_t  orig_src_mask;
    ipv4_addr_t  orig_dst;
    ipv4_addr_t  orig_dst_mask;
    uint16_t     orig_port_lo;
    uint16_t     orig_port_hi;
    ipv4_addr_t  trans_src;     /* for SNAT/MASQUERADE                  */
    uint16_t     trans_port_lo;
    uint16_t     trans_port_hi;
    ipv4_addr_t  trans_dst;     /* for DNAT                            */
    uint16_t     out_port_lo;
    uint16_t     out_port_hi;
    char         comment[32];
    uint32_t     counter;
} fw_nat_rule_t;

/* Connection tracking */
#define FW_CONN_MAX 1024
#define FW_CONN_HASH 256

void     fw_init(void);
int      fw_conntrack_packet(net_buffer_t *buf, int hook);
fw_conn_t *fw_conntrack_lookup(uint8_t proto,
                                ipv4_addr_t src, uint16_t sport,
                                ipv4_addr_t dst, uint16_t dport);
void     fw_conntrack_flush(void);
uint32_t fw_conntrack_count(void);
const fw_conn_t *fw_conntrack_at(uint32_t i);

/* NAT */
#define FW_NAT_RULES_MAX 32
int  fw_nat_add(const fw_nat_rule_t *r);
int  fw_nat_delete(uint32_t idx);
void fw_nat_flush(void);
uint32_t fw_nat_count(void);
const fw_nat_rule_t *fw_nat_at(uint32_t i);

/* Apply NAT in place to the buffer.  Returns 1 if the packet was
 * modified, 0 if it passed through untouched, <0 on error. */
int  fw_nat_apply(net_buffer_t *buf, int hook);

/* Master switch: when 0, all fw hooks are pass-through. */
void     fw_set_enabled(int on);
int      fw_is_enabled(void);

/* Stats */
typedef struct {
    uint32_t packets_accepted;
    uint32_t packets_dropped;
    uint32_t packets_nat;
    uint32_t packets_invalid;
    uint32_t conntracks_active;
    uint32_t nat_rules_active;
} fw_stats_t;

const fw_stats_t *fw_get_stats(void);
void fw_reset_stats(void);

/* Convenience: render an IPv4 address into a static string buffer. */
const char *fw_ip_to_str(ipv4_addr_t ip, char *out, uint32_t out_size);

#endif
