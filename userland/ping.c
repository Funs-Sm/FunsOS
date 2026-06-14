/* ping.c - ICMP ping 工具 (用户态)
 * 改造说明: 原版 RTT 计算已损坏 (sys_get_ticks not available)。
 * 现已使用 SYS_GET_TICKS (140) 系统调用实现正确的 RTT 测量。
 * 100Hz 定时器 => 每tick = 10ms, RTT精度为10ms。
 */
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

static void print_dec(uint32_t val) {
    char tmp[16];
    int len = 0;
    if (val == 0) { sys_write(1, "0", 1); return; }
    while (val > 0) { tmp[len++] = '0' + (val % 10); val /= 10; }
    for (int i = len - 1; i >= 0; i--) sys_write(1, &tmp[i], 1);
}

static void print_ip(uint32_t ip) {
    for (int i = 0; i < 4; i++) {
        if (i > 0) sys_write(1, ".", 1);
        print_dec((ip >> (i * 8)) & 0xFF);
    }
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

    sys_write(1, "PING ", 5);
    print_ip(target_ip);
    sys_write(1, " 56 data bytes\n", 15);

    int sock = sys_socket(2, 3, 1); /* AF_INET, SOCK_RAW, ICMP */
    if (sock < 0) {
        sys_write(1, "ping: socket failed\n", 20);
        return 1;
    }

    uint32_t seq = 0;
    uint32_t sent = 0;
    uint32_t received = 0;
    uint32_t min_rtt = 0xFFFFFFFF, max_rtt = 0, total_rtt = 0;

    while (seq < 4) {
        uint8_t packet[64];
        memset(packet, 0, 64);

        packet[0] = 8;  /* ICMP Echo Request */
        packet[1] = 0;  /* Code */
        packet[4] = (seq >> 8) & 0xFF;
        packet[5] = seq & 0xFF;

        for (int i = 8; i < 56; i++) packet[i] = (uint8_t)i;

        uint16_t cs = checksum(packet, 56);
        packet[2] = cs & 0xFF;
        packet[3] = (cs >> 8) & 0xFF;

        struct {
            uint16_t family;
            uint16_t port;
            uint32_t addr;
            uint8_t zero[8];
        } dest;
        memset(&dest, 0, sizeof(dest));
        dest.family = 2;
        dest.addr = target_ip;

        uint32_t send_time = sys_get_ticks();

        sys_sendto(sock, packet, 56, 0, &dest);
        sent++;

        /* 设置接收超时: 等待约2秒 (200 ticks @ 100Hz) */
        uint32_t deadline = send_time + 200;
        uint8_t reply[128];
        int got_reply = 0;

        while (sys_get_ticks() < deadline) {
            int rlen = sys_recvfrom(sock, reply, 128, 0, 0);
            if (rlen > 0) {
                uint32_t recv_time = sys_get_ticks();
                /* RTT in ms: (recv - send) * 10 (100Hz timer) */
                uint32_t rtt_ms = (recv_time - send_time) * 10;
                if (rtt_ms < min_rtt) min_rtt = rtt_ms;
                if (rtt_ms > max_rtt) max_rtt = rtt_ms;
                total_rtt += rtt_ms;
                received++;

                sys_write(1, "Reply from ", 11);
                print_ip(target_ip);
                sys_write(1, " seq=", 5);
                print_dec(seq);
                sys_write(1, " time=", 6);
                print_dec(rtt_ms);
                sys_write(1, "ms\n", 3);
                got_reply = 1;
                break;
            }
        }

        if (!got_reply) {
            sys_write(1, "Request timed out\n", 18);
        }

        seq++;
        if (seq < 4) sys_sleep(1);
    }

    /* 统计 */
    sys_write(1, "\n--- ", 5);
    print_ip(target_ip);
    sys_write(1, " ping statistics ---\n", 21);

    sys_write(1, "Sent=", 5);
    print_dec(sent);
    sys_write(1, " Received=", 10);
    print_dec(received);
    sys_write(1, " Loss=", 6);
    print_dec((sent - received) * 100 / sent);
    sys_write(1, "%\n", 2);

    if (received > 0) {
        sys_write(1, "RTT min=", 8);
        print_dec(min_rtt);
        sys_write(1, "ms avg=", 7);
        print_dec(total_rtt / received);
        sys_write(1, "ms max=", 7);
        print_dec(max_rtt);
        sys_write(1, "ms\n", 3);
    }

    sys_close(sock);
    return 0;
}
