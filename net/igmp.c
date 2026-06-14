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
