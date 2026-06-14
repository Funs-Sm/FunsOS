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
