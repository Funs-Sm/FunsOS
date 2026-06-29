#include "netstat.h"
#include "net.h"
#include "tcp.h"
#include "udp.h"
#include "ip.h"
#include "arp.h"
#include "icmp.h"
#include "route.h"
#include "igmp.h"
#include "raw_sock.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"
#include "stdarg.h"

static char *append(char *p, char *end, const char *fmt, ...) {
    if (!p || p >= end) return p;
    va_list ap;
    va_start(ap, fmt);
    int avail = (int)(end - p);
    int n = vsnprintf(p, (uint32_t)avail, fmt, ap);
    va_end(ap);
    if (n < 0) return p;
    if (n >= avail) return end;
    return p + n;
}

static void fmt_ip(char *buf, uint32_t addr) {
    sprintf(buf, "%u.%u.%u.%u",
            (addr >> 24) & 0xFF, (addr >> 16) & 0xFF,
            (addr >> 8) & 0xFF, addr & 0xFF);
}

int netstat_format(int kind, char *out, uint32_t cap) {
    if (!out || cap == 0) return -1;
    char *p = out;
    char *end = out + cap;

    if (kind == NETSTAT_TCP) {
        p = append(p, end,
            "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid\n");
        uint32_t sc = 0;
        const tcp_socket_t *s = tcp_get_sockets(&sc);
        for (uint32_t i = 0; i < sc && s; i++, s = s->next_all) {
            char lip[32], rip[32];
            fmt_ip(lip, s->local_ip.addr);
            fmt_ip(rip, s->remote_ip.addr);
            p = append(p, end, "%4u: %08X:%04X %08X:%04X %02X %08X:%08X 00:00000000 00000000     0\n",
                       i, s->local_ip.addr, s->local_port,
                       s->remote_ip.addr, s->remote_port,
                       s->state, s->snd_una, s->rcv_nxt);
        }
        return (int)(p - out);
    }

    if (kind == NETSTAT_UDP) {
        p = append(p, end,
            "  sl  local_address rem_address   st tx_queue rx_queue tr tm->when retrnsmt   uid\n");
        uint32_t sc = 0;
        const udp_socket_t *u = udp_get_sockets(&sc);
        for (uint32_t i = 0; i < sc && u; i++, u = u->next) {
            p = append(p, end, "%4u: %08X:%04X %08X:%04X %02X %08X:%08X 00:00000000 00000000     0\n",
                       i, 0, u->local_port, u->remote_ip.addr, u->remote_port, 7, 0, 0);
        }
        return (int)(p - out);
    }

    if (kind == NETSTAT_ARP) {
        p = append(p, end,
            "IP address       HW address     Flags     Mask    Device\n");
        uint32_t cc = 0;
        const arp_entry_t *e = arp_get_entries(&cc);
        for (uint32_t i = 0; i < cc; i++) {
            if (!e[i].valid) continue;
            char ip[32]; fmt_ip(ip, e[i].ip.addr);
            p = append(p, end, "%-15s  %02X:%02X:%02X:%02X:%02X:%02X  0x%02X       *      eth0\n",
                       ip,
                       e[i].mac.bytes[0], e[i].mac.bytes[1], e[i].mac.bytes[2],
                       e[i].mac.bytes[3], e[i].mac.bytes[4], e[i].mac.bytes[5],
                       e[i].static_entry ? 6 : 2);
        }
        return (int)(p - out);
    }

    if (kind == NETSTAT_DEV) {
        p = append(p, end,
            "Inter-|   Receive                            |  Transmit\n"
            " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n");
        for (uint32_t i = 0; i < net_get_interface_count(); i++) {
            net_interface_t *n = net_get_interface(i);
            if (!n) continue;
            p = append(p, end,
                "%5s: %8u %8u %4u %4u %4u %4u %10u %10u %8u %8u %4u %4u %4u %4u %7u %10u\n",
                n->name, n->rx_bytes, n->rx_packets, n->rx_errors, 0, 0, 0, 0, 0,
                n->tx_bytes, n->tx_packets, n->tx_errors, 0, 0, 0, 0, 0);
        }
        return (int)(p - out);
    }

    if (kind == NETSTAT_ROUTE) {
        p = append(p, end,
            "Iface\tDestination\tGateway\tFlags\tRefCnt\tUse\tMetric\tMask\tMTU\tWindow\tIRTT\n");
        for (uint32_t i = 0; i < net_get_interface_count(); i++) {
            net_interface_t *n = net_get_interface(i);
            if (!n) continue;
            char dest[32], gw[32], mask[32];
            fmt_ip(dest, n->ip.addr);
            fmt_ip(gw, n->gateway.addr);
            fmt_ip(mask, n->mask.addr);
            p = append(p, end, "%s\t%s\t%s\t%04X\t0\t0\t0\t%s\t%u\t0\t0\n",
                       n->name, dest, gw, n->flags | 3, mask, n->mtu);
        }
        return (int)(p - out);
    }

    if (kind == NETSTAT_SOCKSTAT) {
        const tcp_stats_t *ts = tcp_get_stats();
        const udp_stats_t *us = udp_get_stats();
        const ip_stats_t  *is = ip_get_stats();
        const icmp_stats_t *ics = icmp_get_stats();
        const arp_stats_t *as = arp_get_stats();
        const igmp_stats_t *igs = igmp_get_stats();
        p = append(p, end,
            "sockets: used %u\n"
            "TCP:   inuse %u active %u passive %u established %u\n"
            "       segs_sent %u segs_rcvd %u bytes_sent %u bytes_rcvd %u\n"
            "       retrans %u bad %u resets_sent %u resets_rcvd %u\n"
            "UDP:   inuse %u datagrams_sent %u datagrams_rcvd %u\n"
            "       bytes_sent %u bytes_rcvd %u csum_err %u port_in_use %u\n"
            "IP:    sent %u rcvd %u frag_sent %u frag_rcvd %u reassembled %u\n"
            "       dropped %u no_route %u csum_err %u ttl_expired %u\n"
            "ICMP:  echo_req_sent %u echo_req_rcvd %u echo_rep_sent %u echo_rep_rcvd %u\n"
            "       dest_unreach_sent %u dest_unreach_rcvd %u\n"
            "       time_exceeded_sent %u time_exceeded_rcvd %u\n"
            "ARP:   req_sent %u rep_sent %u req_rcvd %u rep_rcvd %u\n"
            "       hits %u misses %u added %u expired %u\n"
            "IGMP:  reports %u queries %u leaves %u csum_err %u\n",
            raw_sock_count(), 0, ts->active_opens, ts->passive_opens, ts->established,
            ts->segs_sent, ts->segs_rcvd, ts->bytes_sent, ts->bytes_rcvd,
            ts->retransmits, ts->bad_segs, ts->resets_sent, ts->resets_rcvd,
            0, us->datagrams_sent, us->datagrams_rcvd, us->bytes_sent, us->bytes_rcvd,
            us->checksum_errors, us->port_in_use,
            is->packets_sent, is->packets_rcvd, is->fragments_sent, is->fragments_rcvd,
            is->reassembled, is->dropped, is->no_route, is->checksum_errors, is->ttl_expired,
            ics->echo_requests_sent, ics->echo_requests_rcvd,
            ics->echo_replies_sent, ics->echo_replies_rcvd,
            ics->dest_unreach_sent, ics->dest_unreach_rcvd,
            ics->time_exceeded_sent, ics->time_exceeded_rcvd,
            as->requests_sent, as->replies_sent, as->requests_rcvd, as->replies_rcvd,
            as->cache_hits, as->cache_misses, as->entries_added, as->entries_expired,
            igs->reports_sent, igs->queries_rcvd, igs->leaves_sent, igs->checksum_errors);
        return (int)(p - out);
    }

    if (kind == NETSTAT_IF_INET6) {
        p = append(p, end, "(no IPv6 interfaces)\n");
        return (int)(p - out);
    }

    if (kind == NETSTAT_TCP_DETAIL) {
        p = append(p, end,
            "Proto Recv-Q Send-Q Local Address           Foreign Address         State\n");
        uint32_t sc = 0;
        const tcp_socket_t *s = tcp_get_sockets(&sc);
        for (uint32_t i = 0; i < sc && s; i++, s = s->next_all) {
            char lip[32], rip[32];
            fmt_ip(lip, s->local_ip.addr);
            fmt_ip(rip, s->remote_ip.addr);
            const char *state_name = tcp_state_name(s->state);
            uint32_t recv_q = s->recv_buf_len;
            uint32_t send_q = s->send_buf_len;
            if (s->state == TCP_STATE_LISTEN) {
                p = append(p, end, "tcp  %6u %6u %s:%-5hu %-23s %s\n",
                    recv_q, send_q, "0.0.0.0", s->local_port, "0.0.0.0:0", state_name);
            } else {
                p = append(p, end, "tcp  %6u %6u %s:%-5hu %s:%-5hu %s\n",
                    recv_q, send_q, lip, s->local_port, rip, s->remote_port, state_name);
            }
        }
        return (int)(p - out);
    }

    if (kind == NETSTAT_UDP_DETAIL) {
        p = append(p, end,
            "Proto Recv-Q Send-Q Local Address           Foreign Address         State\n");
        uint32_t sc = 0;
        const udp_socket_t *u = udp_get_sockets(&sc);
        for (uint32_t i = 0; i < sc && u; i++, u = u->next) {
            char rip[32];
            fmt_ip(rip, u->remote_ip.addr);
            p = append(p, end, "udp  %6u %6u %s:%-5hu %s:%-5hu %s\n",
                u->recv_len, 0u, "0.0.0.0", u->local_port,
                u->remote_port ? rip : "0.0.0.0", u->remote_port,
                u->bound ? "ESTAB" : "UNCONN");
        }
        return (int)(p - out);
    }

    if (kind == NETSTAT_ROUTE_FULL) {
        p = append(p, end,
            "Kernel IP routing table\n"
            "Destination     Gateway         Genmask         Flags Metric Ref    Use Iface\n");
        uint32_t count = 0;
        const route_entry_t *routes = route_get_all(&count);
        for (uint32_t i = 0; i < count; i++) {
            if (!routes[i].iface || !(routes[i].flags & ROUTE_FLAG_UP))
                continue;
            char dest[32], gw[32], mask[32];
            fmt_ip(dest, routes[i].dest.addr);
            fmt_ip(gw, routes[i].gateway.addr);
            fmt_ip(mask, routes[i].mask.addr);
            char flags[8] = {0};
            int fi = 0;
            if (routes[i].flags & ROUTE_FLAG_UP)       flags[fi++] = 'U';
            if (routes[i].flags & ROUTE_FLAG_GATEWAY)  flags[fi++] = 'G';
            if (routes[i].flags & ROUTE_FLAG_STATIC)   flags[fi++] = 'S';
            if (routes[i].flags & ROUTE_FLAG_BLACKHOLE) flags[fi++] = '!';
            if (fi == 0) flags[fi++] = 'U';
            p = append(p, end, "%-15s %-15s %-15s %-5s %6u %3u %6u %s\n",
                dest, gw, mask, flags, routes[i].metric, 0u, 0u,
                routes[i].iface ? routes[i].iface->name : "?");
        }
        if (count == 0) {
            for (uint32_t i = 0; i < net_get_interface_count(); i++) {
                net_interface_t *n = net_get_interface(i);
                if (!n || !n->up) continue;
                char dest[32], gw[32], mask[32];
                fmt_ip(dest, n->ip.addr & n->mask.addr);
                fmt_ip(gw, n->gateway.addr);
                fmt_ip(mask, n->mask.addr);
                p = append(p, end, "%-15s %-15s %-15s %-5s %6u %3u %6u %s\n",
                    dest, "0.0.0.0", mask, "U", 0u, 0u, 0u, n->name);
                if (n->gateway.addr) {
                    p = append(p, end, "%-15s %-15s %-15s %-5s %6u %3u %6u %s\n",
                        "0.0.0.0", gw, "0.0.0.0", "UG", 0u, 0u, 0u, n->name);
                }
            }
        }
        return (int)(p - out);
    }

    if (kind == NETSTAT_IF_STATS) {
        p = append(p, end,
            "Kernel Interface table\n"
            "%-10s %-15s %-10s %-12s %-12s %-10s %-10s\n"
            "%-10s %-15s %-10s %-12s %-12s %-10s %-10s\n",
            "Iface", "MTU", "State", "RX-OK", "RX-ERR", "RX-DRP", "RX-OVR",
            "", "", "", "TX-OK", "TX-ERR", "TX-DRP", "TX-OVR");
        for (uint32_t i = 0; i < net_get_interface_count(); i++) {
            net_interface_t *n = net_get_interface(i);
            if (!n) continue;
            const char *state = n->up ? "UP" : "DOWN";
            p = append(p, end, "%-10s %-15u %-10s %-12u %-12u %-10u %-10u\n",
                n->name, n->mtu, state, n->rx_packets, n->rx_errors, 0u, 0u);
            p = append(p, end, "%-10s %-15s %-10s %-12u %-12u %-10u %-10u\n",
                "", "", "", n->tx_packets, n->tx_errors, 0u, 0u);
            char ip[32], mask[32];
            fmt_ip(ip, n->ip.addr);
            fmt_ip(mask, n->mask.addr);
            p = append(p, end, "  inet %s  netmask %s\n", ip, mask);
        }
        return (int)(p - out);
    }

    if (kind == NETSTAT_ALL) {
        int len;
        p = append(p, end, "=== Active Internet connections ===\n");
        len = netstat_format(NETSTAT_TCP_DETAIL, p, (uint32_t)(end - p));
        if (len > 0) p += len;
        len = netstat_format(NETSTAT_UDP_DETAIL, p, (uint32_t)(end - p));
        if (len > 0) p += len;
        p = append(p, end, "\n=== Kernel IP routing table ===\n");
        len = netstat_format(NETSTAT_ROUTE_FULL, p, (uint32_t)(end - p));
        if (len > 0) p += len;
        p = append(p, end, "\n=== Kernel Interface statistics ===\n");
        len = netstat_format(NETSTAT_IF_STATS, p, (uint32_t)(end - p));
        if (len > 0) p += len;
        return (int)(p - out);
    }

    return -1;
}

#ifdef HAS_PROCFS
#include "procfs.h"
extern int32_t procfs_create_named_entry(const char *name, uint32_t mode,
    int32_t (*read)(char *, uint32_t, uint32_t));
static int32_t tcp_read(char *b, uint32_t off, uint32_t cnt) {
    static char buf[2048];
    static int len = 0;
    if (off == 0) { len = netstat_format(NETSTAT_TCP, buf, sizeof(buf)); if (len < 0) return 0; }
    if (off >= (uint32_t)len) return 0;
    uint32_t avail = (uint32_t)len - off;
    if (avail > cnt) avail = cnt;
    memcpy(b, buf + off, avail);
    return (int32_t)avail;
}
/* placeholder; actual wiring requires the procfs to support /proc/net */
#endif
