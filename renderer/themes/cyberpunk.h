/* cyberpunk.h - 霓虹赛博朋克主题
 * 暗色背景配上霓虹色调的赛博朋克风格主题
 * 特点: 深黑背景、霓虹粉/青色强调、发光效果
 */

#ifndef FR_THEME_CYBERPUNK_H
#define FR_THEME_CYBERPUNK_H

#include "../include/funrender.h"
#include "../include/fr_theme.h"

static fr_theme_t fr_theme_cyberpunk(void)
{
    fr_theme_t t;
    t.name[0] = 'c'; t.name[1] = 'y'; t.name[2] = 'b'; t.name[3] = 'e';
    t.name[4] = 'r'; t.name[5] = 'p'; t.name[6] = 'u'; t.name[7] = 'n';
    t.name[8] = 'k'; t.name[9] = '\0';

    /* 颜色 - 暗黑背景 + 霓虹色调 */
    t.colors.window_bg       = FR_RGB(10, 10, 20);        /* 深黑蓝背景 */
    t.colors.window_fg       = FR_RGB(220, 220, 240);     /* 亮白文字 */
    t.colors.window_border   = FR_RGB(60, 30, 80);        /* 暗紫边框 */

    t.colors.title_bar_bg    = FR_RGB(20, 5, 35);         /* 深紫标题栏 */
    t.colors.title_bar_fg    = FR_RGB(0, 255, 255);       /* 青色标题 */
    t.colors.title_bar_border = FR_RGB(80, 0, 120);       /* 紫色边框 */

    t.colors.button_bg       = FR_RGB(30, 15, 45);
    t.colors.button_fg       = FR_RGB(255, 0, 128);       /* 霓虹粉色按钮 */
    t.colors.button_hover    = FR_RGB(50, 20, 70);
    t.colors.button_pressed  = FR_RGB(80, 30, 100);
    t.colors.button_border   = FR_RGB(255, 0, 128);       /* 粉色边框 */

    t.colors.input_bg        = FR_RGB(15, 15, 30);
    t.colors.input_fg        = FR_RGB(0, 255, 255);       /* 青色输入 */
    t.colors.input_border    = FR_RGB(60, 30, 80);
    t.colors.input_focus_border = FR_RGB(255, 0, 128);    /* 粉色焦点 */

    t.colors.menu_bg         = FR_RGB(20, 10, 35);
    t.colors.menu_fg         = FR_RGB(220, 220, 240);
    t.colors.menu_hover      = FR_RGB(50, 20, 70);
    t.colors.menu_border     = FR_RGB(80, 0, 120);

    t.colors.scrollbar_bg    = FR_RGB(15, 10, 25);
    t.colors.scrollbar_thumb = FR_RGB(80, 30, 100);
    t.colors.scrollbar_hover = FR_RGB(120, 40, 140);

    t.colors.progress_bg     = FR_RGB(20, 10, 35);
    t.colors.progress_fill   = FR_RGB(255, 0, 128);       /* 粉色进度条 */

    t.colors.accent          = FR_RGB(255, 0, 128);       /* 主强调色: 霓虹粉 */
    t.colors.accent_hover    = FR_RGB(255, 50, 160);
    t.colors.error           = FR_RGB(255, 40, 80);       /* 亮红错误 */
    t.colors.warning         = FR_RGB(255, 200, 0);       /* 明黄警告 */
    t.colors.success         = FR_RGB(0, 255, 128);       /* 霓虹绿成功 */

    t.colors.disabled_bg     = FR_RGB(25, 20, 35);
    t.colors.disabled_fg     = FR_RGB(80, 70, 90);
    t.colors.shadow          = FR_RGBA(255, 0, 128, 30);  /* 粉色阴影 */

    /* 字体 - 使用等宽字体营造终端感 */
    t.fonts.default_font[0] = 'C'; t.fonts.default_font[1] = 'o'; t.fonts.default_font[2] = 'n'; t.fonts.default_font[3] = 's';
    t.fonts.default_font[4] = 'o'; t.fonts.default_font[5] = 'l'; t.fonts.default_font[6] = 'a'; t.fonts.default_font[7] = 's';
    t.fonts.default_font[8] = '\0';
    t.fonts.title_font[0] = 'C'; t.fonts.title_font[1] = 'o'; t.fonts.title_font[2] = 'n'; t.fonts.title_font[3] = 's';
    t.fonts.title_font[4] = 'o'; t.fonts.title_font[5] = 'l'; t.fonts.title_font[6] = 'a'; t.fonts.title_font[7] = 's';
    t.fonts.title_font[8] = '\0';
    t.fonts.mono_font[0] = 'C'; t.fonts.mono_font[1] = 'o'; t.fonts.mono_font[2] = 'n'; t.fonts.mono_font[3] = 's';
    t.fonts.mono_font[4] = 'o'; t.fonts.mono_font[5] = 'l'; t.fonts.mono_font[6] = 'a'; t.fonts.mono_font[7] = 's';
    t.fonts.mono_font[8] = '\0';
    t.fonts.default_size = 13;
    t.fonts.title_size = 15;
    t.fonts.small_size = 10;

    /* 度量 - 锐利边框，强发光效果 */
    t.metrics.border_radius = 0;        /* 直角边框，赛博朋克风格 */
    t.metrics.border_width = 2;          /* 加粗边框 */
    t.metrics.shadow_radius = 16;         /* 强发光阴影 */
    t.metrics.shadow_offset_x = 0;
    t.metrics.shadow_offset_y = 0;
    t.metrics.padding = 8;
    t.metrics.spacing = 4;
    t.metrics.title_bar_height = 28;
    t.metrics.scrollbar_width = 14;

    return t;
}

#endif /* FR_THEME_CYBERPUNK_H */