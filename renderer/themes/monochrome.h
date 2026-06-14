/* monochrome.h - 高对比度单色主题
 * 无障碍高对比度主题，以纯灰度调色板为特色
 * 符合 WCAG AAA 对比度标准
 */

#ifndef FR_THEME_MONOCHROME_H
#define FR_THEME_MONOCHROME_H

#include "../include/funrender.h"
#include "../include/fr_theme.h"

static fr_theme_t fr_theme_monochrome(void)
{
    fr_theme_t t;
    t.name[0] = 'm'; t.name[1] = 'o'; t.name[2] = 'n'; t.name[3] = 'o';
    t.name[4] = 'c'; t.name[5] = 'h'; t.name[6] = 'r'; t.name[7] = 'o';
    t.name[8] = 'm'; t.name[9] = 'e'; t.name[10] = '\0';

    /* 颜色 - 纯灰度无色系 */
    t.colors.window_bg       = FR_RGB(255, 255, 255);     /* 纯白背景 */
    t.colors.window_fg       = FR_RGB(0, 0, 0);           /* 纯黑文字 */
    t.colors.window_border   = FR_RGB(0, 0, 0);           /* 纯黑边框 */

    t.colors.title_bar_bg    = FR_RGB(0, 0, 0);           /* 纯黑标题栏 */
    t.colors.title_bar_fg    = FR_RGB(255, 255, 255);     /* 纯白标题文字 */
    t.colors.title_bar_border = FR_RGB(0, 0, 0);

    t.colors.button_bg       = FR_RGB(255, 255, 255);     /* 白底按钮 */
    t.colors.button_fg       = FR_RGB(0, 0, 0);           /* 黑字 */
    t.colors.button_hover    = FR_RGB(230, 230, 230);     /* 浅灰悬停 */
    t.colors.button_pressed  = FR_RGB(180, 180, 180);     /* 中灰按下 */
    t.colors.button_border   = FR_RGB(0, 0, 0);           /* 黑边框 */

    t.colors.input_bg        = FR_RGB(255, 255, 255);
    t.colors.input_fg        = FR_RGB(0, 0, 0);
    t.colors.input_border    = FR_RGB(0, 0, 0);
    t.colors.input_focus_border = FR_RGB(0, 0, 0);        /* 加粗黑焦点 */

    t.colors.menu_bg         = FR_RGB(255, 255, 255);
    t.colors.menu_fg         = FR_RGB(0, 0, 0);
    t.colors.menu_hover      = FR_RGB(220, 220, 220);
    t.colors.menu_border     = FR_RGB(0, 0, 0);

    t.colors.scrollbar_bg    = FR_RGB(240, 240, 240);
    t.colors.scrollbar_thumb = FR_RGB(80, 80, 80);
    t.colors.scrollbar_hover = FR_RGB(40, 40, 40);

    t.colors.progress_bg     = FR_RGB(240, 240, 240);
    t.colors.progress_fill   = FR_RGB(0, 0, 0);

    t.colors.accent          = FR_RGB(0, 0, 0);           /* 主强调色: 黑色 */
    t.colors.accent_hover    = FR_RGB(40, 40, 40);
    t.colors.error           = FR_RGB(0, 0, 0);           /* 使用粗体/下划线区分 */
    t.colors.warning         = FR_RGB(0, 0, 0);
    t.colors.success         = FR_RGB(0, 0, 0);

    t.colors.disabled_bg     = FR_RGB(240, 240, 240);
    t.colors.disabled_fg     = FR_RGB(128, 128, 128);     /* 灰色禁用文字 */
    t.colors.shadow          = FR_RGBA(0, 0, 0, 0);       /* 无阴影 */

    /* 字体 - 使用较大字号提高可读性 */
    t.fonts.default_font[0] = 's'; t.fonts.default_font[1] = 'a'; t.fonts.default_font[2] = 'n'; t.fonts.default_font[3] = 's'; t.fonts.default_font[4] = '\0';
    t.fonts.title_font[0] = 's'; t.fonts.title_font[1] = 'a'; t.fonts.title_font[2] = 'n'; t.fonts.title_font[3] = 's'; t.fonts.title_font[4] = '\0';
    t.fonts.mono_font[0] = 'm'; t.fonts.mono_font[1] = 'o'; t.fonts.mono_font[2] = 'n'; t.fonts.mono_font[3] = 'o'; t.fonts.mono_font[4] = '\0';
    t.fonts.default_size = 16;
    t.fonts.title_size = 20;
    t.fonts.small_size = 13;

    /* 度量 - 强化边框以区分元素 */
    t.metrics.border_radius = 0;        /* 无圆角，清晰边缘 */
    t.metrics.border_width = 2;          /* 加粗边框 */
    t.metrics.shadow_radius = 0;
    t.metrics.shadow_offset_x = 0;
    t.metrics.shadow_offset_y = 0;
    t.metrics.padding = 10;
    t.metrics.spacing = 8;
    t.metrics.title_bar_height = 32;
    t.metrics.scrollbar_width = 18;

    return t;
}

#endif /* FR_THEME_MONOCHROME_H */