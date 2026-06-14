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
