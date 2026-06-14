#ifndef NET_DRIVER_H
#define NET_DRIVER_H

#include "stdint.h"

#define NET_MAX_INTERFACES 8
#define NET_MTU 1500
#define MAC_ADDR_LEN 6

typedef struct {
    uint8_t mac[MAC_ADDR_LEN];
    uint32_t ip_addr;
    uint32_t netmask;
    uint32_t gateway;
    uint32_t mtu;
    uint8_t up;
} net_interface_t;

void net_driver_init(void);
int32_t net_send(uint32_t iface, const void *buf, uint32_t len);
int32_t net_recv(uint32_t iface, void *buf, uint32_t len);

#endif
