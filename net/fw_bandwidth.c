#include "fw_bandwidth.h"
#include "netfilter.h"
#include "string.h"
#include "stddef.h"
#include "spinlock.h"
#include "sync.h"
#include "timer.h"

static fw_qdisc_t qdiscs[FW_QDISC_MAX];
static uint32_t    qdisc_used;
static spinlock_t  qd_lock;

extern uint32_t timer_get_ticks(void);

static inline uint32_t now_ms(void) {
    return timer_get_ticks() * 10U;
}

static fw_qdisc_t *find_by_name(const char *iface) {
    for (uint32_t i = 0; i < FW_QDISC_MAX; i++) {
        if (qdiscs[i].active && strcmp(qdiscs[i].name, iface) == 0) {
            return &qdiscs[i];
        }
    }
    return NULL;
}

static fw_qdisc_t *find_wildcard(void) {
    for (uint32_t i = 0; i < FW_QDISC_MAX; i++) {
        if (qdiscs[i].active && strcmp(qdiscs[i].name, "*") == 0) {
            return &qdiscs[i];
        }
    }
    return NULL;
}

void fw_qdisc_init(void) {
    spinlock_init(&qd_lock);
    memset(qdiscs, 0, sizeof(qdiscs));
    qdisc_used = 0;
}

int fw_qdisc_add(const char *iface, uint32_t rate_bps, uint32_t burst_bytes) {
    if (!iface || !iface[0] || rate_bps == 0) return -1;
    spinlock_lock(&qd_lock);
    if (find_by_name(iface)) { spinlock_unlock(&qd_lock); return -2; }
    int idx = -1;
    for (uint32_t i = 0; i < FW_QDISC_MAX; i++) {
        if (!qdiscs[i].active) { idx = (int)i; break; }
    }
    if (idx < 0) { spinlock_unlock(&qd_lock); return -1; }
    fw_qdisc_t *q = &qdiscs[idx];
    memset(q, 0, sizeof(*q));
    int n = 0;
    while (iface[n] && n < 15) { q->name[n] = iface[n]; n++; }
    q->name[n] = '\0';
    q->rate_bps    = rate_bps;
    q->burst_bytes = burst_bytes;
    q->tokens      = burst_bytes;
    q->last_refill_ms = now_ms();
    q->active = 1;
    qdisc_used++;
    spinlock_unlock(&qd_lock);
    return idx;
}

int fw_qdisc_delete(const char *iface) {
    if (!iface) return -1;
    spinlock_lock(&qd_lock);
    fw_qdisc_t *q = find_by_name(iface);
    if (!q) { spinlock_unlock(&qd_lock); return -1; }
    q->active = 0;
    if (qdisc_used > 0) qdisc_used--;
    spinlock_unlock(&qd_lock);
    return 0;
}

int fw_qdisc_flush(void) {
    spinlock_lock(&qd_lock);
    memset(qdiscs, 0, sizeof(qdiscs));
    qdisc_used = 0;
    spinlock_unlock(&qd_lock);
    return 0;
}

uint32_t fw_qdisc_count(void) { return qdisc_used; }

const fw_qdisc_t *fw_qdisc_at(uint32_t i) {
    if (i >= FW_QDISC_MAX) return NULL;
    return qdiscs[i].active ? &qdiscs[i] : NULL;
}

const fw_qdisc_t *fw_qdisc_for(const char *iface) {
    if (!iface) return NULL;
    spinlock_lock(&qd_lock);
    fw_qdisc_t *q = find_by_name(iface);
    if (!q) q = find_wildcard();
    spinlock_unlock(&qd_lock);
    return q;
}

int fw_qdisc_admit(const char *iface, uint32_t len) {
    if (!iface || !iface[0] || len == 0) return NF_ACCEPT;
    spinlock_lock(&qd_lock);
    fw_qdisc_t *q = find_by_name(iface);
    if (!q) q = find_wildcard();
    if (!q) { spinlock_unlock(&qd_lock); return NF_ACCEPT; }

    /* Refill tokens at rate_bps per second. */
    uint32_t now = now_ms();
    uint32_t delta = now - q->last_refill_ms;
    q->last_refill_ms = now;
    /* tokens is in bytes; rate is in bits/s. */
    uint64_t add = ((uint64_t)q->rate_bps * delta) / 8000U;
    if (add > 0) {
        uint64_t t = q->tokens + add;
        if (t > q->burst_bytes) t = q->burst_bytes;
        q->tokens = (uint32_t)t;
    }
    if (q->tokens >= len) {
        q->tokens -= len;
        q->packets_pass++;
        q->bytes_pass += len;
        spinlock_unlock(&qd_lock);
        return NF_ACCEPT;
    }
    q->packets_drop++;
    q->bytes_drop += len;
    spinlock_unlock(&qd_lock);
    return NF_DROP;
}
