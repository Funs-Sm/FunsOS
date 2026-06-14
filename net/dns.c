#include "dns.h"
#include "udp.h"
#include "kheap.h"
#include "string.h"
#include "timer.h"
#include "sync.h"
#include "stddef.h"

static dns_cache_entry_t cache[DNS_CACHE_SIZE];
static dns_pending_t     pending[DNS_MAX_PENDING];
static ipv4_addr_t       server;
static uint16_t          next_token = 0x1234;
static mutex_t           dns_lock;
static uint8_t           server_set;

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static uint32_t now_ms(void) { return timer_get_ticks() * 10U; }

static uint16_t be16(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | p[1];
}

static void wr16(uint8_t *p, uint16_t v) {
    p[0] = (v >> 8) & 0xFF; p[1] = v & 0xFF;
}

/* Encode a dotted name into DNS label format (length-prefixed). */
static int encode_name(const char *name, uint8_t *out, uint32_t cap) {
    uint32_t pos = 0;
    const char *p = name;
    while (*p) {
        const char *dot = strchr(p, '.');
        uint32_t len = dot ? (uint32_t)(dot - p) : (uint32_t)strlen(p);
        if (len == 0 || len > 63) return -1;
        if (pos + 1 + len + 1 > cap) return -1;
        out[pos++] = (uint8_t)len;
        for (uint32_t i = 0; i < len; i++) out[pos++] = (uint8_t)p[i];
        if (!dot) break;
        p = dot + 1;
    }
    if (pos + 1 > cap) return -1;
    out[pos++] = 0;
    return (int)pos;
}

/* Decode a DNS-encoded name starting at *pos.  Honours compression
 * (RFC 1035 §4.1.4).  Returns total bytes consumed at this level,
 * or -1 on error. */
static int decode_name(const uint8_t *msg, uint32_t msg_len, uint32_t pos,
                       char *out, uint32_t cap, uint32_t *consumed) {
    uint32_t p = pos;
    int      jumped = 0;
    uint32_t first_jump = 0;
    uint32_t out_pos = 0;
    int      rc = 0;

    while (p < msg_len) {
        uint8_t b = msg[p];
        if ((b & 0xC0) == 0xC0) {
            if (p + 1 >= msg_len) return -1;
            if (!jumped) { first_jump = p + 2; jumped = 1; }
            uint16_t off = ((uint16_t)(b & 0x3F) << 8) | msg[p + 1];
            p = off;
            continue;
        }
        if (b == 0) {
            p++;
            break;
        }
        p++;
        if (p + b > msg_len) return -1;
        if (out_pos + b + 1 >= cap) return -1;
        for (uint8_t i = 0; i < b; i++) out[out_pos++] = msg[p + i];
        out[out_pos++] = '.';
        p += b;
    }
    if (out_pos > 0) out[out_pos - 1] = 0;  /* drop trailing dot */
    else out[0] = 0;
    if (jumped) rc = (int)first_jump;
    else       rc = (int)p;
    if (consumed) *consumed = p - pos;
    return rc;
}

/* ------------------------------------------------------------------ */
/*  Cache management                                                   */
/* ------------------------------------------------------------------ */

static dns_cache_entry_t *cache_lookup_unlocked(const char *name) {
    for (uint32_t i = 0; i < DNS_CACHE_SIZE; i++) {
        if (cache[i].used && strcmp(cache[i].name, name) == 0)
            return &cache[i];
    }
    return NULL;
}

static void cache_insert(const char *name, ipv4_addr_t ip, uint32_t ttl_ms) {
    if (ttl_ms < 5000) ttl_ms = 5000;       /* minimum 5 s       */
    if (ttl_ms > 24 * 3600 * 1000U) ttl_ms = 24 * 3600 * 1000U;
    mutex_lock(&dns_lock);
    dns_cache_entry_t *e = cache_lookup_unlocked(name);
    if (!e) {
        for (uint32_t i = 0; i < DNS_CACHE_SIZE; i++) {
            if (!cache[i].used) { e = &cache[i]; break; }
        }
        if (!e) {
            /* Evict LRU. */
            uint32_t oldest = 0xFFFFFFFFU;
            int oi = 0;
            for (uint32_t i = 0; i < DNS_CACHE_SIZE; i++) {
                if (cache[i].expires_ms < oldest) {
                    oldest = cache[i].expires_ms; oi = (int)i;
                }
            }
            e = &cache[oi];
        }
        strncpy(e->name, name, DNS_MAX_NAME - 1);
        e->name[DNS_MAX_NAME - 1] = 0;
    }
    e->ip = ip;
    e->expires_ms = now_ms() + ttl_ms;
    e->used = 1;
    mutex_unlock(&dns_lock);
}

void dns_clear_cache(void) {
    mutex_lock(&dns_lock);
    memset(cache, 0, sizeof(cache));
    mutex_unlock(&dns_lock);
}

const dns_cache_entry_t *dns_get_cache(uint32_t *count) {
    if (count) *count = DNS_CACHE_SIZE;
    return cache;
}

/* ------------------------------------------------------------------ */
/*  Pending queries                                                    */
/* ------------------------------------------------------------------ */

static dns_pending_t *pending_alloc(uint16_t token) {
    for (uint32_t i = 0; i < DNS_MAX_PENDING; i++) {
        if (!pending[i].used) {
            memset(&pending[i], 0, sizeof(pending[i]));
            pending[i].used = 1;
            pending[i].token = token;
            return &pending[i];
        }
    }
    return NULL;
}

static dns_pending_t *pending_find(uint16_t token) {
    for (uint32_t i = 0; i < DNS_MAX_PENDING; i++) {
        if (pending[i].used && pending[i].token == token) return &pending[i];
    }
    return NULL;
}

static void pending_release(dns_pending_t *p) {
    if (p) { p->used = 0; memset(p, 0, sizeof(*p)); }
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void dns_init(void) {
    memset(cache, 0, sizeof(cache));
    memset(pending, 0, sizeof(pending));
    server.addr = 0;
    server_set = 0;
    mutex_init(&dns_lock);
}

int dns_set_server(ipv4_addr_t s) {
    server = s;
    server_set = (s.addr != 0);
    return 0;
}

ipv4_addr_t dns_get_server(void) { return server; }

static int dns_send_query(const char *name, uint16_t *out_token) {
    if (!server_set) return -1;
    uint8_t buf[512];
    uint32_t pos = 0;
    wr16(buf + 0, 0x1234);            /* transaction id (lower 16 bits)  */
    wr16(buf + 2, 0x0100);            /* standard query, recursion desired */
    wr16(buf + 4, 1);                 /* qdcount                          */
    wr16(buf + 6, 0); wr16(buf + 8, 0); wr16(buf + 10, 0); wr16(buf + 12, 0);
    pos = 12;
    int n = encode_name(name, buf + pos, sizeof(buf) - pos);
    if (n < 0) return -1;
    pos += n;
    wr16(buf + pos, 1);   pos += 2;    /* QTYPE = A                       */
    wr16(buf + pos, 1);   pos += 2;    /* QCLASS = IN                      */
    uint16_t token = next_token++;
    wr16(buf, token);
    mutex_lock(&dns_lock);
    dns_pending_t *p = pending_alloc(token);
    if (!p) { mutex_unlock(&dns_lock); return -1; }
    strncpy(p->qname, name, DNS_MAX_NAME - 1);
    p->qname[DNS_MAX_NAME - 1] = 0;
    p->qtype = 1; p->qclass = 1;
    p->server = server;
    p->started_ms = now_ms();
    p->resolved = 0;
    mutex_unlock(&dns_lock);
    /* Send via UDP. */
    net_interface_t *iface = net_get_default_interface();
    if (!iface) return -1;
    int r = udp_sendto(iface, server, DNS_PORT, 0, buf, pos);
    if (r != 0) {
        mutex_lock(&dns_lock);
        pending_release(p);
        mutex_unlock(&dns_lock);
        return -1;
    }
    *out_token = token;
    return 0;
}

int dns_resolve(const char *name, ipv4_addr_t *out) {
    if (!name || !out) return -1;
    /* Check cache first. */
    mutex_lock(&dns_lock);
    dns_cache_entry_t *e = cache_lookup_unlocked(name);
    if (e && e->expires_ms > now_ms()) {
        *out = e->ip;
        mutex_unlock(&dns_lock);
        return 0;
    }
    mutex_unlock(&dns_lock);

    if (!server_set) return -1;
    uint16_t token = 0;
    if (dns_send_query(name, &token) != 0) return -1;

    /* Spin-wait for the answer.  In a real OS we'd block on a wait
     * queue; for a single-threaded demo we busy-wait. */
    uint32_t start = now_ms();
    while ((uint32_t)(now_ms() - start) < DNS_TIMEOUT_MS) {
        dns_tick(now_ms());
        mutex_lock(&dns_lock);
        dns_pending_t *p = pending_find(token);
        if (p && p->resolved) {
            *out = p->answer;
            pending_release(p);
            mutex_unlock(&dns_lock);
            return 0;
        }
        mutex_unlock(&dns_lock);
        /* Yield to allow the network stack to run. */
        extern void thread_yield(void);
        thread_yield();
    }
    mutex_lock(&dns_lock);
    dns_pending_t *p = pending_find(token);
    if (p) pending_release(p);
    mutex_unlock(&dns_lock);
    return -2;
}

/* ------------------------------------------------------------------ */
/*  Tick                                                               */
/* ------------------------------------------------------------------ */

void dns_tick(uint32_t now) {
    /* Expire cache entries. */
    mutex_lock(&dns_lock);
    for (uint32_t i = 0; i < DNS_CACHE_SIZE; i++) {
        if (cache[i].used && cache[i].expires_ms <= now) cache[i].used = 0;
    }
    /* Expire pending queries. */
    for (uint32_t i = 0; i < DNS_MAX_PENDING; i++) {
        if (pending[i].used && (now - pending[i].started_ms) > DNS_TIMEOUT_MS) {
            pending_release(&pending[i]);
        }
    }
    mutex_unlock(&dns_lock);
}

/* ------------------------------------------------------------------ */
/*  Response parser                                                    */
/* ------------------------------------------------------------------ */

void dns_handle_response(const uint8_t *msg, uint32_t len,
                         ipv4_addr_t from, uint16_t from_port) {
    (void)from; (void)from_port;
    if (len < 12) return;
    uint16_t id     = be16(msg + 0);
    uint16_t flags  = be16(msg + 2);
    uint16_t qd     = be16(msg + 4);
    uint16_t an     = be16(msg + 6);
    (void)flags;
    if (qd != 1) return;

    uint32_t pos = 12;
    char qname[DNS_MAX_NAME];
    uint32_t consumed = 0;
    int rc = decode_name(msg, len, pos, qname, sizeof(qname), &consumed);
    if (rc < 0) return;
    pos += (uint32_t)rc;
    pos += 4; /* QTYPE + QCLASS */

    for (uint16_t i = 0; i < an; i++) {
        if (pos >= len) return;
        char name[DNS_MAX_NAME];
        uint32_t c2 = 0;
        int r2 = decode_name(msg, len, pos, name, sizeof(name), &c2);
        if (r2 < 0) return;
        pos += (uint32_t)r2;
        if (pos + 10 > len) return;
        uint16_t rtype  = be16(msg + pos);
        uint16_t rclass = be16(msg + pos + 2);
        uint32_t ttl    = ((uint32_t)msg[pos + 4] << 24) |
                          ((uint32_t)msg[pos + 5] << 16) |
                          ((uint32_t)msg[pos + 6] <<  8) |
                           (uint32_t)msg[pos + 7];
        uint16_t rdlen  = be16(msg + pos + 8);
        pos += 10;
        if (pos + rdlen > len) return;
        if (rtype == 1 && rclass == 1 && rdlen == 4) {
            ipv4_addr_t ip;
            ip.addr = ((uint32_t)msg[pos] << 24) |
                      ((uint32_t)msg[pos + 1] << 16) |
                      ((uint32_t)msg[pos + 2] <<  8) |
                       (uint32_t)msg[pos + 3];
            cache_insert(name, ip, ttl * 1000U);
            mutex_lock(&dns_lock);
            dns_pending_t *p = pending_find(id);
            if (p) {
                p->resolved = 1;
                p->answer = ip;
            }
            mutex_unlock(&dns_lock);
        }
        pos += rdlen;
    }
}
