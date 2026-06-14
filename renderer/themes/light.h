/* light.h - 亮色主题
 * 明亮简洁的浅色主题
 */

#ifndef FR_THEME_LIGHT_H
#define FR_THEME_LIGHT_H

#include "../include/funrender.h"
#include "../include/fr_theme.h"

static fr_theme_t fr_theme_light(void)
{
    fr_theme_t t;
    t.name[0] = 'l'; t.name[1] = 'i'; t.name[2] = 'g'; t.name[3] = 'h'; t.name[4] = 't'; t.name[5] = '\0';

    t.colors.window_bg       = FR_RGB(255, 255, 255);
    t.colors.window_fg       = FR_RGB(32, 32, 32);
    t.colors.window_border   = FR_RGB(200, 200, 200);
    t.colors.title_bar_bg    = FR_RGB(245, 245, 245);
    t.colors.title_bar_fg    = FR_RGB(32, 32, 32);
    t.colors.title_bar_border = FR_RGB(220, 220, 220);
    t.colors.button_bg       = FR_RGB(245, 245, 245);
    t.colors.button_fg       = FR_RGB(32, 32, 32);
    t.colors.button_hover    = FR_RGB(235, 235, 235);
    t.colors.button_pressed  = FR_RGB(220, 220, 220);
    t.colors.button_border   = FR_RGB(200, 200, 200);
    t.colors.input_bg        = FR_RGB(255, 255, 255);
    t.colors.input_fg        = FR_RGB(32, 32, 32);
    t.colors.input_border    = FR_RGB(200, 200, 200);
    t.colors.input_focus_border = FR_RGB(0, 120, 212);
    t.colors.menu_bg         = FR_RGB(255, 255, 255);
    t.colors.menu_fg         = FR_RGB(32, 32, 32);
    t.colors.menu_hover      = FR_RGB(240, 240, 240);
    t.colors.menu_border     = FR_RGB(200, 200, 200);
    t.colors.scrollbar_bg    = FR_RGB(245, 245, 245);
    t.colors.scrollbar_thumb = FR_RGB(200, 200, 200);
    t.colors.scrollbar_hover = FR_RGB(180, 180, 180);
    t.colors.progress_bg     = FR_RGB(230, 230, 230);
    t.colors.progress_fill   = FR_RGB(0, 120, 212);
    t.colors.accent          = FR_RGB(0, 120, 212);
    t.colors.accent_hover    = FR_RGB(0, 100, 180);
    t.colors.error           = FR_RGB(200, 0, 0);
    t.colors.warning         = FR_RGB(200, 140, 0);
    t.colors.success         = FR_RGB(0, 120, 0);
    t.colors.disabled_bg     = FR_RGB(245, 245, 245);
    t.colors.disabled_fg     = FR_RGB(180, 180, 180);
    t.colors.shadow          = FR_RGBA(0, 0, 0, 20);

    t.fonts.default_font[0] = 's'; t.fonts.default_font[1] = 'a'; t.fonts.default_font[2] = 'n'; t.fonts.default_font[3] = 's'; t.fonts.default_font[4] = '\0';
    t.fonts.title_font[0] = 's'; t.fonts.title_font[1] = 'a'; t.fonts.title_font[2] = 'n'; t.fonts.title_font[3] = 's'; t.fonts.title_font[4] = '\0';
    t.fonts.mono_font[0] = 'm'; t.fonts.mono_font[1] = 'o'; t.fonts.mono_font[2] = 'n'; t.fonts.mono_font[3] = 'o'; t.fonts.mono_font[4] = '\0';
    t.fonts.default_size = 14;
    t.fonts.title_size = 16;
    t.fonts.small_size = 11;

    t.metrics.border_radius = 6;
    t.metrics.border_width = 1;
    t.metrics.shadow_radius = 4;
    t.metrics.shadow_offset_x = 0;
    t.metrics.shadow_offset_y = 2;
    t.metrics.padding = 10;
    t.metrics.spacing = 6;
    t.metrics.title_bar_height = 32;
    t.metrics.scrollbar_width = 14;

    return t;
}

#endif /* FR_THEME_LIGHT_H */
