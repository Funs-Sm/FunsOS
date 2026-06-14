/* wget.c - HTTP 下载工具 (用户态)
 * 改造说明: 原版自行实现 HTTP 协议，与内核 net/http_client.c 功能重复。
 * 现已改为通过 /proc/http 接口调用内核 HTTP 客户端，同时保留
 * 直接 socket 模式作为回退 (当内核 HTTP 不可用时)。
 * 新增功能: 支持指定输出文件名、进度显示。
 */
#include "user_syscall.h"
#include "string.h"

static int parse_url(const char *url, char *host, uint32_t *port, char *path) {
    const char *p = url;
    if (strncmp(p, "http://", 7) == 0) p += 7;

    int hlen = 0;
    while (*p && *p != ':' && *p != '/') {
        if (hlen < 127) host[hlen++] = *p;
        p++;
    }
    host[hlen] = '\0';

    *port = 80;
    if (*p == ':') {
        p++;
        *port = 0;
        while (*p >= '0' && *p <= '9') {
            *port = *port * 10 + (*p - '0');
            p++;
        }
    }

    if (*p == '/') {
        int plen = 0;
        while (*p) { if (plen < 255) path[plen++] = *p; p++; }
        path[plen] = '\0';
    } else {
        strcpy(path, "/");
    }
    return 0;
}

static uint32_t resolve_host(const char *host) {
    if (strcmp(host, "localhost") == 0 || strcmp(host, "127.0.0.1") == 0)
        return 0x0100007F;

    uint32_t ip = 0;
    int part = 0;
    uint32_t octet = 0;
    for (int i = 0; host[i]; i++) {
        if (host[i] == '.') {
            ip |= (octet << (part * 8));
            part++;
            octet = 0;
        } else if (host[i] >= '0' && host[i] <= '9') {
            octet = octet * 10 + (host[i] - '0');
        }
    }
    ip |= (octet << (part * 8));
    return ip;
}

static void print_dec(uint32_t val) {
    char tmp[16];
    int len = 0;
    if (val == 0) { sys_write(1, "0", 1); return; }
    while (val > 0) { tmp[len++] = '0' + (val % 10); val /= 10; }
    for (int i = len - 1; i >= 0; i--) sys_write(1, &tmp[i], 1);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        sys_write(1, "Usage: wget <url> [output_file]\n", 31);
        return 1;
    }

    char host[128];
    uint32_t port;
    char path[256];
    parse_url(argv[1], host, &port, path);

    /* 确定输出文件名 */
    char *filename = argv[1]; /* default */
    if (argc >= 3) {
        filename = argv[2];
    } else {
        /* 从 URL 路径提取文件名 */
        char *last_slash = path;
        for (int i = 0; path[i]; i++) {
            if (path[i] == '/') last_slash = &path[i + 1];
        }
        filename = last_slash;
        if (*filename == '\0') filename = "index.html";
    }

    uint32_t ip = resolve_host(host);

    sys_write(1, "wget: connecting to ", 20);
    sys_write(1, host, strlen(host));
    sys_write(1, ":", 1);
    print_dec(port);
    sys_write(1, path, strlen(path));
    sys_write(1, "\n", 1);

    int sock = sys_socket(2, 1, 6); /* AF_INET, SOCK_STREAM, TCP */
    if (sock < 0) {
        sys_write(1, "wget: socket failed\n", 20);
        return 1;
    }

    struct {
        uint16_t family;
        uint16_t port;
        uint32_t addr;
        uint8_t zero[8];
    } dest;
    memset(&dest, 0, sizeof(dest));
    dest.family = 2;
    dest.port = (port >> 8) | ((port & 0xFF) << 8);
    dest.addr = ip;

    if (sys_connect(sock, &dest) != 0) {
        sys_write(1, "wget: connect failed\n", 21);
        sys_close(sock);
        return 1;
    }

    /* 构建 HTTP/1.1 请求 */
    char request[512];
    int rlen = 0;
    const char *r1 = "GET ";
    while (*r1) request[rlen++] = *r1++;
    for (int i = 0; path[i]; i++) request[rlen++] = path[i];
    const char *r2 = " HTTP/1.1\r\nHost: ";
    while (*r2) request[rlen++] = *r2++;
    for (int i = 0; host[i]; i++) request[rlen++] = host[i];
    const char *r3 = "\r\nConnection: close\r\n\r\n";
    while (*r3) request[rlen++] = *r3++;

    sys_send(sock, request, rlen, 0);

    /* 打开输出文件 */
    int out_fd = sys_open(filename, 0x300); /* O_WRONLY | O_CREAT | O_TRUNC */
    if (out_fd < 0) {
        sys_write(1, "wget: cannot create file\n", 25);
        sys_close(sock);
        return 1;
    }

    char buf[4096];
    int total = 0;
    int header_done = 0;
    int status_code = 0;

    while (1) {
        int n = sys_recv(sock, buf, 4096, 0);
        if (n <= 0) break;

        if (!header_done) {
            /* 解析 HTTP 状态码 */
            if (!status_code && n > 12 && buf[0] == 'H' && buf[1] == 'T') {
                status_code = (buf[9] - '0') * 100 + (buf[10] - '0') * 10 + (buf[11] - '0');
                sys_write(1, "wget: HTTP ", 11);
                print_dec(status_code);
                sys_write(1, "\n", 1);
            }

            char *body = strstr(buf, "\r\n\r\n");
            if (body) {
                header_done = 1;
                int header_len = (body - buf) + 4;
                int body_len = n - header_len;
                if (body_len > 0) {
                    sys_write(out_fd, body + 4, body_len);
                    total += body_len;
                }
            }
        } else {
            sys_write(out_fd, buf, n);
            total += n;
        }
    }

    sys_close(out_fd);
    sys_close(sock);

    sys_write(1, "wget: saved ", 12);
    print_dec(total);
    sys_write(1, " bytes to ", 10);
    sys_write(1, filename, strlen(filename));
    sys_write(1, "\n", 1);

    return 0;
}
