#ifndef NETFILTER_H
#define NETFILTER_H

#include "net.h"
#include "stdint.h"

/* Lightweight netfilter-style hook subsystem.
 *
 * Inspired by Linux's netfilter, the network stack exposes five
 * hook points (NF_INET_PRE_ROUTING, NF_INET_LOCAL_IN, NF_INET_FORWARD,
 * NF_INET_LOCAL_OUT, NF_INET_POST_ROUTING).  Each hook can be
 * registered with a callback that may inspect / drop / mutate the
 * packet by returning a verdict.  Verdict values are loosely
 * compatible with Linux so that they are easy to remember. */

#define NF_DROP        0
#define NF_ACCEPT      1
#define NF_STOLEN      2
#define NF_QUEUE       3
#define NF_REPEAT      4

/* Hook points: packets traverse them in the order
 *   incoming:  PRE_ROUTING -> LOCAL_IN   -> (user)
 *   outgoing:  LOCAL_OUT  -> POST_ROUTING
 *   forwarded: PRE_ROUTING -> FORWARD -> POST_ROUTING */
enum {
    NF_INET_PRE_ROUTING  = 0,
    NF_INET_LOCAL_IN     = 1,
    NF_INET_FORWARD      = 2,
    NF_INET_LOCAL_OUT    = 3,
    NF_INET_POST_ROUTING = 4,
    NF_INET_NUMHOOKS     = 5
};

#define NF_HOOK_MAX 8

typedef struct {
    /* The hook callback returns one of the NF_* verdicts. */
    int (*fn)(net_buffer_t *buf, int hook, void *priv);
    void   *priv;
    uint8_t active;
} nf_hook_t;

/* Low-level hook registration.  Most callers should use the
 * table/match API further below. */
void       netfilter_init(void);
int        netfilter_register(int hook, int (*fn)(net_buffer_t *, int, void *), void *priv);
int        netfilter_unregister(int hook, int (*fn)(net_buffer_t *, int, void *));
int        netfilter_run(int hook, net_buffer_t *buf);
uint32_t   netfilter_drops(int hook);

/* ------------------------------------------------------------------- */
/*  High-level packet-match / rule-table API                           */
/* ------------------------------------------------------------------- */

/* Match primitives for the built-in filter table. */
#define NF_MATCH_SRC_IP      0x01
#define NF_MATCH_DST_IP      0x02
#define NF_MATCH_PROTOCOL    0x04
#define NF_MATCH_SRC_PORT    0x08
#define NF_MATCH_DST_PORT    0x10
#define NF_MATCH_IFACE_IN    0x20
#define NF_MATCH_IFACE_OUT   0x40
#define NF_MATCH_TCP_FLAGS   0x80

typedef struct {
    uint8_t      used;
    uint8_t      flags;          /* bitmap of NF_MATCH_* */
    ipv4_addr_t  src_ip;
    ipv4_addr_t  src_mask;
    ipv4_addr_t  dst_ip;
    ipv4_addr_t  dst_mask;
    uint8_t      protocol;       /* IP protocol number, or 0 */
    uint16_t     src_port;       /* host byte order */
    uint16_t     dst_port;
    uint16_t     src_port_end;   /* inclusive range end */
    uint16_t     dst_port_end;
    const char  *iface_in;
    const char  *iface_out;
    uint8_t      tcp_flags;      /* TCP_* bits to test */
    uint8_t      tcp_flags_mask; /* which bits to compare */
} nf_match_t;

/* A rule is a (match, target) pair.  If the match is empty, the rule
 * applies to every packet. */
typedef struct {
    nf_match_t match;
    uint8_t    target;           /* NF_ACCEPT / NF_DROP                */
    uint32_t   counter;          /* hit count                          */
    char       comment[32];
} nf_rule_t;

#define NF_CHAIN_MAX 64
#define NF_TABLE_MAX 8
#define NF_HOOKS_PER_CHAIN NF_INET_NUMHOOKS

typedef struct {
    char      name[16];
    int       hook;              /* one of NF_INET_*                   */
    uint8_t   active;
    nf_rule_t rules[NF_CHAIN_MAX];
    uint32_t  rule_count;
    uint32_t  default_policy;    /* NF_ACCEPT or NF_DROP               */
    uint32_t  drop_count;        /* lifetime drops attributed to chain */
    uint32_t  accept_count;
} nf_chain_t;

typedef struct {
    char        name[16];
    uint8_t     family;          /* always 2 (AF_INET) for now         */
    nf_chain_t  chains[NF_HOOKS_PER_CHAIN];
} nf_table_t;

int  nf_table_create(const char *name);
int  nf_table_delete(const char *name);
nf_table_t *nf_table_get(const char *name);
int  nf_table_attach(const char *name, int hook);
int  nf_table_detach(const char *name, int hook);
int  nf_chain_set_policy(const char *name, int hook, uint32_t policy);
int  nf_rule_append(const char *name, int hook, const nf_rule_t *r);
int  nf_rule_insert(const char *name, int hook, uint32_t pos, const nf_rule_t *r);
int  nf_rule_delete(const char *name, int hook, uint32_t pos);
int  nf_rule_flush(const char *name, int hook);
uint32_t nf_rule_counter(const char *name, int hook, uint32_t pos);

/* Iterate over all tables (for /proc/net/iptables-style export). */
uint32_t nf_table_count(void);
const nf_table_t *nf_table_at(uint32_t i);

/* Run every table that is attached to this hook point in
 * registration order.  Returns NF_DROP / NF_ACCEPT verdict. */
int  netfilter_run_tables(int hook, net_buffer_t *buf);

/* Test a single match against a packet.  Returns 1 on hit, 0 on miss.
 * If buf is NULL, only the IP-header-independent fields are used. */
int  nf_match_test(const nf_match_t *m, net_buffer_t *buf);

/* ------------------------------------------------------------------- */
/*  规则日志                                                            */
/* ------------------------------------------------------------------- */

#define NF_LOG_MAX 256
typedef struct nf_log_entry {
    uint32_t timestamp;
    int      hook;
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t  protocol;
    uint8_t  verdict; /* NF_ACCEPT/NF_DROP */
} nf_log_entry_t;

void   nf_log_init(void);
void   nf_log_packet(int hook, net_buffer_t *buf, int verdict);
void   nf_log_dump(void (*fn)(const char *));

/* ------------------------------------------------------------------- */
/*  连接跟踪(Conntrack)基础                                             */
/* ------------------------------------------------------------------- */

#define NF_CONNTRACK_MAX 128
typedef struct nf_conntrack {
    uint32_t src_ip; uint16_t src_port;
    uint32_t dst_ip; uint16_t dst_port;
    uint8_t  protocol;
    uint8_t  state; /* 0=new, 1=established, 2=closing */
    uint32_t timeout;
    uint8_t  used;
} nf_conntrack_t;

void   nf_ct_init(void);
int    nf_ct_check(net_buffer_t *buf, int *state);
void   nf_ct_update(net_buffer_t *buf, uint8_t state);
void   nf_ct_cleanup(uint32_t now);

#endif
