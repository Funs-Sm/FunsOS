#ifndef RAW_SOCK_H
#define RAW_SOCK_H

#include "net.h"
#include "stdint.h"

typedef struct pcb_t pcb_t;

/* SOCK_RAW socket layer.
 *
 * Implements a small subset of Linux's AF_INET, SOCK_RAW semantics.
 * A raw socket is bound to a protocol number (e.g. IPPROTO_ICMP) and
 * receives a copy of every inbound IP datagram whose protocol field
 * matches.  Outbound writes are wrapped in a minimal IP header and
 * dispatched through ip_send().  The layer is fully non-blocking; if
 * no datagram is available, recv returns -EAGAIN.
 *
 * The implementation deliberately avoids copying: each receive
 * enqueues a pointer to the net_buffer_t in a per-socket ring and
 * the application consumes it via raw_sock_recv() which hands the
 * buffer over.  This is how Berkeley-style raw sockets avoid an
 * extra copy on the fast path. */

#define RAW_SOCK_MAX         32
#define RAW_SOCK_RING_SIZE   32
#define RAW_SOCK_PROTOCOL_ANY 0

typedef struct raw_sock {
    int        in_use;
    uint8_t    protocol;     /* IP protocol number, 0 = any */
    pcb_t     *owner;        /* process that owns the socket */
    int        hdr_incl;     /* 1 = application provides IP hdr */
    uint32_t   drops;        /* datagrams dropped because ring was full */
    net_buffer_t *ring[RAW_SOCK_RING_SIZE];
    uint32_t   ring_head;
    uint32_t   ring_tail;
    uint32_t   ring_count;
    struct raw_sock *next;
} raw_sock_t;

void  raw_sock_init(void);
raw_sock_t *raw_sock_create(uint8_t protocol, int hdr_incl);
int   raw_sock_destroy(raw_sock_t *s);
int   raw_sock_send(raw_sock_t *s, const void *data, uint32_t len,
                    ipv4_addr_t dst);
int   raw_sock_recv(raw_sock_t *s, net_buffer_t **out_buf);
void  raw_sock_deliver(uint8_t protocol, net_buffer_t *buf);
uint32_t raw_sock_count(void);

/* Return non-zero if at least one raw socket is interested in the
 * given IP protocol.  Used by ip_dispatch_payload() to decide
 * whether the buffer should be handed to a raw socket instead of
 * the regular upper-layer handler. */
int   raw_sock_has_consumer(uint8_t protocol);

#endif
