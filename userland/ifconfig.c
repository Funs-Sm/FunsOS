#include "user_syscall.h"
#include "string.h"

static void print_ip(uint32_t ip) {
    char buf[16];
    int len = 0;
    for (int i = 0; i < 4; i++) {
        if (i > 0) buf[len++] = '.';
        uint8_t oct = (ip >> (i * 8)) & 0xFF;
        if (oct >= 100) buf[len++] = '0' + oct / 100;
        if (oct >= 10) buf[len++] = '0' + (oct / 10) % 10;
        buf[len++] = '0' + oct % 10;
    }
    sys_write(1, buf, len);
}

static void print_mac(uint8_t *mac) {
    char buf[18];
    int len = 0;
    for (int i = 0; i < 6; i++) {
        if (i > 0) buf[len++] = ':';
        uint8_t hi = (mac[i] >> 4) & 0xF;
        uint8_t lo = mac[i] & 0xF;
        buf[len++] = (hi < 10) ? ('0' + hi) : ('a' + hi - 10);
        buf[len++] = (lo < 10) ? ('0' + lo) : ('a' + lo - 10);
    }
    sys_write(1, buf, len);
}

int main(int argc, char *argv[]) {
    if (argc >= 3) {
        int sock = sys_socket(2, 2, 0);
        if (sock < 0) {
            sys_write(1, "ifconfig: socket failed\n", 24);
            return 1;
        }

        struct {
            uint16_t family;
            uint16_t port;
            uint32_t addr;
            uint8_t zero[8];
        } addr;
        memset(&addr, 0, sizeof(addr));
        addr.family = 2;

        uint32_t ip = 0;
        char *ip_str = argv[2];
        int part = 0;
        uint32_t octet = 0;
        for (int i = 0; ip_str[i]; i++) {
            if (ip_str[i] == '.') {
                ip |= (octet << (part * 8));
                part++;
                octet = 0;
            } else if (ip_str[i] >= '0' && ip_str[i] <= '9') {
                octet = octet * 10 + (ip_str[i] - '0');
            }
        }
        ip |= (octet << (part * 8));
        addr.addr = ip;

        sys_ioctl(sock, 0x8916, &addr);
        sys_close(sock);
        sys_write(1, "Interface configured\n", 21);
        return 0;
    }

    int fd = sys_open("/proc/net/ifinfo", 0);
    if (fd < 0) {
        sys_write(1, "ifconfig: cannot open /proc/net/ifinfo\n", 39);
        return 1;
    }

    char buf[512];
    int n = sys_read(fd, buf, 511);
    if (n > 0) {
        buf[n] = '\0';
        sys_write(1, buf, n);
    }
    sys_close(fd);

    return 0;
}
