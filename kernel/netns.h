#ifndef NETNS_H
#define NETNS_H

#include "stdint.h"

#define NETNS_MAX       32
#define NETNS_NAME_MAX  32
#define NETNS_MAX_ROUTES 16
#define NETNS_MAX_RULES  32

struct net;

struct netns_route {
    uint32_t dest;
    uint32_t gateway;
    uint32_t netmask;
    uint32_t ifindex;
    uint32_t used;
};

struct netns_rule {
    uint32_t table;
    uint32_t chain;
    uint32_t action;
    uint32_t used;
    uint64_t packets;
    uint64_t bytes;
};

struct net {
    char name[NETNS_NAME_MAX];
    uint32_t nsid;
    uint32_t used;
    uint32_t refcount;
    uint32_t ifindex_count;
    uint32_t loopback_up;
    struct netns_route routes[NETNS_MAX_ROUTES];
    uint32_t route_count;
    struct netns_rule rules[NETNS_MAX_RULES];
    uint32_t rule_count;
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t tcp_connections;
    uint64_t udp_sockets;
};

typedef struct net net_t;

int netns_init(void);
net_t *netns_get(void);
net_t *netns_find_by_name(const char *name);
int netns_create(const char *name);
int copy_net_ns(net_t *src, net_t *dst);
void netns_put(net_t *ns);
void netns_print_stats(void);

#endif
