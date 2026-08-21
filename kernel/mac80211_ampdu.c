/* mac80211_ampdu.c - 802.11n A-MPDU aggregation / reassembly.
 *
 * Aggregates multiple MSDU subframes into a single A-MPDU with proper
 * MPDU delimiters and 4-octet padding. The receiver reuses the same
 * struct to track incoming subframes and reassemble into MSDUs.
 */

#include "mac80211_ampdu.h"
#include "string.h"

static uint32_t padded_len(uint32_t n) {
    return (n + 3) & ~3u;
}

int ampdu_aggregator_init(ampdu_aggregator_t *a) {
    if (!a) return -1;
    memset(a, 0, sizeof(*a));
    a->inited = 1;
    return 0;
}

int ampdu_aggregator_add(ampdu_aggregator_t *a, const uint8_t *data,
                          uint32_t len, uint8_t tid)
{
    if (!a || !a->inited) return -1;
    if (a->pending.n_subframes >= AMPDU_MAX_SUBFRAMES) return -1;
    ampdu_subframe_t *s = &a->pending.subframes[a->pending.n_subframes++];
    s->data = (uint8_t *)data;
    s->len = len;
    s->tid = tid;
    s->seq_no = a->next_seq_no++;
    s->complete = 0;
    a->pending.total_bytes += padded_len(len) + AMPDU_DELIMITER_SIZE;
    return 0;
}

int ampdu_aggregator_build(ampdu_aggregator_t *a, uint8_t *out,
                            uint32_t out_len, uint32_t *out_used)
{
    if (!a || !a->inited || !out || !out_used) return -1;
    uint32_t off = 0;
    for (uint8_t i = 0; i < a->pending.n_subframes; i++) {
        ampdu_subframe_t *s = &a->pending.subframes[i];
        uint32_t total = padded_len(s->len) + AMPDU_DELIMITER_SIZE;
        if (off + total > out_len) return -1;
        /* MPDU delimiter. */
        ampdu_delimiter_t d;
        memset(&d, 0, sizeof(d));
        d.signature = 0x4E;
        d.length = (uint8_t)(s->len >> 8);
        d.length_low = (uint8_t)(s->len & 0x3F);
        d.eof = (i + 1 == a->pending.n_subframes) ? 1 : 0;
        memcpy(out + off, &d, AMPDU_DELIMITER_SIZE);
        off += AMPDU_DELIMITER_SIZE;
        /* Subframe body + 4-byte pad to enforce alignment. */
        memcpy(out + off, s->data, s->len);
        off += s->len;
        uint32_t pad = padded_len(s->len) - s->len;
        memset(out + off, 0, pad);
        off += pad;
    }
    *out_used = off;
    a->pending.total_bytes = off;
    return 0;
}

int ampdu_aggregator_finish(ampdu_aggregator_t *a) {
    if (!a) return -1;
    a->pending.n_subframes = 0;
    a->pending.total_bytes = 0;
    return 0;
}

int ampdu_reassembler_init(ampdu_reassembler_t *r) {
    if (!r) return -1;
    memset(r, 0, sizeof(*r));
    r->reassembling = 0;
    return 0;
}

int ampdu_reassembler_feed(ampdu_reassembler_t *r, const uint8_t *data,
                            uint32_t len)
{
    if (!r || !data || len < AMPDU_DELIMITER_SIZE) return -1;
    uint32_t off = 0;
    while (off < len) {
        ampdu_delimiter_t *d = (ampdu_delimiter_t *)(data + off);
        if (d->signature != 0x4E) return -1;
        uint16_t plen = (uint16_t)(d->length << 8) | d->length_low;
        uint32_t sub_total = padded_len(plen) + AMPDU_DELIMITER_SIZE;
        if (off + sub_total > len) break;
        if (r->buf.n_subframes < AMPDU_MAX_SUBFRAMES) {
            r->buf.subframes[r->buf.n_subframes].data =
                (uint8_t *)(data + off + AMPDU_DELIMITER_SIZE);
            r->buf.subframes[r->buf.n_subframes].len = plen;
            r->buf.n_subframes++;
        }
        off += sub_total;
        if (d->eof) break;
    }
    return 0;
}

int ampdu_reassembler_complete(ampdu_reassembler_t *r) {
    if (!r) return -1;
    r->buf.n_subframes = 0;
    return 0;
}