#ifndef UDP_H
#define UDP_H

#include "stdint.h"
#include "net.h"

typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} udp_header_t;

#define UDP_MAX_PORTS 65536
#define UDP_RCVBUF_SIZE 8192
#define UDP_STATS

typedef struct udp_socket {
    uint16_t local_port;
    uint16_t remote_port;
    ipv4_addr_t remote_ip;
    net_interface_t *iface;
    uint8_t bound;
    void *recv_buffer;
    uint32_t recv_len;
    ipv4_addr_t recv_from_ip;
    uint16_t recv_from_port;
    struct udp_socket *next;
} udp_socket_t;

typedef struct {
    uint32_t datagrams_sent;
    uint32_t datagrams_rcvd;
    uint32_t bytes_sent;
    uint32_t bytes_rcvd;
    uint32_t no_socket;
    uint32_t checksum_errors;
    uint32_t port_in_use;
} udp_stats_t;

void udp_init(void);
int  udp_send(net_interface_t *iface, ipv4_addr_t dst, uint16_t dst_port, uint16_t src_port, const void *data, uint32_t len);
int  udp_sendto(net_interface_t *iface, ipv4_addr_t dst, uint16_t dst_port, uint16_t src_port, const void *data, uint32_t len);
int  udp_send_broadcast(net_interface_t *iface, uint16_t dst_port, uint16_t src_port, const void *data, uint32_t len);
void udp_receive(net_buffer_t *buf);
udp_socket_t *udp_socket_create(void);
int  udp_socket_bind(udp_socket_t *sock, uint16_t port);
int  udp_socket_connect(udp_socket_t *sock, ipv4_addr_t ip, uint16_t port);
int  udp_socket_recv(udp_socket_t *sock, void *buf, uint32_t len);
int  udp_socket_recvfrom(udp_socket_t *sock, void *buf, uint32_t len, ipv4_addr_t *from_ip, uint16_t *from_port);
int  udp_socket_send(udp_socket_t *sock, const void *buf, uint32_t len);
void udp_socket_close(udp_socket_t *sock);

uint16_t udp_checksum(ipv4_addr_t src, ipv4_addr_t dst, const void *data, uint32_t len);
const udp_stats_t *udp_get_stats(void);
const udp_socket_t *udp_get_sockets(uint32_t *count);

#endif
