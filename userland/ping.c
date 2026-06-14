#include "user_syscall.h"
#include "string.h"

static uint16_t checksum(void *data, uint32_t len) {
    uint32_t sum = 0;
    uint16_t *ptr = (uint16_t *)data;
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    if (len == 1) {
        sum += *(uint8_t *)ptr;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)(~sum);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        sys_write(1, "Usage: ping <ip>\n", 17);
        return 1;
    }

    uint32_t target_ip = 0;
    char *ip_str = argv[1];
    int part = 0;
    uint32_t octet = 0;
    for (int i = 0; ip_str[i]; i++) {
        if (ip_str[i] == '.') {
            target_ip |= (octet << (part * 8));
            part++;
            octet = 0;
        } else if (ip_str[i] >= '0' && ip_str[i] <= '9') {
            octet = octet * 10 + (ip_str[i] - '0');
        }
    }
    target_ip |= (octet << (part * 8));

    int sock = sys_socket(2, 3, 1);
    if (sock < 0) {
        sys_write(1, "ping: socket failed\n", 20);
        return 1;
    }

    uint32_t seq = 0;
    uint32_t sent = 0;
    uint32_t received = 0;
    uint32_t total_rtt = 0;

    while (seq < 4) {
        uint8_t packet[64];
        memset(packet, 0, 64);

        packet[0] = 8;
        packet[1] = 0;
        packet[2] = 0;
        packet[3] = 0;
        packet[4] = (seq >> 8) & 0xFF;
        packet[5] = seq & 0xFF;
        packet[6] = 0;
        packet[7] = 0;

        for (int i = 8; i < 56; i++) {
            packet[i] = (uint8_t)i;
        }

        uint16_t cs = checksum(packet, 56);
        packet[2] = cs & 0xFF;
        packet[3] = (cs >> 8) & 0xFF;

        uint32_t send_time = 0; /* sys_get_ticks not available */

        struct {
            uint16_t family;
            uint16_t port;
            uint32_t addr;
            uint8_t zero[8];
        } dest;
        memset(&dest, 0, sizeof(dest));
        dest.family = 2;
        dest.addr = target_ip;

        sys_sendto(sock, packet, 56, 0, &dest);
        sent++;

        uint8_t reply[128];
        int rlen = sys_recvfrom(sock, reply, 128, 0, 0);
        if (rlen > 0) {
            uint32_t recv_time = 0; /* sys_get_ticks not available */
            uint32_t rtt = 0; /* RTT calculation disabled */
            total_rtt += rtt;
            received++;

            char buf[128];
            int len = 0;
            const char *msg = "Reply from ";
            while (*msg) buf[len++] = *msg++;
            for (int i = 0; i < 4; i++) {
                if (i > 0) buf[len++] = '.';
                uint8_t oct = (target_ip >> (i * 8)) & 0xFF;
                if (oct >= 100) buf[len++] = '0' + oct / 100;
                if (oct >= 10) buf[len++] = '0' + (oct / 10) % 10;
                buf[len++] = '0' + oct % 10;
            }
            buf[len++] = ' ';
            const char *msg2 = "time=";
            while (*msg2) buf[len++] = *msg2++;
            if (rtt >= 1000) {
                buf[len++] = '0' + rtt / 1000;
                rtt %= 1000;
            }
            buf[len++] = '0' + rtt / 100;
            buf[len++] = 'm';
            buf[len++] = 's';
            buf[len++] = '\n';
            sys_write(1, buf, len);
        } else {
            sys_write(1, "Request timed out\n", 18);
        }

        seq++;
        sys_sleep(1);
    }

    char stats[128];
    int slen = 0;
    const char *s1 = "Sent=";
    while (*s1) stats[slen++] = *s1++;
    stats[slen++] = '0' + sent;
    const char *s2 = " Received=";
    while (*s2) stats[slen++] = *s2++;
    stats[slen++] = '0' + received;
    const char *s3 = " Avg RTT=";
    while (*s3) stats[slen++] = *s3++;
    if (received > 0) {
        uint32_t avg = total_rtt / received;
        stats[slen++] = '0' + avg / 100;
    }
    stats[slen++] = 'm';
    stats[slen++] = 's';
    stats[slen++] = '\n';
    sys_write(1, stats, slen);

    sys_close(sock);
    return 0;
}
