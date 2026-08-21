/* http_client.c - HTTP客户端示例
 * 演示 FUNSOS SDK v1.5.0 HTTP 客户端功能。
 */

#include "funsos.h"
#include "funsos_network.h"

#define HTTP_PORT   80
#define BUF_SIZE    4096

static char g_response[BUF_SIZE];
static int  g_response_len = 0;

int main(void)
{
    funsos_window_t win = funsos_create_window(100, 80, 650, 500, "HTTP客户端示例 v1.5.0");
    funsos_fill_window(win, 0xFFFFFF);

    funsos_color_t black = {0x00, 0x00, 0x00, 0xFF};
    funsos_color_t blue  = {0x00, 0x00, 0xFF, 0xFF};
    funsos_color_t green = {0x00, 0x80, 0x00, 0xFF};
    funsos_color_t red   = {0xFF, 0x00, 0x00, 0xFF};
    funsos_color_t dark_gray = {0x40, 0x40, 0x40, 0xFF};

    funsos_draw_text(win, 20, 20, "FUNSOS SDK v1.5.0 - HTTP 客户端示例", blue);
    funsos_draw_text(win, 20, 45, "使用套接字 API 实现简单 HTTP 客户端", black);

    /* 目标服务器信息 */
    const char *host = "example.com";
    const char *path = "/index.html";

    funsos_draw_text(win, 20, 80, "服务器: example.com", dark_gray);
    funsos_draw_text(win, 20, 100, "端口: 80 (HTTP)", dark_gray);
    funsos_draw_text(win, 20, 120, "请求: GET /index.html", dark_gray);

    /* 显示 HTTP 请求格式 */
    funsos_draw_text(win, 20, 160, "HTTP 请求格式:", blue);
    funsos_draw_text(win, 40, 185, "GET /path HTTP/1.1", black);
    funsos_draw_text(win, 40, 205, "Host: hostname", black);
    funsos_draw_text(win, 40, 225, "Connection: close", black);
    funsos_draw_text(win, 40, 245, "User-Agent: FUNSOS-SDK/1.5.0", black);
    funsos_draw_text(win, 40, 265, "", black);

    /* 显示支持的网络 API */
    funsos_draw_text(win, 20, 290, "网络 API 列表:", blue);
    funsos_draw_text(win, 40, 315, "socket()      - 创建套接字", black);
    funsos_draw_text(win, 40, 335, "connect()     - 连接服务器", black);
    funsos_draw_text(win, 40, 355, "send()/recv() - 发送/接收数据", black);
    funsos_draw_text(win, 40, 375, "closesocket() - 关闭套接字", black);
    funsos_draw_text(win, 40, 395, "setsockopt()  - 设置套接字选项", black);
    funsos_draw_text(win, 40, 415, "getsockopt()  - 获取套接字选项", black);

    /* 显示 HTTP 方法 */
    funsos_draw_text(win, 350, 160, "HTTP 方法:", blue);
    funsos_draw_text(win, 370, 185, "GET    - 获取资源", black);
    funsos_draw_text(win, 370, 205, "POST   - 提交数据", black);
    funsos_draw_text(win, 370, 225, "PUT    - 上传资源", black);
    funsos_draw_text(win, 370, 245, "DELETE - 删除资源", black);
    funsos_draw_text(win, 370, 265, "HEAD   - 获取头部", black);
    funsos_draw_text(win, 370, 285, "OPTIONS- 查询选项", black);

    /* 显示 HTTP 状态码 */
    funsos_draw_text(win, 350, 320, "常见状态码:", blue);
    funsos_draw_text(win, 370, 345, "200 OK", green);
    funsos_draw_text(win, 370, 365, "301 Moved Permanently", dark_gray);
    funsos_draw_text(win, 370, 385, "400 Bad Request", red);
    funsos_draw_text(win, 370, 405, "404 Not Found", red);
    funsos_draw_text(win, 370, 425, "500 Internal Server Error", red);

    funsos_draw_text(win, 20, 460, "按 ESC 退出", black);

    /* 事件循环 */
    funsos_event_t event;
    while (1) {
        if (funsos_wait_event(&event) != 0)
            continue;
        if (event.type == FUNSOS_EVENT_KEY_PRESS && event.key == 0x1B)
            break;
    }

    funsos_destroy_window(win);
    return 0;
}
