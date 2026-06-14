/* window_mgr.h - FUNSOS 窗口管理器
 * 管理所有窗口的创建、销毁、移动、缩放和焦点
 */

#ifndef WINDOW_MGR_H
#define WINDOW_MGR_H

#include "stdint.h"
#include "sys_api.h"

/* 窗口信息 */
typedef struct {
    uint32_t id;
    char title[64];
    int32_t x, y;
    int32_t width, height;
    uint32_t workspace;
    uint8_t focused;
    uint8_t minimized;
    uint8_t maximized;
    sys_window_t sys_win;   /* 内核窗口句柄 */
} wm_window_t;

/* 初始化窗口管理器 */
int window_mgr_init(int screen_width, int screen_height);

/* 清理 */
void window_mgr_cleanup(void);

/* 窗口操作 */
uint32_t window_mgr_create_window(const char *title, int x, int y, int w, int h);
void window_mgr_close_window(sys_window_t win);
void window_mgr_minimize(uint32_t window_id);
void window_mgr_maximize(uint32_t window_id);
void window_mgr_restore(uint32_t window_id);

/* 焦点管理 */
void window_mgr_set_focus(uint32_t window_id);
uint32_t window_mgr_get_focused(void);

/* 工作区 */
void window_mgr_switch_workspace(uint32_t index);

/* 事件处理 */
void window_mgr_handle_click(int32_t x, int32_t y);
void window_mgr_handle_move(int32_t x, int32_t y);
void window_mgr_handle_key(uint32_t key);

/* 渲染 */
void window_mgr_render(void);

#endif /* WINDOW_MGR_H */
