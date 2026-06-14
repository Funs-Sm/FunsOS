#ifndef WINDOW_H
#define WINDOW_H

#include "gfx.h"
#include "stdint.h"

#define WINDOW_EVENT_NONE        0
#define WINDOW_EVENT_MOUSE_MOVE  1
#define WINDOW_EVENT_MOUSE_PRESS 2
#define WINDOW_EVENT_MOUSE_RELEASE 3
#define WINDOW_EVENT_KEY_PRESS   4
#define WINDOW_EVENT_KEY_RELEASE 5
#define WINDOW_EVENT_CLOSE       6
#define WINDOW_EVENT_MOVE        7
#define WINDOW_EVENT_RESIZE      8
#define WINDOW_EVENT_FOCUS       9
#define WINDOW_EVENT_UNFOCUS     10
#define WINDOW_EVENT_EXPOSE      11

typedef struct {
    uint32_t type;
    int32_t x;
    int32_t y;
    uint8_t button;
    uint8_t key;
    uint32_t modifiers;
} window_event_t;

#define WINDOW_FLAG_VISIBLE     0x01
#define WINDOW_FLAG_BORDER      0x02
#define WINDOW_FLAG_TITLE       0x04
#define WINDOW_FLAG_RESIZABLE   0x08
#define WINDOW_FLAG_CLOSABLE    0x10
#define WINDOW_FLAG_MINIMIZABLE 0x20

#define WINDOW_STATE_NORMAL     0
#define WINDOW_STATE_MINIMIZED  1
#define WINDOW_STATE_MAXIMIZED  2

typedef struct window_t {
    uint32_t id;
    char title[64];
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    uint32_t flags;
    uint32_t state;
    uint8_t focused;
    uint8_t dirty;
    gfx_context_t *context;
    struct window_t *parent;
    struct window_t *first_child;
    struct window_t *next_sibling;
    struct window_t *prev;
    struct window_t *next;
    void (*event_handler)(struct window_t *win, window_event_t *event);
    void *user_data;
    gfx_color_t bg_color;
    gfx_color_t border_color;
    gfx_color_t title_color;
    gfx_color_t title_text_color;
} window_t;

window_t *window_create(window_t *parent, const char *title, int32_t x, int32_t y, int32_t w, int32_t h, uint32_t flags);
void window_destroy(window_t *win);
void window_show(window_t *win);
void window_hide(window_t *win);
void window_move(window_t *win, int32_t x, int32_t y);
void window_resize(window_t *win, int32_t w, int32_t h);
void window_set_title(window_t *win, const char *title);
void window_invalidate(window_t *win);
gfx_context_t *window_get_context(window_t *win);

#endif
