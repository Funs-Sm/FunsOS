#ifndef NET_H
#define NET_H

#include "stdint.h"

#define NET_MAX_INTERFACES 8
#define NET_RX_RING_SIZE 256
#define NET_TX_RING_SIZE 256

#define ETH_P_IP   0x0800
#define ETH_P_ARP  0x0806
#define ETH_P_IPV6 0x86DD

typedef struct net_interface net_interface_t;
typedef struct net_buffer net_buffer_t;

typedef struct {
    uint8_t bytes[6];
} mac_addr_t;

typedef struct {
    uint32_t addr;
} ipv4_addr_t;

struct net_interface {
    char name[16];
    mac_addr_t mac;
    ipv4_addr_t ip;
    ipv4_addr_t mask;
    ipv4_addr_t gateway;
    uint8_t up;
    uint32_t mtu;
    uint32_t flags;          /* IFF_UP / IFF_RUNNING / IFF_LOOPBACK ... */
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t tx_bytes;
    uint32_t rx_bytes;
    uint32_t tx_errors;
    uint32_t rx_errors;
    void *driver_data;
    int (*send)(net_interface_t *iface, const void *data, uint32_t len);
};

/* Interface flags (subset of net/if.h) */
#define IFF_UP          0x0001
#define IFF_BROADCAST   0x0002
#define IFF_LOOPBACK    0x0008
#define IFF_RUNNING     0x0040
#define IFF_MULTICAST   0x1000

struct net_buffer {
    uint8_t data[1518];
    uint32_t len;
    uint32_t offset;
    net_interface_t *iface;
    net_buffer_t *next;
};

typedef struct {
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_bytes;
    uint32_t tx_bytes;
    uint32_t rx_errors;
    uint32_t tx_errors;
    uint32_t rx_dropped;
    uint32_t tx_dropped;
} net_stats_t;

/* 扩展的网络性能统计 */
typedef struct {
    uint64_t rx_bytes_total;
    uint64_t tx_bytes_total;
    uint64_t rx_packets_total;
    uint64_t tx_packets_total;
    uint32_t rx_pps;           /* 每秒接收包数 */
    uint32_t tx_pps;           /* 每秒发送包数 */
    uint32_t rx_bps;           /* 每秒接收位数 */
    uint32_t tx_bps;           /* 每秒发送位数 */
    uint32_t peak_rx_bps;
    uint32_t peak_tx_bps;
    uint32_t peak_rx_pps;
    uint32_t peak_tx_pps;
    uint32_t collisions;
    uint32_t multicast_rx;
    uint32_t broadcast_rx;
    uint32_t multicast_tx;
    uint32_t broadcast_tx;
    uint32_t rx_frame_errors;
    uint32_t rx_over_errors;
    uint32_t rx_crc_errors;
    uint32_t rx_fifo_errors;
    uint32_t rx_length_errors;
    uint32_t tx_aborted_errors;
    uint32_t tx_carrier_errors;
    uint32_t tx_fifo_errors;
    uint32_t tx_heartbeat_errors;
    uint32_t tx_window_errors;
    uint32_t rx_compressed;
    uint32_t tx_compressed;
} net_perf_stats_t;

void net_init(void);
void net_register_interface(net_interface_t *iface);
net_interface_t *net_get_interface(uint32_t index);
uint32_t net_get_interface_count(void);
net_interface_t *net_get_interface_by_name(const char *name);
net_interface_t *net_get_default_interface(void);
void net_set_interface_flags(net_interface_t *iface, uint32_t flags);
int  net_set_interface_address(net_interface_t *iface, ipv4_addr_t ip,
                               ipv4_addr_t mask, ipv4_addr_t gw);
void net_receive(net_buffer_t *buf);
void net_transmit(net_interface_t *iface, net_buffer_t *buf);
net_buffer_t *net_alloc_buffer(void);
void net_free_buffer(net_buffer_t *buf);
void net_tick(uint32_t now_ms);

const net_stats_t *net_get_stats(void);
void net_reset_stats(void);

/* 扩展性能统计接口 */
const net_perf_stats_t *net_get_perf_stats(void);
void net_perf_stats_reset(void);
void net_perf_tick(uint32_t now_ms);
int  net_get_iface_stats(const char *iface_name, net_stats_t *out);
uint32_t net_get_iface_mtu(const char *iface_name);
int  net_set_iface_mtu(const char *iface_name, uint32_t mtu);
int  net_iface_up(const char *iface_name);
int  net_iface_down(const char *iface_name);
int  net_get_iface_link(const char *iface_name);
uint32_t net_get_iface_speed(const char *iface_name);
const char *net_get_iface_duplex(const char *iface_name);

#endif

