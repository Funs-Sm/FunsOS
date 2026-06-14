#ifndef WIDGET_H
#define WIDGET_H

#include "gfx.h"
#include "window.h"
#include "font.h"
#include "stdint.h"

#define WIDGET_BUTTON   0
#define WIDGET_LABEL    1
#define WIDGET_TEXTBOX  2

typedef struct widget_t {
    uint32_t type;
    gfx_rect_t bounds;
    char text[256];
    uint32_t fg_color;
    uint32_t bg_color;
    uint8_t focused;
    uint8_t hovered;
    uint8_t pressed;
    void (*on_click)(struct widget_t *widget);
    void (*on_key)(struct widget_t *widget, uint8_t key);
    void *user_data;
} widget_t;

/* ---- Extended widget types for FunRender compatibility ---- */
#define WIDGET_CHECKBOX   3
#define WIDGET_SLIDER     4
#define WIDGET_PROGRESS   5
#define WIDGET_RADIO      6
#define WIDGET_LISTBOX    7
#define WIDGET_MENU       8
#define WIDGET_TOOLBAR    9
#define WIDGET_STATUSBAR  10
#define WIDGET_TABVIEW    11
#define WIDGET_SCROLLBAR  12

/* Widget creation flags */
#define WIDGET_FLAG_VISIBLE  0x01
#define WIDGET_FLAG_ENABLED  0x02
#define WIDGET_FLAG_CHECKED  0x04
#define WIDGET_FLAG_MULTILINE 0x08

widget_t *widget_create_button(int32_t x, int32_t y, int32_t w, int32_t h, const char *text);
widget_t *widget_create_label(int32_t x, int32_t y, const char *text);
widget_t *widget_create_textbox(int32_t x, int32_t y, int32_t w, int32_t h);
widget_t *widget_create_checkbox(int32_t x, int32_t y, int32_t w, int32_t h, const char *text, uint8_t checked);
widget_t *widget_create_slider(int32_t x, int32_t y, int32_t w, int32_t h, int32_t min, int32_t max, int32_t val);
widget_t *widget_create_progress(int32_t x, int32_t y, int32_t w, int32_t h, int32_t val, int32_t max);
widget_t *widget_create_radio(int32_t x, int32_t y, const char *text, uint8_t selected, uint8_t group);
widget_t *widget_create_listbox(int32_t x, int32_t y, int32_t w, int32_t h);
widget_t *widget_create_menu(int32_t x, int32_t y, int32_t w, int32_t h);
widget_t *widget_create_toolbar(int32_t x, int32_t y, int32_t w, int32_t h);
widget_t *widget_create_statusbar(int32_t x, int32_t y, int32_t w, int32_t h);
widget_t *widget_create_tabview(int32_t x, int32_t y, int32_t w, int32_t h);
widget_t *widget_create_scrollbar(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t vertical);
void widget_draw(gfx_context_t *ctx, widget_t *widget);
void widget_handle_event(widget_t *widget, window_event_t *event);
void widget_set_text(widget_t *widget, const char *text);
void widget_set_bounds(widget_t *widget, gfx_rect_t bounds);
void widget_set_enabled(widget_t *widget, uint8_t enabled);
void widget_set_visible(widget_t *widget, uint8_t visible);
void widget_set_checked(widget_t *widget, uint8_t checked);
uint8_t widget_get_checked(widget_t *widget);
int32_t widget_get_value(widget_t *widget);
void widget_set_value(widget_t *widget, int32_t value);
void widget_destroy(widget_t *widget);

/* ---- Bridge: Old GUI Widget <-> FunRender Widget ---- */
typedef void *fr_widget_handle_t;

fr_widget_handle_t gui_widget_to_fr_widget(widget_t *gui_widget);
widget_t *fr_widget_to_gui_widget(fr_widget_handle_t fr_widget);
widget_t *widget_create_unified(uint32_t type, gfx_rect_t bounds, const char *text, uint32_t flags);
void widget_draw_unified(gfx_context_t *ctx, widget_t *widget, uint32_t render_backend);

#endif
