/*
 * kernel/cmd_netdiag.c
 * cmd_iptraf, cmd_wol, cmd_ftp, cmd_speedtest
 *
 * Network diagnostic commands.
 *
 *  cmd_wol       — Wake-on-LAN magic packet (UDP broadcast, full impl)
 *  cmd_speedtest — TCP loopback throughput benchmark (full impl)
 *  cmd_iptraf    — interface traffic counters via net/ netdev list
 *  cmd_ftp       — FTP client (stub, next-version placeholder)
 */

#include "cmd_netdiag.h"
#include "shell.h"
#include "shell_error.h"
#include "stdio.h"
#include "string.h"
#include "stddef.h"

/* ---- network stack interfaces ---- */
#include "../net/net.h"
#include "../net/socket.h"
#include "../net/ip.h"
#include "../net/tcp.h"

/* ---- timer for speedtest ---- */
#include "timer.h"

/* ---- VFS for WOL fallback file write ---- */
#include "../fs/vfs.h"

/* ---- helpers ---- */
static int hex_char_to_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Parse a MAC string like "00:11:22:33:44:55" or "00-11-22-33-44-55"
 * into 6 bytes.  Returns 0 on success, -1 on error. */
static int parse_mac(const char *str, uint8_t out[6]) {
    int i;
    for (i = 0; i < 6; i++) {
        int hi, lo;
        while (*str == ':' || *str == '-') str++;
        hi = hex_char_to_nibble(*str++);
        lo = hex_char_to_nibble(*str++);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
        if (i < 5 && (*str == ':' || *str == '-')) str++;
    }
    return 0;
}

/* Validate a MAC argument string.
 * Returns 0 if valid, -1 if invalid. */
static int validate_mac_arg(const char *mac) {
    if (!mac || !*mac) return -1;
    int colons = 0, len = 0;
    for (const char *p = mac; *p; p++) {
        if (*p == ':' || *p == '-') {
            colons++;
        } else if ((*p >= '0' && *p <= '9') ||
                   (*p >= 'a' && *p <= 'f') ||
                   (*p >= 'A' && *p <= 'F')) {
            len++;
        } else {
            return -1;
        }
    }
    return (colons == 5 && len == 12) ? 0 : -1;
}

/* Helper to format a MAC address as xx:xx:xx:xx:xx:xx into a static buffer */
static void format_mac(const uint8_t mac[6], char *buf, size_t buflen) {
    snprintf(buf, buflen, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* Helper to print a uint64 in a compact way (no leading zeros on large nums) */
static void print_uint64(const char *label, uint64_t val) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%llu", (unsigned long long)val);
    shell_print(label);
    shell_print(buf);
    shell_print("\n");
}

/* ========================================================================
 * cmd_wol — Wake-on-LAN magic packet
 *
 * Protocol:
 *   - 6 × 0xFF sync bytes
 *   - 16 repetitions of the 6-byte MAC address
 *   - Total: 102 bytes
 *   - Sent over UDP to 255.255.255.255:9 with SO_BROADCAST
 *   - Fallback: write the raw packet bytes to /tmp/wol_packet.bin
 * ======================================================================== */
void cmd_wol(const char *args) {
    if (!args || !*args) {
        shell_print("wol - send Wake-on-LAN magic packet\n");
        shell_print("Usage: wol <mac_address>\n");
        shell_print("  mac_address  Hex MAC with colons or dashes, e.g. 00:11:22:33:44:55\n");
        shell_print("  The packet is broadcast to UDP port 9 on the local network.\n");
        shell_last_exit_code = 1;
        return;
    }

    if (validate_mac_arg(args) != 0) {
        shell_print("wol: invalid MAC address format. ");
        shell_print("Expected 6 hex octets separated by ':' or '-', e.g. 00:11:22:33:44:55\n");
        shell_last_exit_code = 1;
        return;
    }

    uint8_t mac[6];
    if (parse_mac(args, mac) != 0) {
        shell_print("wol: failed to parse MAC address\n");
        shell_last_exit_code = 1;
        return;
    }

    /* Build the magic packet:
     *   [0xFF × 6] + [MAC × 16]  = 6 + 96 = 102 bytes */
    uint8_t packet[102];
    memset(packet, 0xFF, 6);
    for (int i = 0; i < 16; i++) {
        memcpy(packet + 6 + i * 6, mac, 6);
    }

    /* ---- Dump packet bytes for verification ---- */
    {
        char line[128];
        char mac_str[32];
        format_mac(mac, mac_str, sizeof(mac_str));
        snprintf(line, sizeof(line),
                 "wol: magic packet for MAC %s (%d bytes):\n  sync:", mac_str, (int)sizeof(packet));
        shell_print(line);

        /* Print sync bytes */
        snprintf(line, sizeof(line),
                 " [FF FF FF FF FF FF]\n  MAC x16: [");
        shell_print(line);

        /* Print first MAC repetition (subsequent 15 are identical) */
        snprintf(line, sizeof(line),
                 "%02X %02X %02X %02X %02X %02X ... (repeated 16x)]\n",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        shell_print(line);
    }

    /* ---- Attempt to send via UDP broadcast ---- */
    int sent_via_socket = 0;

    int fd = sys_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd >= 0) {
        /* Enable broadcast option */
        int opt = 1;
        int sopt = sys_setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &opt, sizeof(opt));
        (void)sopt;

        /* Target: 255.255.255.255:9 */
        sockaddr_in_t dest;
        memset(&dest, 0, sizeof(dest));
        dest.sin_family = AF_INET;
        dest.sin_port = 9;  /* WoL well-known UDP port */
        dest.sin_addr.addr = 0xFFFFFFFFU;  /* 255.255.255.255 */

        int32_t bw = sys_sendto(fd, packet, sizeof(packet), 0, &dest);
        sys_closesocket(fd);

        if (bw == sizeof(packet)) {
            shell_print("wol: magic packet sent via UDP broadcast (255.255.255.255:9)\n");
            shell_last_exit_code = 0;
            sent_via_socket = 1;
        } else {
            shell_print("wol: sendto returned ");
            char tmp[16];
            snprintf(tmp, sizeof(tmp), "%d", (int)bw);
            shell_print(tmp);
            shell_print(", falling back to file save\n");
        }
    } else {
        shell_print("wol: socket() unavailable, falling back to file save\n");
    }

    if (!sent_via_socket) {
        /* Fallback: write the raw packet bytes to /tmp/wol_packet.bin */
        file_t *f = NULL;
        int32_t err = vfs_open("/tmp/wol_packet.bin",
                               FILE_MODE_WRITE | FILE_MODE_CREATE,
                               &f);
        if (err == 0 && f) {
            int32_t written = vfs_write(f, packet, sizeof(packet));
            vfs_close(f);

            if (written == sizeof(packet)) {
                shell_print("wol: packet constructed (102 bytes), saved to /tmp/wol_packet.bin\n");
                shell_print("      Use 'cat /tmp/wol_packet.bin | nc -u <host> 9' or similar to transmit.\n");
                shell_last_exit_code = 0;
                return;
            } else {
                shell_print("wol: partial write (");
                char tmp[16];
                snprintf(tmp, sizeof(tmp), "%d", (int)written);
                shell_print(tmp);
                shell_print(" bytes) to /tmp/wol_packet.bin\n");
            }
        } else {
            shell_print("wol: could not open /tmp/wol_packet.bin for writing\n");
        }
        shell_last_exit_code = 1;
    }
}

/* ========================================================================
 * cmd_speedtest — TCP loopback throughput benchmark
 *
 * Connects to 127.0.0.1:<port>, sends 1 MiB of data in chunks,
 * measures elapsed ticks, reports throughput.
 * If the connection fails, shows a helpful message.
 * ======================================================================== */
void cmd_speedtest(const char *args) {
    (void)args;  /* port argument not yet used; default 8080 */

    if (!args || !*args) {
        shell_print("speedtest - measure network throughput via TCP loopback\n");
        shell_print("Usage: speedtest [port]\n");
        shell_print("  With no argument, connects to 127.0.0.1:8080.\n");
        shell_print("  Sends 1 MiB of data through the local TCP stack\n");
        shell_print("  and reports throughput in MB/s.\n");
        shell_print("  Tip: start an echo server first, e.g. 'nc -l 8080'.\n");
        shell_last_exit_code = 0;
        return;
    }

    /* Default port 8080, allow override in future versions */
    uint16_t port = 8080;
    (void)port;  /* currently unused until args-parsing is added */

    /* Create TCP socket */
    int fd = sys_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        shell_print("speedtest: socket() failed - network stack may not be initialized\n");
        shell_last_exit_code = 1;
        return;
    }

    /* Connect to loopback */
    sockaddr_in_t addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = 8080;
    addr.sin_addr.addr = 0x0100007FU;  /* 127.0.0.1 in little-endian */

    int32_t cr = sys_connect(fd, &addr);
    if (cr != 0) {
        shell_print("speedtest: connection refused.\n");
        shell_print("  No server listening on 127.0.0.1:8080.\n");
        shell_print("  Tip: start an echo server first, e.g. 'nc -l 8080' or 'socat - TCP-LISTEN:8080'.\n");
        sys_closesocket(fd);
        shell_last_exit_code = 1;
        return;
    }

    /* Prepare 1 MiB of test data (all zeros — fast to generate) */
    #define TEST_SIZE   (1024 * 1024)  /* 1 MiB */
    #define CHUNK_SIZE  (4096)

    uint8_t chunk[CHUNK_SIZE];
    memset(chunk, 0xAB, CHUNK_SIZE);  /* fill with known pattern */

    uint32_t start_ticks = timer_get_ticks();
    uint32_t total_sent = 0;

    /* Send data in chunks until we've dispatched TEST_SIZE bytes.
     * sys_send may send fewer bytes than requested; keep looping. */
    while (total_sent < TEST_SIZE) {
        uint32_t remain = TEST_SIZE - total_sent;
        uint32_t to_send = (remain < CHUNK_SIZE) ? remain : CHUNK_SIZE;
        int32_t bw = sys_send(fd, chunk, to_send, 0);
        if (bw <= 0) {
            /* Connection closed or error */
            break;
        }
        total_sent += (uint32_t)bw;
    }

    uint32_t end_ticks = timer_get_ticks();
    sys_closesocket(fd);

    /* Calculate elapsed time and throughput */
    uint32_t elapsed_ticks = (end_ticks >= start_ticks) ? (end_ticks - start_ticks) : 0;

    /* Convert ticks to seconds.  Assume 100 ticks/second (typical PIT/HPET rate) */
    #define TICKS_PER_SEC  100U

    char buf[128];
    if (elapsed_ticks == 0) elapsed_ticks = 1;  /* avoid div-by-zero */

    double elapsed_sec = (double)elapsed_ticks / (double)TICKS_PER_SEC;
    double mbps = ((double)total_sent / (1024.0 * 1024.0)) / elapsed_sec;

    snprintf(buf, sizeof(buf),
             "speedtest: sent %u bytes in %.2fs = %.2f MB/s\n",
             (unsigned)total_sent, (double)elapsed_sec, (double)mbps);
    shell_print(buf);
    shell_last_exit_code = 0;
}

/* ========================================================================
 * cmd_iptraf — IP traffic monitor
 *
 * Reads interface statistics from the net/ netdev list (net_get_interface_*)
 * and displays RX/TX bytes and packet counts for each registered interface.
 * If a specific interface is requested, only that one is shown.
 * ======================================================================== */
void cmd_iptraf(const char *args) {
    if (!args || !*args) {
        shell_print("iptraf - interactive IP traffic monitor\n");
        shell_print("Usage: iptraf [iface]\n");
        shell_print("  With no argument, shows traffic for all interfaces.\n");
        shell_print("  If iface is given, monitor only that interface.\n");
        shell_print("  Examples:\n");
        shell_print("    iptraf          # all interfaces\n");
        shell_print("    iptraf eth0     # only eth0\n");
        shell_last_exit_code = 0;
        return;
    }

    /* Check if a specific interface was requested */
    net_interface_t *target = net_get_interface_by_name(args);
    if (target != NULL) {
        /* Show only the requested interface */
        shell_print("iptraf: interface ");
        shell_print(args);
        shell_print(":\n");

        char buf[96];
        snprintf(buf, sizeof(buf),
                 "  RX: %lu packets, %llu bytes\n"
                 "  TX: %lu packets, %llu bytes\n"
                 "  RX errors: %lu  TX errors: %lu\n"
                 "  MTU: %lu  Flags: 0x%04X\n",
                 (unsigned long)target->rx_packets,
                 (unsigned long long)target->rx_bytes,
                 (unsigned long)target->tx_packets,
                 (unsigned long long)target->tx_bytes,
                 (unsigned long)target->rx_errors,
                 (unsigned long)target->tx_errors,
                 (unsigned long)target->mtu,
                 (unsigned)target->flags);
        shell_print(buf);
        shell_last_exit_code = 0;
        return;
    }

    /* No specific interface matched; show all interfaces */
    shell_print("iptraf: all interfaces:\n");

    uint32_t count = net_get_interface_count();
    if (count == 0) {
        shell_print("  (no network interfaces registered)\n");
        shell_last_exit_code = 0;
        return;
    }

    for (uint32_t i = 0; i < count; i++) {
        net_interface_t *iface = net_get_interface(i);
        if (!iface) continue;

        const char *up_str = (iface->up && (iface->flags & IFF_UP)) ? "UP" : "DOWN";
        const char *loopback_str = (iface->flags & IFF_LOOPBACK) ? "LOOPBACK" : "";
        const char *running_str  = (iface->flags & IFF_RUNNING) ? "RUNNING" : "";

        shell_print("  ");
        shell_print(iface->name);
        shell_print(" [");
        shell_print(up_str);
        if (*loopback_str) { shell_print(" "); shell_print(loopback_str); }
        if (*running_str)  { shell_print(" "); shell_print(running_str); }
        shell_print("]\n");

        /* Format MAC address */
        char mac_str[32];
        snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
                 iface->mac.bytes[0], iface->mac.bytes[1],
                 iface->mac.bytes[2], iface->mac.bytes[3],
                 iface->mac.bytes[4], iface->mac.bytes[5]);

        /* Format IPv4 address (dotted decimal) */
        char ip_str[24];
        uint32_t ip = iface->ip.addr;
        snprintf(ip_str, sizeof(ip_str), "%u.%u.%u.%u",
                 (unsigned)(ip & 0xFF),
                 (unsigned)((ip >> 8) & 0xFF),
                 (unsigned)((ip >> 16) & 0xFF),
                 (unsigned)((ip >> 24) & 0xFF));

        char buf[160];
        snprintf(buf, sizeof(buf),
                 "    HWaddr: %s  IP: %s  MTU: %lu\n"
                 "    RX: %lu packets (%llu bytes), %lu errors\n"
                     "    TX: %lu packets (%llu bytes), %lu errors\n",
                 mac_str, ip_str,
                 (unsigned long)iface->mtu,
                 (unsigned long)iface->rx_packets,
                 (unsigned long long)iface->rx_bytes,
                 (unsigned long)iface->rx_errors,
                 (unsigned long)iface->tx_packets,
                 (unsigned long long)iface->tx_bytes,
                 (unsigned long)iface->tx_errors);
        shell_print(buf);
    }

    /* Also show aggregate network stack statistics */
    const net_stats_t *agg = net_get_stats();
    if (agg) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "  [aggregate] RX: %lu pkts (%llu bytes), TX: %lu pkts (%llu bytes)\n",
                 (unsigned long)agg->rx_packets,
                 (unsigned long long)agg->rx_bytes,
                 (unsigned long)agg->tx_packets,
                 (unsigned long long)agg->tx_bytes);
        shell_print(buf);
    }

    shell_last_exit_code = 0;
}

/* ========================================================================
 * cmd_ftp — FTP client stub
 *
 * Full FTP client requires a complete TCP protocol-stack implementation
 * with proper state machine (PORT/PASV/EPRT/EPSV, binary mode, etc.).
 * Reserved for a future release.
 * ======================================================================== */
void cmd_ftp(const char *args) {
    if (!args || !*args) {
        shell_print("ftp - simple file transfer client\n");
        shell_print("Usage: ftp <command> [args]\n");
        shell_print("Subcommands:\n");
        shell_print("  open <host>      Connect to FTP server\n");
        shell_print("  close            Close current session\n");
        shell_print("  get <remote>     Download a file\n");
        shell_print("  put <local>      Upload a file\n");
        shell_print("  ls [path]        List remote directory\n");
        shell_print("  pwd              Print remote working directory\n");
        shell_print("  cd <path>        Change remote directory\n");
        shell_print("  quit             Exit ftp\n");
        shell_last_exit_code = 0;
        return;
    }
    shell_print("ftp: command '");
    shell_print(args);
    shell_print("' - FTP client requires a complete TCP protocol stack.\n");
    shell_print("      Reserved for the next release.\n");
    shell_last_exit_code = 0;
}
