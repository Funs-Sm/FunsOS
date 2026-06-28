#include "udp.h"
#include "ip.h"
#include "kheap.h"
#include "string.h"
#include "sync.h"
#include "stddef.h"

static udp_socket_t *sockets[UDP_MAX_PORTS];
static udp_socket_t *socket_list;
static udp_stats_t   stats;
static mutex_t       udp_table_lock;

void udp_init(void) {
    memset(sockets, 0, sizeof(sockets));
    memset(&stats, 0, sizeof(stats));
    socket_list = NULL;
    mutex_init(&udp_table_lock);
}

uint16_t udp_checksum(ipv4_addr_t src, ipv4_addr_t dst, const void *data, uint32_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = 0;

    /* IPv4伪头部 (RFC 768):
     * - src ip (4 bytes)
     * - dst ip (4 bytes)
     * - zero (1 byte)
     * - protocol (1 byte)
     * - udp length (2 bytes)
     */
    sum += ((src.addr >> 16) & 0xFFFF);
    sum += (src.addr & 0xFFFF);
    sum += ((dst.addr >> 16) & 0xFFFF);
    sum += (dst.addr & 0xFFFF);
    sum += IP_PROTO_UDP;
    sum += (len & 0xFFFF);

    while (len > 1) {
        sum += ((uint16_t)p[0] << 8) | p[1];
        p += 2; len -= 2;
    }
    if (len == 1) {
        sum += ((uint16_t)p[0] << 8);
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

int udp_send(net_interface_t *iface, ipv4_addr_t dst, uint16_t dst_port,
             uint16_t src_port, const void *data, uint32_t len) {
    return udp_sendto(iface, dst, dst_port, src_port, data, len);
}

int udp_sendto(net_interface_t *iface, ipv4_addr_t dst, uint16_t dst_port,
              uint16_t src_port, const void *data, uint32_t len) {
    if (!iface) return -1;
    uint32_t total = sizeof(udp_header_t) + len;
    if (total > 65535) return -1;

    uint8_t *packet = (uint8_t *)kmalloc(total);
    if (!packet) return -1;

    udp_header_t *hdr = (udp_header_t *)packet;
    hdr->src_port = src_port;
    hdr->dst_port = dst_port;
    hdr->length   = (uint16_t)total;
    hdr->checksum = 0;
    memcpy(packet + sizeof(udp_header_t), data, len);

    /* Optional checksum.  If src is non-zero, compute over the IPv4
     * pseudo-header.  If the resulting checksum is zero, the standard
     * requires us to send 0xFFFF (since 0 means "no checksum"). */
    if (src_port != 0 || iface->ip.addr != 0) {
        uint16_t c = udp_checksum(iface->ip, dst, packet, total);
        if (c == 0) c = 0xFFFF;
        hdr->checksum = c;
    }

    int result = ip_send(iface, dst, IP_PROTO_UDP, packet, total);
    kfree(packet);
    if (result == 0) {
        stats.datagrams_sent++;
        stats.bytes_sent += len;
    }
    return result;
}

int udp_send_broadcast(net_interface_t *iface, uint16_t dst_port, uint16_t src_port,
                       const void *data, uint32_t len) {
    ipv4_addr_t bcast; bcast.addr = 0xFFFFFFFF;
    return udp_sendto(iface, bcast, dst_port, src_port, data, len);
}

void udp_receive(net_buffer_t *buf) {
    if (buf->len < (int)sizeof(udp_header_t)) return;
    udp_header_t *hdr = (udp_header_t *)(buf->data + buf->offset);
    uint16_t dst_port = hdr->dst_port;
    uint16_t src_port = hdr->src_port;
    if (hdr->length < sizeof(udp_header_t)) return;
    uint32_t payload_len = hdr->length - sizeof(udp_header_t);
    if (payload_len > buf->len - sizeof(udp_header_t)) {
        payload_len = buf->len - sizeof(udp_header_t);
    }

    /* Verify checksum if non-zero. */
    if (hdr->checksum != 0) {
        ip_header_t *ip_hdr = (ip_header_t *)(buf->data + buf->offset - 20);
        uint16_t save = hdr->checksum;
        hdr->checksum = 0;
        uint16_t calc = udp_checksum(ip_hdr->src_ip, ip_hdr->dst_ip, hdr, hdr->length);
        hdr->checksum = save;
        if (calc != save && calc != 0xFFFF) {
            stats.checksum_errors++;
            return;
        }
    }

    udp_socket_t *sock = sockets[dst_port];
    if (!sock) {
        stats.no_socket++;
        return;
    }

    if (sock->recv_buffer) kfree(sock->recv_buffer);
    sock->recv_buffer = kmalloc(payload_len ? payload_len : 1);
    if (!sock->recv_buffer) return;
    memcpy(sock->recv_buffer, buf->data + buf->offset + sizeof(udp_header_t), payload_len);
    sock->recv_len = payload_len;
    ip_header_t *ip_hdr = (ip_header_t *)(buf->data + buf->offset - 20);
    sock->recv_from_ip   = ip_hdr->src_ip;
    sock->recv_from_port = src_port;
    stats.datagrams_rcvd++;
    stats.bytes_rcvd += payload_len;
}

udp_socket_t *udp_socket_create(void) {
    udp_socket_t *sock = (udp_socket_t *)kmalloc(sizeof(udp_socket_t));
    if (!sock) return NULL;
    memset(sock, 0, sizeof(udp_socket_t));
    return sock;
}

int udp_socket_bind(udp_socket_t *sock, uint16_t port) {
    if (!sock) return -1;
    if (port >= UDP_MAX_PORTS) return -1;
    mutex_lock(&udp_table_lock);
    if (sockets[port] && sockets[port] != sock) {
        stats.port_in_use++;
        mutex_unlock(&udp_table_lock);
        return -1;
    }
    sockets[port] = sock;
    sock->local_port = port;
    sock->bound = 1;
    /* Maintain user-facing list. */
    sock->next = socket_list;
    socket_list = sock;
    mutex_unlock(&udp_table_lock);
    return 0;
}

int udp_socket_connect(udp_socket_t *sock, ipv4_addr_t ip, uint16_t port) {
    if (!sock) return -1;
    sock->remote_ip = ip;
    sock->remote_port = port;
    return 0;
}

int udp_socket_recv(udp_socket_t *sock, void *buf, uint32_t len) {
    return udp_socket_recvfrom(sock, buf, len, NULL, NULL);
}

int udp_socket_recvfrom(udp_socket_t *sock, void *buf, uint32_t len,
                        ipv4_addr_t *from_ip, uint16_t *from_port) {
    if (!sock || !sock->recv_buffer || sock->recv_len == 0) return 0;
    uint32_t copy_len = sock->recv_len < len ? sock->recv_len : len;
    memcpy(buf, sock->recv_buffer, copy_len);
    if (from_ip)   *from_ip   = sock->recv_from_ip;
    if (from_port) *from_port = sock->recv_from_port;
    kfree(sock->recv_buffer);
    sock->recv_buffer = NULL;
    sock->recv_len = 0;
    return (int)copy_len;
}

int udp_socket_send(udp_socket_t *sock, const void *buf, uint32_t len) {
    if (!sock || !sock->remote_ip.addr) return -1;
    net_interface_t *iface = sock->iface ? sock->iface : NULL;
    if (!iface) {
        for (uint32_t i = 0; i < NET_MAX_INTERFACES; i++) {
            net_interface_t *f = net_get_interface(i);
            if (f && f->up) { iface = f; break; }
        }
    }
    if (!iface) return -1;
    return udp_sendto(iface, sock->remote_ip, sock->remote_port, sock->local_port, buf, len);
}

void udp_socket_close(udp_socket_t *sock) {
    if (!sock) return;
    mutex_lock(&udp_table_lock);
    if (sock->bound && sock->local_port < UDP_MAX_PORTS) {
        if (sockets[sock->local_port] == sock) sockets[sock->local_port] = NULL;
    }
    udp_socket_t **pp = &socket_list;
    while (*pp) {
        if (*pp == sock) { *pp = sock->next; break; }
        pp = &(*pp)->next;
    }
    mutex_unlock(&udp_table_lock);
    if (sock->recv_buffer) kfree(sock->recv_buffer);
    kfree(sock);
}

const udp_stats_t *udp_get_stats(void) { return &stats; }

const udp_socket_t *udp_get_sockets(uint32_t *count) {
    if (count) {
        uint32_t c = 0;
        for (udp_socket_t *s = socket_list; s; s = s->next) c++;
        *count = c;
    }
    return socket_list;
}
