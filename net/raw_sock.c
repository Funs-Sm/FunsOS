#include "raw_sock.h"
#include "kheap.h"
#include "string.h"
#include "sync.h"
#include "ip.h"
#include "stddef.h"

static raw_sock_t  pool[RAW_SOCK_MAX];
static raw_sock_t *head;             /* simple linked list of in-use sockets */
static mutex_t     raw_lock;
static uint32_t    total_count;

void raw_sock_init(void) {
    memset(pool, 0, sizeof(pool));
    head = NULL;
    mutex_init(&raw_lock);
    total_count = 0;
}

raw_sock_t *raw_sock_create(uint8_t protocol, int hdr_incl) {
    mutex_lock(&raw_lock);
    raw_sock_t *s = NULL;
    for (int i = 0; i < RAW_SOCK_MAX; i++) {
        if (!pool[i].in_use) { s = &pool[i]; break; }
    }
    if (!s) { mutex_unlock(&raw_lock); return NULL; }
    memset(s, 0, sizeof(*s));
    s->in_use   = 1;
    s->protocol = protocol;
    s->hdr_incl = hdr_incl;
    s->next     = head;
    head        = s;
    total_count++;
    mutex_unlock(&raw_lock);
    return s;
}

int raw_sock_destroy(raw_sock_t *s) {
    if (!s) return -1;
    mutex_lock(&raw_lock);
    if (!s->in_use) { mutex_unlock(&raw_lock); return -1; }
    /* Drain the ring: any buffer not yet consumed is dropped. */
    for (uint32_t i = 0; i < s->ring_count; i++) {
        uint32_t idx = (s->ring_head + i) % RAW_SOCK_RING_SIZE;
        if (s->ring[idx]) net_free_buffer(s->ring[idx]);
        s->ring[idx] = NULL;
    }
    /* Unlink from list. */
    raw_sock_t **pp = &head;
    while (*pp) {
        if (*pp == s) { *pp = s->next; break; }
        pp = &(*pp)->next;
    }
    s->in_use = 0;
    s->next   = NULL;
    total_count--;
    mutex_unlock(&raw_lock);
    return 0;
}

int raw_sock_send(raw_sock_t *s, const void *data, uint32_t len,
                  ipv4_addr_t dst) {
    if (!s || !s->in_use || !data || len == 0) return -1;
    net_interface_t *iface = net_get_default_interface();
    if (!iface) return -1;
    if (s->hdr_incl) {
        /* Application supplied a full IP header. */
        return ip_send(iface, dst, s->protocol, data, len);
    }
    /* Build a header-less payload and let ip_send encapsulate it. */
    return ip_send(iface, dst, s->protocol, data, len);
}

int raw_sock_recv(raw_sock_t *s, net_buffer_t **out_buf) {
    if (!s || !s->in_use) return -1;
    mutex_lock(&raw_lock);
    if (s->ring_count == 0) {
        mutex_unlock(&raw_lock);
        return -2;  /* would block */
    }
    *out_buf = s->ring[s->ring_head];
    s->ring[s->ring_head] = NULL;
    s->ring_head = (s->ring_head + 1) % RAW_SOCK_RING_SIZE;
    s->ring_count--;
    mutex_unlock(&raw_lock);
    return 0;
}

/* Called from ip_receive() after the IP header has been validated and
 * the payload is ready to be dispatched.  If at least one raw socket
 * is interested, ownership of the buffer is transferred to that
 * socket; the caller MUST NOT free it.  Otherwise the buffer is left
 * untouched and the caller is responsible for disposing of it. */
void raw_sock_deliver(uint8_t protocol, net_buffer_t *buf) {
    if (!buf) return;
    mutex_lock(&raw_lock);
    raw_sock_t *s = head;
    int delivered = 0;
    while (s) {
        if (s->in_use && (s->protocol == protocol || s->protocol == RAW_SOCK_PROTOCOL_ANY)) {
            if (s->ring_count < RAW_SOCK_RING_SIZE) {
                uint32_t idx = s->ring_tail;
                s->ring[idx] = buf;
                s->ring_tail = (s->ring_tail + 1) % RAW_SOCK_RING_SIZE;
                s->ring_count++;
                delivered = 1;
                break;  /* one consumer per packet, BSD style */
            } else {
                s->drops++;
            }
        }
        s = s->next;
    }
    mutex_unlock(&raw_lock);
    /* If nobody consumed the buffer, we leave it intact; the upper
     * layer (icmp/tcp/udp) will continue processing. */
    (void)delivered;
}

uint32_t raw_sock_count(void) { return total_count; }

int raw_sock_has_consumer(uint8_t protocol) {
    int found = 0;
    mutex_lock(&raw_lock);
    for (raw_sock_t *s = head; s; s = s->next) {
        if (s->in_use && (s->protocol == protocol ||
                          s->protocol == RAW_SOCK_PROTOCOL_ANY)) {
            found = 1; break;
        }
    }
    mutex_unlock(&raw_lock);
    return found;
}
