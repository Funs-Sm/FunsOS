#include "igmp.h"
#include "ip.h"
#include "kheap.h"
#include "string.h"
#include "timer.h"
#include "sync.h"

static igmp_group_t groups[IGMP_MAX_GROUPS];
static igmp_stats_t stats;
static mutex_t      igmp_lock;

static uint32_t now_ms(void) { return timer_get_ticks() * 10U; }

static uint16_t igmp_checksum(const void *data, uint32_t len) {
    return ip_checksum(data, len);
}

void igmp_init(void) {
    memset(groups, 0, sizeof(groups));
    memset(&stats, 0, sizeof(stats));
    mutex_init(&igmp_lock);
}

static int send_igmp(net_interface_t *iface, uint8_t type, ipv4_addr_t group) {
    if (!iface) return -1;
    uint8_t pkt[8];
    pkt[0] = type;
    pkt[1] = 0;          /* code = 0 for v2 reports                */
    pkt[2] = 0; pkt[3] = 0;  /* checksum placeholder               */
    pkt[4] = (group.addr >> 24) & 0xFF;
    pkt[5] = (group.addr >> 16) & 0xFF;
    pkt[6] = (group.addr >>  8) & 0xFF;
    pkt[7] = (group.addr      ) & 0xFF;
    uint16_t cs = igmp_checksum(pkt, sizeof(pkt));
    pkt[2] = (cs >> 0) & 0xFF;
    pkt[3] = (cs >> 8) & 0xFF;
    /* Send to the group address (or all-routers for Leave). */
    ipv4_addr_t dst = (type == IGMP_TYPE_LEAVE) ?
                      (ipv4_addr_t){ .addr = 0x020000E0 } /* 224.0.0.2 */ : group;
    int r = ip_send(iface, dst, 2 /* IGMP */, pkt, sizeof(pkt));
    return r;
}

int igmp_join(net_interface_t *iface, ipv4_addr_t group) {
    if (!iface) return -1;
    mutex_lock(&igmp_lock);
    /* Replace if already joined. */
    for (uint32_t i = 0; i < IGMP_MAX_GROUPS; i++) {
        if (groups[i].used && groups[i].group.addr == group.addr) {
            groups[i].iface = iface;
            groups[i].last_report_ms = 0;
            mutex_unlock(&igmp_lock);
            return 0;
        }
    }
    for (uint32_t i = 0; i < IGMP_MAX_GROUPS; i++) {
        if (!groups[i].used) {
            groups[i].used = 1;
            groups[i].group = group;
            groups[i].iface = iface;
            groups[i].last_report_ms = 0;
            mutex_unlock(&igmp_lock);
            send_igmp(iface, IGMP_TYPE_REPORT_V2, group);
            stats.reports_sent++;
            return 0;
        }
    }
    mutex_unlock(&igmp_lock);
    return -1;
}

int igmp_leave(net_interface_t *iface, ipv4_addr_t group) {
    mutex_lock(&igmp_lock);
    for (uint32_t i = 0; i < IGMP_MAX_GROUPS; i++) {
        if (groups[i].used && groups[i].group.addr == group.addr) {
            groups[i].used = 0;
            mutex_unlock(&igmp_lock);
            send_igmp(iface, IGMP_TYPE_LEAVE, group);
            stats.leaves_sent++;
            return 0;
        }
    }
    mutex_unlock(&igmp_lock);
    return -1;
}

int igmp_is_member(ipv4_addr_t group) {
    int found = 0;
    mutex_lock(&igmp_lock);
    for (uint32_t i = 0; i < IGMP_MAX_GROUPS; i++) {
        if (groups[i].used && groups[i].group.addr == group.addr) { found = 1; break; }
    }
    mutex_unlock(&igmp_lock);
    return found;
}

void igmp_handle_report(net_interface_t *iface, const uint8_t *msg, uint32_t len) {
    if (!msg || len < 8) return;
    uint16_t recv = (uint16_t)msg[2] | ((uint16_t)msg[3] << 8);
    uint8_t tmp[8];
    for (int i = 0; i < 8; i++) tmp[i] = msg[i];
    tmp[2] = tmp[3] = 0;
    uint16_t calc = igmp_checksum(tmp, 8);
    if (recv != calc) { stats.checksum_errors++; return; }
    if (msg[0] == IGMP_TYPE_QUERY) {
        stats.queries_rcvd++;
        ipv4_addr_t g;
        g.addr = ((uint32_t)msg[4] << 24) | ((uint32_t)msg[5] << 16) |
                 ((uint32_t)msg[6] <<  8) |  (uint32_t)msg[7];
        /* If the query is for a specific group, only respond if
         * we are a member.  General queries (group 0) are answered
         * for every joined group. */
        mutex_lock(&igmp_lock);
        uint32_t now = now_ms();
        for (uint32_t i = 0; i < IGMP_MAX_GROUPS; i++) {
            if (!groups[i].used) continue;
            if (g.addr != 0 && g.addr != groups[i].group.addr) continue;
            if ((uint32_t)(now - groups[i].last_report_ms) < IGMP_REPORT_SUPPRESS_MS) continue;
            ipv4_addr_t grp = groups[i].group;
            mutex_unlock(&igmp_lock);
            send_igmp(iface, IGMP_TYPE_REPORT_V2, grp);
            stats.reports_sent++;
            mutex_lock(&igmp_lock);
            now = now_ms();
            groups[i].last_report_ms = now;
        }
        mutex_unlock(&igmp_lock);
    }
}

void igmp_tick(uint32_t now) {
    (void)now;
    /* Rate-limiting bookkeeping is inline in igmp_handle_report. */
}

const igmp_stats_t *igmp_get_stats(void) { return &stats; }

/* ========================================================================= */
/*  Multicast Routing & IGMP Snooping Implementation                          */
/* ========================================================================= */

/* ---- Multicast forwarder state ---- */
static mcast_forwarder_t mcast_fwd = {0, 1, 32, 0, 0};
static mcast_route_t     mcast_routes[MCAST_ROUTE_MAX];
static mcast_stats_t     mcast_stats;
static mutex_t           mcast_lock;

/* ---- IGMP Snooping state ---- */
static igmp_snoop_entry_t snoop_entries[IGMP_SNOOP_MAX];
static mutex_t             snoop_lock;

/* ---- Multicast route management ---- */

int mcast_route_add(ipv4_addr_t source, ipv4_addr_t group,
                     net_interface_t *in_iface,
                     net_interface_t **out_ifaces, uint8_t out_count) {
    mutex_lock(&mcast_lock);
    if (mcast_fwd.route_limit > 0) {
        uint32_t count = 0;
        for (uint32_t i = 0; i < MCAST_ROUTE_MAX; i++)
            if (mcast_routes[i].used) count++;
        if (count >= mcast_fwd.route_limit) { mutex_unlock(&mcast_lock); return -1; }
    }

    for (uint32_t i = 0; i < MCAST_ROUTE_MAX; i++) {
        if (!mcast_routes[i].used) {
            mcast_route_t *r = &mcast_routes[i];
            r->used     = 1;
            r->source   = source;
            r->group    = group;
            r->in_iface = in_iface;
            r->out_count = out_count > 8 ? 8 : out_count;
            for (uint8_t j = 0; j < r->out_count && out_ifaces; j++)
                r->out_ifaces[j] = out_ifaces[j];
            r->expiry_ms = timer_get_ticks() * 10U + MCAST_ROUTE_TIMEOUT_MS;
            r->packets_fwd = 0;
            r->bytes_fwd = 0;
            mcast_stats.routes_created++;
            mutex_unlock(&mcast_lock);
            return 0;
        }
    }
    mutex_unlock(&mcast_lock);
    return -1;
}

int mcast_route_del(ipv4_addr_t source, ipv4_addr_t group) {
    mutex_lock(&mcast_lock);
    for (uint32_t i = 0; i < MCAST_ROUTE_MAX; i++) {
        if (mcast_routes[i].used &&
            mcast_routes[i].source.addr == source.addr &&
            mcast_routes[i].group.addr == group.addr) {
            mcast_routes[i].used = 0;
            mutex_unlock(&mcast_lock);
            return 0;
        }
    }
    mutex_unlock(&mcast_lock);
    return -1;
}

mcast_route_t *mcast_route_lookup(ipv4_addr_t source, ipv4_addr_t group) {
    for (uint32_t i = 0; i < MCAST_ROUTE_MAX; i++) {
        if (mcast_routes[i].used &&
            mcast_routes[i].group.addr == group.addr) {
            /* Source-specific or any-source match */
            if (source.addr == 0 || mcast_routes[i].source.addr == 0 ||
                mcast_routes[i].source.addr == source.addr) {
                return &mcast_routes[i];
            }
        }
    }
    return NULL;
}

void mcast_route_tick(uint32_t now_ms) {
    mutex_lock(&mcast_lock);
    for (uint32_t i = 0; i < MCAST_ROUTE_MAX; i++) {
        if (mcast_routes[i].used && now_ms > mcast_routes[i].expiry_ms) {
            mcast_routes[i].used = 0;
            mcast_stats.routes_expired++;
        }
    }
    mutex_unlock(&mcast_lock);
}

void mcast_route_flush(void) {
    mutex_lock(&mcast_lock);
    memset(mcast_routes, 0, sizeof(mcast_routes));
    mutex_unlock(&mcast_lock);
}

/* ---- Multicast forwarding ---- */

int mcast_forward(net_buffer_t *buf, ipv4_addr_t source, ipv4_addr_t group,
                   uint8_t ttl) {
    if (!mcast_fwd.enabled || !buf) return -1;
    if (ttl <= mcast_fwd.ttl_threshold) {
        mcast_stats.ttl_drops++;
        return -1;
    }

    mcast_route_t *route = mcast_route_lookup(source, group);
    if (!route) {
        mcast_fwd.cache_misses++;

        /* RPF check: verify source interface */
        /* Simplified: use default interface for RPF */
        net_interface_t *rpf_iface = net_get_default_interface();
        if (!rpf_iface) {
            mcast_stats.rpf_failures++;
            return -1;
        }

        /* Create a (*,G) route if possible */
        ipv4_addr_t src_any = {0};
        net_interface_t *out = net_get_default_interface();
        if (mcast_route_add(src_any, group, rpf_iface, &out, 1) < 0) {
            mcast_stats.rpf_failures++;
            return -1;
        }
        route = mcast_route_lookup(source, group);
        if (!route) return -1;
    } else {
        mcast_fwd.cache_hits++;
    }

    /* Forward to each output interface */
    int sent = 0;
    for (uint8_t i = 0; i < route->out_count; i++) {
        if (route->out_ifaces[i] && route->out_ifaces[i]->up) {
            route->packets_fwd++;
            route->bytes_fwd += buf->len;
            mcast_stats.forward_packets++;
            mcast_stats.forward_bytes += buf->len;
            sent++;
        }
    }
    return sent;
}

/* ---- IGMP Snooping ---- */

int igmp_snoop_init(void) {
    memset(snoop_entries, 0, sizeof(snoop_entries));
    mutex_init(&snoop_lock);
    return 0;
}

int igmp_snoop_report(ipv4_addr_t group, uint8_t port) {
    mutex_lock(&snoop_lock);
    /* Update existing entry */
    for (uint32_t i = 0; i < IGMP_SNOOP_MAX; i++) {
        if (snoop_entries[i].used &&
            snoop_entries[i].group.addr == group.addr) {
            snoop_entries[i].port_map[port / 8] |= (1 << (port % 8));
            snoop_entries[i].timer_ms = timer_get_ticks() * 10U + 260000;  /* ~260 sec */
            mcast_stats.snoop_reports++;
            mutex_unlock(&snoop_lock);
            return 0;
        }
    }
    /* Create new entry */
    for (uint32_t i = 0; i < IGMP_SNOOP_MAX; i++) {
        if (!snoop_entries[i].used) {
            snoop_entries[i].used = 1;
            snoop_entries[i].group = group;
            memset(snoop_entries[i].port_map, 0, sizeof(snoop_entries[i].port_map));
            snoop_entries[i].port_map[port / 8] |= (1 << (port % 8));
            snoop_entries[i].timer_ms = timer_get_ticks() * 10U + 260000;
            snoop_entries[i].fast_leave = 0;
            mcast_stats.snoop_reports++;
            mutex_unlock(&snoop_lock);
            return 0;
        }
    }
    mutex_unlock(&snoop_lock);
    return -1;
}

int igmp_snoop_leave(ipv4_addr_t group, uint8_t port) {
    mutex_lock(&snoop_lock);
    for (uint32_t i = 0; i < IGMP_SNOOP_MAX; i++) {
        if (snoop_entries[i].used &&
            snoop_entries[i].group.addr == group.addr) {
            snoop_entries[i].port_map[port / 8] &= ~(1 << (port % 8));
            mcast_stats.snoop_leaves++;

            /* Check if any ports remain */
            uint8_t has_members = 0;
            for (int j = 0; j < 8; j++) {
                if (snoop_entries[i].port_map[j] != 0) { has_members = 1; break; }
            }
            if (!has_members || snoop_entries[i].fast_leave) {
                snoop_entries[i].used = 0;
            }
            mutex_unlock(&snoop_lock);
            return 0;
        }
    }
    mutex_unlock(&snoop_lock);
    return -1;
}

int igmp_snoop_is_member(ipv4_addr_t group, uint8_t port) {
    mutex_lock(&snoop_lock);
    for (uint32_t i = 0; i < IGMP_SNOOP_MAX; i++) {
        if (snoop_entries[i].used &&
            snoop_entries[i].group.addr == group.addr) {
            int result = (snoop_entries[i].port_map[port / 8] >> (port % 8)) & 1;
            mutex_unlock(&snoop_lock);
            return result;
        }
    }
    mutex_unlock(&snoop_lock);
    return 0;
}

void igmp_snoop_tick(uint32_t now_ms) {
    mutex_lock(&snoop_lock);
    for (uint32_t i = 0; i < IGMP_SNOOP_MAX; i++) {
        if (snoop_entries[i].used && now_ms > snoop_entries[i].timer_ms) {
            snoop_entries[i].used = 0;
        }
    }
    mutex_unlock(&snoop_lock);

    mcast_route_tick(now_ms);
}

void igmp_snoop_flush(void) {
    mutex_lock(&snoop_lock);
    memset(snoop_entries, 0, sizeof(snoop_entries));
    mutex_unlock(&snoop_lock);
}

/* ---- Multicast forwarder control ---- */

void mcast_forwarder_enable(int on) { mcast_fwd.enabled = on ? 1 : 0; }
int  mcast_forwarder_is_enabled(void) { return mcast_fwd.enabled; }
void mcast_forwarder_set_ttl(uint32_t ttl) { mcast_fwd.ttl_threshold = ttl; }

const mcast_stats_t *mcast_get_stats(void) { return &mcast_stats; }
