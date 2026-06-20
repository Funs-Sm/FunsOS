#include "netfilter.h"
#include "string.h"
#include "sync.h"
#include "stddef.h"
#include "stdint.h"
#include "kheap.h"
#include "timer.h"
#include "stdio.h"

/* Per-hook chain of registered callbacks.  We use a small static
 * table to avoid the cost of heap allocation; the typical number of
 * hooks is one or two (e.g. a firewall module), so eight slots per
 * hook point is plenty. */
static nf_hook_t hooks[NF_INET_NUMHOOKS][NF_HOOK_MAX];
static uint32_t  drops[NF_INET_NUMHOOKS];
static mutex_t   nf_lock;

/* Rule tables (for the iptables-like API).  Each table is a small
 * allocation that owns a set of chains, one per hook point. */
static nf_table_t *tables[NF_TABLE_MAX];
static uint32_t    table_count;
static mutex_t     table_lock;

void netfilter_init(void) {
    memset(hooks, 0, sizeof(hooks));
    memset(drops, 0, sizeof(drops));
    memset(tables, 0, sizeof(tables));
    table_count = 0;
    mutex_init(&nf_lock);
    mutex_init(&table_lock);
    nf_log_init();
    nf_ct_init();
}

int netfilter_register(int hook, int (*fn)(net_buffer_t *, int, void *), void *priv) {
    if (hook < 0 || hook >= NF_INET_NUMHOOKS || !fn) return -1;
    mutex_lock(&nf_lock);
    for (int i = 0; i < NF_HOOK_MAX; i++) {
        if (!hooks[hook][i].active) {
            hooks[hook][i].fn     = fn;
            hooks[hook][i].priv   = priv;
            hooks[hook][i].active = 1;
            mutex_unlock(&nf_lock);
            return 0;
        }
    }
    mutex_unlock(&nf_lock);
    return -1;
}

int netfilter_unregister(int hook, int (*fn)(net_buffer_t *, int, void *)) {
    if (hook < 0 || hook >= NF_INET_NUMHOOKS || !fn) return -1;
    mutex_lock(&nf_lock);
    for (int i = 0; i < NF_HOOK_MAX; i++) {
        if (hooks[hook][i].active && hooks[hook][i].fn == fn) {
            hooks[hook][i].active = 0;
            hooks[hook][i].fn     = NULL;
            hooks[hook][i].priv   = NULL;
            mutex_unlock(&nf_lock);
            return 0;
        }
    }
    mutex_unlock(&nf_lock);
    return -1;
}

int netfilter_run(int hook, net_buffer_t *buf) {
    if (hook < 0 || hook >= NF_INET_NUMHOOKS || !buf) return NF_ACCEPT;
    int verdict = NF_ACCEPT;
    /* Snapshot the active hooks under the lock, then run them
     * outside the lock to avoid deadlocks. */
    nf_hook_t snapshot[NF_HOOK_MAX];
    mutex_lock(&nf_lock);
    memcpy(snapshot, hooks[hook], sizeof(snapshot));
    mutex_unlock(&nf_lock);
    for (int i = 0; i < NF_HOOK_MAX; i++) {
        if (snapshot[i].active && snapshot[i].fn) {
            int rc = snapshot[i].fn(buf, hook, snapshot[i].priv);
            if (rc == NF_DROP) {
                drops[hook]++;
                verdict = NF_DROP;
                break;
            } else if (rc == NF_STOLEN) {
                verdict = NF_STOLEN;
                break;
            } else if (rc == NF_QUEUE || rc == NF_REPEAT) {
                verdict = rc;
            }
        }
    }
    return verdict;
}

uint32_t netfilter_drops(int hook) {
    if (hook < 0 || hook >= NF_INET_NUMHOOKS) return 0;
    return drops[hook];
}

/* ------------------------------------------------------------------ */
/* Rule-table management                                              */
/* ------------------------------------------------------------------ */

static nf_table_t *table_lookup(const char *name) {
    if (!name) return NULL;
    for (uint32_t i = 0; i < NF_TABLE_MAX; i++) {
        if (tables[i] && strcmp(tables[i]->name, name) == 0) return tables[i];
    }
    return NULL;
}

int nf_table_create(const char *name) {
    if (!name || !name[0]) return -1;
    mutex_lock(&table_lock);
    if (table_lookup(name)) { mutex_unlock(&table_lock); return -1; }
    int slot = -1;
    for (uint32_t i = 0; i < NF_TABLE_MAX; i++) {
        if (!tables[i]) { slot = (int)i; break; }
    }
    if (slot < 0) { mutex_unlock(&table_lock); return -1; }
    nf_table_t *t = (nf_table_t *)kmalloc(sizeof(nf_table_t));
    if (!t) { mutex_unlock(&table_lock); return -1; }
    memset(t, 0, sizeof(*t));
    strncpy(t->name, name, sizeof(t->name) - 1);
    t->family = 2;   /* AF_INET */
    for (int h = 0; h < NF_INET_NUMHOOKS; h++) {
        t->chains[h].default_policy = NF_ACCEPT;
    }
    tables[slot] = t;
    table_count++;
    mutex_unlock(&table_lock);
    return 0;
}

int nf_table_delete(const char *name) {
    mutex_lock(&table_lock);
    nf_table_t *t = table_lookup(name);
    if (!t) { mutex_unlock(&table_lock); return -1; }
    for (uint32_t i = 0; i < NF_TABLE_MAX; i++) {
        if (tables[i] == t) { tables[i] = NULL; break; }
    }
    if (table_count > 0) table_count--;
    kfree(t);
    mutex_unlock(&table_lock);
    return 0;
}

nf_table_t *nf_table_get(const char *name) {
    mutex_lock(&table_lock);
    nf_table_t *t = table_lookup(name);
    mutex_unlock(&table_lock);
    return t;
}

int nf_table_attach(const char *name, int hook) {
    if (hook < 0 || hook >= NF_INET_NUMHOOKS) return -1;
    mutex_lock(&table_lock);
    nf_table_t *t = table_lookup(name);
    if (!t) { mutex_unlock(&table_lock); return -1; }
    t->chains[hook].active = 1;
    mutex_unlock(&table_lock);
    return 0;
}

int nf_table_detach(const char *name, int hook) {
    if (hook < 0 || hook >= NF_INET_NUMHOOKS) return -1;
    mutex_lock(&table_lock);
    nf_table_t *t = table_lookup(name);
    if (!t) { mutex_unlock(&table_lock); return -1; }
    t->chains[hook].active = 0;
    mutex_unlock(&table_lock);
    return 0;
}

int nf_chain_set_policy(const char *name, int hook, uint32_t policy) {
    if (hook < 0 || hook >= NF_INET_NUMHOOKS) return -1;
    if (policy != NF_ACCEPT && policy != NF_DROP) return -1;
    mutex_lock(&table_lock);
    nf_table_t *t = table_lookup(name);
    if (!t) { mutex_unlock(&table_lock); return -1; }
    t->chains[hook].default_policy = policy;
    mutex_unlock(&table_lock);
    return 0;
}

int nf_rule_append(const char *name, int hook, const nf_rule_t *r) {
    if (!r || hook < 0 || hook >= NF_INET_NUMHOOKS) return -1;
    mutex_lock(&table_lock);
    nf_table_t *t = table_lookup(name);
    if (!t) { mutex_unlock(&table_lock); return -1; }
    nf_chain_t *c = &t->chains[hook];
    if (c->rule_count >= NF_CHAIN_MAX) { mutex_unlock(&table_lock); return -1; }
    c->rules[c->rule_count++] = *r;
    mutex_unlock(&table_lock);
    return 0;
}

int nf_rule_insert(const char *name, int hook, uint32_t pos, const nf_rule_t *r) {
    if (!r || hook < 0 || hook >= NF_INET_NUMHOOKS) return -1;
    mutex_lock(&table_lock);
    nf_table_t *t = table_lookup(name);
    if (!t) { mutex_unlock(&table_lock); return -1; }
    nf_chain_t *c = &t->chains[hook];
    if (c->rule_count >= NF_CHAIN_MAX) { mutex_unlock(&table_lock); return -1; }
    if (pos > c->rule_count) { mutex_unlock(&table_lock); return -1; }
    for (uint32_t i = c->rule_count; i > pos; i--) c->rules[i] = c->rules[i - 1];
    c->rules[pos] = *r;
    c->rule_count++;
    mutex_unlock(&table_lock);
    return 0;
}

int nf_rule_delete(const char *name, int hook, uint32_t pos) {
    if (hook < 0 || hook >= NF_INET_NUMHOOKS) return -1;
    mutex_lock(&table_lock);
    nf_table_t *t = table_lookup(name);
    if (!t) { mutex_unlock(&table_lock); return -1; }
    nf_chain_t *c = &t->chains[hook];
    if (pos >= c->rule_count) { mutex_unlock(&table_lock); return -1; }
    for (uint32_t i = pos; i + 1 < c->rule_count; i++) c->rules[i] = c->rules[i + 1];
    c->rule_count--;
    c->rules[c->rule_count].target = 0;
    mutex_unlock(&table_lock);
    return 0;
}

int nf_rule_flush(const char *name, int hook) {
    if (hook < 0 || hook >= NF_INET_NUMHOOKS) return -1;
    mutex_lock(&table_lock);
    nf_table_t *t = table_lookup(name);
    if (!t) { mutex_unlock(&table_lock); return -1; }
    nf_chain_t *c = &t->chains[hook];
    memset(c->rules, 0, sizeof(c->rules));
    c->rule_count = 0;
    mutex_unlock(&table_lock);
    return 0;
}

uint32_t nf_rule_counter(const char *name, int hook, uint32_t pos) {
    uint32_t v = 0;
    mutex_lock(&table_lock);
    nf_table_t *t = table_lookup(name);
    if (t && hook >= 0 && hook < NF_INET_NUMHOOKS && pos < t->chains[hook].rule_count) {
        v = t->chains[hook].rules[pos].counter;
    }
    mutex_unlock(&table_lock);
    return v;
}

uint32_t nf_table_count(void) { return table_count; }

const nf_table_t *nf_table_at(uint32_t i) {
    if (i >= NF_TABLE_MAX) return NULL;
    return tables[i];
}

/* Match a single rule against a packet. */
int nf_match_test(const nf_match_t *m, net_buffer_t *buf) {
    if (!m || !m->used) return 1;   /* empty match = always */
    if (!buf) return 0;
    /* The match operates on the IP/TCP/UDP headers found in
     * buf->data + buf->offset.  We do a simple parse: read 12 bytes
     * for IP src/dst and the protocol, then advance to the L4
     * header for the port match. */
    if (buf->len < 20) return 0;
    const uint8_t *p = (const uint8_t *)buf->data + buf->offset;
    uint8_t vihl = p[0];
    if (((vihl >> 4) & 0x0F) != 4) return 0;
    uint8_t ihl = (vihl & 0x0F) * 4;
    if (ihl < 20 || buf->len < (int)ihl) return 0;
    uint8_t proto = p[9];
    uint32_t sip = ((uint32_t)p[12]) | ((uint32_t)p[13] << 8) | (((uint32_t)p[14] << 16) | ((uint32_t)p[15] << 24));
    uint32_t dip = ((uint32_t)p[16]) | ((uint32_t)p[17] << 8) | (((uint32_t)p[18] << 16) | ((uint32_t)p[19] << 24));

    if (m->flags & NF_MATCH_SRC_IP) {
        if ((sip & m->src_mask.addr) != (m->src_ip.addr & m->src_mask.addr)) return 0;
    }
    if (m->flags & NF_MATCH_DST_IP) {
        if ((dip & m->dst_mask.addr) != (m->dst_ip.addr & m->dst_mask.addr)) return 0;
    }
    if (m->flags & NF_MATCH_PROTOCOL) {
        if (proto != m->protocol) return 0;
    }
    if ((m->flags & (NF_MATCH_SRC_PORT | NF_MATCH_DST_PORT)) &&
        (proto == 6 || proto == 17)) {
        if (buf->len < (int)(ihl + 4)) return 0;
        const uint8_t *l4 = p + ihl;
        uint16_t sport = ((uint16_t)l4[0] << 8) | l4[1];
        uint16_t dport = ((uint16_t)l4[2] << 8) | l4[3];
        if (m->flags & NF_MATCH_SRC_PORT) {
            uint16_t lo = m->src_port, hi = m->src_port_end ? m->src_port_end : m->src_port;
            if (sport < lo || sport > hi) return 0;
        }
        if (m->flags & NF_MATCH_DST_PORT) {
            uint16_t lo = m->dst_port, hi = m->dst_port_end ? m->dst_port_end : m->dst_port;
            if (dport < lo || dport > hi) return 0;
        }
    }
    (void)dip; (void)sip;
    return 1;
}

int netfilter_run_tables(int hook, net_buffer_t *buf) {
    if (hook < 0 || hook >= NF_INET_NUMHOOKS || !buf) return NF_ACCEPT;
    int verdict = NF_ACCEPT;
    mutex_lock(&table_lock);
    /* Snapshot active tables to avoid holding the lock during packet
     * evaluation.  We allow up to 8 active tables per hook. */
    nf_table_t *snap[NF_TABLE_MAX];
    int snap_n = 0;
    for (uint32_t i = 0; i < NF_TABLE_MAX && snap_n < NF_TABLE_MAX; i++) {
        if (tables[i] && tables[i]->chains[hook].active) {
            snap[snap_n++] = tables[i];
        }
    }
    mutex_unlock(&table_lock);
    for (int s = 0; s < snap_n; s++) {
        nf_chain_t *c = &snap[s]->chains[hook];
        uint8_t drop = 0;
        for (uint32_t r = 0; r < c->rule_count; r++) {
            if (nf_match_test(&c->rules[r].match, buf)) {
                c->rules[r].counter++;
                if (c->rules[r].target == NF_DROP) {
                    c->drop_count++;
                    drops[hook]++;
                    drop = 1;
                    verdict = NF_DROP;
                } else {
                    c->accept_count++;
                    verdict = NF_ACCEPT;
                }
                break;
            }
        }
        if (drop) break;
        if (c->default_policy == NF_DROP) {
            drops[hook]++;
            c->drop_count++;
            verdict = NF_DROP;
            break;
        }
    }
    return verdict;
}

/* ------------------------------------------------------------------ */
/*  规则日志                                                           */
/* ------------------------------------------------------------------ */

static nf_log_entry_t nf_log[NF_LOG_MAX];
static uint32_t nf_log_pos;
static mutex_t   nf_log_lock;

void nf_log_init(void) {
    memset(nf_log, 0, sizeof(nf_log));
    nf_log_pos = 0;
    mutex_init(&nf_log_lock);
}

void nf_log_packet(int hook, net_buffer_t *buf, int verdict) {
    if (!buf || hook < 0 || hook >= NF_INET_NUMHOOKS) return;
    if (buf->len < 20) return;

    const uint8_t *p = (const uint8_t *)buf->data + buf->offset;
    uint8_t vihl = p[0];
    if (((vihl >> 4) & 0x0F) != 4) return;
    uint8_t ihl = (vihl & 0x0F) * 4;
    if (ihl < 20 || buf->len < (int)ihl) return;

    uint8_t proto = p[9];
    uint32_t sip = ((uint32_t)p[12]) | ((uint32_t)p[13] << 8) | (((uint32_t)p[14] << 16) | ((uint32_t)p[15] << 24));
    uint32_t dip = ((uint32_t)p[16]) | ((uint32_t)p[17] << 8) | (((uint32_t)p[18] << 16) | ((uint32_t)p[19] << 24));

    uint16_t sport = 0, dport = 0;
    if ((proto == 6 || proto == 17) && buf->len >= (int)(ihl + 4)) {
        const uint8_t *l4 = p + ihl;
        sport = ((uint16_t)l4[0] << 8) | l4[1];
        dport = ((uint16_t)l4[2] << 8) | l4[3];
    }

    mutex_lock(&nf_log_lock);
    nf_log_entry_t *entry = &nf_log[nf_log_pos % NF_LOG_MAX];
    entry->timestamp = timer_get_ticks();
    entry->hook = hook;
    entry->src_ip = sip;
    entry->dst_ip = dip;
    entry->src_port = sport;
    entry->dst_port = dport;
    entry->protocol = proto;
    entry->verdict = (uint8_t)verdict;
    nf_log_pos++;
    mutex_unlock(&nf_log_lock);
}

void nf_log_dump(void (*fn)(const char *)) {
    if (!fn) return;
    mutex_lock(&nf_log_lock);
    uint32_t count = (nf_log_pos > NF_LOG_MAX) ? NF_LOG_MAX : nf_log_pos;
    for (uint32_t i = 0; i < count; i++) {
        nf_log_entry_t *e = &nf_log[i];
        char line[128];
        /* 格式化输出日志条目 */
        int len = snprintf(line, sizeof(line),
            "[%u] HOOK=%d SRC=%u.%u.%u.%u:%u DST=%u.%u.%u.%u:%u PROTO=%u VERDICT=%s\n",
            e->timestamp, e->hook,
            (e->src_ip >> 0) & 0xFF, (e->src_ip >> 8) & 0xFF,
            (e->src_ip >> 16) & 0xFF, (e->src_ip >> 24) & 0xFF, e->src_port,
            (e->dst_ip >> 0) & 0xFF, (e->dst_ip >> 8) & 0xFF,
            (e->dst_ip >> 16) & 0xFF, (e->dst_ip >> 24) & 0xFF, e->dst_port,
            e->protocol, e->verdict == NF_ACCEPT ? "ACCEPT" : "DROP");
        (void)len;
        fn(line);
    }
    mutex_unlock(&nf_log_lock);
}

/* ------------------------------------------------------------------ */
/*  连接跟踪(Conntrack)基础                                            */
/* ------------------------------------------------------------------ */

static nf_conntrack_t ct_table[NF_CONNTRACK_MAX];

void nf_ct_init(void) {
    memset(ct_table, 0, sizeof(ct_table));
}

int nf_ct_check(net_buffer_t *buf, int *state) {
    if (!buf || !state) return -1;
    if (buf->len < 20) return -1;

    const uint8_t *p = (const uint8_t *)buf->data + buf->offset;
    uint8_t vihl = p[0];
    if (((vihl >> 4) & 0x0F) != 4) return -1;
    uint8_t ihl = (vihl & 0x0F) * 4;
    if (ihl < 20 || buf->len < (int)ihl) return -1;

    uint8_t proto = p[9];
    /* 只跟踪TCP和UDP */
    if (proto != 6 && proto != 17) return -1;

    uint32_t sip = ((uint32_t)p[12]) | ((uint32_t)p[13] << 8) | (((uint32_t)p[14] << 16) | ((uint32_t)p[15] << 24));
    uint32_t dip = ((uint32_t)p[16]) | ((uint32_t)p[17] << 8) | (((uint32_t)p[18] << 16) | ((uint32_t)p[19] << 24));

    uint16_t sport = 0, dport = 0;
    if (buf->len >= (int)(ihl + 4)) {
        const uint8_t *l4 = p + ihl;
        sport = ((uint16_t)l4[0] << 8) | l4[1];
        dport = ((uint16_t)l4[2] << 8) | l4[3];
    }

    for (uint32_t i = 0; i < NF_CONNTRACK_MAX; i++) {
        if (ct_table[i].used &&
            ct_table[i].src_ip == sip && ct_table[i].src_port == sport &&
            ct_table[i].dst_ip == dip && ct_table[i].dst_port == dport &&
            ct_table[i].protocol == proto) {
            *state = ct_table[i].state;
            return 0;
        }
    }
    *state = 0; /* 未找到，状态为new */
    return 0;
}

void nf_ct_update(net_buffer_t *buf, uint8_t state) {
    if (!buf) return;
    if (buf->len < 20) return;

    const uint8_t *p = (const uint8_t *)buf->data + buf->offset;
    uint8_t vihl = p[0];
    if (((vihl >> 4) & 0x0F) != 4) return;
    uint8_t ihl = (vihl & 0x0F) * 4;
    if (ihl < 20 || buf->len < (int)ihl) return;

    uint8_t proto = p[9];
    if (proto != 6 && proto != 17) return;

    uint32_t sip = ((uint32_t)p[12]) | ((uint32_t)p[13] << 8) | (((uint32_t)p[14] << 16) | ((uint32_t)p[15] << 24));
    uint32_t dip = ((uint32_t)p[16]) | ((uint32_t)p[17] << 8) | (((uint32_t)p[18] << 16) | ((uint32_t)p[19] << 24));

    uint16_t sport = 0, dport = 0;
    if (buf->len >= (int)(ihl + 4)) {
        const uint8_t *l4 = p + ihl;
        sport = ((uint16_t)l4[0] << 8) | l4[1];
        dport = ((uint16_t)l4[2] << 8) | l4[3];
    }

    for (uint32_t i = 0; i < NF_CONNTRACK_MAX; i++) {
        if (ct_table[i].used &&
            ct_table[i].src_ip == sip && ct_table[i].src_port == sport &&
            ct_table[i].dst_ip == dip && ct_table[i].dst_port == dport &&
            ct_table[i].protocol == proto) {
            ct_table[i].state = state;
            ct_table[i].timeout = timer_get_ticks() + 300; /* 默认超时300 ticks */
            return;
        }
    }

    /* 未找到，创建新条目 */
    for (uint32_t i = 0; i < NF_CONNTRACK_MAX; i++) {
        if (!ct_table[i].used) {
            ct_table[i].src_ip = sip;
            ct_table[i].src_port = sport;
            ct_table[i].dst_ip = dip;
            ct_table[i].dst_port = dport;
            ct_table[i].protocol = proto;
            ct_table[i].state = state;
            ct_table[i].timeout = timer_get_ticks() + 300;
            ct_table[i].used = 1;
            break;
        }
    }
}

void nf_ct_cleanup(uint32_t now) {
    for (uint32_t i = 0; i < NF_CONNTRACK_MAX; i++) {
        if (ct_table[i].used && now > ct_table[i].timeout) {
            ct_table[i].used = 0;
        }
    }
}
