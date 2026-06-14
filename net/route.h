#ifndef ROUTE_H
#define ROUTE_H

#include "net.h"
#include "stdint.h"

/* Routing table with longest-prefix match (RFC 1812 §2.3).
 *
 * The table supports static routes (added by the administrator) and
 * dynamic routes (e.g. learnt from ICMP redirects or DHCP).  It is
 * consulted by ip_route_lookup() which in turn uses it to find the
 * best interface / next-hop for outgoing packets. */

#define ROUTE_TABLE_MAX    64
#define ROUTE_FLAG_STATIC  0x01
#define ROUTE_FLAG_GATEWAY 0x02
#define ROUTE_FLAG_UP      0x04
#define ROUTE_FLAG_BLACKHOLE 0x08
#define ROUTE_FLAG_REJECT    0x10

/* ---- 多路由表支持 ---- */
#define ROUTE_TABLE_MAX_ID  8
#define ROUTE_TABLE_MAIN    0
#define ROUTE_TABLE_LOCAL   1
#define ROUTE_TABLE_DEFAULT 2

/* ---- 策略路由规则 ---- */
#define ROUTE_POLICY_SRC_ADDR   1
#define ROUTE_POLICY_DST_ADDR   2
#define ROUTE_POLICY_TOS        3
#define ROUTE_POLICY_FW_MARK    4
#define ROUTE_POLICY_INTERFACE  5

#define ROUTE_POLICY_MAX        32

typedef struct route_policy {
    uint32_t priority;
    uint32_t match_type;
    uint32_t match_value;
    uint32_t match_mask;
    uint32_t table_id;
    struct route_policy *next;
} route_policy_t;

/* ---- 路由条目 ---- */
typedef struct {
    ipv4_addr_t     dest;        /* destination prefix             */
    ipv4_addr_t     mask;        /* prefix length mask             */
    ipv4_addr_t     gateway;     /* next-hop, 0 for directly-attached */
    net_interface_t *iface;      /* outgoing interface             */
    uint32_t        metric;      /* lower is better                */
    uint32_t        flags;
} route_entry_t;

/* ---- 路由缓存 ---- */
#define ROUTE_CACHE_SIZE 256

typedef struct {
    uint32_t dst, src;
    route_entry_t *route;
    uint32_t last_used;
    int valid;
} route_cache_entry_t;

typedef struct {
    uint32_t  lookups;
    uint32_t  hits;
    uint32_t  misses;
    uint32_t  inserts;
    uint32_t  deletes;
    uint32_t  longest_prefix;
    uint32_t  cache_hits;
    uint32_t  cache_misses;
    uint32_t  policy_matches;
} route_stats_t;

void  route_init(void);
int   route_add(ipv4_addr_t dest, ipv4_addr_t mask, ipv4_addr_t gw,
                net_interface_t *iface, uint32_t metric, uint32_t flags);
int   route_delete(ipv4_addr_t dest, ipv4_addr_t mask);
void  route_purge_dynamic(void);
const route_entry_t *route_get_all(uint32_t *count);
const route_stats_t *route_get_stats(void);

/* Longest-prefix match lookup.  Returns the best route, or a
 * zero-initialised entry with iface == NULL on miss. */
route_entry_t route_lookup(ipv4_addr_t dst);

/* Translate a route lookup into a (iface, next-hop) pair.  Honours
 * the IFF_UP flag and falls back to a default route if the host
 * route is missing. */
int route_resolve(ipv4_addr_t dst, net_interface_t **out_iface,
                  ipv4_addr_t *out_gw);

/* Convenience: install a direct subnet and (if the interface has a
 * gateway) a default route entry.  Used at boot time to bootstrap
 * the routing table. */
int  route_install_iface_defaults(net_interface_t *iface);

/* ---- 多路由表 API ---- */
int route_add_to_table(uint32_t table_id, ipv4_addr_t dest, ipv4_addr_t mask,
                        ipv4_addr_t gw, net_interface_t *iface,
                        uint32_t metric, uint32_t flags);
int route_delete_from_table(uint32_t table_id, ipv4_addr_t dest, ipv4_addr_t mask);
route_entry_t route_lookup_table(uint32_t table_id, ipv4_addr_t dst);

/* ---- 策略路由 API ---- */
int route_add_policy(uint32_t priority, uint32_t match_type,
                     uint32_t match_value, uint32_t match_mask, uint32_t table_id);
int route_del_policy(uint32_t priority);
route_entry_t *route_lookup_policy(uint32_t src, uint32_t dst,
                                    uint32_t tos, uint32_t fwmark);

/* ---- 路由缓存 API ---- */
void route_cache_flush(void);
route_entry_t *route_cache_lookup(uint32_t dst, uint32_t src);
void route_cache_insert(uint32_t dst, uint32_t src, route_entry_t *route);

#endif
