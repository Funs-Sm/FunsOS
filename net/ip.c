#include "ip.h"
#include "ethernet.h"
#include "arp.h"
#include "icmp.h"
#include "tcp.h"
#include "udp.h"
#include "raw_sock.h"
#include "udp_lite.h"
#include "igmp.h"
#include "route.h"
#include "netfilter.h"
#include "kheap.h"
#include "string.h"
#include "timer.h"
#include "sync.h"
#include "stddef.h"

static ip_stats_t stats;

void ip_init(void) {
    memset(&stats, 0, sizeof(stats));
}

uint16_t ip_checksum(const void *data, uint32_t len) {
    const uint16_t *ptr = (const uint16_t *)data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += *ptr;
        ptr++;
        len -= 2;
    }
    if (len == 1) {
        sum += *(const uint8_t *)ptr;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

ip_route_t ip_route_lookup(ipv4_addr_t dst) {
    ip_route_t route;
    route.iface = NULL;
    route.gateway.addr = 0;

    /* 1) Longest-prefix match via the routing table. */
    net_interface_t *riface = NULL;
    ipv4_addr_t      rgw    = (ipv4_addr_t){0};
    if (route_resolve(dst, &riface, &rgw) == 0) {
        route.iface   = riface;
        route.gateway = rgw;
        return route;
    }

    /* 2) Fallback: directly-attached subnet on the destination address. */
    for (uint32_t i = 0; i < NET_MAX_INTERFACES; i++) {
        net_interface_t *iface = net_get_interface(i);
        if (iface && iface->up) {
            if ((dst.addr & iface->mask.addr) == (iface->ip.addr & iface->mask.addr)) {
                route.iface = iface;
                route.gateway.addr = 0;
                return route;
            }
        }
    }

    /* 3) Last resort: any up interface with a default gateway. */
    for (uint32_t i = 0; i < NET_MAX_INTERFACES; i++) {
        net_interface_t *iface = net_get_interface(i);
        if (iface && iface->up && iface->gateway.addr != 0) {
            route.iface = iface;
            route.gateway = iface->gateway;
            return route;
        }
    }
    return route;
}

static uint16_t next_ip_id = 0;

int ip_send(net_interface_t *iface, ipv4_addr_t dst, uint8_t proto,
            const void *payload, uint32_t len) {
    return ip_send_with_ttl(iface, dst, proto, payload, len, IP_DEFAULT_TTL, 0);
}

int ip_send_with_ttl(net_interface_t *iface, ipv4_addr_t dst, uint8_t proto,
                     const void *payload, uint32_t len, uint8_t ttl, uint8_t tos) {
    if (!iface) return -1;

    /* Decide whether to fragment.  We honour the path-MTU if the
     * interface has a known MTU and the datagram is too large; in that
     * case the caller should have used ip_fragment_send.  For now, the
     * typical MTU is 1500, and any datagram that exceeds iface->mtu
     * is sent with the DF bit set: we let the lower layer handle it
     * rather than silently truncating. */
    if (iface->mtu > 0 && len + sizeof(ip_header_t) > iface->mtu) {
        return ip_fragment_send(iface, dst, proto, payload, len, ttl, tos,
                                next_ip_id++);
    }

    ip_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.version_ihl = (4 << 4) | 5;
    hdr.tos = tos;
    hdr.total_length = sizeof(ip_header_t) + len;
    hdr.identification = next_ip_id++;
    hdr.flags_fragment = 0;
    hdr.ttl = ttl;
    hdr.protocol = proto;
    hdr.checksum = 0;
    hdr.src_ip = iface->ip;
    hdr.dst_ip = dst;

    hdr.checksum = ip_checksum(&hdr, sizeof(ip_header_t));

    uint32_t total = sizeof(ip_header_t) + len;
    uint8_t *packet = (uint8_t *)kmalloc(total);
    if (!packet) return -1;

    memcpy(packet, &hdr, sizeof(ip_header_t));
    if (len) memcpy(packet + sizeof(ip_header_t), payload, len);

    ipv4_addr_t next_hop;
    if ((dst.addr & iface->mask.addr) == (iface->ip.addr & iface->mask.addr)) {
        next_hop = dst;
    } else {
        next_hop = iface->gateway;
    }

    mac_addr_t dst_mac;
    if (!arp_resolve(iface, next_hop, &dst_mac)) {
        kfree(packet);
        return -1;
    }

    int result = ethernet_send(iface, dst_mac, ETH_P_IP, packet, total);
    kfree(packet);
    if (result == 0) stats.packets_sent++;
    return result;
}

int ip_fragment_send(net_interface_t *iface, ipv4_addr_t dst, uint8_t proto,
                     const void *payload, uint32_t len, uint8_t ttl, uint8_t tos,
                     uint16_t ident) {
    if (!iface || iface->mtu == 0) return -1;
    uint32_t mtu = iface->mtu;
    uint32_t max_payload = mtu - sizeof(ip_header_t);
    /* Make payload 8-byte aligned (required by RFC 791 except last). */
    max_payload &= ~7U;
    if (max_payload < 8) return -1;

    uint32_t sent_total = 0;
    uint16_t my_id = ident ? ident : next_ip_id++;
    uint16_t off = 0;
    while (sent_total < len) {
        uint32_t chunk = len - sent_total;
        if (chunk > max_payload) chunk = max_payload;

        ip_header_t hdr;
        memset(&hdr, 0, sizeof(hdr));
        hdr.version_ihl = (4 << 4) | 5;
        hdr.tos = tos;
        hdr.total_length = sizeof(ip_header_t) + chunk;
        hdr.identification = my_id;
        hdr.flags_fragment = (sent_total + chunk < len) ? IP_FLAG_MF : 0;
        hdr.flags_fragment |= (off & 0x1FFF);
        hdr.ttl = ttl;
        hdr.protocol = proto;
        hdr.checksum = 0;
        hdr.src_ip = iface->ip;
        hdr.dst_ip = dst;
        hdr.checksum = ip_checksum(&hdr, sizeof(ip_header_t));

        uint32_t total = sizeof(ip_header_t) + chunk;
        uint8_t *packet = (uint8_t *)kmalloc(total);
        if (!packet) return -1;
        memcpy(packet, &hdr, sizeof(ip_header_t));
        memcpy(packet + sizeof(ip_header_t), (const uint8_t *)payload + sent_total, chunk);

        ipv4_addr_t next_hop;
        if ((dst.addr & iface->mask.addr) == (iface->ip.addr & iface->mask.addr)) {
            next_hop = dst;
        } else {
            next_hop = iface->gateway;
        }
        mac_addr_t dst_mac;
        int arp_ok = arp_resolve(iface, next_hop, &dst_mac);
        if (arp_ok) {
            ethernet_send(iface, dst_mac, ETH_P_IP, packet, total);
            stats.fragments_sent++;
        }
        kfree(packet);
        sent_total += chunk;
        off        += chunk;
    }
    return 0;
}

/* ------------------------------------------------------------------- */
/*  Reassembly                                                          */
/* ------------------------------------------------------------------- */

typedef struct ip_frag {
    uint16_t        offset;       /* fragment offset in 8-byte units */
    uint8_t        *data;
    uint32_t        len;
    uint8_t         flags;        /* MF + status */
    struct ip_frag *next;
} ip_frag_t;

typedef struct {
    ipv4_addr_t   src;
    ipv4_addr_t   dst;
    uint8_t       proto;
    uint16_t      id;
    uint8_t       used;
    uint32_t      first_ms;
    uint32_t      total_bytes;
    uint32_t      holes_end;     /* next expected offset (no hole) */
    uint8_t       complete;
    ip_frag_t    *frags;
    net_buffer_t *buf;           /* buffer being assembled */
    uint32_t      buf_cap;
} reasm_entry_t;

static reasm_entry_t reasm_table[IP_REASM_MAX];
static mutex_t       reasm_lock;

static reasm_entry_t *reasm_lookup(const ipv4_addr_t *src, const ipv4_addr_t *dst,
                                   uint8_t proto, uint16_t id) {
    for (uint32_t i = 0; i < IP_REASM_MAX; i++) {
        reasm_entry_t *r = &reasm_table[i];
        if (r->used && r->id == id && r->proto == proto &&
            r->src.addr == src->addr && r->dst.addr == dst->addr) {
            return r;
        }
    }
    return NULL;
}

static reasm_entry_t *reasm_alloc(void) {
    uint32_t now = timer_get_ticks() * 10U;
    /* Reap old entries first. */
    for (uint32_t i = 0; i < IP_REASM_MAX; i++) {
        if (reasm_table[i].used &&
            (uint32_t)(now - reasm_table[i].first_ms) > IP_FRAG_TIMEOUT_MS) {
            ip_frag_t *f = reasm_table[i].frags;
            while (f) { ip_frag_t *n = f->next; if (f->data) kfree(f->data); kfree(f); f = n; }
            if (reasm_table[i].buf) net_free_buffer(reasm_table[i].buf);
            reasm_table[i].frags = NULL;
            reasm_table[i].buf   = NULL;
            reasm_table[i].used  = 0;
            stats.dropped++;
        }
    }
    for (uint32_t i = 0; i < IP_REASM_MAX; i++) {
        if (!reasm_table[i].used) return &reasm_table[i];
    }
    /* Out of slots: drop the oldest. */
    uint32_t oldest = 0xFFFFFFFF; int oi = 0;
    for (uint32_t i = 0; i < IP_REASM_MAX; i++) {
        if (reasm_table[i].first_ms < oldest) { oldest = reasm_table[i].first_ms; oi = (int)i; }
    }
    reasm_entry_t *r = &reasm_table[oi];
    ip_frag_t *f = r->frags;
    while (f) { ip_frag_t *n = f->next; if (f->data) kfree(f->data); kfree(f); f = n; }
    if (r->buf) net_free_buffer(r->buf);
    r->frags = NULL;
    r->buf   = NULL;
    r->used  = 0;
    return r;
}

void ip_reassemble_tick(uint32_t now_ms) {
    mutex_lock(&reasm_lock);
    for (uint32_t i = 0; i < IP_REASM_MAX; i++) {
        if (reasm_table[i].used &&
            (uint32_t)(now_ms - reasm_table[i].first_ms) > IP_FRAG_TIMEOUT_MS) {
            ip_frag_t *f = reasm_table[i].frags;
            while (f) { ip_frag_t *n = f->next; if (f->data) kfree(f->data); kfree(f); f = n; }
            if (reasm_table[i].buf) net_free_buffer(reasm_table[i].buf);
            reasm_table[i].frags = NULL;
            reasm_table[i].buf   = NULL;
            reasm_table[i].used  = 0;
            stats.dropped++;
        }
    }
    mutex_unlock(&reasm_lock);
}

static int reasm_insert(reasm_entry_t *r, uint16_t off, const uint8_t *data, uint32_t len, uint8_t mf) {
    /* Insert sorted by offset. */
    ip_frag_t **pp = &r->frags;
    while (*pp && (*pp)->offset < off) pp = &(*pp)->next;
    ip_frag_t *n = (ip_frag_t *)kmalloc(sizeof(ip_frag_t));
    if (!n) return -1;
    n->offset = off;
    n->len    = len;
    n->flags  = mf ? 1 : 0;
    n->data   = (uint8_t *)kmalloc(len);
    if (!n->data) { kfree(n); return -1; }
    memcpy(n->data, data, len);
    n->next = *pp;
    *pp = n;
    r->total_bytes += len;

    /* Coalesce with next neighbour. */
    if (n->next && n->offset + n->len == n->next->offset) {
        ip_frag_t *m = n->next;
        uint8_t *nb = (uint8_t *)kmalloc(n->len + m->len);
        if (nb) {
            memcpy(nb, n->data, n->len);
            memcpy(nb + n->len, m->data, m->len);
            kfree(n->data); kfree(m->data);
            n->data = nb;
            n->len  += m->len;
            n->next  = m->next;
            kfree(m);
        }
    }
    return 0;
}

static int reasm_finalize(reasm_entry_t *r) {
    /* Allocate the output buffer and copy in order. */
    uint32_t total = r->total_bytes;
    if (total == 0 || total > 65535) return -1;
    net_buffer_t *out = net_alloc_buffer();
    if (!out) return -1;
    uint32_t pos = 0;
    for (ip_frag_t *f = r->frags; f; f = f->next) {
        if (pos + f->len > sizeof(out->data)) { net_free_buffer(out); return -1; }
        memcpy(out->data + pos, f->data, f->len);
        pos += f->len;
    }
    out->len    = pos;
    out->offset = 0;
    out->iface  = r->buf ? r->buf->iface : NULL;
    /* Dispatch to upper layer by replaying through ip_receive-ish path */
    extern void ip_dispatch_payload(net_buffer_t *buf, uint8_t proto);
    if (out->iface) {
        ip_dispatch_payload(out, r->proto);
    } else {
        net_free_buffer(out);
    }
    return 0;
}

void ip_dispatch_payload(net_buffer_t *buf, uint8_t proto) {
    /* If a raw socket is bound to this protocol, hand the buffer
     * to it and stop further processing.  This is the BSD-style
     * "first match wins" rule. */
    if (raw_sock_has_consumer(proto)) {
        raw_sock_deliver(proto, buf);
        return;
    }
    switch (proto) {
    case IP_PROTO_ICMP:     icmp_receive(buf);      break;
    case IP_PROTO_TCP:      tcp_receive(buf);       break;
    case IP_PROTO_UDP:      udp_receive(buf);       break;
    case IP_PROTO_UDP_LITE: udp_lite_receive(buf);  break;
    case 2:                 igmp_handle_report(buf->iface,
                                               buf->data + buf->offset,
                                               buf->len); break;
    default: break;
    }
}

void ip_receive(net_buffer_t *buf) {
    ip_header_t *hdr = (ip_header_t *)(buf->data + buf->offset);

    if (((hdr->version_ihl >> 4) & 0x0F) != 4) {
        return;
    }

    uint16_t recv_cksum = hdr->checksum;
    hdr->checksum = 0;
    uint16_t calc_cksum = ip_checksum(hdr, sizeof(ip_header_t));
    hdr->checksum = recv_cksum;

    if (recv_cksum != calc_cksum) {
        stats.checksum_errors++;
        return;
    }

    if (hdr->dst_ip.addr != buf->iface->ip.addr && hdr->dst_ip.addr != 0xFFFFFFFF) {
        return;
    }

    /* TTL safety. */
    if (hdr->ttl == 0) {
        stats.ttl_expired++;
        return;
    }

    uint8_t ihl = (hdr->version_ihl & 0x0F) * 4;
    if (ihl < 20) return;
    if (hdr->total_length < ihl) return;

    uint16_t flags = (hdr->flags_fragment >> 13) & 0x07;
    uint16_t frag_offset = hdr->flags_fragment & 0x1FFF;
    int more_frags  = (flags & 0x01) != 0;
    int is_fragment = (frag_offset != 0) || more_frags;

    uint8_t *payload = (uint8_t *)hdr + ihl;
    uint32_t payload_len = hdr->total_length - ihl;
    /* Cap by buffer length to avoid over-reads on stripped frames. */
    if (payload_len > buf->len - ihl) payload_len = buf->len - ihl;

    if (!is_fragment) {
        /* Fast path: not fragmented. */
        buf->offset = (uint32_t)((uint8_t *)payload - buf->data);
        buf->len    = payload_len;
        stats.packets_rcvd++;
        ip_dispatch_payload(buf, hdr->protocol);
        return;
    }

    /* Fragmented path: assemble. */
    stats.fragments_rcvd++;
    mutex_lock(&reasm_lock);
    reasm_entry_t *r = reasm_lookup(&hdr->src_ip, &hdr->dst_ip, hdr->protocol, hdr->identification);
    if (!r) {
        r = reasm_alloc();
        if (!r) { mutex_unlock(&reasm_lock); stats.dropped++; return; }
        memset(r, 0, sizeof(*r));
        r->src  = hdr->src_ip;
        r->dst  = hdr->dst_ip;
        r->proto = hdr->protocol;
        r->id   = hdr->identification;
        r->first_ms = timer_get_ticks() * 10U;
        r->buf  = NULL;
        r->used = 1;
    }
    reasm_insert(r, frag_offset, payload, payload_len, more_frags);
    if (!more_frags) {
        r->complete = 1;
    }
    /* If no more fragments are expected, attempt to reassemble. */
    if (r->complete) {
        int rc = reasm_finalize(r);
        if (rc == 0) stats.reassembled++;
        /* Release the cache slot. */
        ip_frag_t *f = r->frags;
        while (f) { ip_frag_t *n = f->next; if (f->data) kfree(f->data); kfree(f); f = n; }
        if (r->buf) net_free_buffer(r->buf);
        r->frags = NULL;
        r->buf   = NULL;
        r->used  = 0;
    }
    mutex_unlock(&reasm_lock);
}

const ip_stats_t *ip_get_stats(void) {
    return &stats;
}

/* ------------------------------------------------------------------- */
/*  Path MTU cache (RFC 1191)                                           */
/* ------------------------------------------------------------------- */

typedef struct {
    ipv4_addr_t dst;
    uint32_t    mtu;
    uint32_t    expires_ms;
    uint8_t     used;
} pmtu_entry_t;

static pmtu_entry_t pmtu_table[IP_PMTU_MAX_ENTRIES];
static mutex_t      pmtu_lock;

static pmtu_entry_t *pmtu_lookup_unlocked(ipv4_addr_t dst) {
    for (uint32_t i = 0; i < IP_PMTU_MAX_ENTRIES; i++) {
        if (pmtu_table[i].used && pmtu_table[i].dst.addr == dst.addr)
            return &pmtu_table[i];
    }
    return NULL;
}

int ip_pmtu_get(ipv4_addr_t dst) {
    int mtu = -1;
    mutex_lock(&pmtu_lock);
    pmtu_entry_t *e = pmtu_lookup_unlocked(dst);
    if (e) mtu = (int)e->mtu;
    mutex_unlock(&pmtu_lock);
    return mtu >= 0 ? mtu : IP_PMTU_DEFAULT;
}

void ip_pmtu_update(ipv4_addr_t dst, uint32_t mtu) {
    if (mtu < 68) mtu = 68;          /* RFC 791 minimum */
    if (mtu > 65535) mtu = 65535;
    mutex_lock(&pmtu_lock);
    pmtu_entry_t *e = pmtu_lookup_unlocked(dst);
    if (!e) {
        for (uint32_t i = 0; i < IP_PMTU_MAX_ENTRIES; i++) {
            if (!pmtu_table[i].used) { e = &pmtu_table[i]; break; }
        }
        if (!e) {
            /* Evict the entry that expires first. */
            uint32_t soon = 0xFFFFFFFFU;
            int oi = 0;
            for (uint32_t i = 0; i < IP_PMTU_MAX_ENTRIES; i++) {
                if (pmtu_table[i].expires_ms < soon) {
                    soon = pmtu_table[i].expires_ms; oi = (int)i;
                }
            }
            e = &pmtu_table[oi];
        }
        e->dst = dst;
        e->used = 1;
    }
    if (mtu < e->mtu || e->mtu == 0) e->mtu = mtu;
    e->expires_ms = timer_get_ticks() * 10U + IP_PMTU_TIMEOUT_MS;
    mutex_unlock(&pmtu_lock);
}

void ip_pmtu_age(uint32_t now_ms) {
    mutex_lock(&pmtu_lock);
    for (uint32_t i = 0; i < IP_PMTU_MAX_ENTRIES; i++) {
        if (pmtu_table[i].used &&
            (uint32_t)(now_ms - pmtu_table[i].expires_ms) > 0 &&
            pmtu_table[i].expires_ms != 0) {
            pmtu_table[i].used = 0;
        }
    }
    mutex_unlock(&pmtu_lock);
}

void ip_pmtu_clear(void) {
    mutex_lock(&pmtu_lock);
    memset(pmtu_table, 0, sizeof(pmtu_table));
    mutex_unlock(&pmtu_lock);
}

/* ------------------------------------------------------------------- */
/*  IP options (RFC 791 §3.1)                                           */
/* ------------------------------------------------------------------- */

int ip_options_length(const uint8_t *options, uint32_t len) {
    if (!options) return 0;
    uint32_t pos = 0;
    while (pos < len) {
        uint8_t k = options[pos];
        if (k == IP_OPT_EOOL) return (int)(((pos + 3) / 4) * 4);
        if (k == IP_OPT_NOP)  { pos++; continue; }
        if (pos + 1 >= len) return (int)(((pos + 3) / 4) * 4);
        uint8_t opt_len = options[pos + 1];
        if (opt_len < 2) return (int)(((pos + 3) / 4) * 4);
        pos += opt_len;
    }
    return (int)(((len + 3) / 4) * 4);
}

int ip_parse_options(const uint8_t *options, uint32_t len, ip_opt_handler_t h, void *ctx) {
    if (!options || len == 0) return 0;
    uint32_t pos = 0;
    int n = 0;
    while (pos < len) {
        uint8_t k = options[pos];
        if (k == IP_OPT_EOOL) break;
        if (k == IP_OPT_NOP)  { pos++; continue; }
        if (pos + 1 >= len) break;
        uint8_t opt_len = options[pos + 1];
        if (opt_len < 2 || pos + opt_len > len) break;
        if (h) h(k, &options[pos + 2], opt_len - 2, ctx);
        pos += opt_len;
        n++;
    }
    return n;
}

int ip_build_record_route(const ipv4_addr_t *addrs, uint8_t count, uint8_t *out, uint32_t max) {
    if (!out || max < 3) return -1;
    out[0] = IP_OPT_RR;
    out[1] = 3;                        /* will fix up below           */
    out[2] = count ? 4 : 0;            /* pointer = 4 (first slot)    */
    uint32_t pos = 3;
    for (uint8_t i = 0; i < count && pos + 4 <= max; i++) {
        out[pos + 0] = (addrs[i].addr >> 24) & 0xFF;
        out[pos + 1] = (addrs[i].addr >> 16) & 0xFF;
        out[pos + 2] = (addrs[i].addr >>  8) & 0xFF;
        out[pos + 3] = (addrs[i].addr      ) & 0xFF;
        pos += 4;
    }
    /* Total option length in bytes (multiple of 4 already since we
     * copy whole 4-byte slots). */
    out[1] = (uint8_t)pos;
    return (int)pos;
}

/* ========================================================================= */
/*  QoS / Packet Scheduling Module                                           */
/* ========================================================================= */

/* Simple queue node for QoS */
typedef struct qos_queue_node {
    net_buffer_t          *buf;
    uint8_t                band;
    struct qos_queue_node *next;
} qos_queue_node_t;

#define QOS_QUEUE_MAX  256

static qos_queue_node_t *qos_queues[QOS_BAND_MAX];  /* linked lists, one per band */
static uint32_t           qos_queue_count;
static uint8_t            qos_disc;                  /* current queue discipline */
static tbf_params_t       qos_tbf;                   /* token bucket for rate limiting */
static qos_stats_t        qos_stats_local;
static mutex_t            qos_lock;

int qos_init(void) {
    for (int i = 0; i < QOS_BAND_MAX; i++) qos_queues[i] = NULL;
    qos_queue_count = 0;
    qos_disc = QDISC_PFIFO_FAST;
    memset(&qos_tbf, 0, sizeof(qos_tbf));
    qos_tbf.rate = 0;       /* unlimited by default */
    qos_tbf.burst = 65536;
    qos_tbf.tokens = 65536;
    qos_tbf.last_update_ms = timer_get_ticks() * 10U;
    memset(&qos_stats_local, 0, sizeof(qos_stats_local));
    mutex_init(&qos_lock);
    return 0;
}

int qos_set_discipline(uint8_t disc) {
    if (disc > QDISC_FQ_CODEL) return -1;
    qos_disc = disc;
    return 0;
}

uint8_t qos_get_discipline(void) { return qos_disc; }

/* DSCP to priority band mapping */
int qos_dscp_to_band(uint8_t dscp) {
    dscp = dscp & 0x3F;
    /* Map DSCP to band based on rough priority */
    if (dscp >= 40) return QOS_BAND_HI;       /* CS5/EF/CS6/CS7 */
    if (dscp >= 24) return QOS_BAND_MID;      /* CS3/AF3x/CS4/AF4x */
    return QOS_BAND_LO;                        /* CS0/AF1x/CS1/AF2x/CS2 */
}

int qos_enqueue(net_buffer_t *buf, uint8_t tos) {
    if (!buf || qos_queue_count >= QOS_QUEUE_MAX) {
        if (buf) qos_stats_local.packets_dropped[QOS_BAND_LO]++;
        return -1;
    }

    uint8_t band = qos_dscp_to_band(tos & 0xFC);

    qos_queue_node_t *node = (qos_queue_node_t *)kmalloc(sizeof(qos_queue_node_t));
    if (!node) {
        qos_stats_local.packets_dropped[band]++;
        return -1;
    }
    node->buf = buf;
    node->band = band;
    node->next = NULL;

    mutex_lock(&qos_lock);
    /* Append to band queue */
    if (!qos_queues[band]) {
        qos_queues[band] = node;
    } else {
        qos_queue_node_t *tail = qos_queues[band];
        while (tail->next) tail = tail->next;
        tail->next = node;
    }
    qos_queue_count++;
    qos_stats_local.packets_queued[band]++;
    qos_stats_local.bytes_queued[band] += buf->len;
    mutex_unlock(&qos_lock);
    return 0;
}

net_buffer_t *qos_dequeue(net_interface_t *iface) {
    (void)iface;
    mutex_lock(&qos_lock);

    /* Token bucket replenish */
    uint32_t now = timer_get_ticks() * 10U;
    uint32_t elapsed = now - qos_tbf.last_update_ms;
    if (elapsed > 0 && qos_tbf.rate > 0) {
        uint32_t new_tokens = (qos_tbf.rate * elapsed) / 1000U;
        qos_tbf.tokens += new_tokens;
        if (qos_tbf.tokens > qos_tbf.burst)
            qos_tbf.tokens = qos_tbf.burst;
        qos_tbf.last_update_ms = now;
    }

    /* Prioritized dequeue: try HI → MID → LO */
    for (int band = QOS_BAND_MAX - 1; band >= 0; band--) {
        if (qos_queues[band]) {
            /* Token bucket check for rate limiting */
            if (qos_tbf.rate > 0 && qos_tbf.tokens < qos_queues[band]->buf->len) {
                /* Not enough tokens for this packet; try lower band */
                continue;
            }

            qos_queue_node_t *node = qos_queues[band];
            qos_queues[band] = node->next;
            qos_queue_count--;

            net_buffer_t *result = node->buf;
            kfree(node);

            /* Consume tokens */
            if (qos_tbf.rate > 0 && qos_tbf.tokens >= result->len)
                qos_tbf.tokens -= result->len;

            qos_stats_local.packets_dequeued[band]++;
            qos_stats_local.bytes_dequeued[band] += result->len;

            if (band == QOS_BAND_HI && qos_queue_count > 0)
                qos_stats_local.high_prio_steals++;

            mutex_unlock(&qos_lock);
            return result;
        }
    }
    mutex_unlock(&qos_lock);
    return NULL;
}

int qos_set_rate_limit(uint32_t rate_bps, uint32_t burst_bytes) {
    qos_tbf.rate = rate_bps;
    qos_tbf.burst = burst_bytes;
    qos_tbf.tokens = burst_bytes;
    qos_tbf.last_update_ms = timer_get_ticks() * 10U;
    return 0;
}

void qos_tick(uint32_t now_ms) {
    (void)now_ms;
    /* Periodic cleanup of stale entries could go here */
}

const qos_stats_t *qos_get_stats(void) { return &qos_stats_local; }

/* ========================================================================= */
/*  IPsec Module (AH / ESP / SPD)                                            */
/* ========================================================================= */

static ipsec_sa_t       ipsec_sa_table[IPSEC_SA_MAX];
static ipsec_spd_entry_t ipsec_spd[IPSEC_SPD_MAX];
static ipsec_stats_t    ipsec_stats;
static mutex_t          ipsec_lock;
static uint32_t         ipsec_seq_num;

void ipsec_init(void) {
    memset(ipsec_sa_table, 0, sizeof(ipsec_sa_table));
    memset(ipsec_spd, 0, sizeof(ipsec_spd));
    memset(&ipsec_stats, 0, sizeof(ipsec_stats));
    ipsec_seq_num = 1;
    mutex_init(&ipsec_lock);
}

/* ---- SA management ---- */

int ipsec_sa_add(uint32_t spi, ipv4_addr_t dst, uint8_t proto, uint8_t mode,
                  uint8_t enc_alg, const uint8_t *enc_key, uint8_t enc_key_len,
                  uint8_t auth_alg, const uint8_t *auth_key, uint8_t auth_key_len) {
    mutex_lock(&ipsec_lock);
    for (uint32_t i = 0; i < IPSEC_SA_MAX; i++) {
        if (!ipsec_sa_table[i].used) {
            ipsec_sa_t *sa = &ipsec_sa_table[i];
            sa->used = 1;
            sa->spi  = spi;
            sa->dst  = dst;
            sa->proto = proto;
            sa->mode  = mode;
            sa->enc_alg  = enc_alg;
            sa->auth_alg = auth_alg;
            if (enc_key && enc_key_len <= 32) {
                memcpy(sa->enc_key, enc_key, enc_key_len);
                sa->enc_key_len = enc_key_len;
            }
            if (auth_key && auth_key_len <= 32) {
                memcpy(sa->auth_key, auth_key, auth_key_len);
                sa->auth_key_len = auth_key_len;
            }
            sa->lifetime_soft = 1000000000;  /* 1 GB soft */
            sa->lifetime_hard = 2000000000;  /* 2 GB hard */
            sa->bytes_processed = 0;
            sa->packets_processed = 0;
            ipsec_stats.sa_created++;
            mutex_unlock(&ipsec_lock);
            return 0;
        }
    }
    mutex_unlock(&ipsec_lock);
    return -1;
}

int ipsec_sa_del(uint32_t spi, ipv4_addr_t dst) {
    mutex_lock(&ipsec_lock);
    for (uint32_t i = 0; i < IPSEC_SA_MAX; i++) {
        if (ipsec_sa_table[i].used &&
            ipsec_sa_table[i].spi == spi &&
            ipsec_sa_table[i].dst.addr == dst.addr) {
            ipsec_sa_table[i].used = 0;
            ipsec_stats.sa_deleted++;
            mutex_unlock(&ipsec_lock);
            return 0;
        }
    }
    mutex_unlock(&ipsec_lock);
    return -1;
}

ipsec_sa_t *ipsec_sa_lookup(uint32_t spi, ipv4_addr_t dst, uint8_t proto) {
    for (uint32_t i = 0; i < IPSEC_SA_MAX; i++) {
        if (ipsec_sa_table[i].used &&
            ipsec_sa_table[i].spi == spi &&
            ipsec_sa_table[i].dst.addr == dst.addr &&
            ipsec_sa_table[i].proto == proto) {
            return &ipsec_sa_table[i];
        }
    }
    return NULL;
}

void ipsec_sa_tick(uint32_t now_ms) {
    (void)now_ms;
    mutex_lock(&ipsec_lock);
    for (uint32_t i = 0; i < IPSEC_SA_MAX; i++) {
        if (ipsec_sa_table[i].used &&
            ipsec_sa_table[i].bytes_processed > ipsec_sa_table[i].lifetime_hard) {
            ipsec_sa_table[i].used = 0;
            ipsec_stats.sa_expired++;
        }
    }
    mutex_unlock(&ipsec_lock);
}

void ipsec_sa_flush(void) {
    mutex_lock(&ipsec_lock);
    memset(ipsec_sa_table, 0, sizeof(ipsec_sa_table));
    mutex_unlock(&ipsec_lock);
}

/* ---- SPD management ---- */

int ipsec_spd_add(uint8_t action, ipv4_addr_t src, ipv4_addr_t src_mask,
                   ipv4_addr_t dst, ipv4_addr_t dst_mask, uint8_t proto,
                   uint16_t sport, uint16_t dport, uint32_t spi_in, uint32_t spi_out) {
    mutex_lock(&ipsec_lock);
    for (uint32_t i = 0; i < IPSEC_SPD_MAX; i++) {
        if (!ipsec_spd[i].used) {
            ipsec_spd_entry_t *spd = &ipsec_spd[i];
            spd->used     = 1;
            spd->action   = action;
            spd->src      = src;
            spd->src_mask = src_mask;
            spd->dst      = dst;
            spd->dst_mask = dst_mask;
            spd->proto    = proto;
            spd->sport    = sport;
            spd->dport    = dport;
            spd->spi_in   = spi_in;
            spd->spi_out  = spi_out;
            spd->counter  = 0;
            mutex_unlock(&ipsec_lock);
            return 0;
        }
    }
    mutex_unlock(&ipsec_lock);
    return -1;
}

int ipsec_spd_del(uint32_t idx) {
    mutex_lock(&ipsec_lock);
    if (idx >= IPSEC_SPD_MAX || !ipsec_spd[idx].used) {
        mutex_unlock(&ipsec_lock);
        return -1;
    }
    ipsec_spd[idx].used = 0;
    mutex_unlock(&ipsec_lock);
    return 0;
}

ipsec_spd_entry_t *ipsec_spd_lookup(net_buffer_t *buf, int dir) {
    if (!buf || buf->len < (int)sizeof(ip_header_t)) return NULL;
    ip_header_t *ip = (ip_header_t *)(buf->data + buf->offset);

    for (uint32_t i = 0; i < IPSEC_SPD_MAX; i++) {
        ipsec_spd_entry_t *spd = &ipsec_spd[i];
        if (!spd->used) continue;

        ipv4_addr_t ck_src = (dir == 0) ? ip->src_ip : ip->dst_ip;
        ipv4_addr_t ck_dst = (dir == 0) ? ip->dst_ip : ip->src_ip;

        if ((ck_src.addr & spd->src_mask.addr) == (spd->src.addr & spd->src_mask.addr) &&
            (ck_dst.addr & spd->dst_mask.addr) == (spd->dst.addr & spd->dst_mask.addr) &&
            (spd->proto == 0 || spd->proto == ip->protocol)) {
            spd->counter++;
            return spd;
        }
    }
    return NULL;
}

int ipsec_spd_apply(net_buffer_t *buf, int dir) {
    ipsec_spd_entry_t *spd = ipsec_spd_lookup(buf, dir);
    if (!spd) return 0;  /* no policy = bypass */

    switch (spd->action) {
    case 0: /* bypass */
        ipsec_stats.pkts_bypassed++;
        return 0;
    case 1: /* protect */
        if (dir == 0) return ipsec_output(buf);
        else          return ipsec_input(buf);
    case 2: /* discard */
        ipsec_stats.pkts_discarded++;
        return -1;
    default:
        return 0;
    }
}

/* ---- IPsec output processing (ESP transport mode) ---- */

int ipsec_output(net_buffer_t *buf) {
    if (!buf || buf->len < (int)sizeof(ip_header_t)) return -1;
    ip_header_t *ip = (ip_header_t *)(buf->data + buf->offset);

    /* Find matching SA */
    ipsec_sa_t *sa = NULL;
    for (uint32_t i = 0; i < IPSEC_SA_MAX; i++) {
        if (ipsec_sa_table[i].used &&
            ipsec_sa_table[i].dst.addr == ip->dst_ip.addr &&
            ipsec_sa_table[i].proto == IPSEC_PROTO_ESP) {
            sa = &ipsec_sa_table[i];
            break;
        }
    }
    if (!sa) return -1;

    /* In a real implementation, we would:
     * 1. Insert ESP header after IP header
     * 2. Encrypt payload
     * 3. Compute ICV
     * 4. Update IP header (proto = ESP, total_length += ESP overhead)
     */
    sa->packets_processed++;
    sa->bytes_processed += buf->len;
    ipsec_stats.pkts_encrypted++;
    return 0;
}

/* ---- IPsec input processing ---- */

int ipsec_input(net_buffer_t *buf) {
    if (!buf || buf->len < (int)sizeof(ip_header_t) + (int)sizeof(esp_header_t)) return -1;
    ip_header_t *ip = (ip_header_t *)(buf->data + buf->offset);

    /* ESP header follows IP header */
    uint8_t ihl = (ip->version_ihl & 0x0F) * 4;
    esp_header_t *esp = (esp_header_t *)((uint8_t *)ip + ihl);

    /* Find SA by SPI */
    ipsec_sa_t *sa = ipsec_sa_lookup(esp->spi, ip->src_ip, IPSEC_PROTO_ESP);
    if (!sa) return -1;

    /* Check anti-replay via sequence number */
    if (esp->sequence < ipsec_seq_num - 1000) {
        ipsec_stats.pkts_discarded++;
        return -1;
    }

    /* In a real implementation, we would:
     * 1. Verify ICV (authentication)
     * 2. Decrypt payload
     * 3. Remove ESP header/trailer
     * 4. Restore original IP protocol
     */
    sa->packets_processed++;
    sa->bytes_processed += buf->len;
    ipsec_stats.pkts_decrypted++;
    ipsec_stats.pkts_auth_ok++;
    return 0;
}

const ipsec_stats_t *ipsec_get_stats(void) { return &ipsec_stats; }
