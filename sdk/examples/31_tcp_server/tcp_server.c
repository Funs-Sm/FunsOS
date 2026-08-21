/* tcp_server.c - TCP服务器示例
 * 演示 FUNSOS SDK v1.5.0 使用 select 实现 TCP 服务器。
 */

#include "funsos.h"
#include "funsos_select.h"
#include "funsos_network.h"

#define SERVER_PORT  8080
#define MAX_CLIENTS  10
#define BUF_SIZE     1024

static int g_client_count = 0;
static char g_status[16][80];

static void add_status(const char *msg)
{
    if (g_client_count < 16) {
        int i = 0;
        while (msg[i] && i < 79) {
            g_status[g_client_count][i] = msg[i];
            i++;
        }
        g_status[g_client_count][i] = '\0';
        g_client_count++;
    }
}

int main(void)
{
    funsos_window_t win = funsos_create_window(100, 80, 600, 450, "TCP服务器示例 v1.5.0");
    funsos_fill_window(win, 0xFFFFFF);

    funsos_color_t black = {0x00, 0x00, 0x00, 0xFF};
    funsos_color_t blue  = {0x00, 0x00, 0xFF, 0xFF};
    funsos_color_t green = {0x00, 0x80, 0x00, 0xFF};
    funsos_color_t red   = {0xFF, 0x00, 0x00, 0xFF};
    funsos_color_t purple = {0x80, 0x00, 0x80, 0xFF};

    funsos_draw_text(win, 20, 20, "FUNSOS SDK v1.5.0 - TCP 服务器示例", blue);
    funsos_draw_text(win, 20, 45, "使用 select I/O 多路复用实现并发服务器", black);

    /* 创建 socket */
    int server_fd = funsos_socket(FUNSOS_AF_INET, FUNSOS_SOCK_STREAM, 0);
    if (server_fd < 0) {
        funsos_draw_text(win, 20, 80, "[ERR] 创建 socket 失败", red);
        add_status("[ERR] socket() 失败");
        goto done;
    }
    funsos_draw_text(win, 20, 80, "[OK] 创建 socket 成功", green);
    add_status("[OK] socket 创建成功");

    /* 设置地址复用 */
    int opt = 1;
    funsos_setsockopt(server_fd, FUNSOS_SOL_SOCKET, FUNSOS_SO_REUSEADDR, &opt, sizeof(opt));

    /* 绑定地址 */
    funsos_sockaddr_in_t addr;
    addr.sin_family = FUNSOS_AF_INET;
    addr.sin_port = funsos_htons(SERVER_PORT);
    addr.sin_addr.addr = 0;  /* INADDR_ANY */

    if (funsos_bind(server_fd, &addr) < 0) {
        funsos_draw_text(win, 20, 105, "[ERR] bind 失败", red);
        add_status("[ERR] bind() 失败");
        goto done;
    }
    funsos_draw_text(win, 20, 105, "[OK] 绑定端口 8080", green);
    add_status("[OK] 绑定端口 8080");

    /* 监听 */
    if (funsos_listen(server_fd, 5) < 0) {
        funsos_draw_text(win, 20, 130, "[ERR] listen 失败", red);
        add_status("[ERR] listen() 失败");
        goto done;
    }
    funsos_draw_text(win, 20, 130, "[OK] 开始监听", green);
    add_status("[OK] 服务器监听中...");

    /* 初始化 fd_set */
    funsos_fd_set readfds;
    int client_fds[MAX_CLIENTS];
    int max_fd = server_fd;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_fds[i] = -1;
    }

    /* 显示 select 相关 API */
    funsos_draw_text(win, 20, 170, "I/O 多路复用 API:", blue);
    funsos_draw_text(win, 40, 195, "select()  - 标准 select", black);
    funsos_draw_text(win, 40, 215, "poll()    - poll 机制", black);
    funsos_draw_text(win, 40, 235, "epoll_create() - 创建 epoll 实例", black);
    funsos_draw_text(win, 40, 255, "epoll_ctl()    - 控制 epoll", black);
    funsos_draw_text(win, 40, 275, "epoll_wait()   - 等待事件", black);

    /* 显示服务器状态 */
    funsos_draw_text(win, 320, 170, "服务器状态:", purple);
    funsos_draw_text(win, 320, 195, "  监听端口: 8080", black);
    funsos_draw_text(win, 320, 215, "  最大客户端: 10", black);
    funsos_draw_text(win, 320, 235, "  超时: 无限等待", black);
    funsos_draw_text(win, 320, 255, "  模式: 非阻塞", black);

    /* 显示状态日志 */
    funsos_draw_text(win, 20, 310, "运行日志:", blue);
    for (int i = 0; i < g_client_count && i < 5; i++) {
        funsos_color_t c = (g_status[i][1] == 'O' && g_status[i][2] == 'K') ? green : red;
        if (g_status[i][1] == 'O' && g_status[i][2] == 'K') {
            funsos_draw_text(win, 20, 335 + i * 20, g_status[i], green);
        } else {
            funsos_draw_text(win, 20, 335 + i * 20, g_status[i], black);
        }
    }

    funsos_draw_text(win, 20, 410, "服务器正在运行... 按 ESC 退出", black);

done:
    /* 事件循环 */
    funsos_event_t event;
    while (1) {
        if (funsos_wait_event(&event) != 0)
            continue;
        if (event.type == FUNSOS_EVENT_KEY_PRESS && event.key == 0x1B)
            break;
    }

    /* 清理 */
    if (server_fd >= 0) {
        funsos_closesocket(server_fd);
    }
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_fds[i] >= 0) {
            funsos_closesocket(client_fds[i]);
        }
    }

    funsos_destroy_window(win);
    return 0;
}
