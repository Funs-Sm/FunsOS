#include "tftp.h"
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

int tftp_get(const char *server_ip, const char *filename,
             uint8_t **out_data, uint32_t *out_size)
{
    if (!server_ip || !filename || !out_data || !out_size)
        return -1;

    *out_data = NULL;
    *out_size = 0;

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

    /* bind to an ephemeral port (49152-65535) */
    uint16_t local_port = 49152 + (uint16_t)(timer_get_ticks() & 0x3FFF);
    if (udp_socket_bind(sock, local_port) != 0) {
        udp_socket_close(sock);
        return -5;
    }

    /* build RRQ packet: opcode + filename + '\0' + "octet" + '\0' */
    uint32_t fname_len = strlen(filename);
    uint32_t rrq_len = 2 + fname_len + 1 + 5 + 1; /* opcode + name\0 + octet\0 */
    uint8_t *rrq_buf = (uint8_t *)kmalloc(rrq_len);
    if (!rrq_buf) {
        udp_socket_close(sock);
        return -9;
    }

    rrq_buf[0] = 0;
    rrq_buf[1] = TFTP_OPCODE_RRQ;
    memcpy(rrq_buf + 2, filename, fname_len + 1); /* includes trailing '\0' */
    memcpy(rrq_buf + 2 + fname_len + 1, "octet", 6); /* includes trailing '\0' */

    /* send RRQ to server port 69 */
    if (udp_sendto(iface, dst_ip, TFTP_SERVER_PORT, local_port,
                   rrq_buf, rrq_len) != 0) {
        kfree(rrq_buf);
        udp_socket_close(sock);
        return -6;
    }
    kfree(rrq_buf);

    /* allocate receive buffer */
    uint32_t data_cap = TFTP_BLOCK_SIZE * 64; /* start with 32 KB */
    uint8_t *file_data = (uint8_t *)kmalloc(data_cap);
    if (!file_data) {
        udp_socket_close(sock);
        return -9;
    }
    uint32_t file_size = 0;

    uint8_t pkt_buf[4 + TFTP_BLOCK_SIZE + 1];
    uint16_t server_port = TFTP_SERVER_PORT;
    uint16_t expected_block = 1;
    int result = 0;

    for (;;) {
        /* receive with timeout */
        uint32_t start = timer_get_ticks();
        int received = -1;
        int retries = 0;

        while (retries < TFTP_MAX_RETRIES) {
            ipv4_addr_t from_ip;
            uint16_t from_port;
            received = udp_socket_recvfrom(sock, pkt_buf, sizeof(pkt_buf),
                                           &from_ip, &from_port);
            if (received > 0) {
                server_port = from_port;
                break;
            }
            /* check timeout */
            if ((timer_get_ticks() - start) >= TFTP_TIMEOUT_TICKS) {
                retries++;
                start = timer_get_ticks();
                /* retransmit last ACK or RRQ */
                if (expected_block == 1) {
                    /* retransmit RRQ */
                    uint8_t rrq2[2 + 256 + 1 + 5 + 1];
                    rrq2[0] = 0;
                    rrq2[1] = TFTP_OPCODE_RRQ;
                    uint32_t fl = strlen(filename);
                    memcpy(rrq2 + 2, filename, fl + 1);
                    memcpy(rrq2 + 2 + fl + 1, "octet", 6);
                    udp_sendto(iface, dst_ip, TFTP_SERVER_PORT, local_port,
                               rrq2, 2 + fl + 1 + 6);
                } else {
                    /* retransmit ACK for previous block */
                    uint8_t ack[4];
                    ack[0] = 0;
                    ack[1] = TFTP_OPCODE_ACK;
                    ack[2] = (uint8_t)((expected_block - 1) >> 8);
                    ack[3] = (uint8_t)((expected_block - 1) & 0xFF);
                    udp_sendto(iface, dst_ip, server_port, local_port,
                               ack, 4);
                }
            }
        }

        if (received <= 0) {
            result = -7; /* timeout */
            break;
        }

        /* need at least 2 bytes for opcode */
        if (received < 2)
            continue;

        uint16_t opcode = ((uint16_t)pkt_buf[0] << 8) | pkt_buf[1];

        if (opcode == TFTP_OPCODE_ERROR) {
            result = -8;
            break;
        }

        if (opcode != TFTP_OPCODE_DATA)
            continue;

        /* DATA packet: opcode(2) + block_num(2) + data */
        if (received < 4)
            continue;

        uint16_t block_num = ((uint16_t)pkt_buf[2] << 8) | pkt_buf[3];

        if (block_num != expected_block)
            continue;

        uint32_t data_len = (uint32_t)received - 4;

        /* grow buffer if needed */
        if (file_size + data_len > data_cap) {
            uint32_t new_cap = data_cap * 2;
            if (new_cap > TFTP_MAX_FILE_SIZE) {
                result = -10;
                break;
            }
            uint8_t *new_buf = (uint8_t *)kmalloc(new_cap);
            if (!new_buf) {
                result = -9;
                break;
            }
            memcpy(new_buf, file_data, file_size);
            kfree(file_data);
            file_data = new_buf;
            data_cap = new_cap;
        }

        memcpy(file_data + file_size, pkt_buf + 4, data_len);
        file_size += data_len;

        /* send ACK */
        uint8_t ack[4];
        ack[0] = 0;
        ack[1] = TFTP_OPCODE_ACK;
        ack[2] = (uint8_t)(block_num >> 8);
        ack[3] = (uint8_t)(block_num & 0xFF);
        udp_sendto(iface, dst_ip, server_port, local_port, ack, 4);

        expected_block++;

        /* last block has < 512 bytes of data */
        if (data_len < TFTP_BLOCK_SIZE)
            break;
    }

    udp_socket_close(sock);

    if (result == 0) {
        *out_data = file_data;
        *out_size = file_size;
    } else {
        kfree(file_data);
    }

    return result;
}
