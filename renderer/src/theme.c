/* theme.c - 主题引擎实现
 * 颜色/字体/圆角/阴影主题管理
 */

#include "funrender.h"
#include "fr_theme.h"
#include "fr_context.h"
#include "string.h"
#include "default.h"
#include "dark.h"
#include "light.h"

/* 主题注册表 */
#define FR_MAX_THEMES 12
static fr_theme_t g_themes[FR_MAX_THEMES];
static uint32_t g_theme_count = 0;
static fr_theme_t *g_active_theme = NULL;

/* ---- 主题过渡动画状态 ---- */
static fr_theme_t g_transition_source;      /* 过渡源主题 */
static fr_theme_t g_transition_target;      /* 过渡目标主题 */
static int g_transition_in_progress = 0;    /* 是否正在进行过渡动画 */
static float g_transition_progress = 0.0f;   /* 过渡进度 (0.0 ~ 1.0) */
static uint32_t g_transition_duration_ms = 300; /* 动画持续时间 (毫秒) */
static uint32_t g_transition_start_tick = 0; /* 动画开始时刻 */

/* ---- 控件级颜色覆盖 ---- */
#define FR_MAX_OVERRIDES 32
typedef struct {
    uint32_t widget_type;
    char color_name[32];
    fr_color_t color;
    int active;
} fr_widget_override_t;
static fr_widget_override_t g_overrides[FR_MAX_OVERRIDES];
static uint32_t g_override_count = 0;

/* 前向声明 - 内置主题函数 */
static fr_theme_t fr_theme_blue(void);
static fr_theme_t fr_theme_green(void);
static fr_theme_t fr_theme_high_contrast(void);

/* 初始化主题系统 */
void fr_theme_system_init(void)
{
    g_theme_count = 0;
    g_active_theme = NULL;
    g_override_count = 0;
    g_transition_in_progress = 0;

    /* 注册默认主题 */
    {
        fr_theme_t t = fr_theme_default();
        fr_theme_register(&t);
    }

    {
        fr_theme_t t = fr_theme_dark();
        fr_theme_register(&t);
    }

    {
        fr_theme_t t = fr_theme_light();
        fr_theme_register(&t);
    }

    /* 注册新增内置主题 */
    {
        fr_theme_t t = fr_theme_blue();
        fr_theme_register(&t);
    }

    {
        fr_theme_t t = fr_theme_green();
        fr_theme_register(&t);
    }

    {
        fr_theme_t t = fr_theme_high_contrast();
        fr_theme_register(&t);
    }

    /* 激活默认主题 */
    fr_theme_set_active(NULL, "default");
}

/* 注册主题 */
int fr_theme_register(const fr_theme_t *theme)
{
    if (g_theme_count >= FR_MAX_THEMES || theme == NULL)
        return -1;

    g_themes[g_theme_count] = *theme;
    g_theme_count++;
    return 0;
}

/* 设置活动主题 */
int fr_theme_set_active(fr_handle_t ctx, const char *name)
{
    fr_theme_t *theme = fr_theme_find(name);
    if (theme == NULL)
        return -1;

    g_active_theme = theme;

    if (ctx) {
        fr_context_t *c = (fr_context_t *)ctx;
        c->current_theme = theme;
        c->dirty = 1;
    }

    return 0;
}

/* 获取活动主题 */
fr_theme_t *fr_theme_get_active(void)
{
    return g_active_theme;
}

/* 查找主题 */
fr_theme_t *fr_theme_find(const char *name)
{
    for (uint32_t i = 0; i < g_theme_count; i++) {
        int match = 1;
        for (int j = 0; j < 63 && (name[j] || g_themes[i].name[j]); j++) {
            if (name[j] != g_themes[i].name[j]) { match = 0; break; }
        }
        if (match) return &g_themes[i];
    }
    return NULL;
}

/* 获取主题颜色 */
fr_color_t fr_theme_color(const char *color_name)
{
    if (g_active_theme == NULL)
        return FR_COLOR_BLACK;

    fr_theme_colors_t *c = &g_active_theme->colors;

    if (color_name[0] == 'w' && color_name[1] == 'i' && color_name[2] == 'n')
        return c->window_bg;
    if (color_name[0] == 'b' && color_name[1] == 't' && color_name[2] == 'n')
        return c->button_bg;
    if (color_name[0] == 'a' && color_name[1] == 'c' && color_name[2] == 'c')
        return c->accent;
    if (color_name[0] == 'e' && color_name[1] == 'r' && color_name[2] == 'r')
        return c->error;
    if (color_name[0] == 'm' && color_name[1] == 'e' && color_name[2] == 'n')
        return c->menu_bg;

    return FR_COLOR_BLACK;
}

int fr_theme_border_radius(void)
{
    return g_active_theme ? g_active_theme->metrics.border_radius : 4;
}

int fr_theme_shadow_radius(void)
{
    return g_active_theme ? g_active_theme->metrics.shadow_radius : 0;
}

/* 设置主题（公共API） */
void fr_set_theme(fr_handle_t ctx, const char *theme_name)
{
    fr_theme_set_active(ctx, theme_name);
}

/* 设置字体 */
void fr_set_font(fr_handle_t ctx, const char *font_name, int size)
{
    fr_context_t *c = (fr_context_t *)ctx;
    if (c == NULL) return;

    for (int i = 0; i < 63 && font_name[i]; i++)
        c->font_name[i] = font_name[i];
    c->font_name[63] = '\0';
    c->font_size = size;
}

/* ================================================================
 *  新增内置主题定义
 * ================================================================ */

/*
 * fr_theme_blue - 蓝色系专业主题
 *
 * 以蓝色为主色调的专业风格主题, 适合开发工具和商业应用。
 * 特点: 深蓝标题栏、浅蓝背景、高对比度文字。
 */
fr_theme_t fr_theme_blue(void)
{
    fr_theme_t t;
    /* 主题名称 */
    for (int i = 0; "blue"[i]; i++) t.name[i] = "blue"[i];
    t.name[4] = '\0';

    /* 颜色方案 - 蓝色系 */
    t.colors.window_bg       = FR_RGB(240, 244, 248);
    t.colors.window_fg       = FR_RGB(30, 30, 30);
    t.colors.window_border   = FR_RGB(180, 200, 220);

    t.colors.title_bar_bg    = FR_RGB(0, 90, 158);     /* 深蓝标题栏 */
    t.colors.title_bar_fg    = FR_RGB(255, 255, 255);
    t.colors.title_bar_border= FR_RGB(0, 70, 130);

    t.colors.button_bg       = FR_RGB(230, 240, 250);
    t.colors.button_fg       = FR_RGB(20, 20, 20);
    t.colors.button_hover    = FR_RGB(210, 228, 245);
    t.colors.button_pressed  = FR_RGB(180, 208, 235);
    t.colors.button_border   = FR_RGB(120, 160, 200);

    t.colors.input_bg        = FR_RGB(255, 255, 255);
    t.colors.input_fg        = FR_RGB(20, 20, 20);
    t.colors.input_border    = FR_RGB(140, 180, 215);
    t.colors.input_focus_border = FR_RGB(0, 120, 212);

    t.colors.menu_bg         = FR_RGB(248, 250, 252);
    t.colors.menu_fg         = FR_RGB(30, 30, 30);
    t.colors.menu_hover      = FR_RGB(200, 225, 245);
    t.colors.menu_border     = FR_RGB(180, 200, 220);

    t.colors.scrollbar_bg    = FR_RGB(224, 232, 240);
    t.colors.scrollbar_thumb = FR_RGB(140, 175, 205);
    t.colors.scrollbar_hover = FR_RGB(100, 150, 195);

    t.colors.progress_bg     = FR_RGB(216, 228, 238);
    t.colors.progress_fill   = FR_RGB(0, 120, 212);

    t.colors.accent          = FR_RGB(0, 120, 212);      /* 主强调色: 蓝色 */
    t.colors.accent_hover    = FR_RGB(0, 100, 180);
    t.colors.error           = FR_RGB(200, 50, 50);
    t.colors.warning         = FR_RGB(200, 150, 30);
    t.colors.success         = FR_RGB(40, 160, 80);

    t.colors.disabled_bg     = FR_RGB(232, 236, 240);
    t.colors.disabled_fg     = FR_RGB(160, 168, 180);

    t.colors.shadow          = FR_RGBA(0, 0, 0, 35);

    /* 字体配置 */
    for (int i = 0; "Segoe UI"[i] && i < 63; i++) t.fonts.default_font[i] = "Segoe UI"[i];
    t.fonts.default_font[9 < 63 ? 9 : 62] = '\0';
    for (int i = 0; "Segoe UI Bold"[i] && i < 63; i++) t.fonts.title_font[i] = "Segoe UI Bold"[i];
    t.fonts.title_font[14 < 63 ? 14 : 62] = '\0';
    for (int i = 0; "Consolas"[i] && i < 63; i++) t.fonts.mono_font[i] = "Consolas"[i];
    t.fonts.mono_font[8 < 63 ? 8 : 62] = '\0';
    t.fonts.default_size = 11;
    t.fonts.title_size = 14;
    t.fonts.small_size = 9;

    /* 度量参数 */
    t.metrics.border_radius   = 4;
    t.metrics.border_width    = 1;
    t.metrics.shadow_radius   = 8;
    t.metrics.shadow_offset_x = 2;
    t.metrics.shadow_offset_y = 2;
    t.metrics.padding         = 6;
    t.metrics.spacing         = 6;
    t.metrics.title_bar_height = 32;
    t.metrics.scrollbar_width = 14;

    return t;
}

/*
 * fr_theme_green - 绿色系清新主题
 *
 * 以绿色为主色调的清新风格主题, 适合创意类和环境类应用。
 * 特点: 柔和的绿色调、低饱和度背景、护眼配色。
 */
fr_theme_t fr_theme_green(void)
{
    fr_theme_t t;
    for (int i = 0; "green"[i]; i++) t.name[i] = "green"[i];
    t.name[5] = '\0';

    t.colors.window_bg       = FR_RGB(242, 248, 242);
    t.colors.window_fg       = FR_RGB(28, 32, 28);
    t.colors.window_border   = FR_RGB(170, 200, 170);

    t.colors.title_bar_bg    = FR_RGB(46, 125, 50);     /* 绿色标题栏 */
    t.colors.title_bar_fg    = FR_RGB(255, 255, 255);
    t.colors.title_bar_border= FR_RGB(34, 95, 38);

    t.colors.button_bg       = FR_RGB(232, 244, 232);
    t.colors.button_fg       = FR_RGB(24, 28, 24);
    t.colors.button_hover    = FR_RGB(214, 236, 212);
    t.colors.button_pressed  = FR_RGB(190, 222, 188);
    t.colors.button_border   = FR_RGB(130, 175, 130);

    t.colors.input_bg        = FR_RGB(255, 255, 255);
    t.colors.input_fg        = FR_RGB(24, 28, 24);
    t.colors.input_border    = FR_RGB(155, 190, 155);
    t.colors.input_focus_border = FR_RGB(56, 142, 60);

    t.colors.menu_bg         = FR_RGB(246, 250, 246);
    t.colors.menu_fg         = FR_RGB(28, 32, 28);
    t.colors.menu_hover      = FR_RGB(218, 240, 214);
    t.colors.menu_border     = FR_RGB(170, 200, 170);

    t.colors.scrollbar_bg    = FR_RGB(228, 240, 228);
    t.colors.scrollbar_thumb = FR_RGB(140, 185, 140);
    t.colors.scrollbar_hover = FR_RGB(110, 165, 110);

    t.colors.progress_bg     = FR_RGB(218, 236, 218);
    t.colors.progress_fill   = FR_RGB(56, 142, 60);

    t.colors.accent          = FR_RGB(56, 142, 60);      /* 主强调色: 绿色 */
    t.colors.accent_hover    = FR_RGB(46, 118, 50);
    t.colors.error           = FR_RGB(198, 48, 48);
    t.colors.warning         = FR_RGB(196, 156, 36);
    t.colors.success         = FR_RGB(40, 148, 72);

    t.colors.disabled_bg     = FR_RGB(230, 238, 230);
    t.colors.disabled_fg     = FR_RGB(155, 172, 155);
    t.colors.shadow          = FR_RGBA(0, 0, 0, 28);

    for (int i = 0; "Arial"[i] && i < 63; i++) t.fonts.default_font[i] = "Arial"[i];
    t.fonts.default_font[5 < 63 ? 5 : 62] = '\0';
    for (int i = 0; "Arial Bold"[i] && i < 63; i++) t.fonts.title_font[i] = "Arial Bold"[i];
    t.fonts.title_font[10 < 63 ? 10 : 62] = '\0';
    for (int i = 0; "Courier New"[i] && i < 63; i++) t.fonts.mono_font[i] = "Courier New"[i];
    t.fonts.mono_font[11 < 63 ? 11 : 62] = '\0';
    t.fonts.default_size = 12;
    t.fonts.title_size = 15;
    t.fonts.small_size = 10;

    t.metrics.border_radius   = 5;
    t.metrics.border_width    = 1;
    t.metrics.shadow_radius   = 10;
    t.metrics.shadow_offset_x = 3;
    t.metrics.shadow_offset_y = 3;
    t.metrics.padding         = 8;
    t.metrics.spacing         = 7;
    t.metrics.title_bar_height = 34;
    t.metrics.scrollbar_width = 14;

    return t;
}

/*
 * fr_theme_high_contrast - 高对比度无障碍主题
 *
 * 符合 WCAG AA/AAA 标准的高对比度主题。
 * 特点: 极高对比度、大字体、清晰的焦点指示器、
 *       无依赖纯颜色传达信息的设计。
 */
fr_theme_t fr_theme_high_contrast(void)
{
    fr_theme_t t;
    const char *hc_name = "high_contrast";
    for (int i = 0; hc_name[i] && i < 63; i++) t.name[i] = hc_name[i];
    t.name[14] = '\0';

    /* 纯黑白高对比度配色方案 */
    t.colors.window_bg       = FR_RGB(0, 0, 0);           /* 黑底 */
    t.colors.window_fg       = FR_RGB(255, 255, 255);   /* 白字 */
    t.colors.window_border   = FR_RGB(255, 255, 255);

    t.colors.title_bar_bg    = FR_RGB(0, 0, 0);
    t.colors.title_bar_fg    = FR_RGB(255, 255, 255);
    t.colors.title_bar_border= FR_RGB(255, 255, 255);

    t.colors.button_bg       = FR_RGB(0, 0, 0);
    t.colors.button_fg       = FR_RGB(255, 255, 255);
    t.colors.button_hover    = FR_RGB(40, 40, 40);
    t.colors.button_pressed  = FR_RGB(80, 80, 80);
    t.colors.button_border   = FR_RGB(255, 255, 255);   /* 白边框 */

    t.colors.input_bg        = FR_RGB(0, 0, 0);
    t.colors.input_fg        = FR_RGB(255, 255, 255);
    t.colors.input_border    = FR_RGB(255, 255, 255);
    t.colors.input_focus_border = FR_RGB(255, 255, 0); /* 黄色焦点 */

    t.colors.menu_bg         = FR_RGB(0, 0, 0);
    t.colors.menu_fg         = FR_RGB(255, 255, 255);
    t.colors.menu_hover      = FR_RGB(40, 40, 40);
    t.colors.menu_border     = FR_RGB(255, 255, 255);

    t.colors.scrollbar_bg    = FR_RGB(20, 20, 20);
    t.colors.scrollbar_thumb = FR_RGB(200, 200, 200);
    t.colors.scrollbar_hover = FR_RGB(255, 255, 255);

    t.colors.progress_bg     = FR_RGB(30, 30, 30);
    t.colors.progress_fill   = FR_RGB(255, 255, 255);

    t.colors.accent          = FR_RGB(255, 255, 0);      /* 黄色强调 */
    t.colors.accent_hover    = FR_RGB(255, 255, 100);
    t.colors.error           = FR_RGB(255, 80, 80);
    t.colors.warning         = FR_RGB(255, 255, 0);
    t.colors.success         = FR_RGB(80, 255, 80);

    t.colors.disabled_bg     = FR_RGB(0, 0, 0);
    t.colors.disabled_fg     = FR_RGB(128, 128, 128);
    t.colors.shadow          = FR_RGBA(0, 0, 0, 0);      /* 无阴影 */

    /* 使用较大字号以提高可读性 */
    for (int i = 0; "Tahoma"[i] && i < 63; i++) t.fonts.default_font[i] = "Tahoma"[i];
    t.fonts.default_font[6 < 63 ? 6 : 62] = '\0';
    for (int i = 0; "Tahoma Bold"[i] && i < 63; i++) t.fonts.title_font[i] = "Tahoma Bold"[i];
    t.fonts.title_font[11 < 63 ? 11 : 62] = '\0';
    for (int i = 0; "Courier New"[i] && i < 63; i++) t.fonts.mono_font[i] = "Courier New"[i];
    t.fonts.mono_font[11 < 63 ? 11 : 62] = '\0';
    t.fonts.default_size = 14;    /* 较大默认字号 */
    t.fonts.title_size = 18;     /* 大标题字号 */
    t.fonts.small_size = 12;

    /* 更大的圆角和间距以增强可点击性 */
    t.metrics.border_radius   = 0;  /* 无圆角, 锐利边缘 */
    t.metrics.border_width    = 2;  /* 加粗边框 */
    t.metrics.shadow_radius   = 0;
    t.metrics.shadow_offset_x = 0;
    t.metrics.shadow_offset_y = 0;
    t.metrics.padding         = 10;
    t.metrics.spacing         = 8;
    t.metrics.title_bar_height = 36;
    t.metrics.scrollbar_width = 18;  /* 更宽滚动条 */

    return t;
}

/* ================================================================
 *  主题过渡动画功能
 * ================================================================ */

/*
 * fr_theme_transition_start - 启动平滑过渡到新主题
 *
 * 在源主题和目标主题之间进行线性插值过渡,
 * 过渡期间每帧调用 fr_theme_transition_update() 推进进度。
 */
void fr_theme_transition_start(fr_handle_t ctx, const char *target_name,
                                 uint32_t duration_ms)
{
    if (g_active_theme == NULL) {
        fr_theme_set_active(ctx, target_name);
        return;
    }

    fr_theme_t *target = fr_theme_find(target_name);
    if (target == NULL) return;

    /* 保存源主题快照 */
    g_transition_source = *g_active_theme;
    g_transition_target = *target;

    g_transition_in_progress = 1;
    g_transition_progress = 0.0f;
    g_transition_duration_ms = duration_ms > 0 ? duration_ms : 300;
    g_transition_start_tick = 0; /* 由外部时钟驱动 */
}

/*
 * fr_theme_transition_update - 更新过渡动画进度
 *
 * 应该在渲染循环中每帧调用。传入当前时间戳,
 * 内部计算插值并更新活动主题的颜色值。
 */
void fr_theme_transition_update(uint32_t current_tick)
{
    if (!g_transition_in_progress || g_active_theme == NULL) return;

    if (g_transition_start_tick == 0)
        g_transition_start_tick = current_tick;

    uint32_t elapsed = current_tick - g_transition_start_tick;
    if (elapsed >= g_transition_duration_ms) {
        /* 动画完成, 直接切换到目标主题 */
        *g_active_theme = g_transition_target;
        g_transition_in_progress = 0;
        g_transition_progress = 1.0f;
        return;
    }

    g_transition_progress = (float)elapsed / (float)g_transition_duration_ms;

    /* 对所有颜色进行线性插值 */
#define LERP_COLOR(field) do { \
    fr_color_t s = g_transition_source.colors.field; \
    fr_color_t d = g_transition_target.colors.field; \
    float p = g_transition_progress; \
    g_active_theme->colors.field.r = (uint8_t)(s.r + (d.r - s.r) * p); \
    g_active_theme->colors.field.g = (uint8_t)(s.g + (d.g - s.g) * p); \
    g_active_theme->colors.field.b = (uint8_t)(s.b + (d.b - s.b) * p); \
    g_active_theme->colors.field.a = (uint8_t)(s.a + (d.a - s.a) * p); \
} while(0)

    LERP_COLOR(window_bg); LERP_COLOR(window_fg); LERP_COLOR(window_border);
    LERP_COLOR(title_bar_bg); LERP_COLOR(title_bar_fg); LERP_COLOR(title_bar_border);
    LERP_COLOR(button_bg); LERP_COLOR(button_fg);
    LERP_COLOR(button_hover); LERP_COLOR(button_pressed); LERP_COLOR(button_border);
    LERP_COLOR(input_bg); LERP_COLOR(input_fg); LERP_COLOR(input_border);
    LERP_COLOR(menu_bg); LERP_COLOR(menu_fg); LERP_COLOR(menu_hover); LERP_COLOR(menu_border);
    LERP_COLOR(scrollbar_bg); LERP_COLOR(scrollbar_thumb); LERP_COLOR(scrollbar_hover);
    LERP_COLOR(progress_bg); LERP_COLOR(progress_fill);
    LERP_COLOR(accent); LERP_COLOR(accent_hover);
    LERP_COLOR(error); LERP_COLOR(warning); LERP_COLOR(success);
    LERP_COLOR(disabled_bg); LERP_COLOR(disabled_fg); LERP_COLOR(shadow);
#undef LERP_COLOR

    /* 标记上下文为脏以触发重绘 */
}

int fr_theme_is_transitioning(void)
{
    return g_transition_in_progress;
}

float fr_theme_get_transition_progress(void)
{
    return g_transition_progress;
}

void fr_theme_cancel_transition(void)
{
    g_transition_in_progress = 0;
}

/* ================================================================
 *  控件级颜色覆盖功能
 * ================================================================ */

/*
 * fr_theme_set_widget_color - 为特定控件类型设置颜色覆盖
 *
 * 允许在不修改整个主题的情况下,
 * 单独调整某个控件类型的某个颜色属性。
 * 覆盖优先级高于主题默认值。
 */
int fr_theme_set_widget_color(uint32_t widget_type, const char *color_name,
                               fr_color_t color)
{
    if (g_override_count >= FR_MAX_OVERRIDES) return -1;

    /* 先检查是否已有同名覆盖, 有则更新 */
    for (uint32_t i = 0; i < g_override_count; i++) {
        if (g_overrides[i].widget_type == widget_type &&
            g_overrides[i].active) {
            int match = 1;
            for (int j = 0; j < 31 && (color_name[j] || g_overrides[i].color_name[j]); j++) {
                if (color_name[j] != g_overrides[i].color_name[j]) { match = 0; break; }
            }
            if (match) {
                g_overrides[i].color = color;
                return 0;
            }
        }
    }

    /* 新增覆盖条目 */
    fr_widget_override_t *ov = &g_overrides[g_override_count];
    ov->widget_type = widget_type;
    ov->color = color;
    ov->active = 1;
    for (int i = 0; i < 31 && color_name[i]; i++)
        ov->color_name[i] = color_name[i];
    ov->color_name[31] = '\0';
    g_override_count++;

    return 0;
}

/*
 * fr_theme_clear_widget_override - 清除特定控件的覆盖
 */
void fr_theme_clear_widget_override(uint32_t widget_type, const char *color_name)
{
    for (uint32_t i = 0; i < g_override_count; i++) {
        if (g_overrides[i].widget_type == widget_type &&
            g_overrides[i].active) {
            if (color_name == NULL) {
                g_overrides[i].active = 0;
            } else {
                int match = 1;
                for (int j = 0; j < 31; j++) {
                    if (color_name[j] != g_overrides[i].color_name[j]) { match = 0; break; }
                    if (!color_name[j] && !g_overrides[i].color_name[j]) break;
                }
                if (match) g_overrides[i].active = 0;
            }
        }
    }
}

/*
 * fr_theme_get_widget_color - 获取控件的颜色 (含覆盖检查)
 *
 * 先查找是否有该控件类型的颜色覆盖,
 * 有则返回覆盖值, 否则从活动主题获取。
 */
fr_color_t fr_theme_get_widget_color(uint32_t widget_type, const char *color_name)
{
    /* 首先检查覆盖表 */
    for (uint32_t i = 0; i < g_override_count; i++) {
        if (!g_overrides[i].active) continue;
        if (g_overrides[i].widget_type != widget_type) continue;

        int match = 1;
        for (int j = 0; j < 31; j++) {
            if (color_name[j] != g_overrides[i].color_name[j]) { match = 0; break; }
            if (!color_name[j] && !g_overrides[i].color_name[j]) break;
        }
        if (match) return g_overrides[i].color;
    }

    /* 回退到主题默认值 */
    return fr_theme_color(color_name);
}
