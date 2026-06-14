/* sunset.h - 日落暖色主题
 * 温暖日落主题，以橙色、珊瑚色和暖灰色调为特色
 */

#ifndef FR_THEME_SUNSET_H
#define FR_THEME_SUNSET_H

#include "../include/funrender.h"
#include "../include/fr_theme.h"

static fr_theme_t fr_theme_sunset(void)
{
    fr_theme_t t;
    t.name[0] = 's'; t.name[1] = 'u'; t.name[2] = 'n'; t.name[3] = 's';
    t.name[4] = 'e'; t.name[5] = 't'; t.name[6] = '\0';

    /* 颜色 - 日落暖色系 */
    t.colors.window_bg       = FR_RGB(255, 250, 242);
    t.colors.window_fg       = FR_RGB(50, 35, 25);
    t.colors.window_border   = FR_RGB(220, 180, 150);

    t.colors.title_bar_bg    = FR_RGB(200, 100, 50);       /* 深橙红标题栏 */
    t.colors.title_bar_fg    = FR_RGB(255, 250, 240);
    t.colors.title_bar_border = FR_RGB(160, 75, 35);

    t.colors.button_bg       = FR_RGB(255, 242, 230);
    t.colors.button_fg       = FR_RGB(50, 35, 25);
    t.colors.button_hover    = FR_RGB(250, 230, 210);
    t.colors.button_pressed  = FR_RGB(240, 215, 190);
    t.colors.button_border   = FR_RGB(200, 160, 130);

    t.colors.input_bg        = FR_RGB(255, 252, 248);
    t.colors.input_fg        = FR_RGB(50, 35, 25);
    t.colors.input_border    = FR_RGB(220, 180, 150);
    t.colors.input_focus_border = FR_RGB(220, 120, 60);

    t.colors.menu_bg         = FR_RGB(255, 248, 240);
    t.colors.menu_fg         = FR_RGB(50, 35, 25);
    t.colors.menu_hover      = FR_RGB(248, 225, 205);
    t.colors.menu_border     = FR_RGB(220, 180, 150);

    t.colors.scrollbar_bg    = FR_RGB(250, 238, 225);
    t.colors.scrollbar_thumb = FR_RGB(210, 170, 140);
    t.colors.scrollbar_hover = FR_RGB(185, 150, 120);

    t.colors.progress_bg     = FR_RGB(242, 225, 210);
    t.colors.progress_fill   = FR_RGB(220, 120, 60);       /* 橙色进度条 */

    t.colors.accent          = FR_RGB(220, 120, 60);       /* 主强调色: 日落橙 */
    t.colors.accent_hover    = FR_RGB(195, 100, 45);
    t.colors.error           = FR_RGB(200, 50, 50);
    t.colors.warning         = FR_RGB(220, 140, 40);
    t.colors.success         = FR_RGB(60, 160, 80);

    t.colors.disabled_bg     = FR_RGB(248, 240, 232);
    t.colors.disabled_fg     = FR_RGB(190, 170, 150);
    t.colors.shadow          = FR_RGBA(80, 40, 20, 25);

    /* 字体 */
    t.fonts.default_font[0] = 's'; t.fonts.default_font[1] = 'a'; t.fonts.default_font[2] = 'n'; t.fonts.default_font[3] = 's'; t.fonts.default_font[4] = '\0';
    t.fonts.title_font[0] = 's'; t.fonts.title_font[1] = 'a'; t.fonts.title_font[2] = 'n'; t.fonts.title_font[3] = 's'; t.fonts.title_font[4] = '\0';
    t.fonts.mono_font[0] = 'm'; t.fonts.mono_font[1] = 'o'; t.fonts.mono_font[2] = 'n'; t.fonts.mono_font[3] = 'o'; t.fonts.mono_font[4] = '\0';
    t.fonts.default_size = 14;
    t.fonts.title_size = 16;
    t.fonts.small_size = 11;

    /* 度量 */
    t.metrics.border_radius = 8;
    t.metrics.border_width = 1;
    t.metrics.shadow_radius = 10;
    t.metrics.shadow_offset_x = 2;
    t.metrics.shadow_offset_y = 3;
    t.metrics.padding = 10;
    t.metrics.spacing = 6;
    t.metrics.title_bar_height = 28;
    t.metrics.scrollbar_width = 14;

    return t;
}

#endif /* FR_THEME_SUNSET_H */