#include "tcpdump.h"
#include "net.h"
#include "ethernet.h"
#include "ip.h"
#include "tcp.h"
#include "udp.h"
#include "icmp.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"
#include "stdarg.h"
#include "timer.h"

#define ETH_HDR_LEN   14
#define IP_MIN_HDR    20
#define TCP_MIN_HDR   20
#define UDP_HDR_LEN   8
#define ICMP_HDR_LEN  8

static char *td_append(char *p, char *end, const char *fmt, ...)
{
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

static void td_fmt_ip(char *buf, uint32_t addr)
{
    sprintf(buf, "%u.%u.%u.%u",
            (addr >> 24) & 0xFF, (addr >> 16) & 0xFF,
            (addr >> 8) & 0xFF, addr & 0xFF);
}

static void td_fmt_mac(char *buf, const uint8_t *mac)
{
    sprintf(buf, "%02X:%02X:%02X:%02X:%02X:%02X",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static uint16_t td_ntohs(uint16_t n)
{
    return (n >> 8) | (n << 8);
}

static uint32_t td_ntohl(uint32_t n)
{
    return ((n >> 24) & 0xFF) |
           ((n >> 8)  & 0xFF00) |
           ((n << 8)  & 0xFF0000) |
           (n << 24);
}

void tcpdump_init(void)
{
}

tcpdump_t *tcpdump_create(void)
{
    tcpdump_t *td = (tcpdump_t *)kmalloc(sizeof(tcpdump_t));
    if (!td)
        return NULL;

    memset(td, 0, sizeof(tcpdump_t));
    td->head = 0;
    td->tail = 0;
    td->count = 0;
    td->running = 0;
    td->config.snaplen = TCPDUMP_MAX_PACKET;

    return td;
}

void tcpdump_destroy(tcpdump_t *td)
{
    if (!td)
        return;

    if (td->running)
        tcpdump_stop(td);

    kfree(td);
}

int tcpdump_start(tcpdump_t *td, tcpdump_config_t *config)
{
    if (!td || td->running)
        return -1;

    if (config) {
        memcpy(&td->config, config, sizeof(tcpdump_config_t));
    }

    td->running = 1;
    return 0;
}

void tcpdump_stop(tcpdump_t *td)
{
    if (!td || !td->running)
        return;

    td->running = 0;
}

int tcpdump_capture(tcpdump_t *td, const void *data, uint32_t len)
{
    if (!td || !td->running || !data || len == 0)
        return -1;

    if (len > TCPDUMP_MAX_PACKET)
        len = TCPDUMP_MAX_PACKET;

    if (td->config.snaplen > 0 && len > td->config.snaplen)
        len = td->config.snaplen;

    if (td->count >= TCPDUMP_MAX_HISTORY) {
        td->tail = (td->tail + 1) % TCPDUMP_MAX_HISTORY;
        td->total_dropped++;
    } else {
        td->count++;
    }

    tcpdump_packet_t *pkt = &td->packets[td->head];
    pkt->timestamp_ms = (uint32_t)timer_get_ticks();
    pkt->length = (uint16_t)len;
    memcpy(pkt->data, data, len);

    td->head = (td->head + 1) % TCPDUMP_MAX_HISTORY;
    td->total_captured++;

    return 0;
}

tcpdump_packet_t *tcpdump_next(tcpdump_t *td)
{
    if (!td || td->count == 0)
        return NULL;

    tcpdump_packet_t *pkt = &td->packets[td->tail];
    td->tail = (td->tail + 1) % TCPDUMP_MAX_HISTORY;
    td->count--;

    return pkt;
}

uint32_t tcpdump_get_count(tcpdump_t *td)
{
    if (!td)
        return 0;
    return td->count;
}

void tcpdump_clear(tcpdump_t *td)
{
    if (!td)
        return;
    td->head = 0;
    td->tail = 0;
    td->count = 0;
}

int tcpdump_format_hex(const uint8_t *data, uint32_t len, char *out, uint32_t cap)
{
    if (!data || !out || cap == 0)
        return -1;

    char *p = out;
    char *end = out + cap;

    for (uint32_t i = 0; i < len; i += 16) {
        p = td_append(p, end, "  0x%04x:  ", i);

        for (uint32_t j = 0; j < 16; j++) {
            if (i + j < len) {
                p = td_append(p, end, "%02x ", data[i + j]);
            } else {
                p = td_append(p, end, "   ");
            }
            if (j == 7)
                p = td_append(p, end, " ");
        }

        p = td_append(p, end, " |");

        for (uint32_t j = 0; j < 16 && i + j < len; j++) {
            uint8_t c = data[i + j];
            if (c >= 0x20 && c < 0x7F) {
                p = td_append(p, end, "%c", c);
            } else {
                p = td_append(p, end, ".");
            }
        }

        p = td_append(p, end, "|\n");
    }

    return (int)(p - out);
}

int tcpdump_parse_ethernet(const uint8_t *data, uint32_t len, char *out, uint32_t cap)
{
    if (!data || len < ETH_HDR_LEN || !out || cap == 0)
        return -1;

    char *p = out;
    char *end = out + cap;

    char dmac[32], smac[32];
    td_fmt_mac(dmac, data);
    td_fmt_mac(smac, data + 6);

    uint16_t ethertype = td_ntohs(*(uint16_t *)(data + 12));

    p = td_append(p, end, "ETHERNET: %s > %s, type 0x%04x, length %u\n",
                  smac, dmac, ethertype, len);

    return (int)(p - out);
}

int tcpdump_parse_ip(const uint8_t *data, uint32_t len, char *out, uint32_t cap)
{
    if (!data || len < IP_MIN_HDR || !out || cap == 0)
        return -1;

    char *p = out;
    char *end = out + cap;

    uint8_t version = (data[0] >> 4) & 0x0F;
    uint8_t ihl = data[0] & 0x0F;
    uint8_t tos = data[1];
    uint16_t total_len = td_ntohs(*(uint16_t *)(data + 2));
    uint16_t id = td_ntohs(*(uint16_t *)(data + 4));
    uint16_t flags_frag = td_ntohs(*(uint16_t *)(data + 6));
    uint8_t ttl = data[8];
    uint8_t protocol = data[9];
    uint16_t checksum = td_ntohs(*(uint16_t *)(data + 10));
    uint32_t src_ip = td_ntohl(*(uint32_t *)(data + 12));
    uint32_t dst_ip = td_ntohl(*(uint32_t *)(data + 16));

    char sip[32], dip[32];
    td_fmt_ip(sip, src_ip);
    td_fmt_ip(dip, dst_ip);

    uint8_t flags = (flags_frag >> 13) & 0x07;
    uint16_t frag_offset = flags_frag & 0x1FFF;

    const char *proto_name = "unknown";
    switch (protocol) {
        case 1:  proto_name = "ICMP"; break;
        case 2:  proto_name = "IGMP"; break;
        case 6:  proto_name = "TCP"; break;
        case 17: proto_name = "UDP"; break;
        case 41: proto_name = "IPv6"; break;
        case 47: proto_name = "GRE"; break;
        case 50: proto_name = "ESP"; break;
        case 51: proto_name = "AH"; break;
        case 89: proto_name = "OSPF"; break;
        case 132: proto_name = "SCTP"; break;
    }

    p = td_append(p, end,
        "IP: (tos 0x%02x, ttl %u, id %u, offset %u, flags [%c%c%c], "
        "proto %s (%u), length %u)\n"
        "    %s > %s\n",
        tos, ttl, id, frag_offset,
        (flags & 0x04) ? 'D' : '.',
        (flags & 0x02) ? 'M' : '.',
        (flags & 0x01) ? 'R' : '.',
        proto_name, protocol, total_len,
        sip, dip);

    (void)checksum;
    (void)version;
    (void)ihl;

    return (int)(p - out);
}

int tcpdump_parse_tcp(const uint8_t *data, uint32_t len, char *out, uint32_t cap)
{
    if (!data || len < TCP_MIN_HDR || !out || cap == 0)
        return -1;

    char *p = out;
    char *end = out + cap;

    uint16_t src_port = td_ntohs(*(uint16_t *)(data + 0));
    uint16_t dst_port = td_ntohs(*(uint16_t *)(data + 2));
    uint32_t seq = td_ntohl(*(uint32_t *)(data + 4));
    uint32_t ack = td_ntohl(*(uint32_t *)(data + 8));
    uint8_t data_off = (data[12] >> 4) & 0x0F;
    uint8_t flags = data[13];
    uint16_t window = td_ntohs(*(uint16_t *)(data + 14));
    uint16_t checksum = td_ntohs(*(uint16_t *)(data + 16));
    uint16_t urgent = td_ntohs(*(uint16_t *)(data + 18));

    char flag_buf[16];
    int fi = 0;
    if (flags & 0x20) flag_buf[fi++] = 'U';
    if (flags & 0x10) flag_buf[fi++] = 'A';
    if (flags & 0x08) flag_buf[fi++] = 'P';
    if (flags & 0x04) flag_buf[fi++] = 'R';
    if (flags & 0x02) flag_buf[fi++] = 'S';
    if (flags & 0x01) flag_buf[fi++] = 'F';
    if (fi == 0) flag_buf[fi++] = '.';
    flag_buf[fi] = '\0';

    uint32_t hdr_len = data_off * 4;
    uint32_t payload_len = len > hdr_len ? len - hdr_len : 0;

    p = td_append(p, end,
        "TCP: %u > %u: Flags [%s], seq %u, ack %u, win %u, "
        "length %u\n",
        src_port, dst_port, flag_buf, seq, ack, window, payload_len);

    (void)checksum;
    (void)urgent;

    return (int)(p - out);
}

int tcpdump_parse_udp(const uint8_t *data, uint32_t len, char *out, uint32_t cap)
{
    if (!data || len < UDP_HDR_LEN || !out || cap == 0)
        return -1;

    char *p = out;
    char *end = out + cap;

    uint16_t src_port = td_ntohs(*(uint16_t *)(data + 0));
    uint16_t dst_port = td_ntohs(*(uint16_t *)(data + 2));
    uint16_t length = td_ntohs(*(uint16_t *)(data + 4));
    uint16_t checksum = td_ntohs(*(uint16_t *)(data + 6));

    uint32_t payload_len = len > UDP_HDR_LEN ? len - UDP_HDR_LEN : 0;

    p = td_append(p, end,
        "UDP: %u > %u, length %u, payload %u\n",
        src_port, dst_port, length, payload_len);

    (void)checksum;

    return (int)(p - out);
}

int tcpdump_parse_icmp(const uint8_t *data, uint32_t len, char *out, uint32_t cap)
{
    if (!data || len < ICMP_HDR_LEN || !out || cap == 0)
        return -1;

    char *p = out;
    char *end = out + cap;

    uint8_t type = data[0];
    uint8_t code = data[1];
    uint16_t checksum = td_ntohs(*(uint16_t *)(data + 2));
    uint16_t id = td_ntohs(*(uint16_t *)(data + 4));
    uint16_t seq = td_ntohs(*(uint16_t *)(data + 6));

    const char *type_name = "unknown";
    switch (type) {
        case 0:  type_name = "Echo Reply"; break;
        case 3:  type_name = "Destination Unreachable"; break;
        case 4:  type_name = "Source Quench"; break;
        case 5:  type_name = "Redirect"; break;
        case 8:  type_name = "Echo Request"; break;
        case 9:  type_name = "Router Advertisement"; break;
        case 10: type_name = "Router Solicitation"; break;
        case 11: type_name = "Time Exceeded"; break;
        case 12: type_name = "Parameter Problem"; break;
        case 13: type_name = "Timestamp"; break;
        case 14: type_name = "Timestamp Reply"; break;
        case 15: type_name = "Information Request"; break;
        case 16: type_name = "Information Reply"; break;
        case 17: type_name = "Address Mask Request"; break;
        case 18: type_name = "Address Mask Reply"; break;
    }

    p = td_append(p, end,
        "ICMP: type %u (%s), code %u, id %u, seq %u, length %u\n",
        type, type_name, code, id, seq, len);

    (void)checksum;

    return (int)(p - out);
}

int tcpdump_format_packet(tcpdump_packet_t *pkt, tcpdump_config_t *cfg,
                           char *out, uint32_t cap)
{
    if (!pkt || !out || cap == 0)
        return -1;

    char *p = out;
    char *end = out + cap;

    uint32_t ms = pkt->timestamp_ms;
    uint32_t secs = ms / 1000;
    uint32_t msec = ms % 1000;

    p = td_append(p, end, "%02u:%02u:%02u.%03u ",
                  (secs / 3600) % 24, (secs / 60) % 60, secs % 60, msec);

    const uint8_t *data = pkt->data;
    uint32_t len = pkt->length;

    if (len < ETH_HDR_LEN) {
        p = td_append(p, end, "packet too short (%u bytes)\n", len);
        return (int)(p - out);
    }

    uint16_t ethertype = td_ntohs(*(uint16_t *)(data + 12));

    if (cfg && (cfg->options & TCPDUMP_OPT_VERBOSE)) {
        tcpdump_parse_ethernet(data, len, p, (uint32_t)(end - p));
        while (*p && p < end) p++;
    }

    if (ethertype == 0x0800 && len >= ETH_HDR_LEN + IP_MIN_HDR) {
        const uint8_t *ip_data = data + ETH_HDR_LEN;
        uint32_t ip_len = len - ETH_HDR_LEN;

        uint8_t ihl = (ip_data[0] & 0x0F) * 4;
        uint8_t protocol = ip_data[9];

        if (cfg && (cfg->options & TCPDUMP_OPT_VERBOSE)) {
            tcpdump_parse_ip(ip_data, ip_len, p, (uint32_t)(end - p));
            while (*p && p < end) p++;
        }

        if (protocol == 6 && ip_len >= ihl + TCP_MIN_HDR) {
            const uint8_t *tcp_data = ip_data + ihl;
            uint32_t tcp_len = ip_len - ihl;

            uint32_t src_ip = td_ntohl(*(uint32_t *)(ip_data + 12));
            uint32_t dst_ip = td_ntohl(*(uint32_t *)(ip_data + 16));
            uint16_t src_port = td_ntohs(*(uint16_t *)(tcp_data + 0));
            uint16_t dst_port = td_ntohs(*(uint16_t *)(tcp_data + 2));
            uint8_t flags = tcp_data[13];

            char sip[32], dip[32];
            td_fmt_ip(sip, src_ip);
            td_fmt_ip(dip, dst_ip);

            char flag_buf[16];
            int fi = 0;
            if (flags & 0x02) flag_buf[fi++] = 'S';
            if (flags & 0x10) flag_buf[fi++] = '.';
            if (flags & 0x01) flag_buf[fi++] = 'F';
            if (flags & 0x04) flag_buf[fi++] = 'R';
            if (flags & 0x08) flag_buf[fi++] = 'P';
            if (fi == 0) flag_buf[fi++] = '.';
            flag_buf[fi] = '\0';

            if (!(cfg && (cfg->options & TCPDUMP_OPT_VERBOSE))) {
                p = td_append(p, end, "IP %s.%u > %s.%u: Flags [%s], length %u\n",
                              sip, src_port, dip, dst_port, flag_buf,
                              tcp_len - (tcp_data[12] >> 4) * 4);
            } else {
                tcpdump_parse_tcp(tcp_data, tcp_len, p, (uint32_t)(end - p));
                while (*p && p < end) p++;
            }

            if (cfg && (cfg->options & TCPDUMP_OPT_HEX)) {
                tcpdump_format_hex(tcp_data, tcp_len, p, (uint32_t)(end - p));
                while (*p && p < end) p++;
            }
        }
        else if (protocol == 17 && ip_len >= ihl + UDP_HDR_LEN) {
            const uint8_t *udp_data = ip_data + ihl;
            uint32_t udp_len = ip_len - ihl;

            uint32_t src_ip = td_ntohl(*(uint32_t *)(ip_data + 12));
            uint32_t dst_ip = td_ntohl(*(uint32_t *)(ip_data + 16));
            uint16_t src_port = td_ntohs(*(uint16_t *)(udp_data + 0));
            uint16_t dst_port = td_ntohs(*(uint16_t *)(udp_data + 2));

            char sip[32], dip[32];
            td_fmt_ip(sip, src_ip);
            td_fmt_ip(dip, dst_ip);

            if (!(cfg && (cfg->options & TCPDUMP_OPT_VERBOSE))) {
                p = td_append(p, end, "IP %s.%u > %s.%u: UDP, length %u\n",
                              sip, src_port, dip, dst_port, udp_len - UDP_HDR_LEN);
            } else {
                tcpdump_parse_udp(udp_data, udp_len, p, (uint32_t)(end - p));
                while (*p && p < end) p++;
            }

            if (cfg && (cfg->options & TCPDUMP_OPT_HEX)) {
                tcpdump_format_hex(udp_data, udp_len, p, (uint32_t)(end - p));
                while (*p && p < end) p++;
            }
        }
        else if (protocol == 1 && ip_len >= ihl + ICMP_HDR_LEN) {
            const uint8_t *icmp_data = ip_data + ihl;
            uint32_t icmp_len = ip_len - ihl;

            if (!(cfg && (cfg->options & TCPDUMP_OPT_VERBOSE))) {
                uint32_t src_ip = td_ntohl(*(uint32_t *)(ip_data + 12));
                uint32_t dst_ip = td_ntohl(*(uint32_t *)(ip_data + 16));
                char sip[32], dip[32];
                td_fmt_ip(sip, src_ip);
                td_fmt_ip(dip, dst_ip);

                uint8_t type = icmp_data[0];
                const char *type_name = "unknown";
                switch (type) {
                    case 0:  type_name = "echo reply"; break;
                    case 8:  type_name = "echo request"; break;
                    case 11: type_name = "time exceeded"; break;
                    case 3:  type_name = "dest unreachable"; break;
                }

                p = td_append(p, end, "IP %s > %s: ICMP %s, length %u\n",
                              sip, dip, type_name, icmp_len);
            } else {
                tcpdump_parse_icmp(icmp_data, icmp_len, p, (uint32_t)(end - p));
                while (*p && p < end) p++;
            }

            if (cfg && (cfg->options & TCPDUMP_OPT_HEX)) {
                tcpdump_format_hex(icmp_data, icmp_len, p, (uint32_t)(end - p));
                while (*p && p < end) p++;
            }
        }
    }
    else if (ethertype == 0x0806) {
        p = td_append(p, end, "ARP, length %u\n", len);
    }
    else {
        p = td_append(p, end, "ethertype 0x%04x, length %u\n", ethertype, len);
    }

    return (int)(p - out);
}
