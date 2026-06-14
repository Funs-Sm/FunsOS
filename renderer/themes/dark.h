/* dark.h - 暗色主题
 * 深色背景主题，适合夜间使用
 */

#ifndef FR_THEME_DARK_H
#define FR_THEME_DARK_H

#include "../include/funrender.h"
#include "../include/fr_theme.h"

static fr_theme_t fr_theme_dark(void)
{
    fr_theme_t t;
    t.name[0] = 'd'; t.name[1] = 'a'; t.name[2] = 'r'; t.name[3] = 'k'; t.name[4] = '\0';

    t.colors.window_bg       = FR_RGB(45, 45, 45);
    t.colors.window_fg       = FR_RGB(220, 220, 220);
    t.colors.window_border   = FR_RGB(80, 80, 80);
    t.colors.title_bar_bg    = FR_RGB(30, 30, 30);
    t.colors.title_bar_fg    = FR_RGB(220, 220, 220);
    t.colors.title_bar_border = FR_RGB(60, 60, 60);
    t.colors.button_bg       = FR_RGB(60, 60, 60);
    t.colors.button_fg       = FR_RGB(220, 220, 220);
    t.colors.button_hover    = FR_RGB(80, 80, 80);
    t.colors.button_pressed  = FR_RGB(50, 50, 50);
    t.colors.button_border   = FR_RGB(90, 90, 90);
    t.colors.input_bg        = FR_RGB(40, 40, 40);
    t.colors.input_fg        = FR_RGB(220, 220, 220);
    t.colors.input_border    = FR_RGB(90, 90, 90);
    t.colors.input_focus_border = FR_RGB(0, 120, 212);
    t.colors.menu_bg         = FR_RGB(45, 45, 45);
    t.colors.menu_fg         = FR_RGB(220, 220, 220);
    t.colors.menu_hover      = FR_RGB(60, 60, 60);
    t.colors.menu_border     = FR_RGB(80, 80, 80);
    t.colors.scrollbar_bg    = FR_RGB(40, 40, 40);
    t.colors.scrollbar_thumb = FR_RGB(80, 80, 80);
    t.colors.scrollbar_hover = FR_RGB(100, 100, 100);
    t.colors.progress_bg     = FR_RGB(60, 60, 60);
    t.colors.progress_fill   = FR_RGB(0, 120, 212);
    t.colors.accent          = FR_RGB(0, 120, 212);
    t.colors.accent_hover    = FR_RGB(0, 100, 180);
    t.colors.error           = FR_RGB(224, 80, 80);
    t.colors.warning         = FR_RGB(255, 180, 50);
    t.colors.success         = FR_RGB(80, 180, 80);
    t.colors.disabled_bg     = FR_RGB(50, 50, 50);
    t.colors.disabled_fg     = FR_RGB(100, 100, 100);
    t.colors.shadow          = FR_RGBA(0, 0, 0, 80);

    t.fonts.default_font[0] = 's'; t.fonts.default_font[1] = 'a'; t.fonts.default_font[2] = 'n'; t.fonts.default_font[3] = 's'; t.fonts.default_font[4] = '\0';
    t.fonts.title_font[0] = 's'; t.fonts.title_font[1] = 'a'; t.fonts.title_font[2] = 'n'; t.fonts.title_font[3] = 's'; t.fonts.title_font[4] = '\0';
    t.fonts.mono_font[0] = 'm'; t.fonts.mono_font[1] = 'o'; t.fonts.mono_font[2] = 'n'; t.fonts.mono_font[3] = 'o'; t.fonts.mono_font[4] = '\0';
    t.fonts.default_size = 14;
    t.fonts.title_size = 16;
    t.fonts.small_size = 11;

    t.metrics.border_radius = 4;
    t.metrics.border_width = 1;
    t.metrics.shadow_radius = 12;
    t.metrics.shadow_offset_x = 2;
    t.metrics.shadow_offset_y = 4;
    t.metrics.padding = 8;
    t.metrics.spacing = 4;
    t.metrics.title_bar_height = 28;
    t.metrics.scrollbar_width = 16;

    return t;
}

#endif /* FR_THEME_DARK_H */
