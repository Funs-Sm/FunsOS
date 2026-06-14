#include "user_syscall.h"
#include "string.h"

static int parse_url(const char *url, char *host, uint32_t *port, char *path) {
    const char *p = url;
    if (strncmp(p, "http://", 7) == 0) {
        p += 7;
    }

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
        while (*p) {
            if (plen < 255) path[plen++] = *p;
            p++;
        }
        path[plen] = '\0';
    } else {
        strcpy(path, "/");
    }

    return 0;
}

static uint32_t resolve_host(const char *host) {
    if (strcmp(host, "localhost") == 0 || strcmp(host, "127.0.0.1") == 0) {
        return 0x0100007F;
    }

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

int main(int argc, char *argv[]) {
    if (argc < 2) {
        sys_write(1, "Usage: wget <url>\n", 18);
        return 1;
    }

    char host[128];
    uint32_t port;
    char path[256];
    parse_url(argv[1], host, &port, path);

    uint32_t ip = resolve_host(host);

    int sock = sys_socket(2, 1, 6);
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

    char request[512];
    int rlen = 0;
    const char *r1 = "GET ";
    while (*r1) request[rlen++] = *r1++;
    for (int i = 0; path[i]; i++) request[rlen++] = path[i];
    const char *r2 = " HTTP/1.0\r\nHost: ";
    while (*r2) request[rlen++] = *r2++;
    for (int i = 0; host[i]; i++) request[rlen++] = host[i];
    const char *r3 = "\r\n\r\n";
    while (*r3) request[rlen++] = *r3++;

    sys_send(sock, request, rlen, 0);

    char *filename = path;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/') filename = &path[i + 1];
    }
    if (*filename == '\0') filename = "index.html";

    int out_fd = sys_open(filename, 0x42);
    if (out_fd < 0) {
        sys_write(1, "wget: cannot create file\n", 25);
        sys_close(sock);
        return 1;
    }

    char buf[4096];
    int total = 0;
    int header_done = 0;

    while (1) {
        int n = sys_recv(sock, buf, 4096, 0);
        if (n <= 0) break;

        if (!header_done) {
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

    char msg[64];
    int mlen = 0;
    const char *m1 = "Saved ";
    while (*m1) msg[mlen++] = *m1++;
    if (total >= 1024) {
        msg[mlen++] = '0' + total / 1024;
        msg[mlen++] = 'K';
    } else {
        msg[mlen++] = '0' + total;
    }
    const char *m2 = " bytes\n";
    while (*m2) msg[mlen++] = *m2++;
    sys_write(1, msg, mlen);

    return 0;
}
