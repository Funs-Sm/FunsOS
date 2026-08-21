#ifndef MAC80211_RATECTRL_H
#define MAC80211_RATECTRL_H

#include "stdint.h"

/* mac80211 rate control (minstrel-style).
 *
 * Implements the Minstrel rate control algorithm used by Linux mac80211:
 *   - Sample 4 candidate rates per transmission attempt
 *   - Track throughput = probability * rate
 *   - Update EWMA success and retry counters per rate
 *   - Adapt probability of selection proportionally to throughput
 *
 * On each sampling round we choose a "primary" rate (best throughput)
 * with a probability ~95%, three "lookaround" rates (sampling the
 * throughput space uniformly), and a "random" rate ~5% of the time.
 */

#define MINSTREL_MAX_RATES     32
#define MINSTREL_SAMPLE_COLUMNS 10

typedef struct {
    uint16_t rate;            /* units of 100 kbps */
    uint8_t  mcs_index;
    uint8_t  nss;
    uint8_t  bw;              /* 20 / 40 / 80 MHz */
    uint8_t  short_guard;
    /* Stats. */
    uint32_t success;
    uint32_t attempts;
    uint32_t retries;
    uint32_t last_attempts;
    uint32_t last_success;
    uint32_t cur_tp;          /* throughput (units / minstrel update interval) */
    uint32_t cur_prob;        /* success probability * 1000 */
    uint8_t  retry_count;
    uint8_t  retry_chain[5];  /* indices of rates in retry chain */
    uint8_t  n_retry_chain;
    uint32_t perfect_tx_time;
    uint32_t ack_time;
} minstrel_rate_t;

typedef struct {
    minstrel_rate_t rates[MINSTREL_MAX_RATES];
    uint8_t  n_rates;
    uint8_t  max_rates;
    uint32_t interval;        /* update interval (us) */
    uint32_t lookaround_size;
    uint32_t sample_count;
    /* Index of best-throughput rate. */
    uint8_t  max_tp_rate;
    uint8_t  max_prob_rate;
    uint8_t  cur_rate;
    uint32_t total_attempts;
    uint32_t total_success;
    uint32_t total_failures;
    int      inited;
} minstrel_t;

int minstrel_init(minstrel_t *m);
int minstrel_add_rate(minstrel_t *m, uint16_t rate_100kbps,
                       uint8_t mcs, uint8_t nss, uint8_t bw);
void minstrel_tx_status(minstrel_t *m, uint8_t rate_idx, int success,
                         uint32_t retries);
int minstrel_next_rate(minstrel_t *m, uint8_t *rate_idx);
void minstrel_print(minstrel_t *m);

#endif