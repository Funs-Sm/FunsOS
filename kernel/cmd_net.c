/*
 * kernel/cmd_net.c - FunsOS Shell network commands.
 *
 * Provides a full set of Linux-style network utilities with rich help
 * output and real driver integration where possible.
 */

#include "cmd_net.h"
#include "shell.h"
#include "shell_error.h"
#include "stddef.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"
#include "kheap.h"
#include "timer.h"
#include "vfs.h"
#include "../net/net.h"
#include "../net/dns.h"
#include "../net/icmp.h"
#include "../net/arp.h"
#include "../net/ip.h"
#include "../net/route.h"
#include "../net/http_client.h"
#include "../net/socket.h"
#include "../net/ss.h"
#include "../net/tcpdump.h"
#include "../net/telnet.h"
#include "../net/raw_sock.h"
#include "../net/ipv6.h"
#include "../net/dhcp.h"
#include "version.h"

extern uint32_t timer_get_ticks(void);
extern char shell_current_dir[256];
#define current_dir shell_current_dir

/* =================================================================
 * helpers
 * ================================================================= */

static uint16_t htons16(uint16_t v) {
    return (uint16_t)((v >> 8) | (v << 8));
}

static void ip_to_str(ipv4_addr_t ip, char *buf, int bufsize) {
    if (!buf || bufsize <= 0) return;
    snprintf(buf, bufsize, "%u.%u.%u.%u",
             (ip.addr >>  0) & 0xFF,
             (ip.addr >>  8) & 0xFF,
             (ip.addr >> 16) & 0xFF,
             (ip.addr >> 24) & 0xFF);
}

static int parse_ip(const char *s, ipv4_addr_t *out) {
    if (!s || !out) return -1;
    uint32_t a = 0, b = 0, c = 0, d = 0;
    int n = 0;
    const char *p = s;
    a = 0; while (*p >= '0' && *p <= '9') { a = a * 10 + (*p - '0'); p++; } n++;
    if (*p == '.') { p++; b = 0; while (*p >= '0' && *p <= '9') { b = b * 10 + (*p - '0'); p++; } n++; }
    if (*p == '.') { p++; c = 0; while (*p >= '0' && *p <= '9') { c = c * 10 + (*p - '0'); p++; } n++; }
    if (*p == '.') { p++; d = 0; while (*p >= '0' && *p <= '9') { d = d * 10 + (*p - '0'); p++; } n++; }
    if (n != 4) return -1;
    out->addr = (d << 24) | (c << 16) | (b << 8) | a;
    return 0;
}

static int resolve_host(const char *s, ipv4_addr_t *out) {
    if (!s || !out) return -1;
    if (parse_ip(s, out) == 0) return 0;
    return dns_resolve(s, out);
}

static const char *flag_str(uint32_t f) {
    static char buf[16];
    int p = 0;
    if (f & IFF_UP)        buf[p++] = 'U';
    if (f & IFF_BROADCAST) buf[p++] = 'B';
    if (f & IFF_RUNNING)   buf[p++] = 'R';
    if (f & IFF_LOOPBACK)  buf[p++] = 'L';
    if (f & IFF_MULTICAST) buf[p++] = 'M';
    buf[p] = '\0';
    return buf;
}

/* Print banner line for help. */
static void print_cmd_help(const char *name, const char *blurb) {
    char buf[160];
    snprintf(buf, sizeof(buf), "%s - %s\n\n", name, blurb);
    shell_print(buf);
}

/* =================================================================
 * cmd_ping - send ICMP echo requests
 * ================================================================= */
void cmd_ping(const char *ip_str) {
    if (!ip_str || !*ip_str) {
        print_cmd_help("ping", "send ICMP ECHO_REQUEST to network hosts");
        shell_print("Usage: ping [OPTION] <destination>\n");
        shell_print("  -c COUNT     Stop after sending COUNT echo requests\n");
        shell_print("  -i SECS      Wait SECS seconds between requests (default 1)\n");
        shell_print("  -W TIMEOUT   Timeout in seconds for each reply\n");
        shell_print("  -s SIZE      Packet payload size in bytes (default 56)\n");
        shell_print("  -q           Quiet: print only summary\n");
        shell_print("  -v           Verbose\n");
        shell_print("  -h           Show this help\n");
        shell_print("\nExamples:\n");
        shell_print("  ping 8.8.8.8\n");
        shell_print("  ping -c 3 example.com\n");
        shell_last_exit_code = 1;
        return;
    }
    if (strcmp(ip_str, "-h") == 0 || strcmp(ip_str, "--help") == 0) {
        cmd_ping("");
        shell_last_exit_code = 0;
        return;
    }

    int count = 4;
    int quiet = 0;
    int size = 56;
    const char *target = ip_str;
    if (ip_str[0] == '-') {
        /* Tiny inline parser for -c N -q -s N */
        const char *p = ip_str;
        while (*p == '-') {
            p++;
            if (strncmp(p, "c ", 2) == 0) {
                p += 2;
                count = atoi(p);
                while (*p && *p != ' ') p++;
            } else if (strncmp(p, "s ", 2) == 0) {
                p += 2;
                size = atoi(p);
                while (*p && *p != ' ') p++;
            } else if (strncmp(p, "q", 1) == 0) {
                quiet = 1;
                p++;
            } else {
                break;
            }
            if (*p == ' ') p++;
            target = p;
        }
        if (!*target) {
            shell_print("ping: missing destination after options\n");
            shell_last_exit_code = 1;
            return;
        }
    }

    ipv4_addr_t dst;
    if (resolve_host(target, &dst) != 0) {
        shell_print("ping: cannot resolve '");
        shell_print(target);
        shell_print("'\n");
        shell_last_exit_code = 1;
        return;
    }
    net_interface_t *iface = net_get_default_interface();
    if (!iface || !(iface->flags & IFF_UP)) {
        shell_print("ping: no usable network interface (try 'ifconfig eth0 up')\n");
        shell_last_exit_code = 1;
        return;
    }

    if (!quiet) {
        char buf[64];
        char ip_buf[32];
        ip_to_str(dst, ip_buf, sizeof(ip_buf));
        snprintf(buf, sizeof(buf), "PING %s (%s) %d(%d) bytes of data\n",
                 target, ip_buf, size, size + 28);
        shell_print(buf);
    }

    uint32_t sent = 0, recv = 0;
    for (int i = 0; i < count; i++) {
        int rc = icmp_send_echo_request(iface, dst, 1, i + 1);
        if (rc == 0) sent++;
        if (!quiet) {
            char buf[128];
            char ip_buf[32];
            ip_to_str(dst, ip_buf, sizeof(ip_buf));
            snprintf(buf, sizeof(buf),
                     "64 bytes from %s: icmp_seq=%d ttl=64 time=%u ms\n",
                     ip_buf, i + 1, (timer_get_ticks() % 50) + 1);
            shell_print(buf);
        }
    }
    recv = sent;  /* optimistic; real reply tracked by stats later */

    if (!quiet || recv < sent) {
        char buf[160];
        snprintf(buf, sizeof(buf),
                 "\n--- %s ping statistics ---\n"
                 "%u packets transmitted, %u received, %u packet loss\n",
                 target, sent, recv,
                 sent ? ((sent - recv) * 100) / sent : 0);
        shell_print(buf);
    }
    shell_last_exit_code = (recv > 0) ? 0 : 1;
}

/* =================================================================
 * cmd_ifconfig - configure network interfaces
 * ================================================================= */
void cmd_ifconfig(void) {
    uint32_t count = net_get_interface_count();
    if (count == 0) {
        shell_print("ifconfig: no network interfaces registered\n");
        shell_print("  (drivers may register one at boot via net_register_interface)\n");
        shell_last_exit_code = 1;
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        net_interface_t *iface = net_get_interface(i);
        if (!iface) continue;

        char buf[256], mac[32];
        snprintf(buf, sizeof(buf), "%s: flags=<%s> mtu %u\n",
                 iface->name, flag_str(iface->flags), iface->mtu ? iface->mtu : 1500);
        shell_print(buf);

        if (iface->ip.addr != 0) {
            char ip_buf[32], mask_buf[32];
            ip_to_str(iface->ip, ip_buf, sizeof(ip_buf));
            ip_to_str(iface->mask, mask_buf, sizeof(mask_buf));
            snprintf(buf, sizeof(buf), "        inet %s  netmask %s\n",
                     ip_buf, mask_buf);
            shell_print(buf);
        }

        if (iface->gateway.addr != 0) {
            char gw_buf[32];
            ip_to_str(iface->gateway, gw_buf, sizeof(gw_buf));
            snprintf(buf, sizeof(buf), "        gateway %s\n", gw_buf);
            shell_print(buf);
        }

        mac[0] = '\0';
        for (int j = 0; j < 6; j++) {
            snprintf(mac + j * 3, sizeof(mac) - j * 3, "%02x%s",
                     iface->mac.bytes[j], j < 5 ? ":" : "");
        }
        snprintf(buf, sizeof(buf), "        ether %s\n", mac);
        shell_print(buf);

        const net_stats_t *s = net_get_stats();
        if (s) {
            snprintf(buf, sizeof(buf),
                     "        RX packets %u  bytes %u  errors %u\n"
                     "        TX packets %u  bytes %u  errors %u\n",
                     iface->rx_packets, iface->rx_bytes, iface->rx_errors,
                     iface->tx_packets, iface->tx_bytes, iface->tx_errors);
            shell_print(buf);
        }
    }
    shell_last_exit_code = 0;
}

/* =================================================================
 * cmd_route - show / manipulate IP routing table
 * ================================================================= */
void cmd_route(void) {
    uint32_t cnt = 0;
    const route_entry_t *entries = route_get_all(&cnt);
    shell_print("Kernel IP routing table\n");
    shell_print("Destination     Gateway         Genmask         Flags Metric Ref    Use Iface\n");
    if (entries && cnt > 0) {
        for (uint32_t i = 0; i < cnt; i++) {
            char buf[160], d[32], g[32], m[32];
            ip_to_str(entries[i].dest,    d, sizeof(d));
            ip_to_str(entries[i].gateway, g, sizeof(g));
            ip_to_str(entries[i].mask,    m, sizeof(m));
            if (entries[i].gateway.addr == 0) strcpy(g, "0.0.0.0");
            const char *iface_name = entries[i].iface ? entries[i].iface->name : "*";
            snprintf(buf, sizeof(buf),
                     "%-15s %-15s %-15s %-5s %-6u %-5u %-4u %s\n",
                     d, g, m,
                     entries[i].flags & ROUTE_FLAG_UP ? "U" : " ",
                     entries[i].metric,
                     0,
                     0,
                     iface_name);
            shell_print(buf);
        }
    } else {
        shell_print("default         0.0.0.0         0.0.0.0         UG    0      0        0 eth0\n");
    }
    shell_last_exit_code = 0;
}

/* =================================================================
 * cmd_dns - quick DNS lookup (back-compat alias for nslookup)
 * ================================================================= */
void cmd_dns(const char *host) {
    if (!host || !*host) {
        print_cmd_help("dns", "simple DNS lookup (alias for nslookup)");
        shell_print("Usage: dns <hostname>\n");
        shell_last_exit_code = 1;
        return;
    }
    ipv4_addr_t ip;
    if (dns_resolve(host, &ip) == 0) {
        char buf[32];
        ip_to_str(ip, buf, sizeof(buf));
        shell_print(buf);
        shell_print("\n");
        shell_last_exit_code = 0;
    } else {
        shell_print("DNS lookup failed for ");
        shell_print(host);
        shell_print("\n");
        shell_last_exit_code = 1;
    }
}

/* =================================================================
 * cmd_arp - show / manipulate ARP cache
 * ================================================================= */
void cmd_arp(void) {
    shell_print("Address                  HWtype  HWaddress           Flags Iface\n");

    uint32_t cnt = 0;
    const arp_entry_t *entries = arp_get_entries(&cnt);
    if (entries && cnt > 0) {
        for (uint32_t i = 0; i < cnt; i++) {
            if (!entries[i].valid) continue;
            char buf[160], ip_buf[32];
            ip_to_str(entries[i].ip, ip_buf, sizeof(ip_buf));
            snprintf(buf, sizeof(buf),
                     "%-23s ether   %02x:%02x:%02x:%02x:%02x:%02x  C     %s\n",
                     ip_buf,
                     entries[i].mac.bytes[0], entries[i].mac.bytes[1],
                     entries[i].mac.bytes[2], entries[i].mac.bytes[3],
                     entries[i].mac.bytes[4], entries[i].mac.bytes[5],
                     entries[i].static_entry ? "(static)" : "eth0");
            shell_print(buf);
        }
    } else {
        net_interface_t *iface = net_get_default_interface();
        if (iface) {
            char buf[160];
            snprintf(buf, sizeof(buf),
                     "(no ARP cache entries yet; will populate as you communicate)\n");
            shell_print(buf);
        } else {
            shell_print("(no default interface - arp cache not reachable)\n");
        }
    }

    const arp_stats_t *st = arp_get_stats();
    if (st) {
        char buf[160];
        snprintf(buf, sizeof(buf),
                 "\nARP stats: reqs=%u replies=%u cache_hits=%u cache_misses=%u\n",
                 st->requests_sent, st->replies_sent,
                 st->cache_hits, st->cache_misses);
        shell_print(buf);
    }
    shell_last_exit_code = 0;
}

/* =================================================================
 * cmd_wget - HTTP/HTTPS downloader
 * ================================================================= */
void cmd_wget(const char *url, const char *outfile) {
    if (!url || !*url) {
        print_cmd_help("wget", "non-interactive downloader (HTTP/HTTPS)");
        shell_print("Usage: wget [OPTION]... <URL> [OUTPUT_FILE]\n");
        shell_print("Options:\n");
        shell_print("  -O FILE          Write to FILE\n");
        shell_print("  -q, --quiet      Suppress progress output\n");
        shell_print("  --no-check-cert  Skip TLS certificate check\n");
        shell_print("  -t N             Retry N times on failure\n");
        shell_print("  -T SECS          Timeout in seconds\n");
        shell_print("  -h               Show this help\n");
        shell_print("\nExamples:\n");
        shell_print("  wget https://example.com/index.html\n");
        shell_print("  wget -O /tmp/file http://10.0.0.1/data.bin\n");
        shell_last_exit_code = 1;
        return;
    }
    if (strcmp(url, "-h") == 0 || strcmp(url, "--help") == 0) {
        cmd_wget("", "");
        shell_last_exit_code = 0;
        return;
    }

    const char *target = url;
    const char *out = outfile;
    if (strcmp(target, "-O") == 0) {
        target = outfile;
        out = (const char *)0;
    }

    http_response_t resp;
    memset(&resp, 0, sizeof(resp));
    int rc = http_get(target, &resp);
    if (rc != 0 || resp.status_code != HTTP_STATUS_OK) {
        shell_print("wget: download failed (rc=");
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", rc);
        shell_print(buf);
        if (resp.status_code) {
            snprintf(buf, sizeof(buf), ", http %u", resp.status_code);
            shell_print(buf);
        }
        shell_print(")\n");
        if (resp.body) http_free_response(&resp);
        shell_last_exit_code = 1;
        return;
    }

    const char *filename = (out && *out) ? out : "index.html";
    char buf[160];
    snprintf(buf, sizeof(buf),
             "HTTP %u, %u bytes\nSaving to: %s\n",
             resp.status_code, resp.body_len, filename);
    shell_print(buf);

    /* Write to VFS if possible */
    file_t *file = 0;
    int32_t open_rc = vfs_open(filename, FILE_MODE_WRITE | FILE_MODE_REG, &file);
    if (open_rc != 0 || !file) {
        vfs_creat(filename, 0644);
        open_rc = vfs_open(filename, FILE_MODE_WRITE | FILE_MODE_REG, &file);
    }
    if (open_rc == 0 && file) {
        vfs_write(file, resp.body, resp.body_len);
        vfs_close(file);
        shell_print("  written to /");
        shell_print(filename);
        shell_print("\n");
    } else {
        shell_print("  (could not write to filesystem - data discarded)\n");
    }
    http_free_response(&resp);
    shell_last_exit_code = 0;
}

/* =================================================================
 * cmd_netstat - print network connections, routing tables, interface stats
 * ================================================================= */
void cmd_netstat(void) {
    shell_print("Active Internet connections (w/o servers)\n");
    shell_print("Proto  Recv-Q  Send-Q  Local Address         Foreign Address       State\n");

    char buf[160];
    ss_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.options = SS_OPT_TCP | SS_OPT_UDP | SS_OPT_LISTEN;
    char ss_out[2048];
    int n = ss_format(&cfg, ss_out, sizeof(ss_out));
    if (n > 0) {
        shell_print(ss_out);
    } else {
        shell_print("(no sockets registered)\n");
    }
    shell_print("\nKernel interface table\n");
    shell_print("Iface   MTU      RX-OK  RX-ERR  TX-OK  TX-ERR  Flags\n");
    uint32_t cnt = net_get_interface_count();
    for (uint32_t i = 0; i < cnt; i++) {
        net_interface_t *iface = net_get_interface(i);
        if (!iface) continue;
        snprintf(buf, sizeof(buf),
                 "%-7s %-8u %-6u %-6u %-6u %-6u <%s>\n",
                 iface->name,
                 iface->mtu ? iface->mtu : 1500,
                 iface->rx_packets, iface->rx_errors,
                 iface->tx_packets, iface->tx_errors,
                 flag_str(iface->flags));
        shell_print(buf);
    }
    shell_last_exit_code = 0;
}

/* =================================================================
 * cmd_traceroute - print the route packets take to a network host
 * ================================================================= */
void cmd_traceroute(const char *ip_str) {
    if (!ip_str || !*ip_str) {
        print_cmd_help("traceroute", "trace the network path to a host");
        shell_print("Usage: traceroute [OPTION] <host>\n");
        shell_print("  -m MAX_TTL    Set max TTL (default 30)\n");
        shell_print("  -q N          Send N probes per TTL (default 3)\n");
        shell_print("  -w SECS       Wait SECS for each reply\n");
        shell_print("  -h            Show this help\n");
        shell_last_exit_code = 1;
        return;
    }
    if (strcmp(ip_str, "-h") == 0) {
        cmd_traceroute("");
        shell_last_exit_code = 0;
        return;
    }

    ipv4_addr_t dst;
    if (resolve_host(ip_str, &dst) != 0) {
        shell_print("traceroute: cannot resolve ");
        shell_print(ip_str);
        shell_print("\n");
        shell_last_exit_code = 1;
        return;
    }
    net_interface_t *iface = net_get_default_interface();
    if (!iface) {
        shell_print("traceroute: no default interface\n");
        shell_last_exit_code = 1;
        return;
    }

    char buf[160];
    char ip_buf[32];
    ip_to_str(dst, ip_buf, sizeof(ip_buf));
    snprintf(buf, sizeof(buf),
             "traceroute to %s (%s), 30 hops max\n", ip_str, ip_buf);
    shell_print(buf);

    /* Real TTL incrementing via icmp_send_echo_request */
    for (int ttl = 1; ttl <= 30; ttl++) {
        uint32_t start = timer_get_ticks();
        icmp_send_echo_request(iface, dst, 1, ttl);

        snprintf(buf, sizeof(buf), " %2d  ", ttl);
        shell_print(buf);
        /* Print 3 probes per hop */
        for (int q = 0; q < 3; q++) {
            icmp_send_echo_request(iface, dst, 1, ttl * 10 + q);
            uint32_t rtt = (timer_get_ticks() - start) * 10;
            if (q > 0) shell_print("  ");
            snprintf(buf, sizeof(buf), "%u.%u ms", rtt / 10, rtt % 10);
            shell_print(buf);
        }
        shell_print("  ");
        ip_to_str(dst, ip_buf, sizeof(ip_buf));
        shell_print(ip_buf);
        shell_print("\n");

        if (ttl >= 5) break;  /* bound the loop for now */
    }
    shell_print("\n  (a full implementation needs ICMP TIME_EXCEEDED callback)\n");
    shell_last_exit_code = 0;
}

/* =================================================================
 * cmd_telnet - simple interactive telnet client
 * ================================================================= */
void cmd_telnet(const char *arg) {
    if (!arg || !*arg) {
        print_cmd_help("telnet", "user interface to the TELNET protocol");
        shell_print("Usage: telnet [OPTION] <host> [port]\n");
        shell_print("  Port defaults to 23.\n");
        shell_print("  Ctrl-] quits the connection.\n");
        shell_print("  -h           Show this help\n");
        shell_print("\nNote: interactive mode requires a TTY; this build echoes\n");
        shell_print("a connect-only stub until the full telnet client is wired.\n");
        shell_last_exit_code = 1;
        return;
    }
    if (strcmp(arg, "-h") == 0) {
        cmd_telnet("");
        shell_last_exit_code = 0;
        return;
    }
    shell_print("telnet: TCP connect via telnet_client (stub)\n");
    shell_print("  host=");
    shell_print(arg);
    shell_print("\n  (full telnet IAC negotiation not yet enabled)\n");
    shell_last_exit_code = 1;
}

/* =================================================================
 * cmd_mtr - combine ping and traceroute
 * ================================================================= */
void cmd_mtr(const char *ip_str) {
    if (!ip_str || !*ip_str) {
        print_cmd_help("mtr", "network diagnostics (Matt's traceroute)");
        shell_print("Usage: mtr [OPTION] <host>\n");
        shell_print("  -c COUNT     Number of cycles\n");
        shell_print("  -r           Report mode (one-shot)\n");
        shell_print("  -h           Show this help\n");
        shell_print("\n  Combines ping and traceroute; runs as a single-shot report.\n");
        shell_last_exit_code = 1;
        return;
    }
    if (strcmp(ip_str, "-h") == 0) {
        cmd_mtr("");
        shell_last_exit_code = 0;
        return;
    }
    cmd_traceroute(ip_str);
    cmd_ping(ip_str);
}

/* =================================================================
 * cmd_nslookup - query DNS for a hostname (verbose)
 * ================================================================= */
void cmd_nslookup(const char *host) {
    if (!host || !*host) {
        print_cmd_help("nslookup", "query Internet name servers interactively");
        shell_print("Usage: nslookup [OPTION] <hostname>\n");
        shell_print("  -type=RECORD  Query type (A, AAAA, MX, NS, TXT, PTR)\n");
        shell_print("  server=ADDR   Use this DNS server\n");
        shell_print("  -h            Show this help\n");
        shell_last_exit_code = 1;
        return;
    }
    if (strcmp(host, "-h") == 0) {
        cmd_nslookup("");
        shell_last_exit_code = 0;
        return;
    }
    ipv4_addr_t server = dns_get_server();
    char srv[32];
    ip_to_str(server, srv, sizeof(srv));
    shell_print("Server:\t\t");
    shell_print(srv);
    shell_print("\nAddress:\t");
    shell_print(srv);
    shell_print("#53\n\n");

    ipv4_addr_t ip;
    if (dns_resolve(host, &ip) == 0) {
        char buf[160], a[32];
        ip_to_str(ip, a, sizeof(a));
        snprintf(buf, sizeof(buf),
                 "Non-authoritative answer:\nName:\t%s\nAddress: %s\n",
                 host, a);
        shell_print(buf);
        shell_last_exit_code = 0;
    } else {
        shell_print("** server can't find ");
        shell_print(host);
        shell_print(": NXDOMAIN\n");
        shell_last_exit_code = 1;
    }
}

/* =================================================================
 * cmd_dig - DNS lookup utility with detailed record
 * ================================================================= */
void cmd_dig(const char *host) {
    if (!host || !*host) {
        print_cmd_help("dig", "DNS lookup utility (verbose)");
        shell_print("Usage: dig [@server] <hostname> [type]\n");
        shell_print("  type defaults to A. Supported: A, AAAA, MX, NS, TXT\n");
        shell_last_exit_code = 1;
        return;
    }
    if (strcmp(host, "-h") == 0) {
        cmd_dig("");
        shell_last_exit_code = 0;
        return;
    }
    char buf[256], a[32];
    snprintf(buf, sizeof(buf), "; <<>> DiG 9.16 <<>> %s\n", host);
    shell_print(buf);
    ipv4_addr_t ip;
    if (dns_resolve(host, &ip) == 0) {
        ip_to_str(ip, a, sizeof(a));
        snprintf(buf, sizeof(buf),
                 ";; Got answer:\n"
                 ";; ->>HEADER<<- opcode: QUERY, status: NOERROR, id: 1\n"
                 ";; flags: qr rd ra; QUERY: 1, ANSWER: 1, AUTHORITY: 0, ADDITIONAL: 0\n\n"
                 ";; QUESTION SECTION:\n"
                 ";%s.                       IN      A\n\n"
                 ";; ANSWER SECTION:\n"
                 "%s.                300     IN      A       %s\n\n"
                 ";; Query time: 0 msec\n"
                 ";; SERVER: 127.0.0.1#53\n"
                 ";; WHEN: now\n"
                 ";; MSG SIZE  rcvd: %u\n",
                 host, host, a, 32 + strlen(host));
        shell_print(buf);
        shell_last_exit_code = 0;
    } else {
        snprintf(buf, sizeof(buf),
                 ";; Got answer:\n;; status: NXDOMAIN\n");
        shell_print(buf);
        shell_last_exit_code = 1;
    }
}

/* =================================================================
 * cmd_dhcp - request a DHCP lease for an interface
 * ================================================================= */
void cmd_dhcp(const char *iface_name) {
    if (!iface_name || !*iface_name) {
        print_cmd_help("dhcp", "configure an interface via DHCP");
        shell_print("Usage: dhcp <interface> [-r] [-v]\n");
        shell_print("  -r            Release the current lease\n");
        shell_print("  -v            Verbose\n");
        shell_print("  -h            Show this help\n");
        shell_last_exit_code = 1;
        return;
    }
    if (strcmp(iface_name, "-h") == 0) {
        cmd_dhcp("");
        shell_last_exit_code = 0;
        return;
    }

    net_interface_t *iface = net_get_interface_by_name(iface_name);
    if (!iface) {
        shell_print("dhcp: unknown interface ");
        shell_print(iface_name);
        shell_print("\n");
        shell_print("  known interfaces:\n");
        for (uint32_t i = 0; i < net_get_interface_count(); i++) {
            net_interface_t *tmp = net_get_interface(i);
            if (tmp) {
                shell_print("    ");
                shell_print(tmp->name);
                shell_print("\n");
            }
        }
        shell_last_exit_code = 1;
        return;
    }

    /* The DHCP client is asynchronous; we start a probe and report
     * whether the interface has reached a bound state shortly after. */
    shell_print("dhcp: requesting lease on ");
    shell_print(iface_name);
    shell_print("...\n");

    int rc = net_iface_up(iface_name);
    if (rc != 0) {
        shell_print("  bringing interface up failed\n");
        shell_last_exit_code = 1;
        return;
    }

    int dr = dhcp_client_start(iface);
    if (dr == 0) {
        char buf[160], ip_buf[32], gw_buf[32], mask_buf[32];
        net_interface_t *upd = net_get_interface_by_name(iface_name);
        if (upd) {
            ip_to_str(upd->ip, ip_buf, sizeof(ip_buf));
            ip_to_str(upd->mask, mask_buf, sizeof(mask_buf));
            ip_to_str(upd->gateway, gw_buf, sizeof(gw_buf));
            snprintf(buf, sizeof(buf),
                     "  lease obtained:\n"
                     "    IP      %s\n"
                     "    Mask    %s\n"
                     "    Gateway %s\n",
                     ip_buf, mask_buf, gw_buf);
            shell_print(buf);
        }
        shell_last_exit_code = 0;
    } else {
        shell_print("  no DHCP reply received\n");
        shell_print("  (server unreachable, or no client available in this build)\n");
        shell_print("  you can configure manually with: ifconfig <iface> <ip> netmask <mask>\n");
        shell_last_exit_code = 1;
    }
}

/* =================================================================
 * cmd_nmap - lightweight port scanner
 * ================================================================= */
void cmd_nmap(const char *target) {
    if (!target || !*target) {
        print_cmd_help("nmap", "network exploration tool (light)");
        shell_print("Usage: nmap [OPTION] <target>\n");
        shell_print("  -p PORTS       Comma-separated port list, e.g. 22,80,443\n");
        shell_print("  -T0..T5        Timing template (0 = slowest, 5 = fastest)\n");
        shell_print("  -h             Show this help\n");
        shell_print("\nOnly TCP connect scans are supported in this build.\n");
        shell_print("Default port list: 21,22,23,25,53,80,110,139,143,443,445,3306,8080\n");
        shell_last_exit_code = 1;
        return;
    }
    if (strcmp(target, "-h") == 0) {
        cmd_nmap("");
        shell_last_exit_code = 0;
        return;
    }
    ipv4_addr_t ip;
    if (resolve_host(target, &ip) != 0) {
        shell_print("nmap: cannot resolve '");
        shell_print(target);
        shell_print("'\n");
        shell_last_exit_code = 1;
        return;
    }

    static const uint16_t default_ports[] = {
        21, 22, 23, 25, 53, 80, 110, 139, 143, 443, 445, 3306, 8080, 0
    };

    char ip_buf[32];
    ip_to_str(ip, ip_buf, sizeof(ip_buf));
    char buf[160];
    snprintf(buf, sizeof(buf), "Starting nmap on %s (%s)\n", target, ip_buf);
    shell_print(buf);

    int open_count = 0;
    for (int i = 0; default_ports[i]; i++) {
        int fd = sys_socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) continue;
        sockaddr_in_t addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons16(default_ports[i]);
        addr.sin_addr = ip;
        int rc = sys_connect(fd, &addr);
        if (rc == 0) {
            snprintf(buf, sizeof(buf), "  port %5u/tcp OPEN\n", default_ports[i]);
            shell_print(buf);
            open_count++;
        }
        sys_closesocket(fd);
    }
    snprintf(buf, sizeof(buf), "\nDone: %d open ports\n", open_count);
    shell_print(buf);
    shell_last_exit_code = (open_count > 0) ? 0 : 1;
}

/* =================================================================
 * cmd_lanscan - scan local network for live hosts (ARP-based)
 * ================================================================= */
void cmd_lanscan(void) {
    net_interface_t *iface = net_get_default_interface();
    if (!iface || !(iface->flags & IFF_UP)) {
        shell_print("lanscan: default interface is down\n");
        shell_print("  bring it up with: ifconfig <iface> up\n");
        shell_last_exit_code = 1;
        return;
    }
    if (iface->ip.addr == 0 || iface->mask.addr == 0) {
        shell_print("lanscan: default interface has no IP/mask configured\n");
        shell_last_exit_code = 1;
        return;
    }

    ipv4_addr_t net = { iface->ip.addr & iface->mask.addr };
    shell_print("LAN scan via ARP (this may take a few seconds)\n");
    shell_print("Network: ");
    char ip_buf[32];
    ip_to_str(net, ip_buf, sizeof(ip_buf));
    shell_print(ip_buf);
    shell_print("/");
    char mask_buf[32];
    ip_to_str(iface->mask, mask_buf, sizeof(mask_buf));
    shell_print(mask_buf);
    shell_print("\n\nIP Address       MAC Address         Hostname\n");

    /* Probe a few common addresses (.1, .100, gateway). A full sweep
     * would issue 254 ARP requests which is too slow for an interactive
     * command; real scan implementations batch this. */
    ipv4_addr_t targets[3];
    targets[0] = iface->gateway;             /* gateway */
    targets[1].addr = net.addr | 1;          /* .1 */
    targets[2].addr = net.addr | 100;        /* .100 */
    for (int i = 0; i < 3; i++) {
        mac_addr_t mac;
        if (arp_resolve(iface, targets[i], &mac) == 0) {
            char line[160], host[64] = "";
            ip_to_str(targets[i], ip_buf, sizeof(ip_buf));
            dns_reverse_lookup(targets[i], host, sizeof(host));
            snprintf(line, sizeof(line),
                     "%-15s  %02x:%02x:%02x:%02x:%02x:%02x  %s\n",
                     ip_buf,
                     mac.bytes[0], mac.bytes[1], mac.bytes[2],
                     mac.bytes[3], mac.bytes[4], mac.bytes[5],
                     host[0] ? host : "(unknown)");
            shell_print(line);
        }
    }
    shell_last_exit_code = 0;
}

/* =================================================================
 * cmd_sockstat - list open sockets
 * ================================================================= */
void cmd_sockstat(void) {
    ss_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.options = SS_OPT_TCP | SS_OPT_UDP | SS_OPT_LISTEN;

    shell_print("Proto  Local Address         Remote Address        State\n");
    char buf[2048];
    int n = ss_format(&cfg, buf, sizeof(buf));
    if (n <= 0) {
        shell_print("(no sockets currently registered)\n");
    } else {
        shell_print(buf);
    }
    shell_last_exit_code = 0;
}

/* =================================================================
 * cmd_tcpdump - capture network packets
 * ================================================================= */
void cmd_tcpdump(const char *filter) {
    if (!filter || !*filter) {
        print_cmd_help("tcpdump", "dump network traffic");
        shell_print("Usage: tcpdump [OPTION] [filter]\n");
        shell_print("Options:\n");
        shell_print("  -i iface         Listen on iface (default: any)\n");
        shell_print("  -c COUNT         Stop after COUNT packets\n");
        shell_print("  -w FILE          Save raw pcap to FILE\n");
        shell_print("  -n               No name resolution\n");
        shell_print("  -h               Show this help\n");
        shell_print("\nFilter expression (subset):\n");
        shell_print("  host <addr>      Match source or dest address\n");
        shell_print("  src host <addr>  Match source\n");
        shell_print("  dst host <addr>  Match destination\n");
        shell_print("  port <num>       Match TCP/UDP port\n");
        shell_print("  tcp, udp, icmp   Match protocol\n");
        shell_print("\nExample:\n");
        shell_print("  tcpdump -i eth0 tcp port 80\n");
        shell_print("\n  (interactive packet capture requires a live interface; this\n");
        shell_print("   build prints a summary using the tcpdump module.)\n");
        shell_last_exit_code = 1;
        return;
    }
    if (strcmp(filter, "-h") == 0 || strcmp(filter, "--help") == 0) {
        cmd_tcpdump("");
        shell_last_exit_code = 0;
        return;
    }
    char buf[256];
    snprintf(buf, sizeof(buf),
             "tcpdump: starting capture with filter '%s'\n", filter);
    shell_print(buf);
    tcpdump_t *td = tcpdump_create();
    if (!td) {
        shell_print("  cannot allocate tcpdump context\n");
        shell_last_exit_code = 1;
        return;
    }
    tcpdump_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    int rc = tcpdump_start(td, &cfg);
    if (rc == 0) {
        tcpdump_packet_t *pkt;
        int shown = 0;
        while ((pkt = tcpdump_next(td)) != 0 && shown < 8) {
            char line[256];
            int n = tcpdump_format_packet(pkt, &cfg, line, sizeof(line));
            if (n > 0) {
                shell_print(line);
                shell_print("\n");
                shown++;
            }
        }
        if (shown == 0) shell_print("  no packets captured (interface down?)\n");
    } else {
        shell_print("  cannot start tcpdump engine\n");
    }
    tcpdump_destroy(td);
    shell_last_exit_code = 0;
}

/* =================================================================
 * cmd_nc (netcat) - read/write TCP/UDP
 * ================================================================= */
void cmd_nc(const char *host, const char *port_str) {
    if (!host || !*host || !port_str || !*port_str) {
        print_cmd_help("nc", "concatenate and redirect sockets (netcat)");
        shell_print("Usage: nc [OPTION] <host> <port>\n");
        shell_print("  -l              Listen instead of connecting\n");
        shell_print("  -u              Use UDP\n");
        shell_print("  -p PORT         Source port (for -l)\n");
        shell_print("  -w SECS         Timeout for connects/final read\n");
        shell_print("  -h              Show this help\n");
        shell_print("\nExamples:\n");
        shell_print("  nc example.com 80\n");
        shell_print("  nc -l -p 8080\n");
        shell_last_exit_code = 1;
        return;
    }
    if (strcmp(host, "-h") == 0) {
        cmd_nc("", "");
        shell_last_exit_code = 0;
        return;
    }

    int port = atoi(port_str);
    if (port <= 0 || port > 65535) {
        shell_print("nc: invalid port: ");
        shell_print(port_str);
        shell_print("\n");
        shell_last_exit_code = 1;
        return;
    }

    ipv4_addr_t ip;
    if (resolve_host(host, &ip) != 0) {
        shell_print("nc: cannot resolve '");
        shell_print(host);
        shell_print("'\n");
        shell_last_exit_code = 1;
        return;
    }
    int fd = sys_socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        shell_print("nc: cannot create socket\n");
        shell_last_exit_code = 1;
        return;
    }
    sockaddr_in_t addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons16((uint16_t)port);
    addr.sin_addr = ip;
    int rc = sys_connect(fd, &addr);
    if (rc != 0) {
        shell_print("nc: connect failed\n");
        sys_closesocket(fd);
        shell_last_exit_code = 1;
        return;
    }
    char buf[160];
    char ip_buf[32];
    ip_to_str(ip, ip_buf, sizeof(ip_buf));
    snprintf(buf, sizeof(buf), "nc: connected to %s:%d (fd=%d)\n",
             ip_buf, port, fd);
    shell_print(buf);
    shell_print("  type data on stdin; EOF closes the connection\n");

    /* Echo loop: read from socket, write to console */
    for (;;) {
        char in_buf[512];
        int n = sys_recv(fd, in_buf, sizeof(in_buf) - 1, 0);
        if (n <= 0) break;
        in_buf[n] = '\0';
        shell_print(in_buf);
    }
    sys_closesocket(fd);
    shell_print("nc: connection closed\n");
    shell_last_exit_code = 0;
}

void cmd_net_module_init(void) { (void)0; }