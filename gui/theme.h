#ifndef THEME_H
#define THEME_H

#include "gfx.h"
#include "stdint.h"

typedef struct window_t window_t;

typedef struct {
    gfx_color_t bg_color;
    gfx_color_t fg_color;
    gfx_color_t accent_color;
    gfx_color_t title_bar_color;
    gfx_color_t title_text_color;
    gfx_color_t border_color;
    gfx_color_t window_bg;
    gfx_color_t button_color;
    gfx_color_t button_hover_color;
    gfx_color_t button_text_color;
    gfx_color_t text_bg;
    gfx_color_t text_fg;
    gfx_color_t menu_bg;
    gfx_color_t menu_fg;
    gfx_color_t menu_highlight;
    char name[64];
    uint8_t enable_shadows;
    uint8_t enable_alpha;
    uint8_t corner_radius;
} theme_t;

void theme_init(void);
int theme_set(const char *name);
theme_t theme_get(void);
void theme_apply_window(window_t *win);
void theme_draw_title_bar(gfx_context_t *ctx, window_t *win);
void theme_draw_border(gfx_context_t *ctx, window_t *win);
void theme_draw_button(gfx_context_t *ctx, gfx_rect_t rect, uint8_t hovered, uint8_t pressed);

/* ---- Theme synchronization with FunRender ---- */
void *gui_theme_from_fr_theme(void *fr_theme);
void *gui_theme_to_fr_theme(void);
int gui_theme_sync_with_fr(const char *theme_name);
void gui_theme_apply_fr_colors(gfx_context_t *ctx);

#endif
