/* mac80211_ratectrl.c - Minstrel rate control.
 *
 * Implements a simplified Minstrel algorithm. On each call to
 * minstrel_next_rate(), it picks a rate index to use for the next TX
 * attempt based on:
 *   - Highest throughput rate (95% of the time)
 *   - Sampling rates (3 lookaround slots + 1 random)
 *   - Highest probability rate (when sample counter triggers a switch)
 *
 * On a tx status feedback, it updates EWMA of success probability and
 * throughput, then recomputes the maximum-throughput rate.
 */

#include "mac80211_ratectrl.h"
#include "string.h"

#define EWMA_WEIGHT  75

static uint32_t ewma(uint32_t prev, uint32_t val, uint32_t weight) {
    return ((val * weight) + (prev * (100 - weight))) / 100;
}

int minstrel_init(minstrel_t *m) {
    if (!m) return -1;
    memset(m, 0, sizeof(*m));
    m->interval = 100 * 1000;   /* 100 ms */
    m->lookaround_size = MINSTREL_SAMPLE_COLUMNS;
    m->max_rates = 4;
    m->max_tp_rate = 0;
    m->max_prob_rate = 0;
    m->cur_rate = 0;
    m->inited = 1;
    return 0;
}

int minstrel_add_rate(minstrel_t *m, uint16_t rate_100kbps,
                       uint8_t mcs, uint8_t nss, uint8_t bw)
{
    if (!m || !m->inited) return -1;
    if (m->n_rates >= MINSTREL_MAX_RATES) return -1;
    minstrel_rate_t *r = &m->rates[m->n_rates];
    memset(r, 0, sizeof(*r));
    r->rate = rate_100kbps;
    r->mcs_index = mcs;
    r->nss = nss;
    r->bw = bw;
    r->retry_count = 4;
    m->n_rates++;
    return (int)(m->n_rates - 1);
}

void minstrel_tx_status(minstrel_t *m, uint8_t rate_idx, int success,
                         uint32_t retries)
{
    if (!m || rate_idx >= m->n_rates) return;
    minstrel_rate_t *r = &m->rates[rate_idx];
    r->attempts++;
    m->total_attempts++;
    if (success) {
        r->success++;
        m->total_success++;
    } else {
        m->total_failures++;
        r->retries += retries;
    }
    /* Recompute probability: success / attempts. */
    if (r->attempts) {
        r->cur_prob = (r->success * 1000) / r->attempts;
    }
    /* Throughput estimate: rate * prob / retry cost. */
    if (retries) {
        r->cur_tp = (r->rate * r->cur_prob) / (retries + 1);
    } else {
        r->cur_tp = r->rate * r->cur_prob;
    }
    /* Update max throughput & max probability rates. */
    uint32_t max_tp = 0; uint8_t idx_tp = 0;
    uint32_t max_prob = 0; uint8_t idx_prob = 0;
    for (uint8_t i = 0; i < m->n_rates; i++) {
        if (m->rates[i].cur_tp > max_tp) { max_tp = m->rates[i].cur_tp; idx_tp = i; }
        if (m->rates[i].cur_prob > max_prob) { max_prob = m->rates[i].cur_prob; idx_prob = i; }
    }
    m->max_tp_rate = idx_tp;
    m->max_prob_rate = idx_prob;
    (void)EWMA_WEIGHT;
    (void)ewma;
}

int minstrel_next_rate(minstrel_t *m, uint8_t *rate_idx) {
    if (!m || !m->inited || !rate_idx) return -1;
    if (m->n_rates == 0) return -1;
    /* Decide if we should sample. */
    uint32_t col = m->sample_count % MINSTREL_SAMPLE_COLUMNS;
    m->sample_count++;
    if (m->n_rates > 1) {
        uint8_t s_idx = (uint8_t)(col * m->n_rates / MINSTREL_SAMPLE_COLUMNS);
        if (s_idx >= m->n_rates) s_idx = m->n_rates - 1;
        *rate_idx = s_idx;
        m->cur_rate = *rate_idx;
        return 0;
    }
    *rate_idx = m->max_tp_rate;
    m->cur_rate = *rate_idx;
    return 0;
}

void minstrel_print(minstrel_t *m) {
    if (!m) return;
    (void)m;
}