/* network_test.c - 网络编程示例程序
 * 演示 FUNSOS 中的网络编程，包括 TCP/UDP Socket、DNS 解析等。
 *
 * 功能说明：
 *   - 创建 TCP Socket
 *   - DNS 域名解析
 *   - TCP 客户端连接
 *   - UDP 数据报发送/接收
 *   - Socket 选项设置
 *
 * 使用的主要 API：
 *   - funsos_socket() - 创建 Socket
 *   - funsos_connect() - 连接服务器
 *   - funsos_send() - 发送数据
 *   - funsos_recv() - 接收数据
 *   - funsos_dns_resolve() - DNS 解析
 *   - funsos_setsockopt() - 设置 Socket 选项
 *   - funsos_closesocket() - 关闭 Socket
 */

#include "funsos.h"

/* 辅助函数：在窗口中绘制状态行 */
static void draw_status(funsos_window_t win, int y, const char *label,
                        const char *value, funsos_color_t color)
{
    funsos_draw_text(win, 20, y, label, FUNSOS_COLOR_BLACK);
    funsos_draw_text(win, 160, y, value, color);
}

/* 辅助函数：整数转字符串 */
static void int_to_str(int value, char *buf)
{
    char tmp[16];
    int pos = 0;
    int neg = 0;

    if (value < 0) {
        neg = 1;
        value = -value;
    }

    if (value == 0) {
        tmp[pos++] = '0';
    } else {
        while (value > 0 && pos < 15) {
            tmp[pos++] = '0' + (value % 10);
            value /= 10;
        }
    }

    int out = 0;
    if (neg) buf[out++] = '-';
    for (int i = pos - 1; i >= 0; i--) {
        buf[out++] = tmp[i];
    }
    buf[out] = '\0';
}

/* 辅助函数：IP 地址转字符串 */
static void ip_to_str(funsos_ipv4_t ip, char *buf)
{
    uint8_t *p = (uint8_t *)&ip.addr;
    int pos = 0;
    char num[4];

    /* 注意：网络字节序，按小端存储的话需要调整顺序 */
    for (int i = 0; i < 4; i++) {
        int val = p[i];
        int n = 0;
        if (val == 0) {
            num[n++] = '0';
        } else {
            while (val > 0) {
                num[n++] = '0' + (val % 10);
                val /= 10;
            }
        }
        for (int j = n - 1; j >= 0; j--) {
            buf[pos++] = num[j];
        }
        if (i < 3) buf[pos++] = '.';
    }
    buf[pos] = '\0';
}

int main(void)
{
    int sock;
    int ret;
    int line_y = 20;
    char buf[64];

    /* 创建窗口 */
    funsos_window_t win = funsos_create_window(80, 60, 550, 480, "Network Test Demo");
    funsos_fill_window(win, FUNSOS_COLOR_WHITE);

    /* 标题 */
    funsos_draw_text(win, 20, line_y, "=== Network Programming Demo ===", FUNSOS_COLOR_BLUE);
    line_y += 30;

    /* --- 1. 创建 TCP Socket --- */
    sock = funsos_socket(FUNSOS_AF_INET, FUNSOS_SOCK_STREAM, FUNSOS_IPPROTO_TCP);
    if (sock >= 0) {
        int_to_str(sock, buf);
        draw_status(win, line_y, "TCP socket:", buf, FUNSOS_COLOR_GREEN);
    } else {
        draw_status(win, line_y, "TCP socket:", "FAILED", FUNSOS_COLOR_RED);
    }
    line_y += 25;

    /* --- 2. 设置 Socket 选项 (SO_REUSEADDR) --- */
    if (sock >= 0) {
        int opt = 1;
        ret = funsos_setsockopt(sock, FUNSOS_SOL_SOCKET, FUNSOS_SO_REUSEADDR,
                                &opt, sizeof(opt));
        if (ret == 0) {
            draw_status(win, line_y, "SO_REUSEADDR:", "Enabled", FUNSOS_COLOR_GREEN);
        } else {
            draw_status(win, line_y, "SO_REUSEADDR:", "FAILED", FUNSOS_COLOR_RED);
        }
    }
    line_y += 25;

    /* --- 3. DNS 域名解析 --- */
    funsos_ipv4_t dns_addr;
    ret = funsos_dns_resolve("localhost", &dns_addr);
    if (ret == 0) {
        ip_to_str(dns_addr, buf);
        draw_status(win, line_y, "DNS resolve:", buf, FUNSOS_COLOR_GREEN);
    } else {
        draw_status(win, line_y, "DNS resolve:", "FAILED", FUNSOS_COLOR_ORANGE);
        /* 手动设置一个地址用于演示 */
        dns_addr.addr = 0x0100007F;  /* 127.0.0.1 网络字节序 */
        ip_to_str(dns_addr, buf);
        draw_status(win, line_y + 20, "Using fallback:", buf, FUNSOS_COLOR_ORANGE);
        line_y += 20;
    }
    line_y += 25;

    /* --- 4. 尝试连接 (演示用，可能会失败) --- */
    funsos_sockaddr_in_t server_addr;
    server_addr.sin_family = FUNSOS_AF_INET;
    server_addr.sin_port = funsos_htons(80);
    server_addr.sin_addr = dns_addr;
    memset(&server_addr.sin_zero, 0, sizeof(server_addr.sin_zero));

    ret = funsos_connect(sock, &server_addr);
    if (ret == 0) {
        draw_status(win, line_y, "TCP connect:", "Connected", FUNSOS_COLOR_GREEN);

        /* 发送 HTTP 请求 */
        const char *request = "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n";
        int sent = funsos_send(sock, request, strlen(request), 0);
        int_to_str(sent, buf);
        strcat(buf, " bytes sent");
        draw_status(win, line_y + 20, "HTTP request:", buf, FUNSOS_COLOR_GREEN);
        line_y += 20;

        /* 尝试接收响应 */
        char recv_buf[256];
        int recvd = funsos_recv(sock, recv_buf, sizeof(recv_buf) - 1, FUNSOS_MSG_DONTWAIT);
        if (recvd > 0) {
            int_to_str(recvd, buf);
            strcat(buf, " bytes received");
            draw_status(win, line_y + 20, "Response:", buf, FUNSOS_COLOR_GREEN);
        } else {
            draw_status(win, line_y + 20, "Response:", "No data", FUNSOS_COLOR_ORANGE);
        }
        line_y += 20;
    } else {
        draw_status(win, line_y, "TCP connect:", "Connection failed", FUNSOS_COLOR_ORANGE);
        draw_status(win, line_y + 20, "(Expected:", "no server running)", FUNSOS_COLOR_DARK_GRAY);
        line_y += 20;
    }
    line_y += 25;

    /* --- 5. 关闭 TCP Socket --- */
    if (sock >= 0) {
        funsos_closesocket(sock);
        draw_status(win, line_y, "Close socket:", "OK", FUNSOS_COLOR_GREEN);
    }
    line_y += 30;

    /* --- 6. 创建 UDP Socket --- */
    int udp_sock = funsos_socket(FUNSOS_AF_INET, FUNSOS_SOCK_DGRAM, FUNSOS_IPPROTO_UDP);
    if (udp_sock >= 0) {
        int_to_str(udp_sock, buf);
        draw_status(win, line_y, "UDP socket:", buf, FUNSOS_COLOR_GREEN);
    } else {
        draw_status(win, line_y, "UDP socket:", "FAILED", FUNSOS_COLOR_RED);
    }
    line_y += 25;

    /* --- 7. UDP 发送数据 --- */
    if (udp_sock >= 0) {
        funsos_sockaddr_in_t dest_addr;
        dest_addr.sin_family = FUNSOS_AF_INET;
        dest_addr.sin_port = funsos_htons(12345);
        dest_addr.sin_addr.addr = 0x0100007F;  /* 127.0.0.1 */
        memset(&dest_addr.sin_zero, 0, sizeof(dest_addr.sin_zero));

        const char *msg = "Hello UDP from FUNSOS!";
        int sent = funsos_sendto(udp_sock, msg, strlen(msg), 0, &dest_addr);
        int_to_str(sent, buf);
        strcat(buf, " bytes");
        draw_status(win, line_y, "UDP sendto:", buf, FUNSOS_COLOR_GREEN);
    }
    line_y += 25;

    /* --- 8. 关闭 UDP Socket --- */
    if (udp_sock >= 0) {
        funsos_closesocket(udp_sock);
        draw_status(win, line_y, "UDP close:", "OK", FUNSOS_COLOR_GREEN);
    }
    line_y += 30;

    /* 网络功能列表 */
    funsos_draw_text(win, 20, line_y, "Supported network features:", FUNSOS_COLOR_BLUE);
    line_y += 25;

    const char *features[] = {
        "  - TCP/IP 协议栈",
        "  - UDP 数据报",
        "  - DNS 域名解析",
        "  - Socket API (POSIX 兼容)",
        "  - I/O 多路复用 (select/poll)",
        "  - 原始套接字支持",
        NULL
    };

    for (int i = 0; features[i] != NULL; i++) {
        funsos_draw_text(win, 30, line_y, features[i], FUNSOS_COLOR_DARK_GRAY);
        line_y += 20;
    }

    /* 底部提示 */
    funsos_draw_line(win, 20, 440, 530, 440, FUNSOS_COLOR_GRAY);
    funsos_draw_text(win, 130, 455, "Press ESC to exit - Network Demo",
                     FUNSOS_COLOR_DARK_GRAY);

    /* 事件循环 */
    funsos_event_t event;
    while (1) {
        if (funsos_wait_event(&event) != 0)
            continue;
        if (event.type == FUNSOS_EVENT_KEY_PRESS && event.key == 0x1B)
            break;
        if (event.type == FUNSOS_EVENT_WINDOW_CLOSE)
            break;
    }

    funsos_destroy_window(win);
    return 0;
}
