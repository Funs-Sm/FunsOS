/* notification.c - FUNSOS 通知服务实现
 * 在屏幕右上角显示和管理通知气泡
 */

#include "notification.h"
#include "sys_api.h"
#include "stddef.h"

/* 通知队列 */
static notification_t g_notifications[8];
static uint32_t g_notify_count = 0;

/* 通知尺寸 */
#define NOTIFY_WIDTH   280
#define NOTIFY_HEIGHT  80
#define NOTIFY_MARGIN  10

/* 初始化 */
int notification_init(void)
{
    g_notify_count = 0;
    return 0;
}

/* 发送通知（默认5秒） */
int notification_send(const char *title, const char *message, uint32_t type)
{
    return notification_send_timed(title, message, type, 5000);
}

/* 发送带持续时间通知 */
int notification_send_timed(const char *title, const char *message,
                            uint32_t type, uint32_t duration_ms)
{
    if (g_notify_count >= 8)
        return -1;

    notification_t *n = &g_notifications[g_notify_count];

    for (int i = 0; i < 63 && title[i]; i++)
        n->title[i] = title[i];
    n->title[63] = '\0';

    for (int i = 0; i < 255 && message[i]; i++)
        n->message[i] = message[i];
    n->message[255] = '\0';

    n->type = type;
    n->duration_ms = duration_ms;
    n->created_tick = sys_get_ticks();

    g_notify_count++;
    return 0;
}

/* 渲染通知 */
void notification_render(void)
{
    sys_window_t win = NULL;

    for (uint32_t i = 0; i < g_notify_count; i++) {
        notification_t *n = &g_notifications[i];
        int32_t x = 800 - NOTIFY_WIDTH - NOTIFY_MARGIN;  /* 右侧 */
        int32_t y = NOTIFY_MARGIN + i * (NOTIFY_HEIGHT + NOTIFY_MARGIN);

        /* 根据类型选择颜色 */
        sys_color_t bg;
        switch (n->type) {
        case NOTIFY_WARNING:
            bg.r = 0xFF; bg.g = 0xAA; bg.b = 0x00; bg.a = 0xFF;
            break;
        case NOTIFY_ERROR:
            bg.r = 0xE0; bg.g = 0x00; bg.b = 0x00; bg.a = 0xFF;
            break;
        case NOTIFY_SUCCESS:
            bg.r = 0x00; bg.g = 0x80; bg.b = 0x00; bg.a = 0xFF;
            break;
        default:
            bg.r = 0x00; bg.g = 0x78; bg.b = 0xD4; bg.a = 0xFF;
            break;
        }

        /* 绘制通知背景 */
        sys_draw_rect(win, x, y, NOTIFY_WIDTH, NOTIFY_HEIGHT, bg);

        /* 绘制标题和消息 */
        sys_color_t white = {0xFF, 0xFF, 0xFF, 0xFF};
        sys_draw_text(win, x + 12, y + 10, n->title, white);
        sys_draw_text(win, x + 12, y + 30, n->message, white);
    }
}

/* 更新通知 */
void notification_update(void)
{
    uint32_t ticks = sys_get_ticks();

    for (uint32_t i = 0; i < g_notify_count; ) {
        uint32_t elapsed = ticks - g_notifications[i].created_tick;
        if (elapsed >= g_notifications[i].duration_ms) {
            /* 移除过期通知 */
            for (uint32_t k = i; k < g_notify_count - 1; k++)
                g_notifications[k] = g_notifications[k + 1];
            g_notify_count--;
        } else {
            i++;
        }
    }
}

/* 清除所有通知 */
void notification_clear_all(void)
{
    g_notify_count = 0;
}
