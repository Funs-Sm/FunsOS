#include "ss.h"
#include "net.h"
#include "tcp.h"
#include "udp.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"
#include "stdarg.h"

static char *ss_append(char *p, char *end, const char *fmt, ...)
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

static void ss_fmt_ip(char *buf, uint32_t addr, int numeric)
{
    (void)numeric;
    sprintf(buf, "%u.%u.%u.%u",
            (addr >> 24) & 0xFF, (addr >> 16) & 0xFF,
            (addr >> 8) & 0xFF, addr & 0xFF);
}

static const char *ss_port_name(uint16_t port, int numeric)
{
    if (numeric) {
        static char buf[16];
        snprintf(buf, sizeof(buf), "%u", port);
        return buf;
    }

    switch (port) {
        case 20:   return "ftp-data";
        case 21:   return "ftp";
        case 22:   return "ssh";
        case 23:   return "telnet";
        case 25:   return "smtp";
        case 53:   return "domain";
        case 67:   return "bootps";
        case 68:   return "bootpc";
        case 69:   return "tftp";
        case 80:   return "http";
        case 110:  return "pop3";
        case 111:  return "sunrpc";
        case 119:  return "nntp";
        case 123:  return "ntp";
        case 135:  return "epmap";
        case 137:  return "netbios-ns";
        case 138:  return "netbios-dgm";
        case 139:  return "netbios-ssn";
        case 143:  return "imap";
        case 161:  return "snmp";
        case 162:  return "snmptrap";
        case 179:  return "bgp";
        case 389:  return "ldap";
        case 443:  return "https";
        case 445:  return "microsoft-ds";
        case 465:  return "imaps";
        case 514:  return "shell";
        case 515:  return "printer";
        case 587:  return "submission";
        case 631:  return "ipp";
        case 636:  return "ldaps";
        case 873:  return "rsync";
        case 990:  return "ftps";
        case 993:  return "imaps";
        case 995:  return "pop3s";
        case 1080: return "socks";
        case 1433: return "ms-sql-s";
        case 1434: return "ms-sql-m";
        case 1521: return "oracle";
        case 2049: return "nfs";
        case 3306: return "mysql";
        case 3389: return "ms-wbt-server";
        case 5432: return "postgresql";
        case 5900: return "vnc";
        case 6379: return "redis";
        case 8080: return "http-proxy";
        case 8443: return "https-alt";
        default: {
            static char buf[16];
            snprintf(buf, sizeof(buf), "%u", port);
            return buf;
        }
    }
}

static const char *ss_tcp_state_name(uint8_t state)
{
    switch (state) {
        case 0:  return "CLOSED";
        case 1:  return "LISTEN";
        case 2:  return "SYN-SENT";
        case 3:  return "SYN-RECV";
        case 4:  return "ESTAB";
        case 5:  return "FIN-WAIT-1";
        case 6:  return "FIN-WAIT-2";
        case 7:  return "CLOSE-WAIT";
        case 8:  return "CLOSING";
        case 9:  return "LAST-ACK";
        case 10: return "TIME-WAIT";
        default: return "UNKNOWN";
    }
}

void ss_init(void)
{
}

int ss_parse_args(int argc, char *argv[], ss_config_t *config)
{
    if (!config)
        return -1;

    memset(config, 0, sizeof(ss_config_t));
    config->options = SS_DEFAULT_OPT;

    for (int i = 1; i < argc; i++) {
        if (!argv[i]) continue;

        if (argv[i][0] == '-') {
            for (int j = 1; argv[i][j]; j++) {
                switch (argv[i][j]) {
                    case 't':
                        config->options |= SS_OPT_TCP;
                        config->options &= ~SS_OPT_UDP;
                        break;
                    case 'u':
                        config->options |= SS_OPT_UDP;
                        config->options &= ~SS_OPT_TCP;
                        break;
                    case 'l':
                        config->options |= SS_OPT_LISTEN;
                        break;
                    case 'n':
                        config->options |= SS_OPT_NUMERIC;
                        break;
                    case 'a':
                        config->options |= SS_OPT_ALL;
                        break;
                    case 'h':
                    case '?':
                        return 1;
                    default:
                        return -1;
                }
            }
        }
    }

    if ((config->options & (SS_OPT_TCP | SS_OPT_UDP)) == 0)
        config->options |= SS_DEFAULT_OPT;

    return 0;
}

int ss_format_summary(char *out, uint32_t cap)
{
    if (!out || cap == 0)
        return -1;

    char *p = out;
    char *end = out + cap;

    const tcp_stats_t *ts = tcp_get_stats();
    const udp_stats_t *us = udp_get_stats();

    uint32_t tcp_count = 0;
    uint32_t udp_count = 0;
    const tcp_socket_t *tcp_socks = tcp_get_sockets(&tcp_count);
    const udp_socket_t *udp_socks = udp_get_sockets(&udp_count);

    uint32_t tcp_estab = 0, tcp_listen = 0, tcp_timewait = 0;
    for (uint32_t i = 0; i < tcp_count && tcp_socks; i++, tcp_socks = tcp_socks->next_all) {
        switch (tcp_socks->state) {
            case 1:  tcp_listen++; break;
            case 4:  tcp_estab++; break;
            case 10: tcp_timewait++; break;
        }
    }

    p = ss_append(p, end,
        "Total: %u (estab %u, closed %u, orphaned %u, synrecv %u, timewait %u)\\n"
        "\n"
        "Transport Total     IP        IPv6\n"
        "*         %-9u %-9u %-9u\n"
        "RAW       0         0         0\n"
        "UDP       %-9u %-9u %-9u\n"
        "TCP       %-9u %-9u %-9u\n"
        "INET      %-9u %-9u %-9u\n"
        "FRAG      0         0         0\n",
        tcp_count + udp_count,
        tcp_estab, 0u, 0u, 0u, tcp_timewait,
        tcp_count + udp_count, tcp_count + udp_count, 0u,
        udp_count, udp_count, 0u,
        tcp_count, tcp_count, 0u,
        tcp_count + udp_count, tcp_count + udp_count, 0u);

    (void)ts;
    (void)us;

    return (int)(p - out);
}

int ss_format(ss_config_t *config, char *out, uint32_t cap)
{
    if (!config || !out || cap == 0)
        return -1;

    char *p = out;
    char *end = out + cap;
    int numeric = (config->options & SS_OPT_NUMERIC) ? 1 : 0;
    int listen_only = (config->options & SS_OPT_LISTEN) ? 1 : 0;

    p = ss_append(p, end,
        "Netid State      Recv-Q Send-Q    Local Address:Port          Peer Address:Port\n");

    if (config->options & SS_OPT_TCP) {
        uint32_t sc = 0;
        const tcp_socket_t *s = tcp_get_sockets(&sc);

        for (uint32_t i = 0; i < sc && s; i++, s = s->next_all) {
            const char *state = ss_tcp_state_name((uint8_t)s->state);

            if (listen_only && s->state != 1)
                continue;

            if (config->sport_filter && s->local_port != config->sport_filter)
                continue;
            if (config->dport_filter && s->remote_port != config->dport_filter)
                continue;

            char lip[32], rip[32];
            ss_fmt_ip(lip, s->local_ip.addr, numeric);
            ss_fmt_ip(rip, s->remote_ip.addr, numeric);

            const char *lport = ss_port_name(s->local_port, numeric);
            const char *rport = ss_port_name(s->remote_port, numeric);

            if (s->state == 1) {
                p = ss_append(p, end,
                    "tcp   %-10s %6u %6u    %s:%-21s %s:%s\n",
                    state, s->recv_buf_len, s->send_buf_len,
                    lip, lport, "0.0.0.0", "*");
            } else {
                p = ss_append(p, end,
                    "tcp   %-10s %6u %6u    %s:%-21s %s:%s\n",
                    state, s->recv_buf_len, s->send_buf_len,
                    lip, lport, rip, rport);
            }
        }
    }

    if (config->options & SS_OPT_UDP) {
        uint32_t sc = 0;
        const udp_socket_t *u = udp_get_sockets(&sc);

        for (uint32_t i = 0; i < sc && u; i++, u = u->next) {
            if (config->sport_filter && u->local_port != config->sport_filter)
                continue;
            if (config->dport_filter && u->remote_port != config->dport_filter)
                continue;

            char rip[32];
            ss_fmt_ip(rip, u->remote_ip.addr, numeric);

            const char *lport = ss_port_name(u->local_port, numeric);
            const char *rport = u->remote_port ?
                                ss_port_name(u->remote_port, numeric) : "*";

            const char *state = u->bound ? "ESTAB" : "UNCONN";

            p = ss_append(p, end,
                "udp   %-10s %6u %6u    %s:%-21s %s:%s\n",
                state, u->recv_len, 0u,
                "0.0.0.0", lport,
                u->remote_port ? rip : "0.0.0.0", rport);
        }
    }

    return (int)(p - out);
}
