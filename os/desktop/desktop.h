/* desktop.h - FUNSOS 桌面环境 API
 * 桌面环境管理器，负责协调窗口管理器、任务栏和开始菜单
 */

#ifndef DESKTOP_H
#define DESKTOP_H

#include "stdint.h"

/* 桌面环境状态 */
typedef struct {
    uint8_t initialized;
    uint8_t fullscreen_app;   /* 是否有全屏应用 */
    uint32_t screen_width;
    uint32_t screen_height;
    uint32_t workspace_count;
    uint32_t current_workspace;
} desktop_state_t;

/* 桌面图标定义 */
typedef struct {
    char name[64];
    char icon_path[128];
    char exec_path[128];
    int32_t x;
    int32_t y;
} desktop_icon_t;

/* 初始化桌面环境 */
int desktop_init(int width, int height);

/* 关闭桌面环境 */
void desktop_shutdown(void);

/* 桌面主循环 */
void desktop_run(void);

/* 获取桌面状态 */
desktop_state_t desktop_get_state(void);

/* 桌面图标管理 */
int desktop_add_icon(const desktop_icon_t *icon);
int desktop_remove_icon(const char *name);
int desktop_refresh_icons(void);

/* 工作区管理 */
int desktop_switch_workspace(uint32_t index);
int desktop_create_workspace(void);
int desktop_destroy_workspace(uint32_t index);

/* 壁纸设置 */
int desktop_set_wallpaper(const char *path);
int desktop_set_wallpaper_color(uint32_t color);

/* 刷新桌面 */
void desktop_refresh(void);

#endif /* DESKTOP_H */
