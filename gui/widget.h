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

widget_t *widget_create_button(int32_t x, int32_t y, int32_t w, int32_t h, const char *text);
widget_t *widget_create_label(int32_t x, int32_t y, const char *text);
widget_t *widget_create_textbox(int32_t x, int32_t y, int32_t w, int32_t h);
void widget_draw(gfx_context_t *ctx, widget_t *widget);
void widget_handle_event(widget_t *widget, window_event_t *event);
void widget_set_text(widget_t *widget, const char *text);
void widget_destroy(widget_t *widget);

#endif
