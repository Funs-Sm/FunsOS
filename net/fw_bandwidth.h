#ifndef FW_BANDWIDTH_H
#define FW_BANDWIDTH_H

#include "net.h"
#include "stdint.h"

/* Per-interface token-bucket rate limiter.  Designed to look vaguely
 * like Linux's TBF qdisc: a bucket that fills at `rate_bps` and
 * never exceeds `burst_bytes`.  When the bucket is empty packets
 * are dropped (or delayed, if you call the *_wait variant). */

typedef struct fw_qdisc {
    char     name[16];          /* interface name (or "*" for all)     */
    uint32_t rate_bps;          /* average bandwidth                   */
    uint32_t burst_bytes;       /* bucket capacity                     */
    uint32_t tokens;            /* current level (fixed-point bytes)   */
    uint64_t last_refill_ms;    /* last time we refilled the bucket    */
    uint32_t packets_pass;
    uint32_t packets_drop;
    uint32_t bytes_pass;
    uint32_t bytes_drop;
    uint8_t  active;
} fw_qdisc_t;

#define FW_QDISC_MAX 8

void     fw_qdisc_init(void);

/* Add / remove / list rules.  Returns the index (>=0) on success. */
int      fw_qdisc_add(const char *iface, uint32_t rate_bps, uint32_t burst_bytes);
int      fw_qdisc_delete(const char *iface);
int      fw_qdisc_flush(void);
uint32_t fw_qdisc_count(void);
const fw_qdisc_t *fw_qdisc_at(uint32_t i);
const fw_qdisc_t *fw_qdisc_for(const char *iface);

/* Charge `len` bytes for an outgoing packet on `iface`.  Returns
 * NF_ACCEPT if the packet fits, NF_DROP if the bucket is empty. */
int  fw_qdisc_admit(const char *iface, uint32_t len);

#endif
