/* window_mgr.c - FUNSOS 窗口管理器实现
 * 窗口的创建、排列、焦点管理和渲染
 */

#include "window_mgr.h"
#include "taskbar.h"
#include "gui_core.h"
#include "sys_api.h"

/* 窗口管理器状态 */
static wm_window_t g_windows[32];
static uint32_t g_window_count = 0;
static uint32_t g_next_id = 1;
static uint32_t g_focused_id = 0;
static int32_t g_screen_width = 0;
static int32_t g_screen_height = 0;
static uint32_t g_current_workspace = 0;

/* 拖拽状态 */
static uint8_t g_dragging = 0;
static int32_t g_drag_offset_x = 0;
static int32_t g_drag_offset_y = 0;
static uint32_t g_drag_window_id = 0;

/* 初始化窗口管理器 */
int window_mgr_init(int screen_width, int screen_height)
{
    g_screen_width = screen_width;
    g_screen_height = screen_height;
    g_window_count = 0;
    g_next_id = 1;
    g_focused_id = 0;
    g_current_workspace = 0;
    return 0;
}

void window_mgr_cleanup(void)
{
    for (uint32_t i = 0; i < g_window_count; i++) {
        if (g_windows[i].sys_win)
            sys_destroy_window(g_windows[i].sys_win);
    }
    g_window_count = 0;
}

/* 创建窗口 */
uint32_t window_mgr_create_window(const char *title, int x, int y, int w, int h)
{
    if (g_window_count >= 32)
        return 0;

    wm_window_t *win = &g_windows[g_window_count];
    win->id = g_next_id++;
    win->x = x;
    win->y = y;
    win->width = w;
    win->height = h;
    win->workspace = g_current_workspace;
    win->focused = 1;
    win->minimized = 0;
    win->maximized = 0;

    /* 复制标题 */
    for (int i = 0; i < 63 && title[i]; i++)
        win->title[i] = title[i];
    win->title[63] = '\0';

    /* 创建内核窗口 */
    win->sys_win = sys_create_window(x, y, w, h, title);

    /* 添加到任务栏 */
    taskbar_add_window_button(win->id, win->title);

    /* 取消其他窗口焦点 */
    for (uint32_t i = 0; i < g_window_count; i++)
        g_windows[i].focused = 0;

    g_focused_id = win->id;
    g_window_count++;

    return win->id;
}

/* 关闭窗口 */
void window_mgr_close_window(sys_window_t sys_win)
{
    for (uint32_t i = 0; i < g_window_count; i++) {
        if (g_windows[i].sys_win == sys_win) {
            taskbar_remove_window_button(g_windows[i].id);
            sys_destroy_window(g_windows[i].sys_win);

            for (uint32_t k = i; k < g_window_count - 1; k++)
                g_windows[k] = g_windows[k + 1];
            g_window_count--;
            return;
        }
    }
}

/* 最小化窗口 */
void window_mgr_minimize(uint32_t window_id)
{
    for (uint32_t i = 0; i < g_window_count; i++) {
        if (g_windows[i].id == window_id) {
            g_windows[i].minimized = 1;
            return;
        }
    }
}

/* 最大化窗口 */
void window_mgr_maximize(uint32_t window_id)
{
    for (uint32_t i = 0; i < g_window_count; i++) {
        if (g_windows[i].id == window_id) {
            g_windows[i].maximized = 1;
            g_windows[i].x = 0;
            g_windows[i].y = 0;
            g_windows[i].width = g_screen_width;
            g_windows[i].height = g_screen_height - GUI_TASKBAR_HEIGHT;
            return;
        }
    }
}

/* 还原窗口 */
void window_mgr_restore(uint32_t window_id)
{
    for (uint32_t i = 0; i < g_window_count; i++) {
        if (g_windows[i].id == window_id) {
            g_windows[i].minimized = 0;
            g_windows[i].maximized = 0;
            return;
        }
    }
}

/* 设置焦点 */
void window_mgr_set_focus(uint32_t window_id)
{
    for (uint32_t i = 0; i < g_window_count; i++)
        g_windows[i].focused = 0;

    for (uint32_t i = 0; i < g_window_count; i++) {
        if (g_windows[i].id == window_id) {
            g_windows[i].focused = 1;
            g_focused_id = window_id;
            /* taskbar active tracking handled by gui_core */
            break;
        }
    }
}

/* 获取焦点窗口 */
uint32_t window_mgr_get_focused(void)
{
    return g_focused_id;
}

/* 切换工作区 */
void window_mgr_switch_workspace(uint32_t index)
{
    g_current_workspace = index;

    /* 隐藏不属于当前工作区的窗口 */
    for (uint32_t i = 0; i < g_window_count; i++) {
        if (g_windows[i].workspace == index) {
            /* 显示窗口 */
        } else {
            /* 隐藏窗口 */
        }
    }
}

/* 处理点击 */
void window_mgr_handle_click(int32_t x, int32_t y)
{
    /* 检查是否点击了窗口标题栏（拖拽区域） */
    for (int32_t i = g_window_count - 1; i >= 0; i--) {
        if (g_windows[i].minimized)
            continue;

        if (x >= g_windows[i].x && x < g_windows[i].x + g_windows[i].width &&
            y >= g_windows[i].y && y < g_windows[i].y + 28) {
            /* 点击标题栏 - 开始拖拽 */
            g_dragging = 1;
            g_drag_offset_x = x - g_windows[i].x;
            g_drag_offset_y = y - g_windows[i].y;
            g_drag_window_id = g_windows[i].id;
            window_mgr_set_focus(g_windows[i].id);
            return;
        }

        if (x >= g_windows[i].x && x < g_windows[i].x + g_windows[i].width &&
            y >= g_windows[i].y && y < g_windows[i].y + g_windows[i].height) {
            /* 点击窗口内部 */
            window_mgr_set_focus(g_windows[i].id);
            return;
        }
    }
}

/* 处理鼠标移动 */
void window_mgr_handle_move(int32_t x, int32_t y)
{
    if (g_dragging) {
        for (uint32_t i = 0; i < g_window_count; i++) {
            if (g_windows[i].id == g_drag_window_id) {
                g_windows[i].x = x - g_drag_offset_x;
                g_windows[i].y = y - g_drag_offset_y;
                break;
            }
        }
    }
}

/* 处理键盘事件 */
void window_mgr_handle_key(uint32_t key)
{
    /* Alt+F4 关闭窗口 */
    /* Alt+Tab 切换窗口 */
    /* 目前简化实现 */
    (void)key;
}

/* 渲染所有窗口 */
void window_mgr_render(void)
{
    for (uint32_t i = 0; i < g_window_count; i++) {
        if (g_windows[i].minimized)
            continue;

        wm_window_t *w = &g_windows[i];

        /* 窗口边框 */
        sys_color_t border = {0x00, 0x78, 0xD4, 0xFF};
        if (!w->focused) {
            border.r = 0x80; border.g = 0x80; border.b = 0x80;
        }
        sys_draw_rect(w->sys_win, w->x, w->y, w->width, 1, border);

        /* 标题栏 */
        sys_color_t title_bg = {0x2D, 0x2D, 0x2D, 0xFF};
        sys_draw_rect(w->sys_win, w->x, w->y, w->width, 28, title_bg);

        /* 标题文字 */
        sys_color_t white = {0xFF, 0xFF, 0xFF, 0xFF};
        sys_draw_text(w->sys_win, w->x + 8, w->y + 8, w->title, white);

        /* 窗口内容区 */
        sys_color_t content_bg = {0xFF, 0xFF, 0xFF, 0xFF};
        sys_draw_rect(w->sys_win, w->x, w->y + 28, w->width, w->height - 28, content_bg);
    }
}
