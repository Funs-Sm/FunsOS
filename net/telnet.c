#include "telnet.h"
#include "tcp.h"
#include "dns.h"
#include "net.h"
#include "kheap.h"
#include "string.h"

/* ---- module state ---- */

static tcp_socket_t *telnet_sock = NULL;

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

/**
 * Send a telnet negotiation response.
 * Responds WONT to WILL, DONT to DO.
 */
static int send_negotiation(uint8_t command, uint8_t option)
{
    if (!telnet_sock)
        return -1;

    uint8_t resp[3];
    resp[0] = TELNET_IAC;

    if (command == TELNET_WILL)
        resp[1] = TELNET_WONT;
    else if (command == TELNET_DO)
        resp[1] = TELNET_DONT;
    else
        return -1;

    resp[2] = option;
    return tcp_send(telnet_sock, resp, 3);
}

/**
 * Process a raw receive buffer, handling IAC sequences and
 * extracting only regular data bytes.
 *
 * @param raw     Raw received data
 * @param raw_len Length of raw data
 * @param out     Output buffer for regular data
 * @param out_max Maximum output buffer size
 * @return number of regular data bytes written to out
 */
static uint32_t process_telnet_data(const uint8_t *raw, uint32_t raw_len,
                                    char *out, uint32_t out_max)
{
    uint32_t out_len = 0;
    uint32_t i = 0;

    while (i < raw_len && out_len < out_max) {
        if (raw[i] != TELNET_IAC) {
            /* regular data byte */
            out[out_len++] = (char)raw[i];
            i++;
            continue;
        }

        /* IAC sequence */
        if (i + 1 >= raw_len) {
            /* incomplete: just IAC at end, skip */
            break;
        }

        uint8_t cmd = raw[i + 1];

        if (cmd == TELNET_IAC) {
            /* escaped 0xFF -> single 0xFF data byte */
            out[out_len++] = (char)0xFF;
            i += 2;
            continue;
        }

        if (cmd == TELNET_WILL || cmd == TELNET_WONT ||
            cmd == TELNET_DO   || cmd == TELNET_DONT) {
            /* 3-byte command: IAC + command + option */
            if (i + 2 >= raw_len)
                break; /* incomplete */

            uint8_t option = raw[i + 2];

            /* respond to WILL/DO negotiations; WONT/DONT are just acknowledged */
            if (cmd == TELNET_WILL || cmd == TELNET_DO)
                send_negotiation(cmd, option);

            i += 3;
            continue;
        }

        if (cmd == TELNET_SB) {
            /* subnegotiation: IAC SB ... IAC SE */
            uint32_t j = i + 2;
            int found_se = 0;
            while (j + 1 < raw_len) {
                if (raw[j] == TELNET_IAC && raw[j + 1] == TELNET_SE) {
                    found_se = 1;
                    j += 2;
                    break;
                }
                j++;
            }
            if (found_se) {
                i = j;
            } else {
                /* incomplete subnegotiation, skip rest */
                break;
            }
            continue;
        }

        /* other 2-byte IAC commands (like IAC SE, IAC NOP, etc.) */
        i += 2;
    }

    return out_len;
}

/* ---- public API ---- */

int telnet_connect(const char *host, uint16_t port)
{
    if (!host)
        return -1;

    if (telnet_sock)
        telnet_close();

    /* resolve host */
    ipv4_addr_t dst_ip;
    if (resolve_host(host, &dst_ip) != 0)
        return -2;

    /* get outgoing interface */
    net_interface_t *iface = net_get_default_interface();
    if (!iface)
        return -3;

    /* allocate ephemeral source port and connect */
    uint16_t src_port = tcp_ephemeral_alloc();
    tcp_socket_t *sock = tcp_connect(iface, dst_ip, port, src_port);
    if (!sock)
        return -4;

    telnet_sock = sock;
    return 0;
}

int telnet_send(const char *data, uint32_t len)
{
    if (!telnet_sock || !data)
        return -1;

    /* escape any 0xFF bytes in the data as IAC IAC */
    uint32_t ff_count = 0;
    for (uint32_t i = 0; i < len; i++) {
        if ((uint8_t)data[i] == 0xFF)
            ff_count++;
    }

    if (ff_count == 0) {
        /* no escaping needed */
        return tcp_send(telnet_sock, data, len);
    }

    /* build escaped buffer */
    uint32_t escaped_len = len + ff_count;
    uint8_t *buf = (uint8_t *)kmalloc(escaped_len);
    if (!buf)
        return -1;

    uint32_t j = 0;
    for (uint32_t i = 0; i < len; i++) {
        if ((uint8_t)data[i] == 0xFF) {
            buf[j++] = TELNET_IAC;
            buf[j++] = TELNET_IAC;
        } else {
            buf[j++] = (uint8_t)data[i];
        }
    }

    int sent = tcp_send(telnet_sock, buf, escaped_len);
    kfree(buf);
    return sent;
}

int32_t telnet_recv(char *buf, uint32_t len)
{
    if (!telnet_sock || !buf || len == 0)
        return -1;

    /* receive raw data into a temporary buffer */
    uint32_t raw_cap = len * 2 + 3; /* extra space for IAC sequences */
    uint8_t *raw = (uint8_t *)kmalloc(raw_cap);
    if (!raw)
        return -1;

    int32_t received = tcp_recv(telnet_sock, raw, raw_cap);
    if (received <= 0) {
        kfree(raw);
        return received;
    }

    /* process IAC sequences and extract regular data */
    uint32_t data_len = process_telnet_data(raw, (uint32_t)received, buf, len);
    kfree(raw);

    return (int32_t)data_len;
}

void telnet_close(void)
{
    if (telnet_sock) {
        tcp_close(telnet_sock);
        telnet_sock = NULL;
    }
}
