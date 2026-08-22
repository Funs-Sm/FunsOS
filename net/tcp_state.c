/*
 * TCP state machine helpers
 *
 * This file complements tcp.c by providing state-machine utility functions
 * (human readable names, predicates, transition logging).  The core
 * `tcp_handle_state` and `tcp_retransmit_check` entry points live in
 * tcp.c; here we expose their behaviour through thin wrappers that
 * perform extra bookkeeping (e.g. transitions counter) and provide
 * per-state helpers used by other modules (socket layer, tests, etc.).
 */

#include "tcp.h"
#include "string.h"
#include "stdio.h"

/* Forward decl: rtx_partial_advance() lives in tcp.c (static) and is
 * what SACK-driven retransmit frees need to call.  Declared as
 * extern so tcp_state.c can use it from its SACK helpers. */
extern void rtx_partial_advance(tcp_socket_t *sock, uint32_t ack,
                                sack_range_t *blk, uint8_t n);

static const char *tcp_state_names[] = {
    "CLOSED",
    "LISTEN",
    "SYN_SENT",
    "SYN_RECEIVED",
    "ESTABLISHED",
    "FIN_WAIT_1",
    "FIN_WAIT_2",
    "CLOSE_WAIT",
    "CLOSING",
    "LAST_ACK",
    "TIME_WAIT"
};

const char *tcp_state_name(uint32_t state) {
    if (state < (sizeof(tcp_state_names) / sizeof(tcp_state_names[0])))
        return tcp_state_names[state];
    return "UNKNOWN";
}

int tcp_state_is_connecting(uint32_t state) {
    return state == TCP_STATE_SYN_SENT || state == TCP_STATE_SYN_RECEIVED;
}

int tcp_state_is_connected(uint32_t state) {
    return state == TCP_STATE_ESTABLISHED;
}

int tcp_state_is_closing(uint32_t state) {
    switch (state) {
    case TCP_STATE_FIN_WAIT1:
    case TCP_STATE_FIN_WAIT2:
    case TCP_STATE_CLOSE_WAIT:
    case TCP_STATE_CLOSING:
    case TCP_STATE_LAST_ACK:
    case TCP_STATE_TIME_WAIT:
        return 1;
    default:
        return 0;
    }
}

int tcp_state_is_listen(uint32_t state) {
    return state == TCP_STATE_LISTEN;
}

int tcp_state_is_closed(uint32_t state) {
    return state == TCP_STATE_CLOSED;
}

void tcp_state_log_transition(tcp_socket_t *sock, uint32_t from, uint32_t to) {
    if (!sock) return;
    printf("[tcp %p] %s -> %s\n", sock,
           tcp_state_name(from), tcp_state_name(to));
}

/* The canonical implementations live in tcp.c.  The forward
 * declarations here are not used; they exist only to ensure tcp_state.c
 * builds standalone if tcp.c is not linked in some configuration. */
void tcp_handle_state(tcp_socket_t *sock, uint8_t flags, const void *data, uint32_t len);
void tcp_retransmit_check(tcp_socket_t *sock);

/* Optional: append a textual segment description to a buffer. */
int tcp_describe_segment(tcp_header_t *hdr, char *out, uint32_t out_size) {
    if (!hdr || !out || out_size == 0) return 0;
    uint8_t flags = hdr->data_offset_flags & 0x3F;
    char fbuf[8];
    int p = 0;
    if (flags & TCP_SYN) fbuf[p++] = 'S';
    if (flags & TCP_ACK) fbuf[p++] = 'A';
    if (flags & TCP_FIN) fbuf[p++] = 'F';
    if (flags & TCP_RST) fbuf[p++] = 'R';
    if (flags & TCP_PSH) fbuf[p++] = 'P';
    if (flags & TCP_URG) fbuf[p++] = 'U';
    if (p == 0) fbuf[p++] = '-';
    fbuf[p] = 0;
    return snprintf(out, out_size, "TCP %u->%u seq=%u ack=%u win=%u flags=%s",
                    hdr->src_port, hdr->dst_port, hdr->seq_num,
                    hdr->ack_num, hdr->window_size, fbuf);
}

/* SACK-driven retransmit helper.
 *
 * Calls rtx_partial_advance() for each parsed SACK block, removing
 * any retransmit entries that have been *acknowledged* by the SACK
 * information.  Returns the total number of retransmit entries
 * dropped (across all blocks).
 *
 * RFC 2018 §4: the receiver only sends SACK when it observes an
 * out-of-order segment.  If we receive SACK info with at least one
 * block, we can drop retransmissions for anything the receiver
 * already has, even if cumulative ACK is older.
 */
int tcp_sack_advance_retransmit(tcp_socket_t *sock)
{
    if (!sock || !sock->sack_enabled || sock->sack_count == 0) return 0;
    int total = 0;
    for (uint8_t b = 0; b < sock->sack_count; b++) {
        uint32_t left  = sock->sack_blocks[b].left;
        uint32_t right = sock->sack_blocks[b].right;
        if (right <= left) continue;
        uint32_t before = sock->retrans_cnt;
        rtx_partial_advance(sock, left, &sock->sack_blocks[b], 1);
        uint32_t after = sock->retrans_cnt;
        total += (int)(before - after);
    }
    return total;
}

/* Send a SACK option block in the next ACK we emit.  Returns the
 * number of bytes needed to encode `max_blocks` blocks (max 4); the
 * caller passes `out` sized to at least 8*max_blocks + 2. */
int tcp_sack_build_option(tcp_socket_t *sock, uint8_t *out, uint32_t max_len,
                          uint8_t max_blocks)
{
    if (!sock || !out || !sock->sack_enabled || sock->sack_count == 0) return 0;
    if (max_blocks > 4) max_blocks = 4;
    if (max_blocks > sock->sack_count) max_blocks = sock->sack_count;
    uint32_t need = (uint32_t)max_blocks * 8u + 2u;
    if (need > max_len) return 0;
    out[0] = TCP_OPT_SACK;
    out[1] = (uint8_t)need;
    for (uint8_t b = 0; b < max_blocks; b++) {
        uint32_t left  = sock->sack_blocks[b].left;
        uint32_t right = sock->sack_blocks[b].right;
        out[2 + b*8 + 0] = (uint8_t)(left  >> 24);
        out[2 + b*8 + 1] = (uint8_t)(left  >> 16);
        out[2 + b*8 + 2] = (uint8_t)(left  >>  8);
        out[2 + b*8 + 3] = (uint8_t)(left  >>  0);
        out[2 + b*8 + 4] = (uint8_t)(right >> 24);
        out[2 + b*8 + 5] = (uint8_t)(right >> 16);
        out[2 + b*8 + 6] = (uint8_t)(right >>  8);
        out[2 + b*8 + 7] = (uint8_t)(right >>  0);
    }
    return (int)need;
}
