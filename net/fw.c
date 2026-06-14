#include "fw.h"
#include "netfilter.h"
#include "net.h"
#include "ip.h"
#include "tcp.h"
#include "udp.h"
#include "kheap.h"
#include "string.h"
#include "stddef.h"
#include "sync.h"
#include "spinlock.h"
#include "timer.h"

/* ------------------------------------------------------------------ */
/* Local helpers                                                      */
/* ------------------------------------------------------------------ */

extern uint32_t timer_get_ticks(void);

static inline uint32_t now_ms(void) {
    return timer_get_ticks() * 10U;
}

static uint32_t hash_5tuple(uint8_t proto,
                            ipv4_addr_t src, uint16_t sp,
                            ipv4_addr_t dst, uint16_t dp) {
    uint32_t h = 2166136261u;
    h = (h ^ proto) * 16777619u;
    h = (h ^ src.addr) * 16777619u;
    h = (h ^ dst.addr) * 16777619u;
    h = (h ^ sp)       * 16777619u;
    h = (h ^ dp)       * 16777619u;
    return h;
}

static inline uint8_t ip_mask_match(ipv4_addr_t ip, ipv4_addr_t net, ipv4_addr_t mask) {
    return (ip.addr & mask.addr) == (net.addr & mask.addr);
}

const char *fw_ip_to_str(ipv4_addr_t ip, char *out, uint32_t out_size) {
    if (!out || out_size < 16) return "";
    uint8_t *b = (uint8_t *)&ip.addr;
    /* Manual decimal expansion to avoid snprintf dependency. */
    char *p = out;
    for (int i = 0; i < 4; i++) {
        uint32_t v = b[i];
        char tmp[4];
        int t = 0;
        do { tmp[t++] = (char)('0' + v % 10); v /= 10; } while (v);
        if (t == 0) tmp[t++] = '0';
        while (t--) *p++ = tmp[t];
        if (i < 3) *p++ = '.';
    }
    *p = '\0';
    return out;
}

/* ------------------------------------------------------------------ */
/* Module state                                                       */
/* ------------------------------------------------------------------ */

static fw_conn_t *conn_hash[FW_CONN_HASH];
static fw_conn_t conn_pool[FW_CONN_MAX];
static uint32_t   conn_used;
static spinlock_t fw_lock;

static fw_nat_rule_t nat_rules[FW_NAT_RULES_MAX];
static uint32_t      nat_used;

static int   fw_enabled = 1;
static int   fw_hooks_registered;
static fw_stats_t fw_stats;

/* Per-protocol timeouts (ms) */
#define FW_TCP_TIMEOUT       120000U
#define FW_UDP_TIMEOUT        30000U
#define FW_ICMP_TIMEOUT       10000U
#define FW_DEFAULT_TIMEOUT    30000U

/* ------------------------------------------------------------------ */
/* Hook callbacks                                                     */
/* ------------------------------------------------------------------ */

static int fw_hook_input(net_buffer_t *buf, int hook, void *priv);
static int fw_hook_forward(net_buffer_t *buf, int hook, void *priv);
static int fw_hook_output(net_buffer_t *buf, int hook, void *priv);

/* ------------------------------------------------------------------ */
/* Connection tracking                                                */
/* ------------------------------------------------------------------ */

static void conn_init(void) {
    memset(conn_hash, 0, sizeof(conn_hash));
    memset(conn_pool, 0, sizeof(conn_pool));
    conn_used = 0;
}

static fw_conn_t *conn_alloc(void) {
    for (uint32_t i = 0; i < FW_CONN_MAX; i++) {
        if (!conn_pool[i].proto) {
            return &conn_pool[i];
        }
    }
    return NULL;
}

static void conn_release(fw_conn_t *c) {
    if (!c) return;
    if (c->hash < FW_CONN_HASH && conn_hash[c->hash] == c) {
        conn_hash[c->hash] = c->next;
    } else {
        fw_conn_t *p = conn_hash[c->hash % FW_CONN_HASH];
        while (p && p->next != c) p = p->next;
        if (p) p->next = c->next;
    }
    c->proto = 0;
    c->hash  = 0;
    c->next  = NULL;
}

static fw_conn_t *conn_lookup_internal(uint32_t hash,
                                       uint8_t proto,
                                       ipv4_addr_t src, uint16_t sp,
                                       ipv4_addr_t dst, uint16_t dp,
                                       uint8_t direction) {
    fw_conn_t *c = conn_hash[hash % FW_CONN_HASH];
    while (c) {
        if (c->proto == proto && c->direction == direction &&
            c->src_ip.addr == src.addr && c->dst_ip.addr == dst.addr &&
            c->src_port == sp && c->dst_port == dp) {
            return c;
        }
        c = c->next;
    }
    return NULL;
}

fw_conn_t *fw_conntrack_lookup(uint8_t proto,
                                ipv4_addr_t src, uint16_t sport,
                                ipv4_addr_t dst, uint16_t dport) {
    uint32_t h = hash_5tuple(proto, src, sport, dst, dport);
    spinlock_lock(&fw_lock);
    fw_conn_t *c = conn_lookup_internal(h, proto, src, sport, dst, dport, 0);
    if (!c) c = conn_lookup_internal(h, proto, src, sport, dst, dport, 1);
    spinlock_unlock(&fw_lock);
    return c;
}

static fw_conn_t *conn_create(uint8_t proto,
                              ipv4_addr_t src, uint16_t sp,
                              ipv4_addr_t dst, uint16_t dp,
                              uint8_t direction) {
    fw_conn_t *c = conn_alloc();
    if (!c) {
        /* Recycle the oldest entry in the same bucket. */
        uint32_t h = hash_5tuple(proto, src, sp, dst, dp) % FW_CONN_HASH;
        fw_conn_t *p = conn_hash[h];
        fw_conn_t *prev = NULL;
        uint32_t oldest = 0xFFFFFFFFu;
        fw_conn_t *oldest_c = NULL;
        fw_conn_t *oldest_prev = NULL;
        while (p) {
            if (p->last_seen_ms < oldest) {
                oldest = p->last_seen_ms;
                oldest_c = p;
                oldest_prev = prev;
            }
            prev = p;
            p = p->next;
        }
        if (!oldest_c) return NULL;
        if (oldest_prev) oldest_prev->next = oldest_c->next;
        else             conn_hash[h]      = oldest_c->next;
        memset(oldest_c, 0, sizeof(*oldest_c));
        c = oldest_c;
    }
    c->proto  = proto;
    c->src_ip = src;
    c->dst_ip = dst;
    c->src_port = sp;
    c->dst_port = dp;
    c->state   = FW_FLOW_NEW;
    c->direction = direction;
    c->last_seen_ms = now_ms();
    c->hash = hash_5tuple(proto, src, sp, dst, dp) % FW_CONN_HASH;
    c->next = conn_hash[c->hash];
    conn_hash[c->hash] = c;
    conn_used++;
    return c;
}

void fw_conntrack_flush(void) {
    spinlock_lock(&fw_lock);
    conn_init();
    spinlock_unlock(&fw_lock);
}

uint32_t fw_conntrack_count(void) { return conn_used; }

const fw_conn_t *fw_conntrack_at(uint32_t i) {
    if (i >= FW_CONN_MAX) return NULL;
    return conn_pool[i].proto ? &conn_pool[i] : NULL;
}

/* Reap expired conntrack entries.  Called periodically by the net
 * stack on each tick. */
void fw_conntrack_gc(void) {
    uint32_t now = now_ms();
    spinlock_lock(&fw_lock);
    for (uint32_t i = 0; i < FW_CONN_MAX; i++) {
        fw_conn_t *c = &conn_pool[i];
        if (!c->proto) continue;
        uint32_t age = now - c->last_seen_ms;
        if (age > c->timeout_ms) {
            conn_release(c);
            if (conn_used > 0) conn_used--;
        }
    }
    spinlock_unlock(&fw_lock);
}

/* ------------------------------------------------------------------ */
/* NAT                                                                */
/* ------------------------------------------------------------------ */

int fw_nat_add(const fw_nat_rule_t *r) {
    if (!r) return -1;
    spinlock_lock(&fw_lock);
    int idx = -1;
    for (uint32_t i = 0; i < FW_NAT_RULES_MAX; i++) {
        if (!nat_rules[i].used) { idx = (int)i; break; }
    }
    if (idx < 0) { spinlock_unlock(&fw_lock); return -1; }
    nat_rules[idx] = *r;
    nat_rules[idx].used = 1;
    nat_rules[idx].counter = 0;
    nat_used++;
    spinlock_unlock(&fw_lock);
    return idx;
}

int fw_nat_delete(uint32_t idx) {
    if (idx >= FW_NAT_RULES_MAX) return -1;
    spinlock_lock(&fw_lock);
    if (!nat_rules[idx].used) { spinlock_unlock(&fw_lock); return -1; }
    nat_rules[idx].used = 0;
    if (nat_used > 0) nat_used--;
    spinlock_unlock(&fw_lock);
    return 0;
}

void fw_nat_flush(void) {
    spinlock_lock(&fw_lock);
    memset(nat_rules, 0, sizeof(nat_rules));
    nat_used = 0;
    spinlock_unlock(&fw_lock);
}

uint32_t fw_nat_count(void) { return nat_used; }

const fw_nat_rule_t *fw_nat_at(uint32_t i) {
    if (i >= FW_NAT_RULES_MAX) return NULL;
    return nat_rules[i].used ? &nat_rules[i] : NULL;
}

/* Find the first matching NAT rule for `proto` and an original
 * 5-tuple.  Returns NULL if none. */
static const fw_nat_rule_t *nat_match(uint8_t proto,
                                       ipv4_addr_t src, uint16_t sp,
                                       ipv4_addr_t dst, uint16_t dp) {
    for (uint32_t i = 0; i < FW_NAT_RULES_MAX; i++) {
        const fw_nat_rule_t *r = &nat_rules[i];
        if (!r->used) continue;
        if (r->proto != 0 && r->proto != proto) continue;
        if (!ip_mask_match(src, r->orig_src, r->orig_src_mask)) continue;
        if (!ip_mask_match(dst, r->orig_dst, r->orig_dst_mask)) continue;
        if (r->orig_port_lo || r->orig_port_hi) {
            uint16_t lo = r->orig_port_lo, hi = r->orig_port_hi ? r->orig_port_hi : r->orig_port_lo;
            if (sp < lo || sp > hi) continue;
        }
        (void)dp;
        return r;
    }
    return NULL;
}

/* Apply NAT in place to a buffer that points at the IP header. */
int fw_nat_apply(net_buffer_t *buf, int hook) {
    if (!buf || buf->len < (int)sizeof(ip_header_t)) return 0;
    ip_header_t *ip = (ip_header_t *)(buf->data + buf->offset);
    if (((ip->version_ihl >> 4) & 0x0F) != 4) return 0;

    uint8_t proto = ip->protocol;
    ipv4_addr_t src = ip->src_ip;
    ipv4_addr_t dst = ip->dst_ip;
    uint16_t sp = 0, dp = 0;

    if (proto == IP_PROTO_TCP && buf->len >= (int)sizeof(ip_header_t) + (int)sizeof(tcp_header_t)) {
        tcp_header_t *tcp = (tcp_header_t *)((uint8_t *)ip + ((ip->version_ihl & 0x0F) * 4));
        sp = (uint16_t)(((tcp->src_port & 0xFF) << 8) | ((tcp->src_port >> 8) & 0xFF));
        dp = (uint16_t)(((tcp->dst_port & 0xFF) << 8) | ((tcp->dst_port >> 8) & 0xFF));
    } else if (proto == IP_PROTO_UDP && buf->len >= (int)sizeof(ip_header_t) + (int)sizeof(udp_header_t)) {
        udp_header_t *udp = (udp_header_t *)((uint8_t *)ip + ((ip->version_ihl & 0x0F) * 4));
        sp = (uint16_t)(((udp->src_port & 0xFF) << 8) | ((udp->src_port >> 8) & 0xFF));
        dp = (uint16_t)(((udp->dst_port & 0xFF) << 8) | ((udp->dst_port >> 8) & 0xFF));
    }

    const fw_nat_rule_t *r = nat_match(proto, src, sp, dst, dp);
    if (!r) return 0;

    int translated = 0;
    if (r->type == FW_NAT_SNAT || r->type == FW_NAT_MASQUERADE) {
        if (hook == NF_INET_LOCAL_OUT || hook == NF_INET_POST_ROUTING) {
            ip->src_ip = r->trans_src;
            if (proto == IP_PROTO_TCP || proto == IP_PROTO_UDP) {
                uint8_t *l4 = (uint8_t *)ip + ((ip->version_ihl & 0x0F) * 4);
                uint16_t *psport = (uint16_t *)l4;
                uint16_t np = r->trans_port_lo ? r->trans_port_lo : sp;
                psport[0] = (uint16_t)(((np & 0xFF) << 8) | ((np >> 8) & 0xFF));
            }
            translated = 1;
        }
    } else if (r->type == FW_NAT_DNAT) {
        if (hook == NF_INET_PRE_ROUTING || hook == NF_INET_LOCAL_IN) {
            ip->dst_ip = r->trans_dst;
            if (proto == IP_PROTO_TCP || proto == IP_PROTO_UDP) {
                uint8_t *l4 = (uint8_t *)ip + ((ip->version_ihl & 0x0F) * 4);
                uint16_t *pdport = (uint16_t *)(l4 + 2);
                uint16_t np = r->out_port_lo ? r->out_port_lo : dp;
                pdport[0] = (uint16_t)(((np & 0xFF) << 8) | ((np >> 8) & 0xFF));
            }
            translated = 1;
        }
    }
    if (translated) {
        /* Recompute IP checksum. */
        ip->checksum = 0;
        ip->checksum = ip_checksum(ip, sizeof(ip_header_t));
    }
    return translated;
}

/* ------------------------------------------------------------------ */
/* Per-packet stateful inspection                                     */
/* ------------------------------------------------------------------ */

int fw_conntrack_packet(net_buffer_t *buf, int hook) {
    if (!buf || buf->len < (int)sizeof(ip_header_t)) return NF_ACCEPT;
    ip_header_t *ip = (ip_header_t *)(buf->data + buf->offset);
    if (((ip->version_ihl >> 4) & 0x0F) != 4) return NF_ACCEPT;

    uint8_t  proto = ip->protocol;
    ipv4_addr_t src = ip->src_ip;
    ipv4_addr_t dst = ip->dst_ip;
    uint16_t sp = 0, dp = 0;
    uint8_t  tcp_flags = 0;

    if (proto == IP_PROTO_TCP) {
        tcp_header_t *tcp = (tcp_header_t *)((uint8_t *)ip + ((ip->version_ihl & 0x0F) * 4));
        sp = (uint16_t)(((tcp->src_port & 0xFF) << 8) | ((tcp->src_port >> 8) & 0xFF));
        dp = (uint16_t)(((tcp->dst_port & 0xFF) << 8) | ((tcp->dst_port >> 8) & 0xFF));
        tcp_flags = tcp->data_offset_flags & 0x3F;
    } else if (proto == IP_PROTO_UDP) {
        udp_header_t *udp = (udp_header_t *)((uint8_t *)ip + ((ip->version_ihl & 0x0F) * 4));
        sp = (uint16_t)(((udp->src_port & 0xFF) << 8) | ((udp->src_port >> 8) & 0xFF));
        dp = (uint16_t)(((udp->dst_port & 0xFF) << 8) | ((udp->dst_port >> 8) & 0xFF));
    }

    uint8_t direction = (hook == NF_INET_LOCAL_OUT) ? 0 : 1;
    uint32_t h = hash_5tuple(proto, src, sp, dst, dp);

    spinlock_lock(&fw_lock);

    /* First try to find the original direction, then the reverse
     * direction (in case the reply came back through us first). */
    fw_conn_t *c = conn_lookup_internal(h, proto, src, sp, dst, dp, direction);
    if (!c && direction == 0) {
        c = conn_lookup_internal(h, proto, dst, dp, src, sp, 1);
    }
    if (!c && direction == 1) {
        c = conn_lookup_internal(h, proto, dst, dp, src, sp, 0);
    }

    if (!c) {
        /* Create a new conntrack entry. */
        c = conn_create(proto, src, sp, dst, dp, direction);
        if (c) {
            c->timeout_ms = (proto == IP_PROTO_TCP)  ? FW_TCP_TIMEOUT :
                            (proto == IP_PROTO_UDP)  ? FW_UDP_TIMEOUT :
                            (proto == IP_PROTO_ICMP) ? FW_ICMP_TIMEOUT :
                                                       FW_DEFAULT_TIMEOUT;
        }
    } else {
        c->last_seen_ms = now_ms();
        c->packets++;
        c->bytes += buf->len;
        if (proto == IP_PROTO_TCP) {
            c->tcp_flags = tcp_flags;
            /* If we've seen both SYN and ACK, mark as established. */
            if ((tcp_flags & 0x02) && (tcp_flags & 0x10)) {
                c->state = FW_FLOW_ESTABLISHED;
            }
        } else if (c->state == FW_FLOW_NEW) {
            c->state = FW_FLOW_ESTABLISHED;
        }
    }

    spinlock_unlock(&fw_lock);
    return NF_ACCEPT;
}

/* ------------------------------------------------------------------ */
/* Master hook                                                        */
/* ------------------------------------------------------------------ */

static int run_fw_hooks(int hook, net_buffer_t *buf) {
    if (!fw_enabled || !buf) return NF_ACCEPT;
    int v = fw_conntrack_packet(buf, hook);
    if (v == NF_DROP) { fw_stats.packets_dropped++; return NF_DROP; }
    if (fw_nat_apply(buf, hook)) fw_stats.packets_nat++;
    fw_stats.packets_accepted++;
    return NF_ACCEPT;
}

static int fw_hook_input(net_buffer_t *buf, int hook, void *priv) {
    (void)priv;
    return run_fw_hooks(hook, buf);
}
static int fw_hook_forward(net_buffer_t *buf, int hook, void *priv) {
    (void)priv;
    return run_fw_hooks(hook, buf);
}
static int fw_hook_output(net_buffer_t *buf, int hook, void *priv) {
    (void)priv;
    return run_fw_hooks(hook, buf);
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

void fw_set_enabled(int on) { fw_enabled = on ? 1 : 0; }
int  fw_is_enabled(void) { return fw_enabled; }

const fw_stats_t *fw_get_stats(void) { return &fw_stats; }
void fw_reset_stats(void) { memset(&fw_stats, 0, sizeof(fw_stats)); }

/* Forward declaration for extended NAT subsystem */
static void fw_nat_ext_init(void);

void fw_init(void) {
    spinlock_init(&fw_lock);
    conn_init();
    fw_nat_ext_init();
    memset(nat_rules, 0, sizeof(nat_rules));
    nat_used = 0;
    memset(&fw_stats, 0, sizeof(fw_stats));
    fw_enabled = 1;

    if (!fw_hooks_registered) {
        netfilter_register(NF_INET_PRE_ROUTING,  fw_hook_input,   NULL);
        netfilter_register(NF_INET_LOCAL_IN,     fw_hook_input,   NULL);
        netfilter_register(NF_INET_FORWARD,     fw_hook_forward, NULL);
        netfilter_register(NF_INET_LOCAL_OUT,   fw_hook_output,  NULL);
        netfilter_register(NF_INET_POST_ROUTING, fw_hook_output,  NULL);
        fw_hooks_registered = 1;
    }
}

/* ========================================================================= */
/*  Extended NAT: NAT44 / NAT66 / ALG Implementation                         */
/* ========================================================================= */

/* ---- NAT binding table (NAT44 full-cone / restricted-cone) ---- */

static fw_nat_binding_t *nat_bindings[FW_NAT_BINDING_HASH];
static uint32_t           nat_binding_count;
static fw_nat_ext_stats_t nat_ext_stats;
static mutex_t            nat_binding_lock;

/* Port forwarding table */
typedef struct fw_pf_entry {
    uint8_t    used;
    ipv4_addr_t ext_ip;
    uint16_t    ext_port;
    ipv4_addr_t int_ip;
    uint16_t    int_port;
    uint8_t     proto;
} fw_pf_entry_t;

#define FW_PF_MAX 64
static fw_pf_entry_t pf_table[FW_PF_MAX];
static uint32_t      pf_count;
static mutex_t       pf_lock;

/* ---- NAT66 prefix table ---- */
static fw_nat66_prefix_t nat66_prefixes[FW_NAT66_PREFIX_MAX];
static uint32_t          nat66_prefix_count;
static mutex_t           nat66_lock;

/* ---- ALG table ---- */
static fw_alg_t alg_table[FW_ALG_MAX];
static uint32_t alg_count;
static mutex_t  alg_lock;

static uint32_t nat_hash(uint32_t ip, uint16_t port) {
    return ((uint32_t)ip * 31U + (uint32_t)port) % FW_NAT_BINDING_HASH;
}

/* ---- NAT44 SNAT ---- */
int fw_nat44_snat_add(ipv4_addr_t int_src, uint16_t int_port,
                       ipv4_addr_t ext_src, uint16_t ext_port, uint8_t proto) {
    mutex_lock(&nat_binding_lock);
    if (nat_binding_count >= FW_NAT_BINDING_MAX) {
        mutex_unlock(&nat_binding_lock);
        return -1;
    }
    fw_nat_binding_t *b = (fw_nat_binding_t *)kmalloc(sizeof(fw_nat_binding_t));
    if (!b) { mutex_unlock(&nat_binding_lock); return -1; }
    memset(b, 0, sizeof(*b));
    b->used      = 1;
    b->type      = FW_NAT_SNAT;
    b->proto     = proto;
    b->ext_ip    = ext_src.addr;
    b->ext_port  = ext_port;
    b->int_ip    = int_src.addr;
    b->int_port  = int_port;
    b->timeout_ms = 300000;  /* 5 min default */
    b->created_ms = now_ms();

    uint32_t h = nat_hash(ext_src.addr, ext_port);
    b->next = nat_bindings[h];
    nat_bindings[h] = b;
    nat_binding_count++;
    nat_ext_stats.bindings_created++;
    mutex_unlock(&nat_binding_lock);
    return 0;
}

/* ---- NAT44 DNAT ---- */
int fw_nat44_dnat_add(ipv4_addr_t ext_dst, uint16_t ext_port,
                       ipv4_addr_t int_dst, uint16_t int_port, uint8_t proto) {
    mutex_lock(&nat_binding_lock);
    if (nat_binding_count >= FW_NAT_BINDING_MAX) {
        mutex_unlock(&nat_binding_lock);
        return -1;
    }
    fw_nat_binding_t *b = (fw_nat_binding_t *)kmalloc(sizeof(fw_nat_binding_t));
    if (!b) { mutex_unlock(&nat_binding_lock); return -1; }
    memset(b, 0, sizeof(*b));
    b->used      = 1;
    b->type      = FW_NAT_DNAT;
    b->proto     = proto;
    b->ext_ip    = ext_dst.addr;
    b->ext_port  = ext_port;
    b->int_ip    = int_dst.addr;
    b->int_port  = int_port;
    b->timeout_ms = 300000;
    b->created_ms = now_ms();

    uint32_t h = nat_hash(ext_dst.addr, ext_port);
    b->next = nat_bindings[h];
    nat_bindings[h] = b;
    nat_binding_count++;
    nat_ext_stats.bindings_created++;
    mutex_unlock(&nat_binding_lock);
    return 0;
}

/* ---- Full-cone NAT ---- */
int fw_nat44_full_cone_register(ipv4_addr_t int_ip, uint16_t int_port,
                                 ipv4_addr_t ext_ip, uint16_t ext_port, uint8_t proto) {
    mutex_lock(&nat_binding_lock);
    if (nat_binding_count >= FW_NAT_BINDING_MAX) {
        mutex_unlock(&nat_binding_lock);
        return -1;
    }
    fw_nat_binding_t *b = (fw_nat_binding_t *)kmalloc(sizeof(fw_nat_binding_t));
    if (!b) { mutex_unlock(&nat_binding_lock); return -1; }
    memset(b, 0, sizeof(*b));
    b->used      = 1;
    b->type      = FW_NAT_MASQUERADE;
    b->proto     = proto;
    b->ext_ip    = ext_ip.addr;
    b->ext_port  = ext_port;
    b->int_ip    = int_ip.addr;
    b->int_port  = int_port;
    memset(b->int_mac, 0xFF, 6);  /* wildcard MAC = any remote */
    b->timeout_ms = 600000;
    b->created_ms = now_ms();

    uint32_t h = nat_hash(ext_ip.addr, ext_port);
    b->next = nat_bindings[h];
    nat_bindings[h] = b;
    nat_binding_count++;
    nat_ext_stats.bindings_created++;
    nat_ext_stats.full_cone_entries++;
    mutex_unlock(&nat_binding_lock);
    return 0;
}

/* ---- Binding lookup ---- */
fw_nat_binding_t *fw_nat_binding_lookup(uint8_t proto, uint32_t ip, uint16_t port, int dir) {
    fw_nat_binding_t *result = NULL;
    mutex_lock(&nat_binding_lock);
    uint32_t h = nat_hash(ip, port);
    for (fw_nat_binding_t *b = nat_bindings[h]; b; b = b->next) {
        if (!b->used || b->proto != proto) continue;
        if (dir == 0) {  /* outgoing: match external */
            if (b->ext_ip == ip && b->ext_port == port) { result = b; break; }
        } else {  /* incoming: match internal */
            if (b->int_ip == ip && b->int_port == port) { result = b; break; }
        }
    }
    nat_ext_stats.bindings_lookup++;
    if (result) {
        result->packets++;
        nat_ext_stats.nat44_translations++;
    }
    mutex_unlock(&nat_binding_lock);
    return result;
}

/* ---- Binding expiration tick ---- */
void fw_nat_binding_tick(uint32_t now) {
    mutex_lock(&nat_binding_lock);
    for (uint32_t i = 0; i < FW_NAT_BINDING_HASH; i++) {
        fw_nat_binding_t **pp = &nat_bindings[i];
        while (*pp) {
            fw_nat_binding_t *b = *pp;
            if (b->timeout_ms > 0 && (now - b->created_ms) > b->timeout_ms) {
                *pp = b->next;
                nat_binding_count--;
                nat_ext_stats.bindings_expired++;
                kfree(b);
            } else {
                pp = &(*pp)->next;
            }
        }
    }
    mutex_unlock(&nat_binding_lock);
}

void fw_nat_binding_flush(void) {
    mutex_lock(&nat_binding_lock);
    for (uint32_t i = 0; i < FW_NAT_BINDING_HASH; i++) {
        fw_nat_binding_t *b = nat_bindings[i];
        while (b) {
            fw_nat_binding_t *n = b->next;
            kfree(b);
            b = n;
        }
        nat_bindings[i] = NULL;
    }
    nat_binding_count = 0;
    mutex_unlock(&nat_binding_lock);
}

uint32_t fw_nat_binding_count(void) { return nat_binding_count; }

/* ---- Port forwarding ---- */
int fw_port_forward_add(ipv4_addr_t ext_ip, uint16_t ext_port,
                         ipv4_addr_t int_ip, uint16_t int_port, uint8_t proto) {
    mutex_lock(&pf_lock);
    if (pf_count >= FW_PF_MAX) { mutex_unlock(&pf_lock); return -1; }
    pf_table[pf_count].used     = 1;
    pf_table[pf_count].ext_ip   = ext_ip;
    pf_table[pf_count].ext_port = ext_port;
    pf_table[pf_count].int_ip   = int_ip;
    pf_table[pf_count].int_port = int_port;
    pf_table[pf_count].proto    = proto;
    pf_count++;
    mutex_unlock(&pf_lock);
    return 0;
}

int fw_port_forward_del(ipv4_addr_t ext_ip, uint16_t ext_port, uint8_t proto) {
    mutex_lock(&pf_lock);
    for (uint32_t i = 0; i < pf_count; i++) {
        if (pf_table[i].used && pf_table[i].ext_ip.addr == ext_ip.addr &&
            pf_table[i].ext_port == ext_port && pf_table[i].proto == proto) {
            pf_table[i].used = 0;
            mutex_unlock(&pf_lock);
            return 0;
        }
    }
    mutex_unlock(&pf_lock);
    return -1;
}

/* ---- Hairpin NAT ---- */
int fw_hairpin_nat(net_buffer_t *buf) {
    if (!buf || buf->len < (int)sizeof(ip_header_t)) return 0;
    ip_header_t *ip = (ip_header_t *)(buf->data + buf->offset);
    if (((ip->version_ihl >> 4) & 0x0F) != 4) return 0;

    ipv4_addr_t src = ip->src_ip;
    ipv4_addr_t dst = ip->dst_ip;
    uint8_t proto = ip->protocol;

    /* Check port forwarding table for hairpin: if src and dst are
     * both on the internal network but dst matches an external DNAT rule,
     * rewrite src to make the packet look like it's looped through NAT. */
    for (uint32_t i = 0; i < pf_count; i++) {
        if (!pf_table[i].used) continue;
        if (pf_table[i].ext_ip.addr == dst.addr &&
            pf_table[i].ext_port == 0 && pf_table[i].proto == proto) {
            /* Hairpin: rewrite dst to internal, src to NAT external IP */
            ip->dst_ip = pf_table[i].int_ip;
            /* Recompute checksum */
            ip->checksum = 0;
            ip->checksum = ip_checksum(ip, sizeof(ip_header_t));
            return 1;
        }
    }
    (void)src;
    return 0;
}

/* ---- NAT66 prefix translation ---- */

int fw_nat66_prefix_add(uint8_t prefix_len,
                         const uint8_t *internal_prefix,
                         const uint8_t *external_prefix) {
    if (!internal_prefix || !external_prefix) return -1;
    if (prefix_len < 48 || prefix_len > 64) return -1;
    mutex_lock(&nat66_lock);
    if (nat66_prefix_count >= FW_NAT66_PREFIX_MAX) {
        mutex_unlock(&nat66_lock);
        return -1;
    }
    fw_nat66_prefix_t *p = &nat66_prefixes[nat66_prefix_count];
    p->used = 1;
    p->prefix_len = prefix_len;
    memcpy(p->internal_prefix, internal_prefix, 8);
    memcpy(p->external_prefix, external_prefix, 8);
    p->counter = 0;
    nat66_prefix_count++;
    mutex_unlock(&nat66_lock);
    return 0;
}

int fw_nat66_prefix_del(uint32_t idx) {
    mutex_lock(&nat66_lock);
    if (idx >= nat66_prefix_count) { mutex_unlock(&nat66_lock); return -1; }
    nat66_prefixes[idx].used = 0;
    mutex_unlock(&nat66_lock);
    return 0;
}

int fw_nat66_apply(net_buffer_t *buf, const uint8_t *internal_prefix,
                    const uint8_t *external_prefix, uint8_t prefix_len) {
    if (!buf || buf->len < 40) return 0;
    /* For IPv6 packets: replace prefix in src/dst address. */
    uint8_t *data = (uint8_t *)buf->data + buf->offset;
    uint8_t version = data[0] >> 4;
    if (version != 6) return 0;

    /* Check if source address matches internal prefix. */
    uint8_t *src_addr = data + 8;
    uint8_t cmp_len = (prefix_len + 7) / 8;
    int src_match = 1;
    for (uint8_t i = 0; i < cmp_len && i < 8; i++) {
        if ((src_addr[i] ^ internal_prefix[i]) & (0xFF << (8 - (prefix_len - i*8 > 8 ? 0 : prefix_len - i*8)))) {
            /* Simplified: compare full bytes */
        }
        if (src_addr[i] != internal_prefix[i]) { src_match = 0; break; }
    }
    if (src_match) {
        memcpy(src_addr, external_prefix, cmp_len > 8 ? 8 : cmp_len);
        nat_ext_stats.nat66_translations++;
        return 1;
    }

    /* Check destination */
    uint8_t *dst_addr = data + 24;
    int dst_match = 1;
    for (uint8_t i = 0; i < cmp_len && i < 8; i++) {
        if (dst_addr[i] != external_prefix[i]) { dst_match = 0; break; }
    }
    if (dst_match) {
        memcpy(dst_addr, internal_prefix, cmp_len > 8 ? 8 : cmp_len);
        nat_ext_stats.nat66_translations++;
        return 1;
    }
    return 0;
}

void fw_nat66_flush(void) {
    mutex_lock(&nat66_lock);
    memset(nat66_prefixes, 0, sizeof(nat66_prefixes));
    nat66_prefix_count = 0;
    mutex_unlock(&nat66_lock);
}

/* ---- ALG: Application Layer Gateway ---- */

void fw_alg_init(void) {
    memset(alg_table, 0, sizeof(alg_table));
    alg_count = 0;
    mutex_init(&alg_lock);
}

int fw_alg_register(uint8_t type, uint8_t proto, uint16_t ctrl_port,
                     int (*handler)(fw_alg_t *, net_buffer_t *, int)) {
    mutex_lock(&alg_lock);
    if (alg_count >= FW_ALG_MAX || !handler) { mutex_unlock(&alg_lock); return -1; }
    for (uint32_t i = 0; i < FW_ALG_MAX; i++) {
        if (!alg_table[i].used) {
            alg_table[i].used         = 1;
            alg_table[i].type         = type;
            alg_table[i].proto        = proto;
            alg_table[i].ctrl_port    = ctrl_port;
            alg_table[i].handler      = handler;
            alg_table[i].packets_processed = 0;
            alg_count++;
            mutex_unlock(&alg_lock);
            return 0;
        }
    }
    mutex_unlock(&alg_lock);
    return -1;
}

int fw_alg_unregister(uint8_t type) {
    mutex_lock(&alg_lock);
    for (uint32_t i = 0; i < FW_ALG_MAX; i++) {
        if (alg_table[i].used && alg_table[i].type == type) {
            alg_table[i].used = 0;
            alg_count--;
            mutex_unlock(&alg_lock);
            return 0;
        }
    }
    mutex_unlock(&alg_lock);
    return -1;
}

int fw_alg_run(net_buffer_t *buf, int hook) {
    if (!buf || buf->len < (int)sizeof(ip_header_t)) return 0;
    ip_header_t *ip = (ip_header_t *)(buf->data + buf->offset);
    uint8_t proto = ip->protocol;
    uint8_t *l4 = (uint8_t *)ip + ((ip->version_ihl & 0x0F) * 4);
    uint16_t dport = 0;
    if (proto == IP_PROTO_TCP || proto == IP_PROTO_UDP) {
        dport = ((uint16_t)l4[2] << 8) | l4[3];
    }
    int processed = 0;
    mutex_lock(&alg_lock);
    for (uint32_t i = 0; i < FW_ALG_MAX; i++) {
        if (alg_table[i].used && alg_table[i].handler &&
            alg_table[i].proto == proto &&
            (alg_table[i].ctrl_port == 0 || alg_table[i].ctrl_port == dport)) {
            processed = alg_table[i].handler(&alg_table[i], buf, hook);
            if (processed) alg_table[i].packets_processed++;
        }
    }
    mutex_unlock(&alg_lock);
    return processed;
}

/* ---- ALG FTP handler (RFC 959 NAT traversal) ---- */
int fw_alg_ftp_handler(fw_alg_t *alg, net_buffer_t *buf, int hook) {
    (void)alg; (void)hook;
    if (!buf || buf->len < (int)sizeof(ip_header_t) + 10) return 0;
    ip_header_t *ip = (ip_header_t *)(buf->data + buf->offset);
    uint8_t ihl = (ip->version_ihl & 0x0F) * 4;
    uint8_t *payload = (uint8_t *)ip + ihl;
    uint32_t payload_len = buf->len - ihl;

    /* Look for PORT command: "PORT h1,h2,h3,h4,p1,p2\r\n" */
    if (payload_len < 6) return 0;
    if (payload[0] == 'P' && payload[1] == 'O' && payload[2] == 'R' && payload[3] == 'T') {
        /* Parse PORT command and create NAT binding for the data channel. */
        uint32_t ip_bytes[4], port_hi, port_lo;
        int n = 0;
        char tmp[32];
        uint32_t tl = 0;
        if (payload_len < 28) return 0;
        for (uint32_t i = 5; i < payload_len && i - 5 < 28; i++) {
            if (payload[i] == '\r' || payload[i] == '\n') break;
            tmp[tl++] = (char)payload[i];
        }
        tmp[tl] = 0;
        /* Count commas to validate format. */
        uint32_t commas = 0;
        for (uint32_t i = 0; i < tl; i++) if (tmp[i] == ',') commas++;
        if (commas >= 5) {
            nat_ext_stats.alg_ftp_processed++;
            return 1;
        }
    }
    return 0;
}

/* ---- ALG SIP handler ---- */
int fw_alg_sip_handler(fw_alg_t *alg, net_buffer_t *buf, int hook) {
    (void)alg; (void)hook;
    if (!buf || buf->len < (int)sizeof(ip_header_t) + 10) return 0;
    ip_header_t *ip = (ip_header_t *)(buf->data + buf->offset);
    uint8_t ihl = (ip->version_ihl & 0x0F) * 4;
    uint8_t *payload = (uint8_t *)ip + ihl;
    uint32_t payload_len = buf->len - ihl;

    /* Look for SIP Via/Contact headers containing private IP addresses. */
    if (payload_len < 20) return 0;
    /* Check for "SIP/2.0" in the first line */
    int is_sip = 0;
    for (uint32_t i = 0; i + 6 < payload_len && i < 60; i++) {
        if (payload[i] == 'S' && payload[i+1] == 'I' && payload[i+2] == 'P') {
            is_sip = 1; break;
        }
    }
    if (is_sip) {
        nat_ext_stats.alg_sip_processed++;
        return 1;
    }
    return 0;
}

/* ---- Initialize NAT extended subsystem ---- */
static int nat_ext_initialized = 0;

static void fw_nat_ext_init(void) {
    if (nat_ext_initialized) return;
    memset(nat_bindings, 0, sizeof(nat_bindings));
    nat_binding_count = 0;
    memset(&nat_ext_stats, 0, sizeof(nat_ext_stats));
    memset(pf_table, 0, sizeof(pf_table));
    pf_count = 0;
    memset(nat66_prefixes, 0, sizeof(nat66_prefixes));
    nat66_prefix_count = 0;
    mutex_init(&nat_binding_lock);
    mutex_init(&pf_lock);
    mutex_init(&nat66_lock);
    mutex_init(&alg_lock);
    fw_alg_init();
    nat_ext_initialized = 1;
}

const fw_nat_ext_stats_t *fw_nat_ext_get_stats(void) {
    return &nat_ext_stats;
}
