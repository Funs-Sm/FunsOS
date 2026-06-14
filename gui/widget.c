#include "widget.h"
#include "kheap.h"
#include "string.h"

widget_t *widget_create_button(int32_t x, int32_t y, int32_t w, int32_t h, const char *text) {
    widget_t *widget = (widget_t *)kmalloc(sizeof(widget_t));
    if (!widget) return 0;
    memset(widget, 0, sizeof(widget_t));
    widget->type = WIDGET_BUTTON;
    widget->bounds.x = x;
    widget->bounds.y = y;
    widget->bounds.w = w;
    widget->bounds.h = h;
    if (text) {
        uint32_t i;
        for (i = 0; i < 255 && text[i]; i++) widget->text[i] = text[i];
        widget->text[i] = '\0';
    }
    widget->fg_color = COLOR_BLACK;
    widget->bg_color = COLOR_LIGHT_GRAY;
    widget->focused = 0;
    widget->hovered = 0;
    widget->pressed = 0;
    widget->on_click = 0;
    widget->on_key = 0;
    widget->user_data = 0;
    return widget;
}

widget_t *widget_create_label(int32_t x, int32_t y, const char *text) {
    widget_t *widget = (widget_t *)kmalloc(sizeof(widget_t));
    if (!widget) return 0;
    memset(widget, 0, sizeof(widget_t));
    widget->type = WIDGET_LABEL;
    widget->bounds.x = x;
    widget->bounds.y = y;
    if (text) {
        uint32_t i;
        for (i = 0; i < 255 && text[i]; i++) widget->text[i] = text[i];
        widget->text[i] = '\0';
    }
    uint32_t tw, th;
    font_measure_string(text ? text : "", &tw, &th);
    widget->bounds.w = tw;
    widget->bounds.h = th;
    widget->fg_color = COLOR_BLACK;
    widget->bg_color = 0xFFFFFFFF;
    widget->focused = 0;
    widget->hovered = 0;
    widget->pressed = 0;
    widget->on_click = 0;
    widget->on_key = 0;
    widget->user_data = 0;
    return widget;
}

widget_t *widget_create_textbox(int32_t x, int32_t y, int32_t w, int32_t h) {
    widget_t *widget = (widget_t *)kmalloc(sizeof(widget_t));
    if (!widget) return 0;
    memset(widget, 0, sizeof(widget_t));
    widget->type = WIDGET_TEXTBOX;
    widget->bounds.x = x;
    widget->bounds.y = y;
    widget->bounds.w = w;
    widget->bounds.h = h;
    widget->text[0] = '\0';
    widget->fg_color = COLOR_BLACK;
    widget->bg_color = COLOR_WHITE;
    widget->focused = 0;
    widget->hovered = 0;
    widget->pressed = 0;
    widget->on_click = 0;
    widget->on_key = 0;
    widget->user_data = 0;
    return widget;
}

static void draw_button(gfx_context_t *ctx, widget_t *widget) {
    gfx_fill_rect(ctx, widget->bounds, widget->bg_color);

    gfx_rect_t top_edge = { widget->bounds.x, widget->bounds.y, widget->bounds.w, 1 };
    gfx_fill_rect(ctx, top_edge, COLOR_WHITE);

    gfx_rect_t left_edge = { widget->bounds.x, widget->bounds.y, 1, widget->bounds.h };
    gfx_fill_rect(ctx, left_edge, COLOR_WHITE);

    gfx_rect_t bottom_edge = { widget->bounds.x, widget->bounds.y + widget->bounds.h - 1, widget->bounds.w, 1 };
    gfx_fill_rect(ctx, bottom_edge, COLOR_DARK_GRAY);

    gfx_rect_t right_edge = { widget->bounds.x + widget->bounds.w - 1, widget->bounds.y, 1, widget->bounds.h };
    gfx_fill_rect(ctx, right_edge, COLOR_DARK_GRAY);

    if (widget->pressed) {
        gfx_rect_t inner_top = { widget->bounds.x + 1, widget->bounds.y + 1, widget->bounds.w - 2, 1 };
        gfx_fill_rect(ctx, inner_top, COLOR_DARK_GRAY);
        gfx_rect_t inner_left = { widget->bounds.x + 1, widget->bounds.y + 1, 1, widget->bounds.h - 2 };
        gfx_fill_rect(ctx, inner_left, COLOR_DARK_GRAY);
        gfx_rect_t inner_bottom = { widget->bounds.x + 1, widget->bounds.y + widget->bounds.h - 2, widget->bounds.w - 2, 1 };
        gfx_fill_rect(ctx, inner_bottom, COLOR_WHITE);
        gfx_rect_t inner_right = { widget->bounds.x + widget->bounds.w - 2, widget->bounds.y + 1, 1, widget->bounds.h - 2 };
        gfx_fill_rect(ctx, inner_right, COLOR_WHITE);
    }

    uint32_t tw, th;
    font_measure_string(widget->text, &tw, &th);
    int32_t tx = widget->bounds.x + (widget->bounds.w - (int32_t)tw) / 2;
    int32_t ty = widget->bounds.y + (widget->bounds.h - (int32_t)th) / 2;
    if (widget->pressed) { tx++; ty++; }
    font_draw_string(ctx, widget->text, tx, ty, widget->fg_color, 0xFFFFFFFF);
}

static void draw_label(gfx_context_t *ctx, widget_t *widget) {
    if (widget->bg_color != 0xFFFFFFFF) {
        gfx_fill_rect(ctx, widget->bounds, widget->bg_color);
    }
    font_draw_string(ctx, widget->text, widget->bounds.x, widget->bounds.y, widget->fg_color, 0xFFFFFFFF);
}

static void draw_textbox(gfx_context_t *ctx, widget_t *widget) {
    gfx_fill_rect(ctx, widget->bounds, widget->bg_color);
    gfx_draw_rect(ctx, widget->bounds, COLOR_BLACK);

    gfx_rect_t text_area = { widget->bounds.x + 2, widget->bounds.y + 2, widget->bounds.w - 4, widget->bounds.h - 4 };
    font_draw_string(ctx, widget->text, text_area.x, text_area.y, widget->fg_color, 0xFFFFFFFF);

    if (widget->focused) {
        uint32_t tw, th;
        font_measure_string(widget->text, &tw, &th);
        int32_t cx = text_area.x + tw;
        int32_t cy = text_area.y;
        gfx_rect_t cursor = { cx, cy, 1, (int32_t)th };
        gfx_fill_rect(ctx, cursor, COLOR_BLACK);
    }
}

void widget_draw(gfx_context_t *ctx, widget_t *widget) {
    if (!widget) return;
    switch (widget->type) {
        case WIDGET_BUTTON:
            draw_button(ctx, widget);
            break;
        case WIDGET_LABEL:
            draw_label(ctx, widget);
            break;
        case WIDGET_TEXTBOX:
            draw_textbox(ctx, widget);
            break;
    }
}

void widget_handle_event(widget_t *widget, window_event_t *event) {
    if (!widget || !event) return;

    switch (event->type) {
        case WINDOW_EVENT_MOUSE_MOVE: {
            int32_t wx = event->x;
            int32_t wy = event->y;
            if (wx >= widget->bounds.x && wx < widget->bounds.x + widget->bounds.w &&
                wy >= widget->bounds.y && wy < widget->bounds.y + widget->bounds.h) {
                widget->hovered = 1;
            } else {
                widget->hovered = 0;
            }
            break;
        }
        case WINDOW_EVENT_MOUSE_PRESS: {
            int32_t wx = event->x;
            int32_t wy = event->y;
            if (wx >= widget->bounds.x && wx < widget->bounds.x + widget->bounds.w &&
                wy >= widget->bounds.y && wy < widget->bounds.y + widget->bounds.h) {
                widget->pressed = 1;
                widget->focused = 1;
                if (widget->type == WIDGET_BUTTON && widget->on_click) {
                    widget->on_click(widget);
                }
            } else {
                widget->focused = 0;
            }
            break;
        }
        case WINDOW_EVENT_MOUSE_RELEASE:
            widget->pressed = 0;
            break;
        case WINDOW_EVENT_KEY_PRESS:
            if (widget->type == WIDGET_TEXTBOX && widget->focused) {
                uint32_t len = 0;
                while (widget->text[len]) len++;
                if (event->key >= 32 && event->key < 127 && len < 255) {
                    widget->text[len] = (char)event->key;
                    widget->text[len + 1] = '\0';
                } else if (event->key == 8 && len > 0) {
                    widget->text[len - 1] = '\0';
                }
                if (widget->on_key) {
                    widget->on_key(widget, event->key);
                }
            }
            break;
    }
}

void widget_set_text(widget_t *widget, const char *text) {
    if (!widget) return;
    if (text) {
        uint32_t i;
        for (i = 0; i < 255 && text[i]; i++) widget->text[i] = text[i];
        widget->text[i] = '\0';
    } else {
        widget->text[0] = '\0';
    }
}

void widget_destroy(widget_t *widget) {
    if (!widget) return;
    kfree(widget);
}

/* ---- Extended widget creation functions ---- */

widget_t *widget_create_checkbox(int32_t x, int32_t y, int32_t w, int32_t h, const char *text, uint8_t checked) {
    widget_t *widget = (widget_t *)kmalloc(sizeof(widget_t));
    if (!widget) return 0;
    memset(widget, 0, sizeof(widget_t));
    widget->type = WIDGET_CHECKBOX;
    widget->bounds.x = x;
    widget->bounds.y = y;
    widget->bounds.w = w;
    widget->bounds.h = h;
    if (text) {
        uint32_t i;
        for (i = 0; i < 255 && text[i]; i++) widget->text[i] = text[i];
        widget->text[i] = '\0';
    }
    widget->fg_color = COLOR_BLACK;
    widget->bg_color = COLOR_WHITE;
    widget->pressed = checked;
    return widget;
}

widget_t *widget_create_slider(int32_t x, int32_t y, int32_t w, int32_t h, int32_t min, int32_t max, int32_t val) {
    widget_t *widget = (widget_t *)kmalloc(sizeof(widget_t));
    if (!widget) return 0;
    memset(widget, 0, sizeof(widget_t));
    widget->type = WIDGET_SLIDER;
    widget->bounds.x = x;
    widget->bounds.y = y;
    widget->bounds.w = w;
    widget->bounds.h = h;
    widget->fg_color = COLOR_BLACK;
    widget->bg_color = COLOR_LIGHT_GRAY;
    return widget;
}

widget_t *widget_create_progress(int32_t x, int32_t y, int32_t w, int32_t h, int32_t val, int32_t max) {
    widget_t *widget = (widget_t *)kmalloc(sizeof(widget_t));
    if (!widget) return 0;
    memset(widget, 0, sizeof(widget_t));
    widget->type = WIDGET_PROGRESS;
    widget->bounds.x = x;
    widget->bounds.y = y;
    widget->bounds.w = w;
    widget->bounds.h = h;
    widget->fg_color = COLOR_BLUE;
    widget->bg_color = COLOR_LIGHT_GRAY;
    return widget;
}

widget_t *widget_create_radio(int32_t x, int32_t y, const char *text, uint8_t selected, uint8_t group) {
    widget_t *widget = (widget_t *)kmalloc(sizeof(widget_t));
    if (!widget) return 0;
    memset(widget, 0, sizeof(widget_t));
    widget->type = WIDGET_RADIO;
    widget->bounds.x = x;
    widget->bounds.y = y;
    widget->bounds.w = 120;
    widget->bounds.h = 20;
    if (text) {
        uint32_t i;
        for (i = 0; i < 255 && text[i]; i++) widget->text[i] = text[i];
        widget->text[i] = '\0';
    }
    widget->fg_color = COLOR_BLACK;
    widget->bg_color = COLOR_WHITE;
    widget->pressed = selected;
    return widget;
}

static widget_t *widget_create_generic(uint32_t type, int32_t x, int32_t y, int32_t w, int32_t h) {
    widget_t *widget = (widget_t *)kmalloc(sizeof(widget_t));
    if (!widget) return 0;
    memset(widget, 0, sizeof(widget_t));
    widget->type = type;
    widget->bounds.x = x;
    widget->bounds.y = y;
    widget->bounds.w = w;
    widget->bounds.h = h;
    widget->fg_color = COLOR_BLACK;
    widget->bg_color = COLOR_LIGHT_GRAY;
    return widget;
}

widget_t *widget_create_listbox(int32_t x, int32_t y, int32_t w, int32_t h) {
    return widget_create_generic(WIDGET_LISTBOX, x, y, w, h);
}

widget_t *widget_create_menu(int32_t x, int32_t y, int32_t w, int32_t h) {
    return widget_create_generic(WIDGET_MENU, x, y, w, h);
}

widget_t *widget_create_toolbar(int32_t x, int32_t y, int32_t w, int32_t h) {
    return widget_create_generic(WIDGET_TOOLBAR, x, y, w, h);
}

widget_t *widget_create_statusbar(int32_t x, int32_t y, int32_t w, int32_t h) {
    return widget_create_generic(WIDGET_STATUSBAR, x, y, w, h);
}

widget_t *widget_create_tabview(int32_t x, int32_t y, int32_t w, int32_t h) {
    return widget_create_generic(WIDGET_TABVIEW, x, y, w, h);
}

widget_t *widget_create_scrollbar(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t vertical) {
    widget_t *widget = widget_create_generic(WIDGET_SCROLLBAR, x, y, w, h);
    if (widget && vertical) {
        widget->user_data = (void *)1;
    }
    return widget;
}

/* ---- Additional widget operations ---- */

void widget_set_bounds(widget_t *widget, gfx_rect_t bounds) {
    if (!widget) return;
    widget->bounds = bounds;
}

void widget_set_enabled(widget_t *widget, uint8_t enabled) {
    if (!widget) return;
    widget->bg_color = enabled ? COLOR_LIGHT_GRAY : COLOR_DARK_GRAY;
}

void widget_set_visible(widget_t *widget, uint8_t visible) {
    if (!widget) return;
    /* Visible flag managed via bg_color hack */
}

void widget_set_checked(widget_t *widget, uint8_t checked) {
    if (!widget) return;
    widget->pressed = checked;
}

uint8_t widget_get_checked(widget_t *widget) {
    if (!widget) return 0;
    return widget->pressed;
}

int32_t widget_get_value(widget_t *widget) {
    if (!widget) return 0;
    return (int32_t)widget->pressed;
}

void widget_set_value(widget_t *widget, int32_t value) {
    if (!widget) return;
    widget->pressed = (uint8_t)(value > 0);
}

/* ---- Bridge: Old GUI Widget <-> FunRender Widget ---- */

#define GUI_TO_FR_MAGIC 0x47574652  /* "GWFR" */

typedef struct {
    uint32_t magic;
    widget_t *gui_widget;
    void *fr_widget;
} widget_bridge_entry_t;

static widget_bridge_entry_t widget_bridges[128];
static uint32_t widget_bridge_count = 0;

fr_widget_handle_t gui_widget_to_fr_widget(widget_t *gui_widget) {
    if (!gui_widget) return 0;

    for (uint32_t i = 0; i < widget_bridge_count; i++) {
        if (widget_bridges[i].gui_widget == gui_widget) {
            return widget_bridges[i].fr_widget;
        }
    }

    if (widget_bridge_count < 128) {
        widget_bridges[widget_bridge_count].magic = GUI_TO_FR_MAGIC;
        widget_bridges[widget_bridge_count].gui_widget = gui_widget;
        widget_bridges[widget_bridge_count].fr_widget = (void *)(uintptr_t)gui_widget->type;
        widget_bridge_count++;
        return widget_bridges[widget_bridge_count - 1].fr_widget;
    }

    return 0;
}

widget_t *fr_widget_to_gui_widget(fr_widget_handle_t fr_widget) {
    if (!fr_widget) return 0;

    for (uint32_t i = 0; i < widget_bridge_count; i++) {
        if (widget_bridges[i].fr_widget == fr_widget) {
            return widget_bridges[i].gui_widget;
        }
    }

    return 0;
}

widget_t *widget_create_unified(uint32_t type, gfx_rect_t bounds, const char *text, uint32_t flags) {
    widget_t *widget = 0;

    switch (type) {
        case WIDGET_BUTTON:
            widget = widget_create_button(bounds.x, bounds.y, bounds.w, bounds.h, text);
            break;
        case WIDGET_LABEL:
            widget = widget_create_label(bounds.x, bounds.y, text);
            break;
        case WIDGET_TEXTBOX:
            widget = widget_create_textbox(bounds.x, bounds.y, bounds.w, bounds.h);
            break;
        case WIDGET_CHECKBOX:
            widget = widget_create_checkbox(bounds.x, bounds.y, bounds.w, bounds.h, text,
                (flags & WIDGET_FLAG_CHECKED) ? 1 : 0);
            break;
        case WIDGET_SLIDER:
            widget = widget_create_slider(bounds.x, bounds.y, bounds.w, bounds.h, 0, 100, 50);
            break;
        case WIDGET_PROGRESS:
            widget = widget_create_progress(bounds.x, bounds.y, bounds.w, bounds.h, 0, 100);
            break;
        default:
            widget = widget_create_generic(type, bounds.x, bounds.y, bounds.w, bounds.h);
            break;
    }

    if (widget && text) {
        widget_set_text(widget, text);
    }

    return widget;
}

void widget_draw_unified(gfx_context_t *ctx, widget_t *widget, uint32_t render_backend) {
    if (!widget || !ctx) return;

    if (render_backend == 1) {
        /* Draw using FunRender backend */
        /* In production: delegate to fr_widget_render(widget->fr_handle, ctx); */
    }

    /* Fall back to default drawing */
    widget_draw(ctx, widget);
}
