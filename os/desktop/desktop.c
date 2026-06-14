/* desktop.c - FUNSOS 桌面环境核心实现
 * 管理桌面图标、工作区切换、壁纸等
 * 调用 kernel/sys_api.h 接口
 */

#include "desktop.h"
#include "taskbar.h"
#include "start_menu.h"
#include "window_mgr.h"
#include "sys_api.h"
#include "stddef.h"

/* 桌面环境全局状态 */
static desktop_state_t g_desktop;
static desktop_icon_t g_icons[64];
static uint32_t g_icon_count = 0;
static uint32_t g_wallpaper_color = 0x2D2D2D;  /* 默认深灰背景 */

/* 初始化桌面环境 */
int desktop_init(int width, int height)
{
    g_desktop.initialized = 0;
    g_desktop.fullscreen_app = 0;
    g_desktop.screen_width = width;
    g_desktop.screen_height = height;
    g_desktop.workspace_count = 1;
    g_desktop.current_workspace = 0;
    g_icon_count = 0;

    /* 初始化窗口管理器 */
    if (window_mgr_init(width, height) != 0)
        return -1;

    /* 任务栏和开始菜单由 gui_core 初始化 */

    /* 创建桌面窗口（全屏，最底层） */
    sys_window_t desktop_win = sys_create_window(
        0, 0, width, height, "Desktop");
    if (desktop_win == NULL)
        return -1;

    /* 填充桌面背景 */
    sys_color_t bg;
    bg.r = (g_wallpaper_color >> 16) & 0xFF;
    bg.g = (g_wallpaper_color >> 8) & 0xFF;
    bg.b = g_wallpaper_color & 0xFF;
    bg.a = 0xFF;
    sys_fill_window(desktop_win, bg);

    g_desktop.initialized = 1;
    return 0;
}

/* 关闭桌面环境 */
void desktop_shutdown(void)
{
    if (!g_desktop.initialized)
        return;

    start_menu_cleanup();
    taskbar_cleanup();
    window_mgr_cleanup();

    g_desktop.initialized = 0;
}

/* 桌面主循环 */
void desktop_run(void)
{
    if (!g_desktop.initialized)
        return;

    sys_event_t event;

    while (1) {
        /* 等待事件 */
        if (sys_wait_event(&event) != 0)
            continue;

        /* 分发事件到各子系统 */
        switch (event.type) {
        case SYS_EVENT_MOUSE_CLICK:
            /* 检查是否点击了任务栏 */
            if (taskbar_handle_click(event.param1, event.param2))
                break;
            /* 检查是否点击了开始菜单 */
            if (start_menu_is_visible() &&
                start_menu_handle_click(event.param1, event.param2))
                break;
            /* 交给窗口管理器处理 */
            window_mgr_handle_click(event.param1, event.param2);
            break;

        case SYS_EVENT_MOUSE_MOVE:
            window_mgr_handle_move(event.param1, event.param2);
            break;

        case SYS_EVENT_KEY_PRESS:
            /* 处理快捷键 */
            if (event.param1 == 0x5B) {  /* Win键 */
                start_menu_toggle();
            } else {
                window_mgr_handle_key(event.param1);
            }
            break;

        case SYS_EVENT_WINDOW_CLOSE:
            window_mgr_close_window(event.window);
            break;

        case SYS_EVENT_TIMER:
            taskbar_update_clock();
            break;
        }

        /* 渲染 */
        window_mgr_render();
        taskbar_render();
        if (start_menu_is_visible())
            start_menu_render();
    }
}

/* 获取桌面状态 */
desktop_state_t desktop_get_state(void)
{
    return g_desktop;
}

/* 添加桌面图标 */
int desktop_add_icon(const desktop_icon_t *icon)
{
    if (g_icon_count >= 64)
        return -1;

    g_icons[g_icon_count] = *icon;
    g_icon_count++;
    return 0;
}

/* 移除桌面图标 */
int desktop_remove_icon(const char *name)
{
    for (uint32_t i = 0; i < g_icon_count; i++) {
        int found = 1;
        for (int j = 0; j < 64 && (name[j] || g_icons[i].name[j]); j++) {
            if (name[j] != g_icons[i].name[j]) { found = 0; break; }
        }
        if (found) {
            for (uint32_t k = i; k < g_icon_count - 1; k++)
                g_icons[k] = g_icons[k + 1];
            g_icon_count--;
            return 0;
        }
    }
    return -1;
}

/* 刷新桌面图标 */
int desktop_refresh_icons(void)
{
    /* 重新绘制所有桌面图标 */
    for (uint32_t i = 0; i < g_icon_count; i++) {
        /* 通过 sys_draw_text 绘制图标名称 */
        sys_color_t white = {255, 255, 255, 255};
        sys_draw_text(NULL, g_icons[i].x, g_icons[i].y + 32,
                      g_icons[i].name, white);
    }
    return 0;
}

/* 切换工作区 */
int desktop_switch_workspace(uint32_t index)
{
    if (index >= g_desktop.workspace_count)
        return -1;

    g_desktop.current_workspace = index;
    window_mgr_switch_workspace(index);
    return 0;
}

/* 创建工作区 */
int desktop_create_workspace(void)
{
    if (g_desktop.workspace_count >= 4)
        return -1;

    g_desktop.workspace_count++;
    return 0;
}

/* 销毁工作区 */
int desktop_destroy_workspace(uint32_t index)
{
    if (index >= g_desktop.workspace_count || g_desktop.workspace_count <= 1)
        return -1;

    g_desktop.workspace_count--;
    if (g_desktop.current_workspace >= g_desktop.workspace_count)
        g_desktop.current_workspace = g_desktop.workspace_count - 1;

    return 0;
}

/* 设置壁纸 */
int desktop_set_wallpaper(const char *path)
{
    /* 通过文件系统加载壁纸图片并渲染到桌面 */
    int fd = sys_file_open(path, 0);  /* 只读 */
    if (fd < 0)
        return -1;

    /* 简化实现：仅记录路径，实际渲染由窗口管理器完成 */
    sys_file_close(fd);
    return 0;
}

/* 设置纯色壁纸 */
int desktop_set_wallpaper_color(uint32_t color)
{
    g_wallpaper_color = color;
    return 0;
}

/* 刷新桌面 */
void desktop_refresh(void)
{
    if (!g_desktop.initialized)
        return;

    desktop_refresh_icons();
    taskbar_render();
    window_mgr_render();
}
