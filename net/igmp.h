#ifndef IGMP_H
#define IGMP_H

#include "net.h"
#include "stdint.h"

/* IGMPv2 (RFC 2236) for IPv4 multicast group management.
 *
 * Maintains a per-interface list of joined multicast groups and
 * sends Membership Report / Leave Group messages when the
 * application modifies the membership.  The implementation is
 * deliberately small: it supports one group per interface, which
 * is enough for mDNS (224.0.0.251) and similar use cases.
 *
 * Reports are rate-limited: after a Report is sent we suppress
 * further Reports for the same (interface, group) for IGMP_REPORT
 * suppression time (10 s, RFC 2236 §3). */

#define IGMP_TYPE_QUERY       0x11
#define IGMP_TYPE_REPORT_V1   0x12
#define IGMP_TYPE_REPORT_V2   0x16
#define IGMP_TYPE_LEAVE       0x17

#define IGMP_REPORT_SUPPRESS_MS 10000U
#define IGMP_MAX_GROUPS          16

typedef struct {
    ipv4_addr_t      group;
    net_interface_t *iface;
    uint32_t         last_report_ms;
    uint8_t          used;
} igmp_group_t;

typedef struct {
    uint32_t reports_sent;
    uint32_t queries_rcvd;
    uint32_t leaves_sent;
    uint32_t checksum_errors;
} igmp_stats_t;

void igmp_init(void);
int  igmp_join(net_interface_t *iface, ipv4_addr_t group);
int  igmp_leave(net_interface_t *iface, ipv4_addr_t group);
int  igmp_is_member(ipv4_addr_t group);
void igmp_handle_report(net_interface_t *iface, const uint8_t *msg, uint32_t len);
void igmp_tick(uint32_t now_ms);
const igmp_stats_t *igmp_get_stats(void);

#endif
