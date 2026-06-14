#ifndef WM_H
#define WM_H

#include "window.h"

#define WM_TITLE_BAR_HEIGHT 20
#define WM_BORDER_WIDTH     2

#define WM_BUTTON_CLOSE     0
#define WM_BUTTON_MAXIMIZE  1
#define WM_BUTTON_MINIMIZE  2

#define WM_TASKBAR_HEIGHT   32
#define WM_RESIZE_HANDLE    12

void wm_init(uint32_t screen_w, uint32_t screen_h);
void wm_add_window(window_t *win);
void wm_remove_window(window_t *win);
void wm_render(gfx_context_t *screen);
void wm_handle_mouse(int32_t x, int32_t y, uint8_t buttons);
void wm_handle_key(uint8_t scancode, uint8_t pressed);
void wm_focus_window(window_t *win);
window_t *wm_get_focused(void);
void wm_move_to_top(window_t *win);
window_t *wm_get_window_at(int32_t x, int32_t y);

/* New window management functions */
void wm_move_window(window_t *win, int32_t x, int32_t y);
void wm_resize_window(window_t *win, uint32_t w, uint32_t h);
void wm_minimize_window(window_t *win);
void wm_maximize_window(window_t *win);
void wm_restore_window(window_t *win);
void wm_set_focus(window_t *win);
void wm_draw_taskbar(gfx_context_t *ctx);
void wm_arrange_cascade(void);
void wm_arrange_tile(void);

#endif
