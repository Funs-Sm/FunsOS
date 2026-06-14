/* window.c - 窗口渲染实现
 * 标题栏/边框/阴影/圆角/拖拽/缩放
 */

#include "funrender.h"
#include "fr_context.h"
#include "gfx.h"
#include "string.h"

/* 窗口渲染参数 */
#define WIN_TITLE_HEIGHT    28
#define WIN_BORDER_WIDTH    1
#define WIN_SHADOW_RADIUS   8
#define WIN_CORNER_RADIUS   8
#define WIN_MIN_WIDTH       100
#define WIN_MIN_HEIGHT      60
#define WIN_RESIZE_MARGIN   6

/* 绘制窗口阴影 */
static void draw_window_shadow(gfx_context_t *gfx, int x, int y, int w, int h)
{
    /* 简化阴影：在窗口周围绘制半透明边框 */
    uint32_t shadow_colors[] = {
        0x10000000, 0x08000000, 0x04000000, 0x02000000
    };

    for (int i = 0; i < 4; i++) {
        gfx_rect_t r = {x - 4 + i, y - 4 + i, w + 8 - 2 * i, h + 8 - 2 * i};
        gfx_draw_rounded_rect(gfx, r, WIN_CORNER_RADIUS + 2, shadow_colors[i]);
    }
}

/* 绘制窗口标题栏 */
static void draw_title_bar(gfx_context_t *gfx, int x, int y, int w,
                           const char *title, int focused,
                           fr_color_t title_bg, fr_color_t title_fg)
{
    /* 标题栏背景 */
    gfx_rect_t bar = {x, y, w, WIN_TITLE_HEIGHT};
    uint32_t bg = (title_bg.r << 16) | (title_bg.g << 8) | title_bg.b;
    gfx_fill_rect(gfx, bar, focused ? bg : 0x404040);

    /* 标题文字 */
    if (title) {
        uint32_t fg = (title_fg.r << 16) | (title_fg.g << 8) | title_fg.b;
        (void)fg;
        /* 实际文字绘制由字体引擎完成 */
    }

    /* 关闭按钮 */
    gfx_rect_t close_btn = {x + w - 28, y + 4, 24, 20};
    gfx_fill_rect(gfx, close_btn, focused ? 0xE04040 : 0x606060);

    /* 最大化按钮 */
    gfx_rect_t max_btn = {x + w - 56, y + 4, 24, 20};
    gfx_fill_rect(gfx, max_btn, focused ? 0x408040 : 0x505050);

    /* 最小化按钮 */
    gfx_rect_t min_btn = {x + w - 84, y + 4, 24, 20};
    gfx_fill_rect(gfx, min_btn, focused ? 0x4040C0 : 0x505050);
}

/* 绘制窗口边框 */
static void draw_window_border(gfx_context_t *gfx, int x, int y, int w, int h,
                               int focused)
{
    uint32_t border_color = focused ? 0x0078D4 : 0x808080;
    gfx_rect_t rect = {x, y, w, h};
    gfx_draw_rounded_rect(gfx, rect, WIN_CORNER_RADIUS, border_color);
}

/* 渲染完整窗口 */
void fr_render_window(fr_context_t *ctx, int x, int y, int w, int h,
                      const char *title, int focused, int flags)
{
    if (ctx == NULL || ctx->framebuffer == NULL) return;

    gfx_context_t gfx;
    gfx.buffer = ctx->framebuffer;
    gfx.width = ctx->width;
    gfx.height = ctx->height;

    /* 阴影 */
    if (flags & 0x01) {  /* 有阴影 */
        draw_window_shadow(&gfx, x, y, w, h);
    }

    /* 窗口背景（圆角） */
    gfx_rect_t win_rect = {x, y, w, h};
    gfx_fill_rounded_rect(&gfx, win_rect, WIN_CORNER_RADIUS, 0xFFFFFF);

    /* 标题栏 */
    if (flags & 0x02) {  /* 有标题栏 */
        fr_color_t title_bg = FR_RGB(0x2D, 0x2D, 0x2D);
        fr_color_t title_fg = FR_RGB(0xFF, 0xFF, 0xFF);
        draw_title_bar(&gfx, x, y, w, title, focused, title_bg, title_fg);
    }

    /* 边框 */
    draw_window_border(&gfx, x, y, w, h, focused);

    /* 内容区域 */
    int content_y = y + ((flags & 0x02) ? WIN_TITLE_HEIGHT : 0);
    int content_h = h - ((flags & 0x02) ? WIN_TITLE_HEIGHT : 0);
    gfx_rect_t content = {x + 1, content_y, w - 2, content_h - 1};
    gfx_fill_rect(&gfx, content, 0xFFFFFF);
}

/* 检测窗口边框区域（用于缩放） */
int fr_window_hit_test(int win_x, int win_y, int win_w, int win_h,
                       int test_x, int test_y)
{
    /* 标题栏区域 */
    if (test_x >= win_x && test_x < win_x + win_w &&
        test_y >= win_y && test_y < win_y + WIN_TITLE_HEIGHT) {
        return 1;  /* 标题栏 - 可拖拽 */
    }

    /* 边框区域 - 可缩放 */
    if (test_x >= win_x - WIN_RESIZE_MARGIN &&
        test_x < win_x + win_w + WIN_RESIZE_MARGIN &&
        test_y >= win_y - WIN_RESIZE_MARGIN &&
        test_y < win_y + win_h + WIN_RESIZE_MARGIN) {
        return 2;  /* 边框 - 可缩放 */
    }

    return 0;  /* 不在窗口区域 */
}

/* 计算窗口缩放后的新尺寸 */
void fr_window_calc_resize(int win_x, int win_y, int win_w, int win_h,
                           int dx, int dy, int edge,
                           int *new_x, int *new_y, int *new_w, int *new_h)
{
    *new_x = win_x;
    *new_y = win_y;
    *new_w = win_w;
    *new_h = win_h;

    switch (edge) {
    case 1: /* 右边 */
        *new_w = win_w + dx;
        break;
    case 2: /* 下边 */
        *new_h = win_h + dy;
        break;
    case 3: /* 左边 */
        *new_x = win_x + dx;
        *new_w = win_w - dx;
        break;
    case 4: /* 上边 */
        *new_y = win_y + dy;
        *new_h = win_h - dy;
        break;
    }

    /* 限制最小尺寸 */
    if (*new_w < WIN_MIN_WIDTH) *new_w = WIN_MIN_WIDTH;
    if (*new_h < WIN_MIN_HEIGHT) *new_h = WIN_MIN_HEIGHT;
}
