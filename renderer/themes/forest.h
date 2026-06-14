/* forest.h - 森林绿主题
 * 自然大地色调主题，以森林绿为主色，暖棕色为点缀
 */

#ifndef FR_THEME_FOREST_H
#define FR_THEME_FOREST_H

#include "../include/funrender.h"
#include "../include/fr_theme.h"

static fr_theme_t fr_theme_forest(void)
{
    fr_theme_t t;
    t.name[0] = 'f'; t.name[1] = 'o'; t.name[2] = 'r'; t.name[3] = 'e';
    t.name[4] = 's'; t.name[5] = 't'; t.name[6] = '\0';

    /* 颜色 - 森林绿与大地色调 */
    t.colors.window_bg       = FR_RGB(245, 248, 240);
    t.colors.window_fg       = FR_RGB(35, 45, 30);
    t.colors.window_border   = FR_RGB(140, 175, 130);

    t.colors.title_bar_bg    = FR_RGB(46, 100, 50);        /* 深绿标题栏 */
    t.colors.title_bar_fg    = FR_RGB(245, 250, 240);
    t.colors.title_bar_border = FR_RGB(34, 75, 38);

    t.colors.button_bg       = FR_RGB(235, 245, 230);
    t.colors.button_fg       = FR_RGB(35, 45, 30);
    t.colors.button_hover    = FR_RGB(218, 235, 210);
    t.colors.button_pressed  = FR_RGB(195, 225, 185);
    t.colors.button_border   = FR_RGB(120, 165, 115);

    t.colors.input_bg        = FR_RGB(250, 252, 248);
    t.colors.input_fg        = FR_RGB(35, 45, 30);
    t.colors.input_border    = FR_RGB(145, 180, 135);
    t.colors.input_focus_border = FR_RGB(56, 130, 60);

    t.colors.menu_bg         = FR_RGB(248, 252, 244);
    t.colors.menu_fg         = FR_RGB(35, 45, 30);
    t.colors.menu_hover      = FR_RGB(210, 235, 200);
    t.colors.menu_border     = FR_RGB(140, 175, 130);

    t.colors.scrollbar_bg    = FR_RGB(230, 242, 225);
    t.colors.scrollbar_thumb = FR_RGB(145, 185, 140);
    t.colors.scrollbar_hover = FR_RGB(115, 165, 110);

    t.colors.progress_bg     = FR_RGB(220, 238, 215);
    t.colors.progress_fill   = FR_RGB(56, 130, 60);        /* 绿色进度条 */

    t.colors.accent          = FR_RGB(56, 130, 60);        /* 主强调色: 森林绿 */
    t.colors.accent_hover    = FR_RGB(46, 108, 50);
    t.colors.error           = FR_RGB(190, 50, 50);
    t.colors.warning         = FR_RGB(190, 150, 35);
    t.colors.success         = FR_RGB(40, 140, 70);

    t.colors.disabled_bg     = FR_RGB(238, 245, 232);
    t.colors.disabled_fg     = FR_RGB(155, 175, 145);
    t.colors.shadow          = FR_RGBA(20, 50, 20, 30);

    /* 字体 */
    t.fonts.default_font[0] = 's'; t.fonts.default_font[1] = 'a'; t.fonts.default_font[2] = 'n'; t.fonts.default_font[3] = 's'; t.fonts.default_font[4] = '\0';
    t.fonts.title_font[0] = 's'; t.fonts.title_font[1] = 'a'; t.fonts.title_font[2] = 'n'; t.fonts.title_font[3] = 's'; t.fonts.title_font[4] = '\0';
    t.fonts.mono_font[0] = 'm'; t.fonts.mono_font[1] = 'o'; t.fonts.mono_font[2] = 'n'; t.fonts.mono_font[3] = 'o'; t.fonts.mono_font[4] = '\0';
    t.fonts.default_size = 14;
    t.fonts.title_size = 16;
    t.fonts.small_size = 11;

    /* 度量 */
    t.metrics.border_radius = 5;
    t.metrics.border_width = 1;
    t.metrics.shadow_radius = 8;
    t.metrics.shadow_offset_x = 2;
    t.metrics.shadow_offset_y = 2;
    t.metrics.padding = 8;
    t.metrics.spacing = 5;
    t.metrics.title_bar_height = 30;
    t.metrics.scrollbar_width = 14;

    return t;
}

#endif /* FR_THEME_FOREST_H */