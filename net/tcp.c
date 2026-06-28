/*
 * TCP - Transmission Control Protocol implementation
 *
 * Largely follows RFC 793 with selected features from RFC 1122,
 * RFC 5681 (congestion control), RFC 6298 (RTO), RFC 7323 (TS/WS) and
 * RFC 2018 (SACK).  The code keeps the original entry points
 * (tcp_init / tcp_connect / tcp_listen / tcp_accept / tcp_send /
 *  tcp_close / tcp_receive / tcp_handle_state / tcp_retransmit_check /
 *  tcp_socket_create / tcp_checksum) so existing callers continue
 * to compile and run unchanged.
 */

#include "tcp.h"
#include "ip.h"
#include "icmp.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"
#include "timer.h"
#include "sync.h"
#include "sched.h"
#include "kernel_types.h"
#include "stddef.h"

/* ------------------------------------------------------------------------- */
/*  Module state                                                             */
/* ------------------------------------------------------------------------- */

static tcp_socket_t *sockets[65536];
static uint16_t      next_ephemeral_port = 49152;
static mutex_t       tcp_table_lock;
static uint32_t      tcp_now_ms;
static tcp_stats_t   stats;
static tcp_socket_t *all_sockets;     /* linked list for /proc/net */
static tcp_cc_stats_t cc_stats;       /* CC algorithm stats        */

#define TCP_HASH_TABLE_SIZE 1024
static tcp_socket_t *tcp_hash[TCP_HASH_TABLE_SIZE];

static uint32_t now_ms(void) {
    uint32_t t = timer_get_ticks();
    return t * 10U; /* PIT runs at 100 Hz */
}

static inline uint32_t seq_lt(uint32_t a, uint32_t b) { return (int32_t)(a - b) < 0; }
static inline uint32_t seq_le(uint32_t a, uint32_t b) { return (int32_t)(a - b) <= 0; }
static inline uint32_t seq_gt(uint32_t a, uint32_t b) { return (int32_t)(a - b) > 0; }
static inline uint32_t seq_ge(uint32_t a, uint32_t b) { return (int32_t)(a - b) >= 0; }

static uint32_t hash_func(uint16_t lp, uint16_t rp, uint32_t lip, uint32_t rip) {
    uint32_t h = (uint32_t)lp * 31u + (uint32_t)rp;
    h ^= lip * 0x9E3779B1u;
    h ^= rip * 0x85EBCA77u;
    return h % TCP_HASH_TABLE_SIZE;
}

static void hash_insert(tcp_socket_t *s) {
    uint32_t h = hash_func(s->local_port, s->remote_port,
                           s->local_ip.addr, s->remote_ip.addr);
    s->next = tcp_hash[h];
    tcp_hash[h] = s;
}

static void hash_remove(tcp_socket_t *s) {
    uint32_t h = hash_func(s->local_port, s->remote_port,
                           s->local_ip.addr, s->remote_ip.addr);
    tcp_socket_t **pp = &tcp_hash[h];
    while (*pp) {
        if (*pp == s) {
            *pp = s->next;
            s->next = NULL;
            return;
        }
        pp = &(*pp)->next;
    }
}

/* ------------------------------------------------------------------------- */
/*  Forward declarations                                                     */
/* ------------------------------------------------------------------------- */

static int  tcp_send_segment(tcp_socket_t *sock, uint8_t flags, const void *data, uint32_t len);
static void tcp_retransmit_segment(tcp_socket_t *sock, tcp_segment_t *seg);
static void tcp_push_pending(tcp_socket_t *sock);
static void tcp_deliver_data(tcp_socket_t *sock);
static void tcp_process_ack(tcp_socket_t *sock, uint32_t ack, uint32_t wnd);
static void tcp_transition_to_established(tcp_socket_t *sock);
static void tcp_close_internal(tcp_socket_t *sock);
static void tcp_wake_owner(tcp_socket_t *sock);
static uint32_t tcp_effective_mss(tcp_socket_t *sock);

/* ------------------------------------------------------------------------- */
/*  Init / setup                                                             */
/* ------------------------------------------------------------------------- */

void tcp_init(void) {
    for (uint32_t i = 0; i < 65536; i++) sockets[i] = NULL;
    for (uint32_t i = 0; i < TCP_HASH_TABLE_SIZE; i++) tcp_hash[i] = NULL;
    mutex_init(&tcp_table_lock);
    tcp_now_ms = now_ms();
    memset(&stats, 0, sizeof(stats));
    all_sockets = NULL;
}

/* ---- Stats helpers ---- */
void tcp_register_stats_active(void)     { stats.active_opens++; }
void tcp_register_stats_passive(void)    { stats.passive_opens++; }
void tcp_register_stats_attempt(void)    { stats.attempts++; }
void tcp_register_stats_established(void){ stats.established++; }
void tcp_register_stats_close(void)      { stats.closes++; }
void tcp_register_stats_sent(uint32_t bytes, int retrans) {
    stats.segs_sent++;
    stats.bytes_sent += bytes;
    if (retrans) stats.retransmits++;
}
void tcp_register_stats_rcvd(uint32_t bytes) {
    stats.segs_rcvd++;
    stats.bytes_rcvd += bytes;
}

const tcp_stats_t *tcp_get_stats(void) { return &stats; }

const tcp_socket_t *tcp_get_sockets(uint32_t *count) {
    if (count) {
        uint32_t c = 0;
        for (tcp_socket_t *s = all_sockets; s; s = s->next_all) c++;
        *count = c;
    }
    return all_sockets;
}

const tcp_socket_t *tcp_get_listeners(uint32_t *count) {
    /* Walk the global list and count listeners.  The pointers are not
     * stable across the call (the kernel may modify the list), but
     * readers expect a snapshot, so we simply report the count and let
     * the caller iterate via the next_all chain directly. */
    if (count) {
        uint32_t c = 0;
        for (tcp_socket_t *s = all_sockets; s; s = s->next_all) {
            if (s->state == TCP_STATE_LISTEN) c++;
        }
        *count = c;
    }
    return all_sockets;
}

int tcp_set_mss(tcp_socket_t *sock, uint16_t mss) {
    if (!sock || mss < 64 || mss > MSS) return -1;
    sock->mss_local = mss;
    return 0;
}
int tcp_get_mss(tcp_socket_t *sock) {
    return sock ? (int)sock->mss_local : -1;
}

tcp_socket_t *tcp_socket_create(void) {
    tcp_socket_t *sock = (tcp_socket_t *)kcalloc(1, sizeof(tcp_socket_t));
    if (!sock) return NULL;

    sock->state        = TCP_STATE_CLOSED;
    sock->snd_wnd      = TCP_MAX_WINDOW;
    sock->rcv_wnd      = TCP_RCVBUF_SIZE;
    sock->rcv_adv      = TCP_RCVBUF_SIZE;
    sock->cwnd         = TCP_INITIAL_CWND * MSS;
    sock->ssthresh     = TCP_SLOW_START_THRESHOLD;
    sock->cc_algo      = TCP_CC_NEWRENO;  /* default: NewReno */
    memset(&sock->cubic, 0, sizeof(sock->cubic));
    sock->srtt         = 0;
    sock->rttvar       = 0;
    sock->rto          = TCP_RETRANS_TIMEOUT;
    sock->rto_min      = 200;
    sock->rto_max      = 60000;
    sock->rtt_pending  = 0;
    sock->retrans_cnt  = 0;
    sock->max_retries  = TCP_MAX_RETRIES;
    sock->mss_local    = MSS;
    sock->mss_peer     = 536;
    sock->wscale_local = 0;
    sock->wscale_peer  = 0;
    sock->ts_option    = 0;
    sock->sack_enabled = 0;
    sock->ts_recent    = 0;
    sock->last_ack_time= now_ms();
    sock->time_wait_expire = 0;
    sock->keepalive_time   = 0;
    sock->keepalive_probes = 0;
    sock->zero_probe_time  = 0;
    sock->backlog_max  = TCP_SYN_BACKLOG_DEFAULT;
    sock->backlog_cur  = 0;
    sock->send_buf_size = TCP_SNDBUF_SIZE;
    sock->recv_buf_size = TCP_RCVBUF_SIZE;
    sock->send_buf = (uint8_t *)kmalloc(sock->send_buf_size);
    sock->recv_buf = (uint8_t *)kmalloc(sock->recv_buf_size);
    spinlock_init(&sock->lock);
    sock->refcount = 1;
    if (!sock->send_buf || !sock->recv_buf) {
        if (sock->send_buf) kfree(sock->send_buf);
        if (sock->recv_buf) kfree(sock->recv_buf);
        kfree(sock);
        return NULL;
    }
    return sock;
}

static void sock_release(tcp_socket_t *s) {
    if (!s) return;
    spinlock_lock(&s->lock);
    if (s->refcount > 0) s->refcount--;
    int dead = (s->refcount == 0);
    spinlock_unlock(&s->lock);
    if (!dead) return;

    tcp_segment_t *o = s->ooo_head;
    while (o) { tcp_segment_t *n = o->next; if (o->data) kfree(o->data); kfree(o); o = n; }
    tcp_segment_t *r = s->rtx_head;
    while (r) { tcp_segment_t *n = r->next; if (r->data) kfree(r->data); kfree(r); r = n; }
    if (s->send_buf) kfree(s->send_buf);
    if (s->recv_buf) kfree(s->recv_buf);
    if (s->accept_head) {
        tcp_socket_t *c = s->accept_head;
        while (c) { tcp_socket_t *n = c->next; sock_release(c); c = n; }
    }
    kfree(s);
}

static void sock_hold(tcp_socket_t *s) {
    spinlock_lock(&s->lock);
    s->refcount++;
    spinlock_unlock(&s->lock);
}

/* ------------------------------------------------------------------------- */
/*  Checksum                                                                 */
/* ------------------------------------------------------------------------- */

uint16_t tcp_checksum(ipv4_addr_t src_ip, ipv4_addr_t dst_ip, uint8_t proto,
                      const void *data, uint32_t len) {
    uint32_t sum = 0;
    const uint16_t *ptr = (const uint16_t *)data;
    sum += (src_ip.addr >> 16) & 0xFFFF;
    sum += src_ip.addr & 0xFFFF;
    sum += (dst_ip.addr >> 16) & 0xFFFF;
    sum += dst_ip.addr & 0xFFFF;
    sum += proto;
    sum += len;
    while (len > 1) { sum += *ptr++; len -= 2; }
    if (len == 1) sum += *(const uint8_t *)ptr;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

/* ------------------------------------------------------------------------- */
/*  TCP options                                                              */
/* ------------------------------------------------------------------------- */

int tcp_option_parse(const uint8_t *opt, uint32_t len, tcp_socket_t *sock) {
    uint32_t i = 0;
    while (i < len) {
        uint8_t kind = opt[i];
        if (kind == TCP_OPT_EOL) break;
        if (kind == TCP_OPT_NOP) { i++; continue; }
        if (i + 1 >= len) break;
        uint8_t olen = opt[i + 1];
        if (olen < 2 || i + olen > len) break;
        switch (kind) {
        case TCP_OPT_MSS:
            if (olen == 4) {
                uint16_t v = ((uint16_t)opt[i+2] << 8) | opt[i+3];
                if (v < sock->mss_local) sock->mss_peer = v;
                else sock->mss_peer = sock->mss_local;
            }
            break;
        case TCP_OPT_WSCALE:
            if (olen == 3) sock->wscale_peer = opt[i+2] & 0x0F;
            break;
        case TCP_OPT_SACK_OK:
            sock->sack_enabled = 1;
            break;
        case TCP_OPT_TIMESTAMP:
            if (olen == 10) {
                sock->ts_option = 1;
                sock->ts_recent = ((uint32_t)opt[i+2] << 24) | ((uint32_t)opt[i+3] << 16) |
                                  ((uint32_t)opt[i+4] << 8)  | opt[i+5];
                sock->ts_recent_tick = now_ms();
            }
            break;
        case TCP_OPT_SACK:
            if (olen > 2 && (((olen - 2) / 8) <= 4)) {
                sock->sack_count = (olen - 2) / 8;
                for (uint8_t b = 0; b < sock->sack_count; b++) {
                    const uint8_t *p = &opt[i+2 + b*8];
                    sock->sack_blocks[b].left  = ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|
                                                 ((uint32_t)p[2]<<8) |p[3];
                    sock->sack_blocks[b].right = ((uint32_t)p[4]<<24)|((uint32_t)p[5]<<16)|
                                                 ((uint32_t)p[6]<<8) |p[7];
                }
            }
            break;
        default: break;
        }
        i += olen;
    }
    return 0;
}

int tcp_option_build(tcp_socket_t *sock, uint8_t *out, uint32_t max_len, int syn) {
    uint32_t i = 0;
    if (syn) {
        if (i + 4 > max_len) return 0;
        out[i++] = TCP_OPT_MSS;
        out[i++] = 4;
        out[i++] = (uint8_t)(sock->mss_local >> 8);
        out[i++] = (uint8_t)(sock->mss_local & 0xFF);

        out[i++] = TCP_OPT_NOP;
        out[i++] = TCP_OPT_WSCALE;
        out[i++] = 3;
        out[i++] = 7; /* shift = 7 -> window scale up to 1<<7 */

        if (i + 2 > max_len) return i;
        out[i++] = TCP_OPT_SACK_OK;
        out[i++] = 2;

        if (i + 10 > max_len) return i;
        out[i++] = TCP_OPT_TIMESTAMP;
        out[i++] = 10;
        uint32_t ts = now_ms();
        out[i++] = (uint8_t)(ts >> 24);
        out[i++] = (uint8_t)(ts >> 16);
        out[i++] = (uint8_t)(ts >> 8);
        out[i++] = (uint8_t)(ts);
        out[i++] = 0; out[i++] = 0; out[i++] = 0; out[i++] = 0;
    } else if (sock->ts_option) {
        if (i + 12 > max_len) return 0;
        out[i++] = TCP_OPT_NOP; out[i++] = TCP_OPT_NOP;
        out[i++] = TCP_OPT_TIMESTAMP;
        out[i++] = 10;
        uint32_t ts = now_ms();
        out[i++] = (uint8_t)(ts >> 24);
        out[i++] = (uint8_t)(ts >> 16);
        out[i++] = (uint8_t)(ts >> 8);
        out[i++] = (uint8_t)(ts);
        out[i++] = (uint8_t)(sock->ts_recent >> 24);
        out[i++] = (uint8_t)(sock->ts_recent >> 16);
        out[i++] = (uint8_t)(sock->ts_recent >> 8);
        out[i++] = (uint8_t)(sock->ts_recent);
    }
    if (i + 2 <= max_len) { out[i++] = TCP_OPT_EOL; }
    return (int)i;
}

/* ------------------------------------------------------------------------- */
/*  RTT / RTO (RFC 6298)                                                     */
/* ------------------------------------------------------------------------- */

static void rtt_measure(tcp_socket_t *sock, uint32_t measured) {
    if (sock->srtt == 0) {
        sock->srtt = measured;
        sock->rttvar = measured / 2;
    } else {
        int32_t delta = (int32_t)measured - (int32_t)sock->srtt;
        if (delta < 0) delta = -delta;
        sock->rttvar = (3 * sock->rttvar + delta) / 4;
        sock->srtt   = (7 * sock->srtt   + measured) / 8;
    }
    sock->rto = sock->srtt + 4 * sock->rttvar;
    if (sock->rto < sock->rto_min) sock->rto = sock->rto_min;
    if (sock->rto > sock->rto_max) sock->rto = sock->rto_max;
}

static void rto_backoff(tcp_socket_t *sock) {
    sock->rto = sock->rto * 2;
    if (sock->rto < sock->rto_min) sock->rto = sock->rto_min;
    if (sock->rto > sock->rto_max) sock->rto = sock->rto_max;
}

/* ------------------------------------------------------------------------- */
/*  Retransmission queue                                                     */
/* ------------------------------------------------------------------------- */

static void rtx_push(tcp_socket_t *sock, uint32_t seq, uint32_t len, uint8_t flags, const uint8_t *data) {
    /* `len` is the byte count of *payload data only*.  SYN and FIN
     * count as one sequence number each but carry no payload. */
    tcp_segment_t *s = (tcp_segment_t *)kmalloc(sizeof(tcp_segment_t));
    if (!s) return;
    s->seq = seq;
    s->len = len;
    s->flags = flags;
    s->send_ts = now_ms();
    s->data = NULL;
    if (data && len) {
        s->data = (uint8_t *)kmalloc(len);
        if (s->data) memcpy(s->data, data, len);
    }
    s->next = NULL;
    if (sock->rtx_tail) sock->rtx_tail->next = s;
    else                sock->rtx_head = s;
    sock->rtx_tail = s;
}

static void rtx_advance(tcp_socket_t *sock, uint32_t ack) {
    /* Remove segments fully covered by ack.  Each segment occupies
     * (len + syn_bit + fin_bit) sequence numbers. */
    while (sock->rtx_head) {
        tcp_segment_t *s = sock->rtx_head;
        uint32_t consume = s->len;
        if (s->flags & TCP_SYN) consume += 1;
        if (s->flags & TCP_FIN) consume += 1;
        if (consume == 0) consume = 1;
        uint32_t end = s->seq + consume;
        if (seq_le(end, ack)) {
            sock->rtx_head = s->next;
            if (sock->rtx_head == NULL) sock->rtx_tail = NULL;
            if (s->data) kfree(s->data);
            kfree(s);
        } else {
            break;
        }
    }
}

static void rtx_clear(tcp_socket_t *sock) {
    tcp_segment_t *s = sock->rtx_head;
    while (s) {
        tcp_segment_t *n = s->next;
        if (s->data) kfree(s->data);
        kfree(s);
        s = n;
    }
    sock->rtx_head = sock->rtx_tail = NULL;
}

static void rtx_partial_advance(tcp_socket_t *sock, uint32_t ack, sack_range_t *blk, uint8_t n) {
    /* Trim SACK-acked bytes from head segment */
    while (sock->rtx_head) {
        tcp_segment_t *s = sock->rtx_head;
        uint32_t consume = s->len;
        if (s->flags & TCP_SYN) consume += 1;
        if (s->flags & TCP_FIN) consume += 1;
        if (seq_ge(ack, s->seq + consume)) {
            sock->rtx_head = s->next;
            if (sock->rtx_head == NULL) sock->rtx_tail = NULL;
            if (s->data) kfree(s->data);
            kfree(s);
            continue;
        }
        if (s->len == 0) break;
        /* Trim front by cumulative ack */
        if (seq_gt(ack, s->seq)) {
            uint32_t trim = ack - s->seq;
            if (trim >= s->len) break; /* shouldn't happen */
            if (s->data) {
                uint8_t *nd = (uint8_t *)kmalloc(s->len - trim);
                if (nd) {
                    memcpy(nd, s->data + trim, s->len - trim);
                    kfree(s->data);
                    s->data = nd;
                }
            }
            s->seq += trim;
            s->len -= trim;
        }
        /* Trim SACK blocks */
        int trimmed = 0;
        for (uint8_t i = 0; i < n; i++) {
            if (seq_ge(blk[i].left, s->seq) && seq_le(blk[i].right, s->seq + s->len)) {
                uint32_t l = blk[i].left - s->seq;
                uint32_t r = blk[i].right - s->seq;
                if (l >= s->len || r > s->len) continue;
                if (r >= s->len) {
                    s->len = l;
                } else {
                    uint8_t *nd = (uint8_t *)kmalloc(s->len - (r - l));
                    if (nd) {
                        if (l) memcpy(nd, s->data, l);
                        if (r < s->len) memcpy(nd + l, s->data + r, s->len - r);
                        kfree(s->data);
                        s->data = nd;
                    }
                    s->len -= (r - l);
                }
                trimmed = 1;
            }
        }
        if (!trimmed) break;
    }
}

static void rtx_retransmit_due(tcp_socket_t *sock) {
    uint32_t now = now_ms();
    if (sock->state == TCP_STATE_CLOSED ||
        sock->state == TCP_STATE_LISTEN ||
        sock->state == TCP_STATE_TIME_WAIT) return;
    if (sock->rtx_head == NULL) return;
    if (seq_ge(sock->snd_una, sock->snd_nxt) && sock->ooo_head == NULL) return;

    uint32_t diff = (int32_t)(now - sock->last_send_time);
    if (diff < sock->rto) return;

    /* Karn's algorithm: skip RTT measurement for retransmitted segments. */
    sock->rtt_pending = 0;

    /* Delegate RTO loss handling to CC algorithm. */
    tcp_cc_on_rto(sock);
    sock->dup_acks = 0;
    sock->fast_recovery = 0;
    sock->retrans_cnt++;
    if (sock->retrans_cnt > sock->max_retries) {
        sock->flags |= TCP_SOCK_FLAG_RST_RCVD;
        sock->state = TCP_STATE_CLOSED;
        tcp_wake_owner(sock);
        return;
    }
    rto_backoff(sock);
    tcp_retransmit_segment(sock, sock->rtx_head);
}

/* ------------------------------------------------------------------------- */
/*  Send path                                                                */
/* ------------------------------------------------------------------------- */

static uint32_t tcp_effective_mss(tcp_socket_t *sock) {
    uint32_t m = sock->mss_peer;
    if (m == 0) m = sock->mss_local;
    if (m == 0) m = MSS;
    return m;
}

static int tcp_send_segment(tcp_socket_t *sock, uint8_t flags, const void *data, uint32_t len) {
    uint8_t optbuf[40];
    int optlen = tcp_option_build(sock, optbuf, sizeof(optbuf), (flags & TCP_SYN) ? 1 : 0);
    if (optlen < 0) optlen = 0;
    /* data offset in 32-bit words */
    uint8_t data_offset_words = (uint8_t)((sizeof(tcp_header_t) + optlen + 3) / 4);

    uint32_t total = sizeof(tcp_header_t) + optlen + len;
    uint8_t *packet = (uint8_t *)kcalloc(1, total);
    if (!packet) return -1;

    tcp_header_t *hdr = (tcp_header_t *)packet;
    hdr->src_port = sock->local_port;
    hdr->dst_port = sock->remote_port;
    hdr->seq_num = sock->snd_nxt;
    hdr->ack_num = sock->rcv_nxt;
    hdr->data_offset_flags = (uint8_t)((data_offset_words << 4) | (flags & 0x3F));
    hdr->window_size = (uint16_t)((sock->rcv_wnd > 65535) ? 65535 : sock->rcv_wnd);
    hdr->checksum = 0;
    hdr->urgent_ptr = (flags & TCP_URG) ? sock->urg_ptr : 0;

    if (optlen > 0) memcpy(packet + sizeof(tcp_header_t), optbuf, optlen);
    if (data && len > 0) memcpy(packet + sizeof(tcp_header_t) + optlen, data, len);

    hdr->checksum = tcp_checksum(sock->local_ip, sock->remote_ip, IP_PROTO_TCP, packet, total);

    int result = ip_send(sock->iface, sock->remote_ip, IP_PROTO_TCP, packet, total);
    kfree(packet);

    if (result == 0) {
        sock->last_send_time = now_ms();
        if (len > 0 || (flags & (TCP_SYN | TCP_FIN))) {
            uint32_t consumed = len;
            if (flags & TCP_SYN) consumed += 1;
            if (flags & TCP_FIN) consumed += 1;
            sock->snd_nxt += consumed;
            if (sock->rtt_pending == 0 && (len > 0) && !(flags & TCP_SYN)) {
                sock->rtt_send_time = sock->last_send_time;
                sock->rtt_pending = 1;
            }
            /* Track on retransmission queue.  We store only the
             * payload length (not the SYN/FIN byte), so the retransmit
             * path can resend data exactly. */
            rtx_push(sock, sock->snd_nxt - consumed, len, flags,
                     (const uint8_t *)data);
        }
    }
    return result;
}

static void tcp_retransmit_segment(tcp_socket_t *sock, tcp_segment_t *seg) {
    if (!seg || !sock->iface) return;
    uint8_t flags = seg->flags;
    uint32_t seq  = seg->seq;
    uint8_t  optbuf[40];
    int      optlen = 0;
    if (flags & TCP_SYN) {
        optlen = tcp_option_build(sock, optbuf, sizeof(optbuf), 1);
    } else if (sock->ts_option) {
        optlen = tcp_option_build(sock, optbuf, sizeof(optbuf), 0);
    }
    if (optlen < 0) optlen = 0;
    uint8_t data_offset_words = (uint8_t)((sizeof(tcp_header_t) + optlen + 3) / 4);

    /* seg->len is the payload length (no SYN/FIN byte). */
    uint32_t payload_len = seg->len;
    if ((flags & (TCP_SYN | TCP_FIN)) && payload_len == 0) {
        /* SYN/FIN-only segment has no payload. */
        payload_len = 0;
    }
    uint32_t total = sizeof(tcp_header_t) + optlen + payload_len;
    uint8_t *packet = (uint8_t *)kcalloc(1, total);
    if (!packet) return;
    tcp_header_t *hdr = (tcp_header_t *)packet;
    hdr->src_port = sock->local_port;
    hdr->dst_port = sock->remote_port;
    hdr->seq_num = seq;
    hdr->ack_num = sock->rcv_nxt;
    hdr->data_offset_flags = (uint8_t)((data_offset_words << 4) | (flags & 0x3F));
    hdr->window_size = (uint16_t)((sock->rcv_wnd > 65535) ? 65535 : sock->rcv_wnd);
    hdr->checksum = 0;
    hdr->urgent_ptr = (flags & TCP_URG) ? sock->urg_ptr : 0;
    if (optlen) memcpy(packet + sizeof(tcp_header_t), optbuf, optlen);
    if (payload_len && seg->data)
        memcpy(packet + sizeof(tcp_header_t) + optlen, seg->data, payload_len);
    hdr->checksum = tcp_checksum(sock->local_ip, sock->remote_ip, IP_PROTO_TCP, packet, total);
    ip_send(sock->iface, sock->remote_ip, IP_PROTO_TCP, packet, total);
    kfree(packet);
    sock->last_send_time = now_ms();
}

static int tcp_send_buffered(tcp_socket_t *sock, const void *data, uint32_t len, uint8_t flags) {
    if (sock->send_buf_len + len > sock->send_buf_size) {
        /* Make room: grow if possible */
        uint32_t need = sock->send_buf_len + len;
        if (need > 256 * 1024U) return -1;
        uint32_t new_size = sock->send_buf_size;
        while (new_size < need) new_size *= 2;
        uint8_t *nb = (uint8_t *)kmalloc(new_size);
        if (!nb) return -1;
        if (sock->send_buf_len) {
            uint32_t first = sock->send_buf_size - sock->send_buf_head;
            if (first > sock->send_buf_len) first = sock->send_buf_len;
            memcpy(nb, sock->send_buf + sock->send_buf_head, first);
            if (sock->send_buf_len - first)
                memcpy(nb + first, sock->send_buf, sock->send_buf_len - first);
        }
        kfree(sock->send_buf);
        sock->send_buf = nb;
        sock->send_buf_size = new_size;
        sock->send_buf_head = 0;
        sock->send_buf_tail = sock->send_buf_len;
    }
    const uint8_t *src = (const uint8_t *)data;
    for (uint32_t i = 0; i < len; i++) {
        sock->send_buf[sock->send_buf_tail] = src[i];
        sock->send_buf_tail = (sock->send_buf_tail + 1) % sock->send_buf_size;
    }
    sock->send_buf_len += len;
    return (int)len;
}

static void tcp_push_pending(tcp_socket_t *sock) {
    if (sock->state != TCP_STATE_ESTABLISHED &&
        sock->state != TCP_STATE_CLOSE_WAIT) return;
    if (sock->snd_wnd == 0) {
        if (!(sock->flags & TCP_SOCK_FLAG_ZERO_PROBE)) {
            sock->flags |= TCP_SOCK_FLAG_ZERO_PROBE;
            sock->zero_probe_time = now_ms() + TCP_ZERO_WINDOW_PROBE_MS;
        }
        return;
    }
    sock->flags &= ~TCP_SOCK_FLAG_ZERO_PROBE;
    uint32_t mss = tcp_effective_mss(sock);
    uint32_t flight = sock->snd_nxt - sock->snd_una;
    uint32_t inflight_cap = (sock->snd_wnd < sock->cwnd) ? sock->snd_wnd : sock->cwnd;
    if (flight >= inflight_cap && sock->send_buf_len == 0) return;

    /* Nagle: if there's a small outstanding segment and no urgent need, defer. */
    int nagle_ok = (sock->send_buf_len >= mss) ||
                   (sock->snd_una == sock->snd_nxt) ||
                   (sock->flags & TCP_SOCK_FLAG_NODELAY) ||
                   (sock->flags & TCP_SOCK_FLAG_CLOSING);
    if (!nagle_ok) return;

    while (sock->send_buf_len > 0) {
        uint32_t to_send = sock->send_buf_len;
        if (to_send > mss) to_send = mss;
        if (flight + to_send > inflight_cap) {
            if (flight >= inflight_cap) break;
            to_send = inflight_cap - flight;
            if (to_send == 0) break;
        }
        uint8_t *out = (uint8_t *)kmalloc(to_send);
        if (!out) break;
        for (uint32_t i = 0; i < to_send; i++) {
            out[i] = sock->send_buf[sock->send_buf_head];
            sock->send_buf_head = (sock->send_buf_head + 1) % sock->send_buf_size;
        }
        sock->send_buf_len -= to_send;
        tcp_send_segment(sock, TCP_PSH | TCP_ACK, out, to_send);
        kfree(out);
        flight = sock->snd_nxt - sock->snd_una;
    }
}

/* ------------------------------------------------------------------------- */
/*  Receive path                                                             */
/* ------------------------------------------------------------------------- */

static void recv_buf_push(tcp_socket_t *sock, const uint8_t *data, uint32_t len) {
    if (len == 0) return;
    if (len > sock->recv_buf_size - sock->recv_buf_len) {
        /* Drop if not enough buffer space - in production we drain
         * and shrink window. */
        return;
    }
    for (uint32_t i = 0; i < len; i++) {
        sock->recv_buf[sock->recv_buf_tail] = data[i];
        sock->recv_buf_tail = (sock->recv_buf_tail + 1) % sock->recv_buf_size;
    }
    sock->recv_buf_len += len;
}

static void rtx_emit_sack(tcp_socket_t *sock, uint8_t *opt, uint32_t *optlen) {
    if (sock->sack_enabled == 0 || sock->sack_count == 0) { *optlen = 0; return; }
    uint8_t *p = opt;
    *p++ = TCP_OPT_NOP; *p++ = TCP_OPT_NOP;
    *p++ = TCP_OPT_SACK;
    uint8_t len = 2 + 8 * sock->sack_count;
    *p++ = len;
    for (uint8_t i = 0; i < sock->sack_count; i++) {
        uint32_t l = sock->sack_blocks[i].left;
        uint32_t r = sock->sack_blocks[i].right;
        *p++ = (uint8_t)(l >> 24); *p++ = (uint8_t)(l >> 16);
        *p++ = (uint8_t)(l >> 8);  *p++ = (uint8_t)(l);
        *p++ = (uint8_t)(r >> 24); *p++ = (uint8_t)(r >> 16);
        *p++ = (uint8_t)(r >> 8);  *p++ = (uint8_t)(r);
    }
    *optlen = (uint32_t)(p - opt);
}

static int tcp_send_ack(tcp_socket_t *sock) {
    uint8_t optbuf[40]; uint32_t optlen = 0;
    rtx_emit_sack(sock, optbuf, &optlen);
    uint8_t data_offset_words = (uint8_t)((sizeof(tcp_header_t) + optlen + 3) / 4);
    uint32_t total = sizeof(tcp_header_t) + optlen;
    uint8_t *packet = (uint8_t *)kcalloc(1, total);
    if (!packet) return -1;
    tcp_header_t *hdr = (tcp_header_t *)packet;
    hdr->src_port = sock->local_port;
    hdr->dst_port = sock->remote_port;
    hdr->seq_num = sock->snd_nxt;
    hdr->ack_num = sock->rcv_nxt;
    hdr->data_offset_flags = (uint8_t)((data_offset_words << 4) | TCP_ACK);
    hdr->window_size = (uint16_t)((sock->rcv_wnd > 65535) ? 65535 : sock->rcv_wnd);
    hdr->checksum = 0;
    hdr->urgent_ptr = 0;
    if (optlen) memcpy(packet + sizeof(tcp_header_t), optbuf, optlen);
    hdr->checksum = tcp_checksum(sock->local_ip, sock->remote_ip, IP_PROTO_TCP, packet, total);
    int r = ip_send(sock->iface, sock->remote_ip, IP_PROTO_TCP, packet, total);
    kfree(packet);
    if (r == 0) {
        sock->last_send_time = now_ms();
        sock->flags &= ~TCP_SOCK_FLAG_NEEDS_ACK;
        sock->last_ack_time = sock->last_send_time;
    }
    return r;
}

static void tcp_deliver_data(tcp_socket_t *sock) {
    /* Promote in-order OOO segments to the receive buffer */
    while (sock->ooo_head) {
        tcp_segment_t *s = sock->ooo_head;
        if (s->seq != sock->rcv_nxt) break;
        recv_buf_push(sock, s->data, s->len);
        sock->rcv_nxt += s->len;
        sock->ooo_head = s->next;
        if (s->data) kfree(s->data);
        kfree(s);
    }
    if (sock->recv_buf_len > 0 && sock->wait_recv) tcp_wake_owner(sock);
}

static void ooo_insert(tcp_socket_t *sock, uint32_t seq, const uint8_t *data, uint32_t len) {
    if (len == 0) return;
    tcp_segment_t *s = (tcp_segment_t *)kmalloc(sizeof(tcp_segment_t));
    if (!s) return;
    s->seq = seq;
    s->len = len;
    s->data = (uint8_t *)kmalloc(len);
    if (!s->data) { kfree(s); return; }
    memcpy(s->data, data, len);
    s->next = NULL;
    /* insert sorted */
    tcp_segment_t **pp = &sock->ooo_head;
    while (*pp && seq_gt((*pp)->seq, seq)) pp = &(*pp)->next;
    s->next = *pp;
    *pp = s;

    /* Coalesce with neighbor */
    if (s->next && seq + len == s->next->seq) {
        tcp_segment_t *n = s->next;
        uint8_t *nb = (uint8_t *)kmalloc(s->len + n->len);
        if (nb) {
            memcpy(nb, s->data, s->len);
            memcpy(nb + s->len, n->data, n->len);
            kfree(s->data);
            s->data = nb;
            s->len += n->len;
            s->next = n->next;
            kfree(n->data);
            kfree(n);
        }
    }
}

static void sack_rebuild(tcp_socket_t *sock) {
    /* Build sack blocks from OOO queue (max 4) */
    sock->sack_count = 0;
    for (tcp_segment_t *s = sock->ooo_head; s && sock->sack_count < 4; s = s->next) {
        sock->sack_blocks[sock->sack_count].left  = s->seq;
        sock->sack_blocks[sock->sack_count].right = s->seq + s->len;
        sock->sack_count++;
    }
}

static void tcp_process_ack(tcp_socket_t *sock, uint32_t ack, uint32_t wnd) {
    if (ack == sock->snd_una && wnd == sock->snd_wnd) {
        /* Duplicate ACK -- delegate to CC algorithm. */
        sock->dup_acks++;
        tcp_cc_on_dup_ack(sock);
        return;
    }
    if (seq_gt(ack, sock->snd_una)) {
        /* Exiting fast recovery (NewReno partial ACK handled in CC module). */
        if (sock->fast_recovery && sock->cc_algo == TCP_CC_RENO) {
            /* Reno: partial ACK exits fast recovery, deflate window */
            sock->cwnd = sock->ssthresh;
            sock->fast_recovery = 0;
        } else if (sock->fast_recovery) {
            /* NewReno/CUBIC: full ACK exits fast recovery */
            if (seq_ge(ack, sock->ssthresh)) {
                /* ssthresh was set to snd_nxt at loss time -- full recovery */
            }
            sock->fast_recovery = 0;
        }
        uint32_t acked = ack - sock->snd_una;
        sock->snd_una = ack;
        /* RTT measurement */
        if (sock->rtt_pending) {
            uint32_t m = now_ms() - sock->rtt_send_time;
            if (m > 0 && m < 60000) rtt_measure(sock, m);
            sock->rtt_pending = 0;
        }
        /* Delegate cwnd increase to CC algorithm. */
        tcp_cc_on_ack(sock, acked);
        sock->dup_acks = 0;
        sock->retrans_cnt = 0;
        if (sock->rto > sock->rto_min) sock->rto = sock->srtt + 4 * sock->rttvar;
        if (sock->rto < sock->rto_min) sock->rto = sock->rto_min;
    }
    if (seq_gt(wnd, 0) && sock->flags & TCP_SOCK_FLAG_ZERO_PROBE) {
        sock->flags &= ~TCP_SOCK_FLAG_ZERO_PROBE;
        sock->zero_probe_time = 0;
    }
    sock->snd_wnd = wnd;
    rtx_advance(sock, ack);
    if (sock->sack_enabled) sack_rebuild(sock);
    tcp_push_pending(sock);
    if (sock->wait_send) tcp_wake_owner(sock);
}

/* ------------------------------------------------------------------------- */
/*  State machine                                                            */
/* ------------------------------------------------------------------------- */

static void tcp_transition_to_established(tcp_socket_t *sock) {
    sock->state = TCP_STATE_ESTABLISHED;
    sock->flags |= TCP_SOCK_FLAG_CONNECTED;
    tcp_register_stats_established();
    if (sock->parent) {
        sock->parent->backlog_cur--;
        /* Wake the listener so the blocked accept() can return. */
        pcb_t *p = sock->parent->owner;
        if (p && sock->parent->wait_accept) {
            sched_unblock(sock->parent->wait_accept);
            sock->parent->wait_accept = NULL;
        }
        (void)p;
    }
    if (sock->wait_accept) tcp_wake_owner(sock);
    tcp_push_pending(sock);
}

void tcp_handle_state(tcp_socket_t *sock, uint8_t flags, const void *data, uint32_t len) {
    (void)flags; (void)data; (void)len;
}

static void tcp_send_rst(tcp_socket_t *sock, uint32_t seq) {
    uint8_t packet[sizeof(tcp_header_t)];
    memset(packet, 0, sizeof(packet));
    tcp_header_t *hdr = (tcp_header_t *)packet;
    hdr->src_port = sock->local_port;
    hdr->dst_port = sock->remote_port;
    hdr->seq_num = seq;
    hdr->ack_num = 0;
    hdr->data_offset_flags = (uint8_t)((5 << 4) | (TCP_RST | TCP_ACK));
    hdr->window_size = 0;
    hdr->checksum = tcp_checksum(sock->local_ip, sock->remote_ip, IP_PROTO_TCP, packet, sizeof(packet));
    ip_send(sock->iface, sock->remote_ip, IP_PROTO_TCP, packet, sizeof(packet));
}

static void tcp_wake_owner(tcp_socket_t *sock) {
    pcb_t *p = sock->owner;
    if (!p) return;
    if (sock->wait_recv)    { sched_unblock(sock->wait_recv);    sock->wait_recv    = NULL; }
    if (sock->wait_accept)  { sched_unblock(sock->wait_accept);  sock->wait_accept  = NULL; }
    if (sock->wait_send)    { sched_unblock(sock->wait_send);    sock->wait_send    = NULL; }
    if (sock->wait_close)   { sched_unblock(sock->wait_close);   sock->wait_close   = NULL; }
}

void tcp_set_owner(tcp_socket_t *sock, pcb_t *proc) {
    sock->owner = proc;
}

void tcp_wakeup(pcb_t *proc) {
    /* Wake every socket owned by proc */
    for (uint32_t i = 0; i < TCP_HASH_TABLE_SIZE; i++) {
        tcp_socket_t *s = tcp_hash[i];
        while (s) {
            tcp_socket_t *n = s->next;
            if (s->owner == proc) tcp_wake_owner(s);
            s = n;
        }
    }
}

void tcp_set_nodelay(tcp_socket_t *sock, int on) {
    if (!sock) return;
    if (on) sock->flags |= TCP_SOCK_FLAG_NODELAY;
    else    sock->flags &= ~TCP_SOCK_FLAG_NODELAY;
}

void tcp_set_keepalive(tcp_socket_t *sock, int on) {
    if (!sock) return;
    if (on) {
        sock->flags |= TCP_SOCK_FLAG_KEEPALIVE;
        sock->keepalive_time = now_ms() + TCP_KEEPALIVE_IDLE_MS;
    } else {
        sock->flags &= ~TCP_SOCK_FLAG_KEEPALIVE;
    }
}

void tcp_set_nonblock(tcp_socket_t *sock, int on) {
    if (!sock) return;
    if (on) sock->flags |= TCP_SOCK_FLAG_NONBLOCK;
    else    sock->flags &= ~TCP_SOCK_FLAG_NONBLOCK;
}

/* ------------------------------------------------------------------------- */
/*  Connect / listen / accept / close                                        */
/* ------------------------------------------------------------------------- */

static uint16_t alloc_ephemeral(void) {
    mutex_lock(&tcp_table_lock);
    for (uint32_t tries = 0; tries < 65536; tries++) {
        uint16_t p = next_ephemeral_port;
        next_ephemeral_port++;
        if (next_ephemeral_port < 49152) next_ephemeral_port = 49152;
        if (p == 0) continue;
        if (sockets[p] == NULL) {
            mutex_unlock(&tcp_table_lock);
            return p;
        }
    }
    mutex_unlock(&tcp_table_lock);
    return 0;
}

uint16_t tcp_ephemeral_alloc(void) {
    return alloc_ephemeral();
}

static void tcp_register(tcp_socket_t *s) {
    mutex_lock(&tcp_table_lock);
    if (s->local_port != 0) sockets[s->local_port] = s;
    hash_insert(s);
    /* Link into the global socket list. */
    s->next_all = all_sockets;
    all_sockets = s;
    mutex_unlock(&tcp_table_lock);
}

static void tcp_unregister(tcp_socket_t *s) {
    mutex_lock(&tcp_table_lock);
    if (s->local_port != 0 && s->local_port < 65536 && sockets[s->local_port] == s)
        sockets[s->local_port] = NULL;
    hash_remove(s);
    /* Remove from global list. */
    tcp_socket_t **pp = &all_sockets;
    while (*pp) {
        if (*pp == s) { *pp = s->next_all; break; }
        pp = &(*pp)->next_all;
    }
    s->next_all = NULL;
    mutex_unlock(&tcp_table_lock);
}

tcp_socket_t *tcp_connect(net_interface_t *iface, ipv4_addr_t dst, uint16_t dst_port, uint16_t src_port) {
    if (!iface) return NULL;
    tcp_socket_t *sock = tcp_socket_create();
    if (!sock) return NULL;
    sock->iface = iface;
    sock->local_ip = iface->ip;
    sock->remote_ip = dst;
    if (src_port == 0) src_port = alloc_ephemeral();
    if (src_port == 0) { sock_release(sock); return NULL; }
    sock->local_port = src_port;
    sock->remote_port = dst_port;
    sock->snd_una = (uint32_t)(now_ms() * 23) ^ (uint32_t)(uintptr_t)sock;
    sock->snd_nxt = sock->snd_una;
    sock->rcv_nxt = 0;
    sock->state = TCP_STATE_SYN_SENT;
    sock->keepalive_time = now_ms() + TCP_KEEPALIVE_IDLE_MS;
    tcp_register(sock);
    tcp_register_stats_active();
    tcp_register_stats_attempt();

    int r = tcp_send_segment(sock, TCP_SYN, NULL, 0);
    if (r < 0) { tcp_unregister(sock); sock_release(sock); return NULL; }

    /* Synchronous wait with timeout - non-blocking would use select. */
    uint32_t deadline = now_ms() + 5000;
    while (sock->state == TCP_STATE_SYN_SENT && (int32_t)(now_ms() - deadline) < 0) {
        sched_yield();
    }
    if (sock->state != TCP_STATE_ESTABLISHED) {
        tcp_unregister(sock);
        sock_release(sock);
        return NULL;
    }
    return sock;
}

tcp_socket_t *tcp_listen(uint16_t port) {
    tcp_socket_t *sock = tcp_socket_create();
    if (!sock) return NULL;
    sock->local_port = port;
    sock->state = TCP_STATE_LISTEN;
    sock->flags |= TCP_SOCK_FLAG_LISTEN;
    sock->backlog_max = TCP_SYN_BACKLOG_DEFAULT;
    tcp_register(sock);
    tcp_register_stats_passive();
    return sock;
}

int tcp_listen_with_backlog(tcp_socket_t *sock, int backlog) {
    if (!sock || sock->state != TCP_STATE_CLOSED) return -1;
    if (backlog < 1) backlog = 1;
    if (backlog > TCP_BACKLOG_MAX) backlog = TCP_BACKLOG_MAX;
    sock->local_port = 0; /* will be set by bind */
    sock->state = TCP_STATE_LISTEN;
    sock->flags |= TCP_SOCK_FLAG_LISTEN;
    sock->backlog_max = (uint8_t)backlog;
    return 0;
}

static tcp_socket_t *accept_from_listener(tcp_socket_t *listener) {
    tcp_socket_t *child = listener->accept_head;
    if (!child) return NULL;
    listener->accept_head = child->next;
    if (listener->accept_head == NULL) listener->accept_tail = NULL;
    child->next = NULL;
    listener->backlog_cur--;
    return child;
}

tcp_socket_t *tcp_accept_nonblock(tcp_socket_t *listener) {
    if (!listener || listener->state != TCP_STATE_LISTEN) return NULL;
    return accept_from_listener(listener);
}

tcp_socket_t *tcp_accept(tcp_socket_t *listener) {
    if (!listener || listener->state != TCP_STATE_LISTEN) return NULL;
    uint32_t deadline = now_ms() + 30000;
    while (!listener->accept_head) {
        if ((int32_t)(now_ms() - deadline) >= 0) return NULL;
        if (listener->flags & TCP_SOCK_FLAG_NONBLOCK) return NULL;
        listener->wait_accept = sched_get_current();
        sched_block_current(BLOCK_REASON_IO);
    }
    return accept_from_listener(listener);
}

int tcp_send_available(tcp_socket_t *sock) {
    if (!sock) return 0;
    if (sock->state != TCP_STATE_ESTABLISHED &&
        sock->state != TCP_STATE_CLOSE_WAIT) return 0;
    return (int)(sock->send_buf_size - sock->send_buf_len);
}

int tcp_recv_available(tcp_socket_t *sock) {
    if (!sock) return 0;
    return (int)sock->recv_buf_len;
}

int tcp_send(tcp_socket_t *sock, const void *data, uint32_t len) {
    if (!sock) return -1;
    if (sock->state != TCP_STATE_ESTABLISHED &&
        sock->state != TCP_STATE_CLOSE_WAIT) return -1;
    if (!data || len == 0) return 0;
    spinlock_lock(&sock->lock);
    int r = tcp_send_buffered(sock, data, len, TCP_PSH | TCP_ACK);
    if (r > 0) tcp_push_pending(sock);
    spinlock_unlock(&sock->lock);
    return r;
}

int tcp_peek(tcp_socket_t *sock, void *buf, uint32_t len) {
    if (!sock || !buf || len == 0) return 0;
    spinlock_lock(&sock->lock);
    uint32_t n = sock->recv_buf_len;
    if (n > len) n = len;
    for (uint32_t i = 0; i < n; i++) {
        ((uint8_t *)buf)[i] = sock->recv_buf[(sock->recv_buf_head + i) % sock->recv_buf_size];
    }
    spinlock_unlock(&sock->lock);
    return (int)n;
}

int tcp_recv(tcp_socket_t *sock, void *buf, uint32_t len) {
    if (!sock || !buf || len == 0) return 0;
    spinlock_lock(&sock->lock);
    while (sock->state != TCP_STATE_TIME_WAIT &&
           sock->state != TCP_STATE_CLOSED &&
           sock->recv_buf_len == 0) {
        if (sock->flags & TCP_SOCK_FLAG_NONBLOCK ||
            sock->state == TCP_STATE_CLOSE_WAIT) {
            spinlock_unlock(&sock->lock);
            return 0;
        }
        sock->wait_recv = sched_get_current();
        spinlock_unlock(&sock->lock);
        sched_block_current(BLOCK_REASON_IO);
        spinlock_lock(&sock->lock);
    }
    uint32_t n = sock->recv_buf_len;
    if (n > len) n = len;
    for (uint32_t i = 0; i < n; i++) {
        ((uint8_t *)buf)[i] = sock->recv_buf[sock->recv_buf_head];
        sock->recv_buf_head = (sock->recv_buf_head + 1) % sock->recv_buf_size;
    }
    sock->recv_buf_len -= n;
    /* Update receive window */
    sock->rcv_wnd = sock->recv_buf_size - sock->recv_buf_len;
    spinlock_unlock(&sock->lock);
    return (int)n;
}

void tcp_shutdown(tcp_socket_t *sock, int how) {
    if (!sock) return;
    spinlock_lock(&sock->lock);
    if (how == 0 || how == 2) {
        /* SHUT_WR: send FIN */
        if (sock->state == TCP_STATE_ESTABLISHED) {
            tcp_send_segment(sock, TCP_FIN | TCP_ACK, NULL, 0);
            sock->state = TCP_STATE_FIN_WAIT1;
        } else if (sock->state == TCP_STATE_CLOSE_WAIT) {
            tcp_send_segment(sock, TCP_FIN | TCP_ACK, NULL, 0);
            sock->state = TCP_STATE_LAST_ACK;
        }
    }
    if (how == 1 || how == 2) {
        /* SHUT_RD: drop recv buffer; peer will see RST when sending */
        sock->recv_buf_len = 0;
        sock->recv_buf_head = sock->recv_buf_tail = 0;
    }
    spinlock_unlock(&sock->lock);
}

int tcp_close(tcp_socket_t *sock) {
    if (!sock) return -1;
    spinlock_lock(&sock->lock);
    tcp_close_internal(sock);
    spinlock_unlock(&sock->lock);
    tcp_register_stats_close();
    return 0;
}

static void tcp_close_internal(tcp_socket_t *sock) {
    if (sock->state == TCP_STATE_CLOSED) return;
    if (sock->state == TCP_STATE_LISTEN) {
        sock->state = TCP_STATE_CLOSED;
        tcp_unregister(sock);
        sock_release(sock);
        return;
    }
    if (sock->state == TCP_STATE_SYN_SENT) {
        sock->state = TCP_STATE_CLOSED;
        tcp_unregister(sock);
        sock_release(sock);
        return;
    }
    if (sock->state == TCP_STATE_ESTABLISHED) {
        tcp_send_segment(sock, TCP_FIN | TCP_ACK, NULL, 0);
        sock->state = TCP_STATE_FIN_WAIT1;
        sock->flags |= TCP_SOCK_FLAG_CLOSING;
        sock->time_wait_expire = now_ms() + TCP_TIME_WAIT_MS;
        return;
    }
    if (sock->state == TCP_STATE_CLOSE_WAIT) {
        tcp_send_segment(sock, TCP_FIN | TCP_ACK, NULL, 0);
        sock->state = TCP_STATE_LAST_ACK;
        sock->flags |= TCP_SOCK_FLAG_CLOSING;
        return;
    }
    if (sock->state == TCP_STATE_TIME_WAIT) {
        sock->time_wait_expire = now_ms() + TCP_TIME_WAIT_MS;
        return;
    }
    /* Other states: go to CLOSED */
    sock->state = TCP_STATE_CLOSED;
    tcp_unregister(sock);
    sock_release(sock);
}

/* ------------------------------------------------------------------------- */
/*  Receive dispatch                                                         */
/* ------------------------------------------------------------------------- */

static int parse_options_field(const uint8_t *opt, uint32_t len, tcp_socket_t *sock) {
    return tcp_option_parse(opt, len, sock);
}

static int address_match(const tcp_socket_t *a, const tcp_socket_t *b) {
    if (a->local_port != b->local_port) return 0;
    if (a->remote_port != b->remote_port) return 0;
    if (a->local_ip.addr  != b->local_ip.addr)  return 0;
    if (a->remote_ip.addr != b->remote_ip.addr) return 0;
    return 1;
}

static tcp_socket_t *find_listener(uint16_t dst_port, ipv4_addr_t dst_ip) {
    for (uint32_t i = 0; i < TCP_HASH_TABLE_SIZE; i++) {
        for (tcp_socket_t *s = tcp_hash[i]; s; s = s->next) {
            if (s->state == TCP_STATE_LISTEN && s->local_port == dst_port &&
                (s->local_ip.addr == 0 || s->local_ip.addr == dst_ip.addr)) {
                return s;
            }
        }
    }
    return NULL;
}

void tcp_receive(net_buffer_t *buf) {
    if (!buf || buf->len < (int)sizeof(tcp_header_t)) return;
    tcp_header_t *hdr = (tcp_header_t *)(buf->data + buf->offset);
    uint8_t flags = hdr->data_offset_flags & 0x3F;
    uint8_t data_offset_words = (hdr->data_offset_flags >> 4) & 0x0F;
    uint32_t hdr_len = (uint32_t)data_offset_words * 4U;
    if (hdr_len < sizeof(tcp_header_t) || hdr_len > buf->len) return;

    /* checksum verification */
    uint16_t recv_cksum = hdr->checksum;
    hdr->checksum = 0;
    uint16_t calc = tcp_checksum(buf->iface->ip, ((ip_header_t *)(buf->data + buf->offset - 20))->src_ip,
                                  IP_PROTO_TCP, hdr, buf->len);
    hdr->checksum = recv_cksum;
    if (recv_cksum != calc) return;

    ip_header_t *ip_hdr = (ip_header_t *)(buf->data + buf->offset - 20);
    uint16_t dst_port = hdr->dst_port;
    uint16_t src_port = hdr->src_port;
    uint8_t *payload = (uint8_t *)hdr + hdr_len;
    uint32_t payload_len = buf->len - hdr_len;

    /* Parse options */
    if (hdr_len > sizeof(tcp_header_t)) {
        /* options will be parsed once socket is known */
    }

    /* Look up socket by 4-tuple */
    tcp_socket_t key;
    key.local_port = dst_port;
    key.remote_port = src_port;
    key.local_ip = buf->iface->ip;
    key.remote_ip = ip_hdr->src_ip;
    tcp_socket_t *sock = NULL;
    uint32_t h = hash_func(dst_port, src_port, buf->iface->ip.addr, ip_hdr->src_ip.addr);
    for (tcp_socket_t *s = tcp_hash[h]; s; s = s->next) {
        if (address_match(s, &key)) { sock = s; break; }
    }

    /* RST handling */
    if (flags & TCP_RST) {
        if (sock) {
            sock->flags |= TCP_SOCK_FLAG_RST_RCVD;
            sock->state = TCP_STATE_CLOSED;
            rtx_clear(sock);
            tcp_wake_owner(sock);
        }
        return;
    }

    /* SYN to LISTEN: create child */
    if (!sock) {
        if (flags & TCP_SYN) {
            tcp_socket_t *listener = find_listener(dst_port, buf->iface->ip);
            if (!listener) {
                /* Send RST */
                tcp_socket_t r;
                r.local_ip = buf->iface->ip;
                r.remote_ip = ip_hdr->src_ip;
                r.local_port = dst_port;
                r.remote_port = src_port;
                r.iface = buf->iface;
                tcp_send_rst(&r, hdr->ack_num);
                return;
            }
            if (listener->backlog_cur >= listener->backlog_max) {
                /* Drop */
                return;
            }
            sock = tcp_socket_create();
            if (!sock) return;
            sock->iface = buf->iface;
            sock->local_ip = listener->local_ip.addr ? listener->local_ip : buf->iface->ip;
            sock->remote_ip = ip_hdr->src_ip;
            sock->local_port = dst_port;
            sock->remote_port = src_port;
            sock->snd_una = (uint32_t)(now_ms() * 31) ^ (uint32_t)(uintptr_t)sock;
            sock->snd_nxt = sock->snd_una;
            sock->rcv_nxt = hdr->seq_num + 1;
            sock->snd_wnd = hdr->window_size;
            sock->snd_wl1 = hdr->seq_num;
            sock->snd_wl2 = hdr->ack_num;
            sock->state = TCP_STATE_SYN_RECEIVED;
            sock->flags |= TCP_SOCK_FLAG_PASSIVE;
            sock->parent = listener;
            sock->backlog_max = 1;
            sock->keepalive_time = now_ms() + TCP_KEEPALIVE_IDLE_MS;
            if (hdr_len > sizeof(tcp_header_t))
                parse_options_field(payload - hdr_len + sizeof(tcp_header_t), hdr_len - sizeof(tcp_header_t), sock);
            listener->backlog_cur++;
            sock->next = listener->accept_head;
            listener->accept_head = sock;
            if (listener->accept_tail == NULL) listener->accept_tail = sock;
            tcp_register(sock);
            tcp_register_stats_passive();
            tcp_send_segment(sock, TCP_SYN | TCP_ACK, NULL, 0);
        }
        return;
    }

    spinlock_lock(&sock->lock);

    if (hdr_len > sizeof(tcp_header_t))
        parse_options_field(payload - hdr_len + sizeof(tcp_header_t), hdr_len - sizeof(tcp_header_t), sock);

    /* Per-state handling */
    switch (sock->state) {
    case TCP_STATE_SYN_SENT:
        if ((flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
            sock->rcv_nxt = hdr->seq_num + 1;
            sock->snd_una = hdr->ack_num;
            sock->snd_wnd = hdr->window_size;
            sock->snd_wl1 = hdr->seq_num;
            sock->snd_wl2 = hdr->ack_num;
            rtx_advance(sock, sock->snd_una);
            tcp_transition_to_established(sock);
        } else if (flags & TCP_SYN) {
            /* Simultaneous open */
            sock->rcv_nxt = hdr->seq_num + 1;
            sock->snd_una = hdr->ack_num;
            sock->state = TCP_STATE_SYN_RECEIVED;
            tcp_send_segment(sock, TCP_SYN | TCP_ACK, NULL, 0);
        }
        tcp_wake_owner(sock);
        break;

    case TCP_STATE_SYN_RECEIVED:
        if (flags & TCP_ACK) {
            if (seq_ge(hdr->ack_num, sock->snd_una) && seq_le(hdr->ack_num, sock->snd_nxt)) {
                sock->snd_una = hdr->ack_num;
                sock->snd_wnd = hdr->window_size;
                if (sock->wscale_peer)
                    sock->snd_wnd = sock->snd_wnd << sock->wscale_peer;
                sock->snd_wl1 = hdr->seq_num;
                sock->snd_wl2 = hdr->ack_num;
                sock->rcv_nxt = hdr->seq_num + 1;
                rtx_advance(sock, sock->snd_una);
                tcp_send_ack(sock);
                tcp_transition_to_established(sock);
            }
        }
        break;

    case TCP_STATE_ESTABLISHED:
    case TCP_STATE_FIN_WAIT1:
    case TCP_STATE_FIN_WAIT2:
    case TCP_STATE_CLOSE_WAIT:
    case TCP_STATE_CLOSING:
    case TCP_STATE_LAST_ACK:
    case TCP_STATE_TIME_WAIT:
        if (flags & TCP_URG) {
            sock->urg_ptr = hdr->urgent_ptr;
            sock->flags |= TCP_SOCK_FLAG_URG_PRESENT;
        }
        if (flags & TCP_ACK) {
            uint32_t wnd = (uint32_t)hdr->window_size;
            if (sock->wscale_peer)
                wnd = wnd << sock->wscale_peer;
            tcp_process_ack(sock, hdr->ack_num, wnd);
        }
        if (payload_len > 0) {
            if (hdr->seq_num == sock->rcv_nxt) {
                recv_buf_push(sock, payload, payload_len);
                sock->rcv_nxt += payload_len;
                tcp_deliver_data(sock);
                sock->flags |= TCP_SOCK_FLAG_NEEDS_ACK;
            } else if (seq_gt(hdr->seq_num, sock->rcv_nxt)) {
                ooo_insert(sock, hdr->seq_num, payload, payload_len);
                sack_rebuild(sock);
                sock->flags |= TCP_SOCK_FLAG_NEEDS_ACK;
            } else {
                /* overlap with already received data */
                uint32_t trim = sock->rcv_nxt - hdr->seq_num;
                if (trim < payload_len) {
                    recv_buf_push(sock, payload + trim, payload_len - trim);
                    sock->rcv_nxt += payload_len - trim;
                    tcp_deliver_data(sock);
                }
                sock->flags |= TCP_SOCK_FLAG_NEEDS_ACK;
            }
        }
        /* Update send window via latest segment */
        if (flags & TCP_ACK) {
            if (seq_gt(hdr->seq_num, sock->snd_wl1) ||
                (hdr->seq_num == sock->snd_wl1 && seq_ge(hdr->ack_num, sock->snd_wl2))) {
                uint32_t wnd = (uint32_t)hdr->window_size;
                if (sock->wscale_peer) wnd = wnd << sock->wscale_peer;
                sock->snd_wnd = wnd;
                sock->snd_wl1 = hdr->seq_num;
                sock->snd_wl2 = hdr->ack_num;
            }
        }
        /* FIN handling */
        if (flags & TCP_FIN) {
            if (sock->state == TCP_STATE_ESTABLISHED ||
                sock->state == TCP_STATE_SYN_RECEIVED) {
                if (hdr->seq_num + payload_len == sock->rcv_nxt) {
                    sock->rcv_nxt++;
                    sock->state = TCP_STATE_CLOSE_WAIT;
                    sock->flags |= TCP_SOCK_FLAG_NEEDS_ACK;
                }
            } else if (sock->state == TCP_STATE_FIN_WAIT1) {
                if (flags & TCP_ACK) {
                    sock->rcv_nxt++;
                    sock->state = TCP_STATE_TIME_WAIT;
                    sock->time_wait_expire = now_ms() + TCP_TIME_WAIT_MS;
                } else {
                    sock->rcv_nxt++;
                    sock->state = TCP_STATE_CLOSING;
                }
                sock->flags |= TCP_SOCK_FLAG_NEEDS_ACK;
            } else if (sock->state == TCP_STATE_FIN_WAIT2) {
                sock->rcv_nxt++;
                sock->state = TCP_STATE_TIME_WAIT;
                sock->time_wait_expire = now_ms() + TCP_TIME_WAIT_MS;
                sock->flags |= TCP_SOCK_FLAG_NEEDS_ACK;
            } else if (sock->state == TCP_STATE_TIME_WAIT) {
                sock->time_wait_expire = now_ms() + TCP_TIME_WAIT_MS;
                sock->flags |= TCP_SOCK_FLAG_NEEDS_ACK;
            }
            sock->flags |= TCP_SOCK_FLAG_NEEDS_ACK;
        }
        if (sock->flags & TCP_SOCK_FLAG_NEEDS_ACK) {
            if ((int32_t)(now_ms() - sock->last_ack_time) >= (int32_t)TCP_DELACK_MS) {
                tcp_send_ack(sock);
            }
        }
        tcp_wake_owner(sock);
        break;

    default:
        break;
    }

    spinlock_unlock(&sock->lock);
}

/* ------------------------------------------------------------------------- */
/*  Periodic tick                                                            */
/* ------------------------------------------------------------------------- */

void tcp_tick(uint32_t now_ms_val) {
    (void)now_ms_val;
    tcp_now_ms = now_ms();
    for (uint32_t i = 0; i < TCP_HASH_TABLE_SIZE; i++) {
        tcp_socket_t *s = tcp_hash[i];
        while (s) {
            tcp_socket_t *n = s->next;
            spinlock_lock(&s->lock);
            /* Retransmission */
            rtx_retransmit_due(s);
            /* TIME_WAIT expiration */
            if (s->state == TCP_STATE_TIME_WAIT &&
                s->time_wait_expire != 0 &&
                (int32_t)(now_ms() - s->time_wait_expire) >= 0) {
                s->state = TCP_STATE_CLOSED;
                tcp_unregister(s);
                spinlock_unlock(&s->lock);
                sock_release(s);
                s = NULL;
                continue;
            }
            /* Keep-alive */
            if ((s->flags & TCP_SOCK_FLAG_KEEPALIVE) &&
                s->state == TCP_STATE_ESTABLISHED &&
                s->keepalive_time != 0 &&
                (int32_t)(now_ms() - s->keepalive_time) >= 0) {
                if (s->keepalive_probes >= TCP_KEEPALIVE_PROBES) {
                    s->flags |= TCP_SOCK_FLAG_RST_RCVD;
                    s->state = TCP_STATE_CLOSED;
                    tcp_wake_owner(s);
                } else {
                    tcp_send_segment(s, TCP_ACK, NULL, 0);
                    s->keepalive_probes++;
                    s->keepalive_time = now_ms() + TCP_KEEPALIVE_INTVL_MS;
                }
            }
            /* Zero window probe */
            if ((s->flags & TCP_SOCK_FLAG_ZERO_PROBE) &&
                s->zero_probe_time != 0 &&
                (int32_t)(now_ms() - s->zero_probe_time) >= 0 &&
                s->state == TCP_STATE_ESTABLISHED) {
                tcp_send_segment(s, TCP_PSH | TCP_ACK, NULL, 0);
                s->zero_probe_time = now_ms() + TCP_ZERO_WINDOW_PROBE_MS;
            }
            /* RTT measurement on pending sample */
            if (s->rtt_pending) {
                uint32_t elapsed = now_ms() - s->rtt_send_time;
                if (elapsed > s->rto) {
                    s->rtt_pending = 0;
                }
            }
            /* Delayed ACK */
            if ((s->flags & TCP_SOCK_FLAG_NEEDS_ACK) &&
                s->state == TCP_STATE_ESTABLISHED &&
                (int32_t)(now_ms() - s->last_ack_time) >= (int32_t)TCP_DELACK_MS) {
                tcp_send_ack(s);
            }
            spinlock_unlock(&s->lock);
            s = n;
        }
    }
}

/* ------------------------------------------------------------------------- */
/*  Backward compatible retransmit check                                     */
/* ------------------------------------------------------------------------- */

void tcp_retransmit_check(tcp_socket_t *sock) {
    if (!sock) return;
    rtx_retransmit_due(sock);
}

/* ------------------------------------------------------------------------- */
/*  ICMP error handler                                                       */
/*                                                                           */
/*  Called from icmp_receive() when an ICMP Destination Unreachable or       */
/*  Time Exceeded message is received.  Walks the active connection list     */
/*  and marks any matching socket as "soft error" so that the next syscall   */
/*  (send/recv) returns the appropriate error code.  This is the standard    */
/*  BSD SO_ERROR / ICMP_FILTER behaviour.                                    */
/* ------------------------------------------------------------------------- */

void tcp_handle_icmp_error(ipv4_addr_t src, uint8_t type, uint8_t code) {
    (void)src;
    (void)code;
    if (type != ICMP_TYPE_DEST_UNREACH) return;
    uint32_t count = 0;
    const tcp_socket_t *s = tcp_get_sockets(&count);
    while (s) {
        if (s->state == TCP_STATE_ESTABLISHED) {
            /* Mark the socket as errored; the next operation will
             * translate this into an EPIPE/ECONNRESET. */
            extern void tcp_mark_error(tcp_socket_t *sock);
            tcp_mark_error((tcp_socket_t *)s);
        }
        s = s->next_all;
    }
}

void tcp_mark_error(tcp_socket_t *sock) {
    if (!sock) return;
    sock->flags |= TCP_SOCK_FLAG_RST_RCVD;
}

/* ========================================================================= */
/*  Congestion Control Module (Reno / NewReno / CUBIC)                       */
/* ========================================================================= */

/* ---- Helper: cubic root via integer approximation ---- */
static uint32_t cubic_root(uint64_t x) {
    /* Binary search for integer cube root. */
    uint32_t lo = 0, hi = 2097151;  /* ~cuberoot(UINT32_MAX) */
    while (lo < hi) {
        uint32_t mid = (lo + hi + 1) / 2;
        if ((uint64_t)mid * mid * mid <= x) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

/* ---- Helper: integer cubic function ---- */
/* cwnd = C * (t - K)^3 + W_max, where K = cubic_root(W_max * beta / C) */
/* We work in integer space: time in ms, cwnd in bytes. */
static uint32_t cubic_update(tcp_socket_t *sock, uint32_t now_ms) {
    tcp_cubic_state_t *cu = &sock->cubic;
    int32_t t = (int32_t)(now_ms - cu->origin_point);
    if (t <= 0) t = 1;   /* avoid negative time */

    int32_t dt = t - (int32_t)cu->K;
    int64_t dt_cubed;

    /* Clamp to avoid absurd values during extreme conditions. */
    if (dt < -20000) dt = -20000;
    if (dt >  20000) dt =  20000;

    dt_cubed = (int64_t)dt * dt * dt;

    /* CUBIC cwnd = C * (t - K)^3 + W_max */
    /* Scale: C is 0.4 → multiply by 4 then divide by 10 */
    int64_t cubic_target;
    if (dt >= 0) {
        cubic_target = (dt_cubed * 4) / 10000 + cu->W_max;
    } else {
        cubic_target = cu->W_max - ((-dt_cubed) * 4) / 10000;
    }

    if (cubic_target < (int64_t)(2 * tcp_effective_mss(sock)))
        cubic_target = 2 * tcp_effective_mss(sock);
    if (cubic_target > 0xFFFFFFFFULL)
        cubic_target = 0xFFFFFFFFU;

    /* TCP-friendly CUBIC: track standard TCP cwnd for fallback. */
    if (cu->tcp_friendliness) {
        uint32_t mss = tcp_effective_mss(sock);
        /* Standard TCP AIMD: cwnd = cwnd + MSS*MSS/cwnd */
        if (cu->tcp_cwnd < cu->W_max) {
            /* In concave region, TCP may grow faster. */
            cu->cwnd_cnt += mss;
            while (cu->cwnd_cnt >= cu->tcp_cwnd) {
                cu->cwnd_cnt -= cu->tcp_cwnd;
                cu->tcp_cwnd += mss;
            }
        } else {
            /* In convex region, TCP grows slower (not tracked). */
        }
        if (cu->tcp_cwnd < 2 * mss) cu->tcp_cwnd = 2 * mss;

        if (cu->tcp_cwnd > (uint32_t)cubic_target) {
            cc_stats.cubic_tcp_friendly_wins++;
            return cu->tcp_cwnd;
        }
    }

    return (uint32_t)cubic_target;
}

/* ---- CC set / get / name ---- */

void tcp_cc_set_algo(tcp_socket_t *sock, uint8_t algo) {
    if (!sock) return;
    if (algo > TCP_CC_CUBIC) return;
    sock->cc_algo = algo;
    if (algo == TCP_CC_CUBIC) {
        tcp_cc_init_cubic(sock);
    }
}

uint8_t tcp_cc_get_algo(tcp_socket_t *sock) {
    return sock ? sock->cc_algo : TCP_CC_NEWRENO;
}

const char *tcp_cc_get_name(uint8_t algo) {
    switch (algo) {
    case TCP_CC_RENO:    return "reno";
    case TCP_CC_NEWRENO: return "newreno";
    case TCP_CC_CUBIC:   return "cubic";
    default:             return "unknown";
    }
}

/* ---- CUBIC initialization (RFC 8312) ---- */

int tcp_cc_init_cubic(tcp_socket_t *sock) {
    if (!sock) return -1;
    tcp_cubic_state_t *cu = &sock->cubic;
    memset(cu, 0, sizeof(*cu));
    cu->tcp_friendliness = 1;
    cu->fast_convergence  = 1;
    cu->beta_cubic        = 0.7f;
    cu->C                 = 0.4f;
    cu->W_max             = 0;
    cu->W_last_max        = 0;
    cu->epoch_start       = now_ms();
    cu->origin_point      = now_ms();
    cu->dMin              = 100;  /* reasonable default */
    cu->cwnd_cnt          = 0;
    cu->K                 = 0;
    cu->last_cwnd         = 0;
    cu->tcp_cwnd          = sock->cwnd;
    return 0;
}

/* ---- Congestion window increase on ACK ---- */

void tcp_cc_on_ack(tcp_socket_t *sock, uint32_t acked_bytes) {
    if (!sock || acked_bytes == 0) return;
    uint32_t mss = tcp_effective_mss(sock);

    switch (sock->cc_algo) {
    case TCP_CC_CUBIC: {
        /* CUBIC: growth governed by cubic function, not AIMD. */
        tcp_cubic_state_t *cu = &sock->cubic;
        uint32_t target = cubic_update(sock, now_ms());
        if (target > (uint32_t)0x7FFFFFFFU) target = 0x7FFFFFFFU;

        /* In slow start, use standard approach */
        if (sock->fast_recovery && sock->cwnd < sock->ssthresh) {
            /* CUBIC uses HyStart slow start */
            sock->cwnd += acked_bytes;
            if (sock->cwnd > target && sock->cwnd > sock->ssthresh)
                sock->cwnd = target;
        } else if (sock->fast_recovery) {
            /* In fast recovery, CUBIC also inflates to target */
            if (target > sock->cwnd)
                sock->cwnd = target;
        } else {
            /* Normal CUBIC operation */
            sock->cwnd = target;
        }
        /* Maintain ssthresh from slow start transition. */
        if (!sock->fast_recovery && sock->cwnd >= sock->ssthresh) {
            cc_stats.reno_slow_starts++;
        }
        cu->last_cwnd = sock->cwnd;
        cu->cwnd_cnt = 0;
        break;
    }
    case TCP_CC_RENO:
    case TCP_CC_NEWRENO:
    default: {
        /* Standard AIMD: slow start or congestion avoidance. */
        if (sock->cwnd < sock->ssthresh) {
            /* Slow start: cwnd += min(acked, MSS) */
            sock->cwnd += acked_bytes;
            cc_stats.reno_slow_starts++;
        } else {
            /* Congestion avoidance: increase by 1 MSS per RTT */
            sock->partial_bytes_acked += acked_bytes;
            while (sock->partial_bytes_acked >= sock->cwnd) {
                sock->partial_bytes_acked -= sock->cwnd;
                sock->cwnd += mss;
            }
        }
        break;
    }
    }
}

/* ---- Duplicate ACK handling ---- */

void tcp_cc_on_dup_ack(tcp_socket_t *sock) {
    if (!sock) return;
    uint32_t mss = tcp_effective_mss(sock);

    switch (sock->cc_algo) {
    case TCP_CC_CUBIC:
        /* CUBIC fast retransmit on 3 dup acks. */
        if (sock->dup_acks == 3 && sock->rtx_head && !sock->fast_recovery) {
            tcp_cubic_state_t *cu = &sock->cubic;
            /* Fast convergence heuristic (RFC 8312 §4.6) */
            if (cu->W_last_max > 0 && cu->W_max < cu->W_last_max) {
                cu->W_last_max = cu->W_max;
                cu->W_max = (uint32_t)((float)cu->W_max * (1.0f + cu->beta_cubic) / 2.0f);
                cc_stats.cubic_fast_convergences++;
            } else {
                cu->W_last_max = cu->W_max;
                cu->W_max = sock->cwnd;
            }
            /* Multiplicative decrease */
            sock->ssthresh = (uint32_t)((float)sock->cwnd * cu->beta_cubic);
            if (sock->ssthresh < 2 * mss) sock->ssthresh = 2 * mss;

            /* Set K = cubic_root(W_max * (1 - beta) / C) */
            uint32_t reduction = cu->W_max - sock->ssthresh;
            if (reduction > 0 && cu->C > 0.0f) {
                cu->K = cubic_root((uint64_t)reduction * 10000 / 4);
            } else {
                cu->K = 0;
            }
            cu->origin_point = now_ms();
            cu->tcp_cwnd = sock->cwnd;
            sock->cwnd = sock->ssthresh + 3 * mss;
            sock->fast_recovery = 1;
            cc_stats.reno_fast_retrans++;
            tcp_retransmit_segment(sock, sock->rtx_head);
        } else if (sock->fast_recovery && sock->dup_acks > 3) {
            /* Inflate cwnd during fast recovery. */
            sock->cwnd += mss;
            tcp_push_pending(sock);
        }
        break;

    case TCP_CC_NEWRENO:
        /* NewReno: same fast retransmit trigger as Reno, but ssthresh
         * is set to snd_nxt at the time of retransmit (for partial ACK
         * detection via recovery_point). */
        if (sock->dup_acks == 3 && sock->rtx_head && !sock->fast_recovery) {
            uint32_t old_cwnd = sock->cwnd;
            sock->ssthresh = (old_cwnd * 3) / 4;
            if (sock->ssthresh < 2 * mss) sock->ssthresh = 2 * mss;
            /* NewReno marks recovery point = snd_nxt */
            uint32_t recovery_point = sock->snd_nxt;
            sock->cwnd = sock->ssthresh + 3 * mss;
            sock->fast_recovery = 1;
            sock->ssthresh = recovery_point;  /* store recovery point in ssthresh temporarily */
            /* Restore proper ssthresh after retransmit */
            uint32_t proper_ss = (old_cwnd * 3) / 4;
            if (proper_ss < 2 * mss) proper_ss = 2 * mss;
            sock->ssthresh = proper_ss;
            cc_stats.reno_fast_retrans++;
            cc_stats.newreno_recoveries++;
            tcp_retransmit_segment(sock, sock->rtx_head);
        } else if (sock->fast_recovery && sock->dup_acks > 3) {
            sock->cwnd += mss;
            cc_stats.newreno_partial_acks++;
            tcp_push_pending(sock);
        }
        break;

    case TCP_CC_RENO:
    default:
        /* Reno: standard fast retransmit / fast recovery. */
        if (sock->dup_acks == 3 && sock->rtx_head && !sock->fast_recovery) {
            sock->ssthresh = (sock->cwnd * 3) / 4;
            if (sock->ssthresh < 2 * mss) sock->ssthresh = 2 * mss;
            sock->cwnd = sock->ssthresh + 3 * mss;
            sock->fast_recovery = 1;
            cc_stats.reno_fast_retrans++;
            tcp_retransmit_segment(sock, sock->rtx_head);
        } else if (sock->fast_recovery && sock->dup_acks > 3) {
            sock->cwnd += mss;
            tcp_push_pending(sock);
        }
        break;
    }
}

/* ---- Loss detection (RTO timeout) ---- */

void tcp_cc_on_loss(tcp_socket_t *sock) {
    if (!sock) return;
    uint32_t mss = tcp_effective_mss(sock);

    switch (sock->cc_algo) {
    case TCP_CC_CUBIC: {
        tcp_cubic_state_t *cu = &sock->cubic;
        /* CUBIC loss reaction */
        cu->W_last_max = cu->W_max;
        cu->W_max = sock->cwnd;
        if (cu->fast_convergence && cu->W_max < cu->W_last_max) {
            cu->W_max = (uint32_t)((float)cu->W_max * (1.0f + cu->beta_cubic) / 2.0f);
        }
        sock->ssthresh = (uint32_t)((float)sock->cwnd * cu->beta_cubic);
        if (sock->ssthresh < 2 * mss) sock->ssthresh = 2 * mss;
        sock->cwnd = mss;
        /* Reset epoch */
        cu->origin_point = now_ms();
        if (sock->ssthresh < cu->W_max) {
            uint32_t diff = cu->W_max - sock->ssthresh;
            cu->K = cubic_root((uint64_t)diff * 10000 / 4);
        } else {
            cu->K = 0;
        }
        cu->tcp_cwnd = sock->cwnd;
        cu->epoch_start = now_ms();
        cc_stats.cubic_epochs++;
        break;
    }
    case TCP_CC_RENO:
    case TCP_CC_NEWRENO:
    default:
        sock->ssthresh = (sock->cwnd * 3) / 4;
        if (sock->ssthresh < 2 * mss) sock->ssthresh = 2 * mss;
        sock->cwnd = mss;
        break;
    }
}

void tcp_cc_on_rto(tcp_socket_t *sock) {
    /* RTO: same as loss for all algorithms. */
    tcp_cc_on_loss(sock);
}

const tcp_cc_stats_t *tcp_cc_get_stats(void) {
    return &cc_stats;
}
