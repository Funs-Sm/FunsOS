#ifndef MAC80211_AMPDU_H
#define MAC80211_AMPDU_H

#include "stdint.h"

/* 802.11n MPDU Aggregation (A-MPDU).
 *
 * Aggregates multiple subframes into a single PPDU (PHY protocol
 * data unit) to reduce MAC overhead. Each subframe is bounded by an
 * MPDU delimiter and a per-MPDU padding to enforce a 4-octet
 * alignment. The receiver strips delimiters and reassembles.
 *
 * The driver implements:
 *   - Aggregation of up to 64 subframes per A-MPDU
 *   - Per-subframe MPDU delimiter construction
 *   - Block-ack bitmap for tracking completion
 *   - Reassembly buffer for the RX side
 */

#define AMPDU_MAX_SUBFRAMES    64
#define AMPDU_DELIMITER_SIZE   4
#define AMPDU_MAX_SIZE         65536

typedef struct __attribute__((packed)) {
    uint8_t  reserved;
    uint8_t  crc;
    uint8_t  signature;       /* always 0x4E */
    uint8_t  length;          /* high 8 bits of 14-bit length */
    uint8_t  length_low;      /* low 6 bits of length */
    uint8_t  eof;             /* 1 if last */
    uint16_t reserved2;
} ampdu_delimiter_t;

typedef struct {
    uint8_t *data;
    uint32_t len;
    uint8_t  tid;
    uint16_t seq_no;
    uint8_t  complete;
} ampdu_subframe_t;

typedef struct {
    ampdu_subframe_t subframes[AMPDU_MAX_SUBFRAMES];
    uint8_t          n_subframes;
    uint8_t          tid;
    uint16_t         starting_seq;
    uint64_t         bitmap;   /* block ack bitmap */
    uint8_t          flags;    /* 1 = started, 2 = in_progress */
    uint32_t         total_bytes;
} ampdu_t;

/* Aggregator (TX). */
typedef struct {
    ampdu_t   pending;
    uint16_t  next_seq_no;
    int       inited;
} ampdu_aggregator_t;

/* Reassembler (RX). */
typedef struct {
    ampdu_t   buf;
    uint16_t  expected_seq;
    int       reassembling;
} ampdu_reassembler_t;

int  ampdu_aggregator_init(ampdu_aggregator_t *a);
int  ampdu_aggregator_add(ampdu_aggregator_t *a, const uint8_t *data,
                           uint32_t len, uint8_t tid);
int  ampdu_aggregator_build(ampdu_aggregator_t *a, uint8_t *out,
                             uint32_t out_len, uint32_t *out_used);
int  ampdu_aggregator_finish(ampdu_aggregator_t *a);

int  ampdu_reassembler_init(ampdu_reassembler_t *r);
int  ampdu_reassembler_feed(ampdu_reassembler_t *r, const uint8_t *data,
                             uint32_t len);
int  ampdu_reassembler_complete(ampdu_reassembler_t *r);

#endif