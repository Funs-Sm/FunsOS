#include "ntp.h"
#include "udp.h"
#include "dns.h"
#include "net.h"
#include "kheap.h"
#include "string.h"
#include "timer.h"

/* ---- internal helpers ---- */

static int parse_ip_addr(const char *s, ipv4_addr_t *out)
{
    if (!s || !out) return -1;
    uint8_t b[4] = {0, 0, 0, 0};
    int n = 0;
    int v = 0;
    int any = 0;
    for (const char *p = s; ; p++) {
        char c = *p;
        if (c >= '0' && c <= '9') {
            v = v * 10 + (c - '0');
            any = 1;
            continue;
        }
        if (c == '.' || c == '\0') {
            if (!any || n >= 4 || v > 255) return -1;
            b[n++] = (uint8_t)v;
            v = 0;
            any = 0;
            if (c == '\0') break;
            continue;
        }
        return -1;
    }
    if (n != 4) return -1;
    out->addr = ((uint32_t)b[0]) | ((uint32_t)b[1] << 8) |
                ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return 0;
}

static int resolve_host(const char *host, ipv4_addr_t *out)
{
    if (parse_ip_addr(host, out) == 0)
        return 0;
    return dns_resolve(host, out);
}

/* ---- public API ---- */

int ntp_get_time(const char *server_ip, uint32_t *seconds, uint32_t *fraction)
{
    if (!server_ip || !seconds || !fraction)
        return -1;

    *seconds = 0;
    *fraction = 0;

    /* resolve server address */
    ipv4_addr_t dst_ip;
    if (resolve_host(server_ip, &dst_ip) != 0)
        return -2;

    /* get outgoing interface */
    net_interface_t *iface = net_get_default_interface();
    if (!iface)
        return -3;

    /* create UDP socket */
    udp_socket_t *sock = udp_socket_create();
    if (!sock)
        return -4;

    /* bind to an ephemeral port */
    uint16_t local_port = 49152 + (uint16_t)(timer_get_ticks() & 0x3FFF);
    if (udp_socket_bind(sock, local_port) != 0) {
        udp_socket_close(sock);
        return -5;
    }

    /* build NTP client request */
    ntp_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    /* LI=0, VN=4, Mode=3 (client) => 0x23 */
    pkt.li_vn_mode = (0 << 6) | (NTP_VERSION << 3) | NTP_MODE_CLIENT;

    /* send request */
    if (udp_sendto(iface, dst_ip, NTP_PORT, local_port,
                   &pkt, sizeof(pkt)) != 0) {
        udp_socket_close(sock);
        return -6;
    }

    /* receive with timeout and retries */
    ntp_packet_t resp;
    int got_response = 0;
    int retries = 0;

    while (retries < NTP_MAX_RETRIES) {
        uint32_t start = timer_get_ticks();

        for (;;) {
            ipv4_addr_t from_ip;
            uint16_t from_port;
            int received = udp_socket_recvfrom(sock, &resp, sizeof(resp),
                                               &from_ip, &from_port);
            if (received > 0) {
                got_response = 1;
                break;
            }
            if ((timer_get_ticks() - start) >= NTP_TIMEOUT_TICKS)
                break;
        }

        if (got_response)
            break;

        retries++;
        if (retries < NTP_MAX_RETRIES) {
            /* retransmit */
            memset(&pkt, 0, sizeof(pkt));
            pkt.li_vn_mode = (0 << 6) | (NTP_VERSION << 3) | NTP_MODE_CLIENT;
            udp_sendto(iface, dst_ip, NTP_PORT, local_port,
                       &pkt, sizeof(pkt));
        }
    }

    udp_socket_close(sock);

    if (!got_response)
        return -7;

    /* extract transmit timestamp (network byte order -> host order) */
    uint32_t ntp_sec  = (uint32_t)((resp.tx_timestamp_sec >> 24) & 0xFF) |
                        (uint32_t)((resp.tx_timestamp_sec >> 8)  & 0xFF00) |
                        (uint32_t)((resp.tx_timestamp_sec << 8)  & 0xFF0000) |
                        (uint32_t)((resp.tx_timestamp_sec << 24) & 0xFF000000);

    uint32_t ntp_frac = (uint32_t)((resp.tx_timestamp_frac >> 24) & 0xFF) |
                        (uint32_t)((resp.tx_timestamp_frac >> 8)  & 0xFF00) |
                        (uint32_t)((resp.tx_timestamp_frac << 8)  & 0xFF0000) |
                        (uint32_t)((resp.tx_timestamp_frac << 24) & 0xFF000000);

    /* convert from NTP epoch (1900) to Unix epoch (1970) */
    if (ntp_sec > NTP_UNIX_OFFSET)
        *seconds = ntp_sec - NTP_UNIX_OFFSET;
    else
        *seconds = 0;

    *fraction = ntp_frac;

    return 0;
}
