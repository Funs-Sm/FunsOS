/* default.h - 默认主题
 * FUNSOS 经典蓝色主题
 */

#ifndef FR_THEME_DEFAULT_H
#define FR_THEME_DEFAULT_H

#include "../include/funrender.h"
#include "../include/fr_theme.h"

static fr_theme_t fr_theme_default(void)
{
    fr_theme_t t;
    t.name[0] = 'd'; t.name[1] = 'e'; t.name[2] = 'f'; t.name[3] = 'a';
    t.name[4] = 'u'; t.name[5] = 'l'; t.name[6] = 't'; t.name[7] = '\0';

    /* 颜色 */
    t.colors.window_bg       = FR_RGB(255, 255, 255);
    t.colors.window_fg       = FR_RGB(0, 0, 0);
    t.colors.window_border   = FR_RGB(128, 128, 128);
    t.colors.title_bar_bg    = FR_RGB(0, 120, 212);
    t.colors.title_bar_fg    = FR_RGB(255, 255, 255);
    t.colors.title_bar_border = FR_RGB(0, 90, 158);
    t.colors.button_bg       = FR_RGB(230, 230, 230);
    t.colors.button_fg       = FR_RGB(0, 0, 0);
    t.colors.button_hover    = FR_RGB(220, 220, 220);
    t.colors.button_pressed  = FR_RGB(180, 180, 180);
    t.colors.button_border   = FR_RGB(128, 128, 128);
    t.colors.input_bg        = FR_RGB(255, 255, 255);
    t.colors.input_fg        = FR_RGB(0, 0, 0);
    t.colors.input_border    = FR_RGB(128, 128, 128);
    t.colors.input_focus_border = FR_RGB(0, 120, 212);
    t.colors.menu_bg         = FR_RGB(255, 255, 255);
    t.colors.menu_fg         = FR_RGB(0, 0, 0);
    t.colors.menu_hover      = FR_RGB(204, 232, 255);
    t.colors.menu_border     = FR_RGB(128, 128, 128);
    t.colors.scrollbar_bg    = FR_RGB(240, 240, 240);
    t.colors.scrollbar_thumb = FR_RGB(192, 192, 192);
    t.colors.scrollbar_hover = FR_RGB(160, 160, 160);
    t.colors.progress_bg     = FR_RGB(224, 224, 224);
    t.colors.progress_fill   = FR_RGB(0, 120, 212);
    t.colors.accent          = FR_RGB(0, 120, 212);
    t.colors.accent_hover    = FR_RGB(0, 100, 180);
    t.colors.error           = FR_RGB(224, 0, 0);
    t.colors.warning         = FR_RGB(255, 170, 0);
    t.colors.success         = FR_RGB(0, 128, 0);
    t.colors.disabled_bg     = FR_RGB(240, 240, 240);
    t.colors.disabled_fg     = FR_RGB(160, 160, 160);
    t.colors.shadow          = FR_RGBA(0, 0, 0, 40);

    /* 字体 */
    t.fonts.default_font[0] = 's'; t.fonts.default_font[1] = 'a'; t.fonts.default_font[2] = 'n'; t.fonts.default_font[3] = 's'; t.fonts.default_font[4] = '\0';
    t.fonts.title_font[0] = 's'; t.fonts.title_font[1] = 'a'; t.fonts.title_font[2] = 'n'; t.fonts.title_font[3] = 's'; t.fonts.title_font[4] = '\0';
    t.fonts.mono_font[0] = 'm'; t.fonts.mono_font[1] = 'o'; t.fonts.mono_font[2] = 'n'; t.fonts.mono_font[3] = 'o'; t.fonts.mono_font[4] = '\0';
    t.fonts.default_size = 14;
    t.fonts.title_size = 16;
    t.fonts.small_size = 11;

    /* 度量 */
    t.metrics.border_radius = 4;
    t.metrics.border_width = 1;
    t.metrics.shadow_radius = 8;
    t.metrics.shadow_offset_x = 2;
    t.metrics.shadow_offset_y = 2;
    t.metrics.padding = 8;
    t.metrics.spacing = 4;
    t.metrics.title_bar_height = 28;
    t.metrics.scrollbar_width = 16;

    return t;
}

#endif /* FR_THEME_DEFAULT_H */
