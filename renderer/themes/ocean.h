/* ocean.h - 海洋蓝主题
 * 深海灵感主题，以青色强调色和海底色调为特色
 */

#ifndef FR_THEME_OCEAN_H
#define FR_THEME_OCEAN_H

#include "../include/funrender.h"
#include "../include/fr_theme.h"

static fr_theme_t fr_theme_ocean(void)
{
    fr_theme_t t;
    t.name[0] = 'o'; t.name[1] = 'c'; t.name[2] = 'e'; t.name[3] = 'a';
    t.name[4] = 'n'; t.name[5] = '\0';

    /* 颜色 - 深海蓝与青色 */
    t.colors.window_bg       = FR_RGB(232, 244, 248);
    t.colors.window_fg       = FR_RGB(15, 30, 45);
    t.colors.window_border   = FR_RGB(120, 180, 200);

    t.colors.title_bar_bg    = FR_RGB(0, 105, 148);       /* 深海蓝标题栏 */
    t.colors.title_bar_fg    = FR_RGB(255, 255, 255);
    t.colors.title_bar_border = FR_RGB(0, 75, 110);

    t.colors.button_bg       = FR_RGB(225, 242, 250);
    t.colors.button_fg       = FR_RGB(15, 30, 45);
    t.colors.button_hover    = FR_RGB(200, 232, 245);
    t.colors.button_pressed  = FR_RGB(175, 218, 238);
    t.colors.button_border   = FR_RGB(100, 170, 200);

    t.colors.input_bg        = FR_RGB(248, 252, 255);
    t.colors.input_fg        = FR_RGB(15, 30, 45);
    t.colors.input_border    = FR_RGB(130, 190, 215);
    t.colors.input_focus_border = FR_RGB(0, 150, 200);

    t.colors.menu_bg         = FR_RGB(238, 248, 252);
    t.colors.menu_fg         = FR_RGB(15, 30, 45);
    t.colors.menu_hover      = FR_RGB(190, 225, 240);
    t.colors.menu_border     = FR_RGB(120, 180, 200);

    t.colors.scrollbar_bg    = FR_RGB(220, 238, 245);
    t.colors.scrollbar_thumb = FR_RGB(100, 165, 195);
    t.colors.scrollbar_hover = FR_RGB(70, 145, 180);

    t.colors.progress_bg     = FR_RGB(210, 232, 242);
    t.colors.progress_fill   = FR_RGB(0, 150, 200);       /* 青色进度条 */

    t.colors.accent          = FR_RGB(0, 150, 200);       /* 主强调色: 海洋青 */
    t.colors.accent_hover    = FR_RGB(0, 130, 175);
    t.colors.error           = FR_RGB(200, 55, 55);
    t.colors.warning         = FR_RGB(210, 160, 40);
    t.colors.success         = FR_RGB(30, 160, 100);

    t.colors.disabled_bg     = FR_RGB(225, 240, 245);
    t.colors.disabled_fg     = FR_RGB(150, 175, 190);
    t.colors.shadow          = FR_RGBA(0, 40, 80, 25);

    /* 字体 */
    t.fonts.default_font[0] = 's'; t.fonts.default_font[1] = 'a'; t.fonts.default_font[2] = 'n'; t.fonts.default_font[3] = 's'; t.fonts.default_font[4] = '\0';
    t.fonts.title_font[0] = 's'; t.fonts.title_font[1] = 'a'; t.fonts.title_font[2] = 'n'; t.fonts.title_font[3] = 's'; t.fonts.title_font[4] = '\0';
    t.fonts.mono_font[0] = 'm'; t.fonts.mono_font[1] = 'o'; t.fonts.mono_font[2] = 'n'; t.fonts.mono_font[3] = 'o'; t.fonts.mono_font[4] = '\0';
    t.fonts.default_size = 14;
    t.fonts.title_size = 16;
    t.fonts.small_size = 11;

    /* 度量 */
    t.metrics.border_radius = 6;
    t.metrics.border_width = 1;
    t.metrics.shadow_radius = 8;
    t.metrics.shadow_offset_x = 2;
    t.metrics.shadow_offset_y = 3;
    t.metrics.padding = 8;
    t.metrics.spacing = 5;
    t.metrics.title_bar_height = 28;
    t.metrics.scrollbar_width = 14;

    return t;
}

#endif /* FR_THEME_OCEAN_H */