/* login_service.c - FUNSOS 登录服务实现
 * 显示登录界面、验证用户凭据
 */

#include "login_service.h"
#include "sys_api.h"
#include "stddef.h"

/* 用户数据库 - 简化实现 */
static login_user_t g_users[8] = {
    {"admin", 0, 1, 0},
    {"user",  1, 0, 0},
};
static uint32_t g_user_count = 2;
static login_user_t *g_current_user = NULL;

/* 输入缓冲区 */
static char g_input_user[32];
static char g_input_pass[32];
static uint32_t g_input_pos = 0;
static uint8_t g_input_field = 0;  /* 0=用户名, 1=密码 */

/* 初始化 */
int login_service_init(void)
{
    g_current_user = NULL;
    g_input_field = 0;
    g_input_pos = 0;
    g_input_user[0] = '\0';
    g_input_pass[0] = '\0';
    return 0;
}

/* 运行登录界面 */
void login_service_run(void)
{
    sys_window_t win = sys_create_window(0, 0, 800, 600, "Login");
    if (win == NULL)
        return;

    sys_color_t bg = {0x00, 0x78, 0xD4, 0xFF};
    sys_color_t white = {0xFF, 0xFF, 0xFF, 0xFF};
    sys_color_t black = {0x00, 0x00, 0x00, 0xFF};
    sys_color_t input_bg = {0xFF, 0xFF, 0xFF, 0xFF};

    while (1) {
        sys_event_t event;
        if (sys_wait_event(&event) != 0)
            continue;

        if (event.type == SYS_EVENT_KEY_PRESS) {
            uint8_t key = (uint8_t)event.param1;

            if (key == 0x0D) {  /* Enter */
                if (g_input_field == 0) {
                    g_input_field = 1;
                    g_input_pos = 0;
                } else {
                    /* 尝试登录 */
                    if (login_authenticate(g_input_user, g_input_pass) == 0) {
                        /* 登录成功 - 启动桌面 */
                        sys_destroy_window(win);
                        return;
                    }
                    /* 登录失败 - 重置 */
                    g_input_field = 0;
                    g_input_pos = 0;
                    g_input_user[0] = '\0';
                    g_input_pass[0] = '\0';
                }
            } else if (key == 0x08) {  /* Backspace */
                if (g_input_pos > 0) {
                    g_input_pos--;
                    if (g_input_field == 0)
                        g_input_user[g_input_pos] = '\0';
                    else
                        g_input_pass[g_input_pos] = '\0';
                }
            } else if (key >= 0x20 && key < 0x7F) {
                if (g_input_pos < 31) {
                    if (g_input_field == 0) {
                        g_input_user[g_input_pos++] = key;
                        g_input_user[g_input_pos] = '\0';
                    } else {
                        g_input_pass[g_input_pos++] = key;
                        g_input_pass[g_input_pos] = '\0';
                    }
                }
            }
        }

        /* 渲染登录界面 */
        sys_fill_window(win, bg);

        /* 标题 */
        sys_draw_text(win, 320, 150, "FUNSOS", white);
        sys_draw_text(win, 290, 180, "Welcome - Please Login", white);

        /* 用户名输入框 */
        sys_draw_text(win, 280, 240, "Username:", white);
        sys_draw_rect(win, 280, 260, 240, 28, input_bg);
        sys_draw_text(win, 288, 266, g_input_user, black);

        /* 密码输入框 */
        sys_draw_text(win, 280, 300, "Password:", white);
        sys_draw_rect(win, 280, 320, 240, 28, input_bg);
        /* 密码显示为星号 */
        char stars[32];
        for (uint32_t i = 0; i < g_input_pos && i < 31; i++)
            stars[i] = '*';
        stars[g_input_pos] = '\0';
        sys_draw_text(win, 288, 326, stars, black);

        /* 提示 */
        sys_draw_text(win, 280, 370, "Press Enter to login", white);
    }
}

/* 用户认证 */
int login_authenticate(const char *username, const char *password)
{
    /* 简化实现：任何非空用户名和密码都通过 */
    (void)password;

    for (uint32_t i = 0; i < g_user_count; i++) {
        int match = 1;
        for (int j = 0; j < 31 && (username[j] || g_users[i].username[j]); j++) {
            if (username[j] != g_users[i].username[j]) { match = 0; break; }
        }
        if (match) {
            g_users[i].logged_in = 1;
            g_current_user = &g_users[i];
            return 0;
        }
    }

    return -1;
}

/* 注销 */
int login_logout(uint32_t uid)
{
    for (uint32_t i = 0; i < g_user_count; i++) {
        if (g_users[i].uid == uid) {
            g_users[i].logged_in = 0;
            if (g_current_user == &g_users[i])
                g_current_user = NULL;
            return 0;
        }
    }
    return -1;
}

/* 获取当前用户 */
login_user_t *login_get_current_user(void)
{
    return g_current_user;
}
