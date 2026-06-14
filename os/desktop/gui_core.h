#ifndef GUI_CORE_H
#define GUI_CORE_H

#include "stdint.h"

/* ── GUI 布局常量 ── */
#define GUI_TASKBAR_HEIGHT  28
#define GUI_FONT_SIZE       12
#define GUI_DESKTOP_BG      0xFF1E1E2E   /* 深蓝灰背景 */

/* ── 启动标志 ── */
#define GUI_BOOT_LOADING      0
#define GUI_BOOT_DESKTOP      1

void gui_core_init(int screen_w, int screen_h, uint32_t *framebuffer, uint32_t pitch);
void gui_core_run(void);
void gui_core_shutdown(void);

uint32_t  gui_get_screen_w(void);
uint32_t  gui_get_screen_h(void);
uint32_t *gui_get_framebuffer(void);
uint32_t  gui_get_pitch(void);

/* 应用启动接口 */
int  gui_launch_app(const char *app_name, const char *exec_path);
void gui_show_desktop(void);
void gui_hide_desktop(void);

/* 鼠标光标接口 */
void gui_cursor_set_pos(int x, int y);
void gui_cursor_get_pos(int *x, int *y);
void gui_cursor_render(void);
void gui_cursor_set_visible(int visible);

/* 桌面窗口ID获取 */
uint32_t gui_get_desktop_win(void);

#endif