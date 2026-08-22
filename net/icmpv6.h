/*
 * net/icmpv6.h - public surface for ICMPv6 + PMTUD.
 */
#ifndef NET_ICMPV6_H
#define NET_ICMPV6_H

#include "ipv6.h"
#include "stdint.h"

#define IPV6_DEFAULT_PMTU 1280u
#define IPV6_LINK_MTU     1500u

uint32_t ipv6_pmtu_get(const uint8_t addr[16]);
void     ipv6_pmtu_set(const uint8_t addr[16], uint32_t mtu);

/* Top-level ICMPv6 entry; returns 1 if the message was consumed. */
int icmpv6_handle(const uint8_t *buf, uint32_t len, ipv6_addr_t src);

#endif /* NET_ICMPV6_H */
