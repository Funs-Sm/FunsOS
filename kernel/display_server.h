#ifndef DISPLAY_SERVER_H
#define DISPLAY_SERVER_H

#include "stdint.h"
#include "gfx.h"

/* Display server commands */
#define DS_CMD_CREATE_WINDOW    1
#define DS_CMD_DESTROY_WINDOW   2
#define DS_CMD_MOVE_WINDOW      3
#define DS_CMD_RESIZE_WINDOW    4
#define DS_CMD_SHOW_WINDOW      5
#define DS_CMD_HIDE_WINDOW      6
#define DS_CMD_SET_TITLE        7
#define DS_CMD_INVALIDATE       8
#define DS_CMD_GET_EVENT        9
#define DS_CMD_DRAW_PIXEL       10
#define DS_CMD_DRAW_RECT        11
#define DS_CMD_DRAW_LINE        12
#define DS_CMD_DRAW_TEXT        13
#define DS_CMD_BLIT             14
#define DS_CMD_SET_CURSOR       15

/* Event types */
#define DS_EVENT_KEY_PRESS      1
#define DS_EVENT_KEY_RELEASE    2
#define DS_EVENT_MOUSE_MOVE     3
#define DS_EVENT_MOUSE_PRESS    4
#define DS_EVENT_MOUSE_RELEASE  5
#define DS_EVENT_WINDOW_CLOSE   6
#define DS_EVENT_WINDOW_RESIZE  7
#define DS_EVENT_WINDOW_FOCUS   8
#define DS_EVENT_EXPOSE         9

typedef struct {
    uint32_t type;
    uint32_t window_id;
    uint32_t param1;
    uint32_t param2;
    uint32_t param3;
    uint32_t param4;
} ds_event_t;

typedef struct {
    uint32_t window_id;
    int32_t x, y;
    uint32_t width, height;
    uint32_t flags;
    char title[128];
    gfx_context_t *ctx;
    uint32_t visible;
    uint32_t focused;
} ds_window_t;

void display_server_init(uint32_t *fb, uint32_t width, uint32_t height, uint32_t pitch);
uint32_t ds_create_window(int32_t x, int32_t y, uint32_t w, uint32_t h, const char *title);
void ds_destroy_window(uint32_t win_id);
void ds_hide_window(uint32_t win_id);
void ds_show_window(uint32_t win_id);
uint32_t ds_find_window_by_title(const char *title);
void ds_move_window(uint32_t win_id, int32_t x, int32_t y);
void ds_resize_window(uint32_t win_id, uint32_t w, uint32_t h);
void ds_set_title(uint32_t win_id, const char *title);
void ds_invalidate(uint32_t win_id, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
int ds_get_event(uint32_t win_id, ds_event_t *event);
void ds_draw_pixel(uint32_t win_id, uint32_t x, uint32_t y, uint32_t color);
void ds_draw_rect(uint32_t win_id, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void ds_draw_text(uint32_t win_id, uint32_t x, uint32_t y, const char *text, uint32_t fg, uint32_t bg);
void ds_process_events(void);
void ds_render(void);
void ds_render_windows_only(void);  /* 仅渲染窗口帧和内容, 不清屏不画光标 */
uint32_t ds_is_running(void);

/* Shell command to start/stop display server */
void ds_start(void);
void ds_stop(void);

#endif
