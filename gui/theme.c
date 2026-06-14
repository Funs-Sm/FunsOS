#include "theme.h"
#include "font.h"
#include "window.h"
#include "kheap.h"
#include "string.h"

static theme_t current_theme;
static theme_t themes[8];
static uint32_t theme_count = 0;

void theme_init(void) {
    theme_t light;
    memset(&light, 0, sizeof(theme_t));
    light.bg_color = 0x00F0F0F0;
    light.fg_color = 0x00202020;
    light.accent_color = 0x000070C0;
    light.title_bar_color = 0x00FFFFFF;
    light.title_text_color = 0x00000000;
    light.border_color = 0x00C0C0C0;
    light.window_bg = 0x00FFFFFF;
    light.button_color = 0x00E0E0E0;
    light.button_hover_color = 0x00D0D0D0;
    light.button_text_color = 0x00000000;
    light.text_bg = 0x00FFFFFF;
    light.text_fg = 0x00000000;
    light.menu_bg = 0x00F0F0F0;
    light.menu_fg = 0x00000000;
    light.menu_highlight = 0x000070C0;
    strcpy(light.name, "Light");
    light.enable_shadows = 1;
    light.enable_alpha = 0;
    light.corner_radius = 4;
    themes[theme_count++] = light;

    theme_t dark;
    memset(&dark, 0, sizeof(theme_t));
    dark.bg_color = 0x00202020;
    dark.fg_color = 0x00E0E0E0;
    dark.accent_color = 0x000080FF;
    dark.title_bar_color = 0x00303030;
    dark.title_text_color = 0x00FFFFFF;
    dark.border_color = 0x00505050;
    dark.window_bg = 0x002A2A2A;
    dark.button_color = 0x00404040;
    dark.button_hover_color = 0x00505050;
    dark.button_text_color = 0x00FFFFFF;
    dark.text_bg = 0x002A2A2A;
    dark.text_fg = 0x00E0E0E0;
    dark.menu_bg = 0x00303030;
    dark.menu_fg = 0x00E0E0E0;
    dark.menu_highlight = 0x000080FF;
    strcpy(dark.name, "Dark");
    dark.enable_shadows = 1;
    dark.enable_alpha = 1;
    dark.corner_radius = 6;
    themes[theme_count++] = dark;

    memcpy(&current_theme, &light, sizeof(theme_t));
}

int theme_set(const char *name) {
    for (uint32_t i = 0; i < theme_count; i++) {
        if (strcmp(themes[i].name, name) == 0) {
            memcpy(&current_theme, &themes[i], sizeof(theme_t));
            return 0;
        }
    }
    return -1;
}

theme_t theme_get(void) {
    return current_theme;
}

void theme_apply_window(window_t *win) {
    win->bg_color = current_theme.window_bg;
    win->border_color = current_theme.border_color;
    win->title_color = current_theme.title_bar_color;
    win->title_text_color = current_theme.title_text_color;
}

void theme_draw_title_bar(gfx_context_t *ctx, window_t *win) {
    int32_t x = win->x;
    int32_t y = win->y;
    int32_t w = win->width;
    int32_t bar_h = 28;

    if (current_theme.enable_shadows) {
        gfx_fill_rect(ctx, (gfx_rect_t){x + 3, y + 3, w, bar_h}, 0x40000000);
    }

    uint8_t r = current_theme.corner_radius;
    for (int32_t i = 0; i < r; i++) {
        for (int32_t j = 0; j < r; j++) {
            if (i * i + j * j < r * r) {
                gfx_set_pixel(ctx, x + i, y + j, current_theme.title_bar_color);
                gfx_set_pixel(ctx, x + w - 1 - i, y + j, current_theme.title_bar_color);
            }
        }
    }

    gfx_fill_rect(ctx, (gfx_rect_t){x + r, y, w - 2 * r, bar_h}, current_theme.title_bar_color);
    gfx_fill_rect(ctx, (gfx_rect_t){x, y + r, w, bar_h - r}, current_theme.title_bar_color);

    uint32_t grad_steps = bar_h;
    for (uint32_t s = 0; s < grad_steps; s++) {
        uint8_t alpha = (uint8_t)(20 * s / grad_steps);
        uint32_t color = (alpha << 24);
        gfx_fill_rect(ctx, (gfx_rect_t){x + r, y + s, w - 2 * r, 1}, color);
    }

    font_draw_string(ctx, win->title, x + 8, y + 6, current_theme.title_text_color, current_theme.title_bar_color);

    int32_t btn_x = x + w - 24;
    gfx_fill_rect(ctx, (gfx_rect_t){btn_x, y + 6, 16, 16}, 0x00E04040);
    gfx_fill_rect(ctx, (gfx_rect_t){btn_x - 22, y + 6, 16, 16}, 0x00E0C040);
    gfx_fill_rect(ctx, (gfx_rect_t){btn_x - 44, y + 6, 16, 16}, 0x0040C040);
}

void theme_draw_border(gfx_context_t *ctx, window_t *win) {
    int32_t x = win->x;
    int32_t y = win->y;
    int32_t w = win->width;
    int32_t h = win->height;

    if (current_theme.enable_shadows) {
        gfx_fill_rect(ctx, (gfx_rect_t){x + 4, y + 4, w, h}, 0x30000000);
    }

    gfx_draw_rect(ctx, (gfx_rect_t){x, y, w, h}, current_theme.border_color);
    gfx_draw_rect(ctx, (gfx_rect_t){x + 1, y + 1, w - 2, h - 2}, current_theme.border_color);
}

void theme_draw_button(gfx_context_t *ctx, gfx_rect_t rect, uint8_t hovered, uint8_t pressed) {
    gfx_color_t color = current_theme.button_color;
    if (pressed) {
        color = 0x00A0A0A0;
    } else if (hovered) {
        color = current_theme.button_hover_color;
    }

    uint8_t r = current_theme.corner_radius;
    int32_t x = rect.x, y = rect.y, w = rect.w, h = rect.h;

    for (int32_t i = 0; i < r; i++) {
        for (int32_t j = 0; j < r; j++) {
            if (i * i + j * j < r * r) {
                gfx_set_pixel(ctx, x + i, y + j, color);
                gfx_set_pixel(ctx, x + w - 1 - i, y + j, color);
                gfx_set_pixel(ctx, x + i, y + h - 1 - j, color);
                gfx_set_pixel(ctx, x + w - 1 - i, y + h - 1 - j, color);
            }
        }
    }

    gfx_fill_rect(ctx, (gfx_rect_t){x + r, y, w - 2 * r, h}, color);
    gfx_fill_rect(ctx, (gfx_rect_t){x, y + r, w, h - 2 * r}, color);

    if (!pressed) {
        uint8_t cr = (color >> 16) & 0xFF;
        uint8_t cg = (color >> 8) & 0xFF;
        uint8_t cb = color & 0xFF;
        cr = (cr + 40 > 255) ? 255 : cr + 40;
        cg = (cg + 40 > 255) ? 255 : cg + 40;
        cb = (cb + 40 > 255) ? 255 : cb + 40;
        gfx_color_t highlight = (cr << 16) | (cg << 8) | cb;
        gfx_draw_line(ctx, x + r, y, x + w - 1 - r, y, highlight);
        gfx_draw_line(ctx, x, y + r, x, y + h - 1 - r, highlight);
    }
}
