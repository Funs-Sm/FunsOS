/* retro.h - 复古蒸汽波主题
 * 复古合成波/蒸汽波主题，以粉彩柔和色调为特色
 * 特点: 柔和粉彩背景、紫色/粉色强调、梦幻渐变感
 */

#ifndef FR_THEME_RETRO_H
#define FR_THEME_RETRO_H

#include "../include/funrender.h"
#include "../include/fr_theme.h"

static fr_theme_t fr_theme_retro(void)
{
    fr_theme_t t;
    t.name[0] = 'r'; t.name[1] = 'e'; t.name[2] = 't'; t.name[3] = 'r';
    t.name[4] = 'o'; t.name[5] = '\0';

    /* 颜色 - 柔和的粉彩色调 */
    t.colors.window_bg       = FR_RGB(255, 220, 240);     /* 淡粉背景 */
    t.colors.window_fg       = FR_RGB(60, 30, 80);        /* 深紫文字 */
    t.colors.window_border   = FR_RGB(200, 160, 220);

    t.colors.title_bar_bg    = FR_RGB(180, 120, 220);     /* 紫色标题栏 */
    t.colors.title_bar_fg    = FR_RGB(255, 255, 255);
    t.colors.title_bar_border = FR_RGB(140, 80, 190);

    t.colors.button_bg       = FR_RGB(255, 210, 235);
    t.colors.button_fg       = FR_RGB(60, 30, 80);
    t.colors.button_hover    = FR_RGB(245, 195, 225);
    t.colors.button_pressed  = FR_RGB(230, 175, 210);
    t.colors.button_border   = FR_RGB(200, 150, 210);

    t.colors.input_bg        = FR_RGB(255, 235, 245);
    t.colors.input_fg        = FR_RGB(60, 30, 80);
    t.colors.input_border    = FR_RGB(200, 160, 220);
    t.colors.input_focus_border = FR_RGB(160, 100, 210);

    t.colors.menu_bg         = FR_RGB(255, 225, 242);
    t.colors.menu_fg         = FR_RGB(60, 30, 80);
    t.colors.menu_hover      = FR_RGB(240, 200, 230);
    t.colors.menu_border     = FR_RGB(200, 160, 220);

    t.colors.scrollbar_bg    = FR_RGB(245, 215, 235);
    t.colors.scrollbar_thumb = FR_RGB(200, 150, 210);
    t.colors.scrollbar_hover = FR_RGB(175, 125, 190);

    t.colors.progress_bg     = FR_RGB(235, 205, 225);
    t.colors.progress_fill   = FR_RGB(160, 100, 210);     /* 紫色进度条 */

    t.colors.accent          = FR_RGB(160, 100, 210);     /* 主强调色: 复古紫 */
    t.colors.accent_hover    = FR_RGB(140, 80, 190);
    t.colors.error           = FR_RGB(220, 80, 120);      /* 柔和红 */
    t.colors.warning         = FR_RGB(220, 170, 60);      /* 柔和黄 */
    t.colors.success         = FR_RGB(80, 180, 140);      /* 柔和绿 */

    t.colors.disabled_bg     = FR_RGB(245, 225, 238);
    t.colors.disabled_fg     = FR_RGB(190, 170, 195);
    t.colors.shadow          = FR_RGBA(140, 80, 190, 20);

    /* 字体 */
    t.fonts.default_font[0] = 's'; t.fonts.default_font[1] = 'a'; t.fonts.default_font[2] = 'n'; t.fonts.default_font[3] = 's'; t.fonts.default_font[4] = '\0';
    t.fonts.title_font[0] = 's'; t.fonts.title_font[1] = 'a'; t.fonts.title_font[2] = 'n'; t.fonts.title_font[3] = 's'; t.fonts.title_font[4] = '\0';
    t.fonts.mono_font[0] = 'm'; t.fonts.mono_font[1] = 'o'; t.fonts.mono_font[2] = 'n'; t.fonts.mono_font[3] = 'o'; t.fonts.mono_font[4] = '\0';
    t.fonts.default_size = 14;
    t.fonts.title_size = 16;
    t.fonts.small_size = 11;

    /* 度量 - 大圆角，柔和阴影 */
    t.metrics.border_radius = 12;       /* 大圆角，复古感 */
    t.metrics.border_width = 1;
    t.metrics.shadow_radius = 12;
    t.metrics.shadow_offset_x = 0;
    t.metrics.shadow_offset_y = 4;
    t.metrics.padding = 10;
    t.metrics.spacing = 6;
    t.metrics.title_bar_height = 30;
    t.metrics.scrollbar_width = 14;

    return t;
}

#endif /* FR_THEME_RETRO_H */