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

/* ---- Multicast Routing / IGMP Snooping ---- */

/* Multicast forwarding cache entry */
typedef struct {
    ipv4_addr_t      source;       /* source IP */
    ipv4_addr_t      group;        /* multicast group */
    net_interface_t *in_iface;     /* incoming interface (RPF) */
    net_interface_t *out_ifaces[8]; /* outgoing interfaces */
    uint8_t          out_count;
    uint32_t         expiry_ms;
    uint32_t         packets_fwd;
    uint32_t         bytes_fwd;
    uint8_t          used;
} mcast_route_t;

#define MCAST_ROUTE_MAX    32
#define MCAST_ROUTE_TIMEOUT_MS 180000  /* 3 min */

/* IGMP Snooping state per port */
typedef struct {
    uint8_t     reporter_present;
    uint32_t    last_report_ms;
    uint32_t    join_count;
    uint32_t    leave_count;
} igmp_snoop_port_t;

/* IGMP Snooping bridge state */
typedef struct {
    ipv4_addr_t    group;
    uint8_t        port_map[8];   /* bitmap of ports that are members */
    uint32_t       timer_ms;      /* membership timer */
    uint8_t        used;
    uint8_t        fast_leave;    /* 1 = immediate leave */
} igmp_snoop_entry_t;

#define IGMP_SNOOP_MAX 64

/* Multicast forwarder (MRouter) state */
typedef struct {
    uint8_t     enabled;
    uint32_t    ttl_threshold;    /* minimum TTL to forward */
    uint32_t    route_limit;
    uint32_t    cache_misses;
    uint32_t    cache_hits;
} mcast_forwarder_t;

typedef struct {
    uint32_t routes_created;
    uint32_t routes_expired;
    uint32_t forward_packets;
    uint32_t forward_bytes;
    uint32_t rpf_failures;
    uint32_t ttl_drops;
    uint32_t snoop_reports;
    uint32_t snoop_leaves;
    uint32_t snoop_queries;
} mcast_stats_t;

/* Multicast routing API */
int  mcast_route_add(ipv4_addr_t source, ipv4_addr_t group,
                      net_interface_t *in_iface,
                      net_interface_t **out_ifaces, uint8_t out_count);
int  mcast_route_del(ipv4_addr_t source, ipv4_addr_t group);
mcast_route_t *mcast_route_lookup(ipv4_addr_t source, ipv4_addr_t group);
void mcast_route_tick(uint32_t now_ms);
void mcast_route_flush(void);

/* Multicast packet forwarding */
int  mcast_forward(net_buffer_t *buf, ipv4_addr_t source, ipv4_addr_t group,
                    uint8_t ttl);

/* IGMP Snooping */
int  igmp_snoop_init(void);
int  igmp_snoop_report(ipv4_addr_t group, uint8_t port);
int  igmp_snoop_leave(ipv4_addr_t group, uint8_t port);
int  igmp_snoop_is_member(ipv4_addr_t group, uint8_t port);
void igmp_snoop_tick(uint32_t now_ms);
void igmp_snoop_flush(void);

/* Multicast forwarder control */
void mcast_forwarder_enable(int on);
int  mcast_forwarder_is_enabled(void);
void mcast_forwarder_set_ttl(uint32_t ttl);

const mcast_stats_t *mcast_get_stats(void);

#endif
