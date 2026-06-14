/* fr_theme.h - 主题系统
 * 颜色/字体/图标/圆角/阴影主题定义
 */

#ifndef FR_THEME_H
#define FR_THEME_H

#include "stdint.h"

/* 主题颜色集 */
typedef struct {
    fr_color_t window_bg;
    fr_color_t window_fg;
    fr_color_t window_border;

    fr_color_t title_bar_bg;
    fr_color_t title_bar_fg;
    fr_color_t title_bar_border;

    fr_color_t button_bg;
    fr_color_t button_fg;
    fr_color_t button_hover;
    fr_color_t button_pressed;
    fr_color_t button_border;

    fr_color_t input_bg;
    fr_color_t input_fg;
    fr_color_t input_border;
    fr_color_t input_focus_border;

    fr_color_t menu_bg;
    fr_color_t menu_fg;
    fr_color_t menu_hover;
    fr_color_t menu_border;

    fr_color_t scrollbar_bg;
    fr_color_t scrollbar_thumb;
    fr_color_t scrollbar_hover;

    fr_color_t progress_bg;
    fr_color_t progress_fill;

    fr_color_t accent;
    fr_color_t accent_hover;
    fr_color_t error;
    fr_color_t warning;
    fr_color_t success;

    fr_color_t disabled_bg;
    fr_color_t disabled_fg;

    fr_color_t shadow;
} fr_theme_colors_t;

/* 主题字体集 */
typedef struct {
    char default_font[64];
    char title_font[64];
    char mono_font[64];
    int default_size;
    int title_size;
    int small_size;
} fr_theme_fonts_t;

/* 主题度量 */
typedef struct {
    int border_radius;         /* 圆角半径 */
    int border_width;          /* 边框宽度 */
    int shadow_radius;         /* 阴影模糊半径 */
    int shadow_offset_x;       /* 阴影偏移 */
    int shadow_offset_y;
    int padding;               /* 内边距 */
    int spacing;               /* 控件间距 */
    int title_bar_height;      /* 标题栏高度 */
    int scrollbar_width;       /* 滚动条宽度 */
} fr_theme_metrics_t;

/* 完整主题 */
typedef struct {
    char name[64];
    fr_theme_colors_t colors;
    fr_theme_fonts_t fonts;
    fr_theme_metrics_t metrics;
} fr_theme_t;

/* 主题操作 */
void fr_theme_system_init(void);
int fr_theme_register(const fr_theme_t *theme);
int fr_theme_set_active(fr_handle_t ctx, const char *name);
fr_theme_t *fr_theme_get_active(void);
fr_theme_t *fr_theme_find(const char *name);

/* 获取当前主题的快捷方法 */
fr_color_t fr_theme_color(const char *color_name);
int fr_theme_border_radius(void);
int fr_theme_shadow_radius(void);

#endif /* FR_THEME_H */
