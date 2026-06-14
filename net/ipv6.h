#ifndef IPV6_H
#define IPV6_H

#include "stdint.h"
#include "net.h"

/* IPv6 协议号 */
#define IPV6_PROTO_HOPOPTS   0    /* Hop-by-Hop Options */
#define IPV6_PROTO_ICMPV6    58   /* ICMPv6 */
#define IPV6_PROTO_TCP       6    /* TCP */
#define IPV6_PROTO_UDP       17   /* UDP */
#define IPV6_PROTO_ROUTING   43   /* Routing Header */
#define IPV6_PROTO_FRAGMENT  44   /* Fragment Header */
#define IPV6_PROTO_ESP       50   /* Encapsulating Security Payload */
#define IPV6_PROTO_AH        51   /* Authentication Header */
#define IPV6_PROTO_NONE      59   /* No Next Header */
#define IPV6_PROTO_DSTOPTS   60   /* Destination Options */

/* ICMPv6 类型 */
#define ICMPV6_TYPE_DEST_UNREACH      1
#define ICMPV6_TYPE_PACKET_TOO_BIG    2
#define ICMPV6_TYPE_TIME_EXCEEDED     3
#define ICMPV6_TYPE_PARAM_PROBLEM     4
#define ICMPV6_TYPE_ECHO_REQUEST      128
#define ICMPV6_TYPE_ECHO_REPLY        129
#define ICMPV6_TYPE_MLD_QUERY         130
#define ICMPV6_TYPE_MLD_REPORT        131
#define ICMPV6_TYPE_MLD_DONE          132
#define ICMPV6_TYPE_ROUTER_SOLICIT    133
#define ICMPV6_TYPE_ROUTER_ADVERT     134
#define ICMPV6_TYPE_NEIGHBOR_SOLICIT  135
#define ICMPV6_TYPE_NEIGHBOR_ADVERT   136
#define ICMPV6_TYPE_REDIRECT          137

/* 邻居缓存状态 */
#define NEIGHBOR_STATE_INCOMPLETE  0
#define NEIGHBOR_STATE_REACHABLE   1
#define NEIGHBOR_STATE_STALE       2
#define NEIGHBOR_STATE_PROBE       3

/* 邻居缓存超时 (毫秒) */
#define NEIGHBOR_TIMEOUT_MS        30000U
#define NEIGHBOR_REACHABLE_MS      30000U
#define NEIGHBOR_STALE_MS          60000U
#define NEIGHBOR_PROBE_MS          5000U

/* 邻居缓存最大条目 */
#define NEIGHBOR_CACHE_MAX         256

/* 默认跳数限制 */
#define IPV6_DEFAULT_HOP_LIMIT     64

/* 多播地址前缀 */
#define IPV6_MULTICAST_PREFIX      0xFF

/* IPv6 地址 */
typedef struct {
    uint8_t addr[16];
} ipv6_addr_t;

/* IPv6 头部 */
typedef struct __attribute__((packed)) {
    uint32_t ver_tc_fl;       /* Version(4) | Traffic Class(8) | Flow Label(20) */
    uint16_t payload_len;
    uint8_t next_header;
    uint8_t hop_limit;
    ipv6_addr_t src;
    ipv6_addr_t dst;
} ipv6_header_t;

/* ICMPv6 头部 */
typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint32_t data;
} icmpv6_header_t;

/* NDP 邻居请求 */
typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint32_t reserved;
    uint8_t target[16];
    /* Options follow */
} ndp_ns_header_t;

/* NDP 邻居通告 */
typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint32_t flags;
    uint8_t target[16];
    /* Options follow */
} ndp_na_header_t;

/* NDP 路由器通告 */
typedef struct __attribute__((packed)) {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint8_t cur_hop_limit;
    uint8_t flags;
    uint16_t router_lifetime;
    uint32_t reachable_time;
    uint32_t retrans_timer;
    /* Options follow */
} ndp_ra_header_t;

/* 邻居缓存条目 */
typedef struct neighbor_entry {
    ipv6_addr_t addr;
    uint8_t mac[6];
    uint32_t state;
    uint32_t timer;
    struct neighbor_entry *next;
} neighbor_entry_t;

/* IPv6 路由条目 */
typedef struct ipv6_route_entry {
    ipv6_addr_t network;
    uint8_t prefix_len;
    ipv6_addr_t gateway;
    net_interface_t *iface;
    uint32_t metric;
    struct ipv6_route_entry *next;
} ipv6_route_entry_t;

/* IPv6 统计 */
typedef struct {
    uint32_t packets_sent;
    uint32_t packets_rcvd;
    uint32_t fragments_sent;
    uint32_t fragments_rcvd;
    uint32_t dropped;
    uint32_t no_route;
    uint32_t checksum_errors;
    uint32_t hop_limit_expired;
    uint32_t nd_solicits_sent;
    uint32_t nd_solicits_rcvd;
    uint32_t nd_adverts_sent;
    uint32_t nd_adverts_rcvd;
    uint32_t ra_rcvd;
    uint32_t rs_sent;
} ipv6_stats_t;

/* 初始化 */
void ipv6_init(void);

/* 地址配置 */
void ipv6_set_link_local(const uint8_t *mac);
int ipv6_configure_address(const ipv6_addr_t *addr, uint8_t prefix_len);
int ipv6_configure_address_on_iface(net_interface_t *iface,
                                     const ipv6_addr_t *addr, uint8_t prefix_len);

/* 发送 */
int ipv6_send_packet(net_interface_t *iface, const ipv6_addr_t *dst,
                     uint8_t protocol, const void *data, uint16_t len);
int ipv6_send_packet_raw(net_interface_t *iface, const ipv6_addr_t *dst,
                         uint8_t protocol, const void *data, uint16_t len,
                         uint8_t hop_limit);

/* 接收 */
int ipv6_receive_packet(net_interface_t *iface, const void *data, uint16_t len);

/* 邻居发现 */
int ipv6_neighbor_solicit(net_interface_t *iface, const ipv6_addr_t *target);
int ipv6_neighbor_advertise(net_interface_t *iface, const ipv6_addr_t *target,
                            const uint8_t *mac);
neighbor_entry_t *ipv6_neighbor_lookup(const ipv6_addr_t *addr);
int ipv6_neighbor_resolve(net_interface_t *iface, const ipv6_addr_t *addr,
                          uint8_t *mac);

/* 工具函数 */
void ipv6_addr_to_str(const ipv6_addr_t *addr, char *buf, int bufsize);
int ipv6_addr_from_str(ipv6_addr_t *addr, const char *str);
int ipv6_addr_is_multicast(const ipv6_addr_t *addr);
int ipv6_addr_is_link_local(const ipv6_addr_t *addr);
int ipv6_addr_is_unspecified(const ipv6_addr_t *addr);
int ipv6_addr_is_loopback(const ipv6_addr_t *addr);
int ipv6_addr_compare(const ipv6_addr_t *a, const ipv6_addr_t *b);
void ipv6_solicited_node_mcast(const ipv6_addr_t *addr, ipv6_addr_t *mcast);
uint16_t ipv6_checksum(const ipv6_addr_t *src, const ipv6_addr_t *dst,
                       uint8_t next_header, const void *data, uint16_t len);

/* 自动配置 (SLAAC) */
int ipv6_slaac_configure(void);

/* 路由 */
int ipv6_route_add(const ipv6_addr_t *network, uint8_t prefix_len,
                   const ipv6_addr_t *gateway, net_interface_t *iface,
                   uint32_t metric);
int ipv6_route_lookup(const ipv6_addr_t *dst, ipv6_addr_t *gateway,
                      net_interface_t **iface);
int ipv6_route_del(const ipv6_addr_t *network, uint8_t prefix_len);
void ipv6_route_flush(void);

/* 统计 */
const ipv6_stats_t *ipv6_get_stats(void);

/* 定时器 */
void ipv6_tick(uint32_t now_ms);

#endif /* IPV6_H */