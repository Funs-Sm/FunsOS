#ifndef TASKBAR_H
#define TASKBAR_H
#include "stdint.h"

#define TASKBAR_ITEM_MAX  20
#define TASKBAR_MENU_LEFT 0
#define TASKBAR_MENU_RIGHT 1

/* 任务栏菜单项 */
typedef struct {
    char label[32];
    int x, y, w, h;   /* 像素坐标 */
    uint32_t *submenu; /* 子菜单项索引列表(目前未实现) */
    int submenu_count;
    int is_app;        /* 1=应用启动项, 0=系统功能 */
    char exec_path[128];
} taskbar_menu_item_t;

/* 任务栏菜单 */
typedef struct {
    char title[32];
    taskbar_menu_item_t items[16];
    int item_count;
    int visible;
    int x, y, w, h;  /* 菜单位置 */
} taskbar_menu_t;

/* 初始化/清理 */
void taskbar_init(int screen_w, int screen_h, uint32_t *fb, uint32_t pitch);
void taskbar_cleanup(void);

/* 渲染(在gui_core中每帧调用) */
void taskbar_render(void);

/* 事件处理 */
int taskbar_handle_click(int x, int y);
int taskbar_handle_hover(int x, int y);

/* 时钟更新 */
void taskbar_update_clock(void);

/* 菜单管理 */
void taskbar_menu_show(taskbar_menu_t *menu);
void taskbar_menu_hide(void);
int taskbar_menu_is_visible(void);
taskbar_menu_t *taskbar_get_active_menu(void);

/* 应用启动(通过菜单点击触发) */
void taskbar_launch_app(const char *exec_path);

/* 窗口管理按钮 */
void taskbar_add_window_button(uint32_t win_id, const char *title);
void taskbar_remove_window_button(uint32_t win_id);

#endif