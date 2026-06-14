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

/* ---- Extended NAT: NAT44, NAT66, ALG ---- */

/* IPv6 address type for NAT66 */
typedef struct {
    uint8_t addr[16];
} ipv6_addr_nat_t;

/* NAT table types */
#define FW_NAT_TABLE_NAT44   0
#define FW_NAT_TABLE_NAT66   1
#define FW_NAT_TABLE_MAX     2

/* NAT binding (maps external to internal for NAT44/NAT66) */
typedef struct fw_nat_binding {
    uint8_t      used;
    uint8_t      type;            /* FW_NAT_SNAT / FW_NAT_DNAT */
    uint8_t      proto;
    uint32_t     ext_ip;          /* external IP (IPv4) or first 4 bytes of IPv6 */
    uint16_t     ext_port;
    uint32_t     int_ip;
    uint16_t     int_port;
    uint8_t      int_mac[6];      /* for full-cone NAT */
    union {
        struct { uint32_t lo, hi; } ipv6_ext;
        uint8_t ext_addr6[16];
    } ext;
    union {
        struct { uint32_t lo, hi; } ipv6_int;
        uint8_t int_addr6[16];
    } internal;
    uint32_t     timeout_ms;
    uint32_t     created_ms;
    uint32_t     packets;
    uint32_t     bytes;
    struct fw_nat_binding *next;
} fw_nat_binding_t;

/* NAT66 prefix translation rule */
typedef struct fw_nat66_prefix {
    uint8_t  used;
    uint8_t  prefix_len;              /* /48, /56, /64 etc. */
    uint8_t  internal_prefix[8];      /* first 64 bits of internal prefix */
    uint8_t  external_prefix[8];      /* first 64 bits of external prefix */
    uint32_t counter;
    char     comment[32];
} fw_nat66_prefix_t;

#define FW_NAT_BINDING_MAX  512
#define FW_NAT_BINDING_HASH 128
#define FW_NAT66_PREFIX_MAX 16

/* NAT Application Layer Gateway (ALG) for FTP, SIP, H.323, etc. */
#define FW_ALG_FTP   0
#define FW_ALG_SIP   1
#define FW_ALG_H323  2
#define FW_ALG_TFTP  3
#define FW_ALG_MAX   8

typedef struct fw_alg {
    uint8_t  used;
    uint8_t  type;
    uint8_t  proto;
    uint16_t ctrl_port;       /* control channel port (e.g. FTP 21) */
    uint32_t packets_processed;
    int      (*handler)(struct fw_alg *alg, net_buffer_t *buf, int hook);
    void     *private_data;
} fw_alg_t;

#define NAT_HELPER_FTP  1
#define NAT_HELPER_SIP  2
#define NAT_HELPER_H323 3

/* ---- NAT extended API ---- */
typedef struct {
    uint32_t bindings_created;
    uint32_t bindings_expired;
    uint32_t bindings_lookup;
    uint32_t nat44_translations;
    uint32_t nat66_translations;
    uint32_t alg_ftp_processed;
    uint32_t alg_sip_processed;
    uint32_t full_cone_entries;
} fw_nat_ext_stats_t;

int  fw_nat44_snat_add(ipv4_addr_t int_src, uint16_t int_port,
                        ipv4_addr_t ext_src, uint16_t ext_port, uint8_t proto);
int  fw_nat44_dnat_add(ipv4_addr_t ext_dst, uint16_t ext_port,
                        ipv4_addr_t int_dst, uint16_t int_port, uint8_t proto);
int  fw_nat44_full_cone_register(ipv4_addr_t int_ip, uint16_t int_port,
                                  ipv4_addr_t ext_ip, uint16_t ext_port, uint8_t proto);
fw_nat_binding_t *fw_nat_binding_lookup(uint8_t proto, uint32_t ip, uint16_t port, int dir);
void fw_nat_binding_tick(uint32_t now_ms);
void fw_nat_binding_flush(void);
uint32_t fw_nat_binding_count(void);

/* NAT66 */
int  fw_nat66_prefix_add(uint8_t prefix_len,
                          const uint8_t *internal_prefix,
                          const uint8_t *external_prefix);
int  fw_nat66_prefix_del(uint32_t idx);
int  fw_nat66_apply(net_buffer_t *buf, const uint8_t *internal_prefix,
                     const uint8_t *external_prefix, uint8_t prefix_len);
void fw_nat66_flush(void);

/* ALG */
int  fw_alg_register(uint8_t type, uint8_t proto, uint16_t ctrl_port,
                      int (*handler)(fw_alg_t *, net_buffer_t *, int));
int  fw_alg_unregister(uint8_t type);
int  fw_alg_run(net_buffer_t *buf, int hook);
void fw_alg_init(void);
int  fw_alg_ftp_handler(fw_alg_t *alg, net_buffer_t *buf, int hook);
int  fw_alg_sip_handler(fw_alg_t *alg, net_buffer_t *buf, int hook);

const fw_nat_ext_stats_t *fw_nat_ext_get_stats(void);

/* ---- Port forwarding / hairpin NAT ---- */
int  fw_hairpin_nat(net_buffer_t *buf);
int  fw_port_forward_add(ipv4_addr_t ext_ip, uint16_t ext_port,
                          ipv4_addr_t int_ip, uint16_t int_port, uint8_t proto);
int  fw_port_forward_del(ipv4_addr_t ext_ip, uint16_t ext_port, uint8_t proto);

#endif
