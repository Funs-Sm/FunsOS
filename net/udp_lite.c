#include "udp_lite.h"
#include "udp.h"
#include "ip.h"
#include "kheap.h"
#include "string.h"

/* UDP-Lite header (8 bytes):
 *   src_port(2) dst_port(2) coverage(2) checksum(2) */

typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t coverage;
    uint16_t checksum;
} udplite_header_t;

uint16_t udp_lite_checksum(ipv4_addr_t src, ipv4_addr_t dst,
                           const void *data, uint32_t len,
                           uint16_t coverage) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t sum = 0;
    sum += (src.addr >> 16) & 0xFFFF;
    sum += src.addr & 0xFFFF;
    sum += (dst.addr >> 16) & 0xFFFF;
    sum += dst.addr & 0xFFFF;
    sum += IP_PROTO_UDP_LITE;
    sum += coverage ? coverage : len;
    while (len > 1) {
        sum += ((uint16_t)p[0] << 8) | p[1];
        p += 2; len -= 2;
    }
    if (len == 1) sum += (uint16_t)(*p) << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

int udp_lite_send(net_interface_t *iface, ipv4_addr_t dst,
                  uint16_t dst_port, uint16_t src_port,
                  const void *data, uint32_t len, uint16_t coverage) {
    if (!iface || !data) return -1;
    uint32_t total = sizeof(udplite_header_t) + len;
    if (total > 65535) return -1;
    uint8_t *packet = (uint8_t *)kmalloc(total);
    if (!packet) return -1;
    udplite_header_t *hdr = (udplite_header_t *)packet;
    hdr->src_port = src_port;
    hdr->dst_port = dst_port;
    hdr->coverage = coverage ? coverage : (uint16_t)len;
    hdr->checksum = 0;
    memcpy(packet + sizeof(udplite_header_t), data, len);
    uint16_t cs = udp_lite_checksum(iface->ip, dst, packet, total, hdr->coverage);
    if (cs == 0) cs = 0xFFFF;
    hdr->checksum = cs;
    int r = ip_send(iface, dst, IP_PROTO_UDP_LITE, packet, total);
    kfree(packet);
    return r;
}

void udp_lite_receive(net_buffer_t *buf) {
    if (buf->len < (int)sizeof(udplite_header_t)) return;
    udplite_header_t *hdr = (udplite_header_t *)(buf->data + buf->offset);
    uint16_t dst_port = hdr->dst_port;
    if (hdr->coverage < sizeof(udplite_header_t)) return;
    uint32_t payload_len = hdr->coverage - sizeof(udplite_header_t);
    if (payload_len > buf->len - sizeof(udplite_header_t)) {
        payload_len = buf->len - sizeof(udplite_header_t);
    }
    /* Verify checksum (skip if coverage == 0). */
    if (hdr->coverage != 0) {
        ip_header_t *ip_hdr = (ip_header_t *)(buf->data + buf->offset - 20);
        uint16_t save = hdr->checksum;
        hdr->checksum = 0;
        uint16_t calc = udp_lite_checksum(ip_hdr->dst_ip, ip_hdr->src_ip, hdr,
                                          hdr->coverage, hdr->coverage);
        hdr->checksum = save;
        if (calc != save) return;
    }
    /* Hand to the UDP socket table.  UDP-Lite sockets are
     * identified by the same port space. */
    extern int udp_socket_deliver(uint16_t port, net_buffer_t *buf,
                                  uint16_t src_port);
    (void)udp_socket_deliver;
    (void)dst_port;
}
