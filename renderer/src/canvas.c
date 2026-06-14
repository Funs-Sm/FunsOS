/* canvas.c - 2D 画布绘图 API
 * 提供类似 HTML5 Canvas 的 2D 绘图接口
 */

#include "funrender.h"
#include "fr_context.h"
#include "gfx.h"
#include "math.h"

/* 设置像素 */
void fr_canvas_set_pixel(fr_context_t *ctx, int x, int y, fr_color_t color)
{
    if (ctx == NULL || ctx->framebuffer == NULL) return;
    if (x < 0 || x >= ctx->width || y < 0 || y >= ctx->height) return;

    uint32_t pixel = ((uint32_t)color.r << 16) |
                     ((uint32_t)color.g << 8) |
                     (uint32_t)color.b;

    if (color.a == 255) {
        ctx->framebuffer[y * ctx->width + x] = pixel;
    } else {
        /* Alpha 混合 */
        uint32_t bg = ctx->framebuffer[y * ctx->width + x];
        uint8_t bg_r = (bg >> 16) & 0xFF;
        uint8_t bg_g = (bg >> 8) & 0xFF;
        uint8_t bg_b = bg & 0xFF;
        uint8_t inv = 255 - color.a;
        uint8_t r = (color.r * color.a + bg_r * inv) / 255;
        uint8_t g = (color.g * color.a + bg_g * inv) / 255;
        uint8_t b = (color.b * color.a + bg_b * inv) / 255;
        ctx->framebuffer[y * ctx->width + x] = (r << 16) | (g << 8) | b;
    }
}

/* 绘制直线 */
void fr_canvas_draw_line(fr_context_t *ctx, int x0, int y0, int x1, int y1,
                         fr_color_t color, int width)
{
    if (ctx == NULL || ctx->framebuffer == NULL) return;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    uint32_t c = ((uint32_t)color.r << 16) | ((uint32_t)color.g << 8) | color.b;
    gfx_draw_line(&gfx_ctx, x0, y0, x1, y1, c);

    /* 线宽 > 1 时加粗 */
    if (width > 1) {
        for (int i = 1; i < width; i++) {
            gfx_draw_line(&gfx_ctx, x0 + i, y0, x1 + i, y1, c);
            gfx_draw_line(&gfx_ctx, x0, y0 + i, x1, y1 + i, c);
        }
    }
}

/* 绘制矩形 */
void fr_canvas_draw_rect(fr_context_t *ctx, int x, int y, int w, int h,
                         fr_color_t color, int line_width)
{
    if (ctx == NULL || ctx->framebuffer == NULL) return;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    uint32_t c = ((uint32_t)color.r << 16) | ((uint32_t)color.g << 8) | color.b;

    for (int i = 0; i < line_width; i++) {
        gfx_rect_t rect = {x + i, y + i, w - 2 * i, h - 2 * i};
        gfx_draw_rect(&gfx_ctx, rect, c);
    }
}

/* 填充矩形 */
void fr_canvas_fill_rect(fr_context_t *ctx, int x, int y, int w, int h,
                         fr_color_t color)
{
    if (ctx == NULL || ctx->framebuffer == NULL) return;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    uint32_t c = ((uint32_t)color.r << 16) | ((uint32_t)color.g << 8) | color.b;
    gfx_rect_t rect = {x, y, w, h};
    gfx_fill_rect(&gfx_ctx, rect, c);
}

/* 绘制圆角矩形 */
void fr_canvas_draw_rounded_rect(fr_context_t *ctx, int x, int y, int w, int h,
                                 int radius, fr_color_t color)
{
    if (ctx == NULL || ctx->framebuffer == NULL) return;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    uint32_t c = ((uint32_t)color.r << 16) | ((uint32_t)color.g << 8) | color.b;
    gfx_rect_t rect = {x, y, w, h};
    gfx_draw_rounded_rect(&gfx_ctx, rect, radius, c);
}

/* 填充圆角矩形 */
void fr_canvas_fill_rounded_rect(fr_context_t *ctx, int x, int y, int w, int h,
                                 int radius, fr_color_t color)
{
    if (ctx == NULL || ctx->framebuffer == NULL) return;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    uint32_t c = ((uint32_t)color.r << 16) | ((uint32_t)color.g << 8) | color.b;
    gfx_rect_t rect = {x, y, w, h};
    gfx_fill_rounded_rect(&gfx_ctx, rect, radius, c);
}

/* 绘制圆形 */
void fr_canvas_draw_circle(fr_context_t *ctx, int cx, int cy, int r,
                           fr_color_t color, int line_width)
{
    if (ctx == NULL || ctx->framebuffer == NULL) return;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    uint32_t c = ((uint32_t)color.r << 16) | ((uint32_t)color.g << 8) | color.b;
    gfx_draw_circle(&gfx_ctx, cx, cy, r, c);

    /* 线宽 > 1 */
    for (int i = 1; i < line_width; i++) {
        gfx_draw_circle(&gfx_ctx, cx, cy, r + i, c);
        gfx_draw_circle(&gfx_ctx, cx, cy, r - i > 0 ? r - i : 0, c);
    }
}

/* 填充圆形 */
void fr_canvas_fill_circle(fr_context_t *ctx, int cx, int cy, int r,
                           fr_color_t color)
{
    if (ctx == NULL || ctx->framebuffer == NULL) return;

    gfx_context_t gfx_ctx;
    gfx_ctx.buffer = ctx->framebuffer;
    gfx_ctx.width = ctx->width;
    gfx_ctx.height = ctx->height;

    uint32_t c = ((uint32_t)color.r << 16) | ((uint32_t)color.g << 8) | color.b;
    gfx_fill_circle(&gfx_ctx, cx, cy, r, c);
}

/* 绘制椭圆 */
void fr_canvas_draw_ellipse(fr_context_t *ctx, int cx, int cy,
                            int rx, int ry, fr_color_t color)
{
    if (ctx == NULL || ctx->framebuffer == NULL) return;

    uint32_t c = ((uint32_t)color.r << 16) | ((uint32_t)color.g << 8) | color.b;

    /* 参数方程绘制椭圆 */
    for (int angle = 0; angle < 360; angle++) {
        float rad = (float)angle * 3.14159f / 180.0f;
        int x = cx + (int)((float)rx * cos(rad));
        int y = cy + (int)((float)ry * sin(rad));
        if (x >= 0 && x < ctx->width && y >= 0 && y < ctx->height)
            ctx->framebuffer[y * ctx->width + x] = c;
    }
}

/* 绘制多边形 */
void fr_canvas_draw_polygon(fr_context_t *ctx, int *points, int count,
                            fr_color_t color)
{
    if (ctx == NULL || count < 3) return;

    for (int i = 0; i < count; i++) {
        int x0 = points[i * 2];
        int y0 = points[i * 2 + 1];
        int x1 = points[((i + 1) % count) * 2];
        int y1 = points[((i + 1) % count) * 2 + 1];
        fr_canvas_draw_line(ctx, x0, y0, x1, y1, color, 1);
    }
}

/* 绘制弧线 */
void fr_canvas_draw_arc(fr_context_t *ctx, int cx, int cy, int r,
                        int start_angle, int end_angle, fr_color_t color)
{
    if (ctx == NULL || ctx->framebuffer == NULL) return;

    uint32_t c = ((uint32_t)color.r << 16) | ((uint32_t)color.g << 8) | color.b;

    for (int angle = start_angle; angle < end_angle; angle++) {
        float rad = (float)angle * 3.14159f / 180.0f;
        int x = cx + (int)((float)r * cos(rad));
        int y = cy + (int)((float)r * sin(rad));
        if (x >= 0 && x < ctx->width && y >= 0 && y < ctx->height)
            ctx->framebuffer[y * ctx->width + x] = c;
    }
}

/* 位图传输 */
void fr_canvas_blit(fr_context_t *dst, int dx, int dy,
                    uint32_t *src, int src_w, int src_h,
                    int sx, int sy, int sw, int sh)
{
    if (dst == NULL || src == NULL || dst->framebuffer == NULL) return;

    for (int y = 0; y < sh; y++) {
        for (int x = 0; x < sw; x++) {
            int tx = dx + x;
            int ty = dy + y;
            int sxi = sx + x;
            int syi = sy + y;

            if (tx >= 0 && tx < dst->width && ty >= 0 && ty < dst->height &&
                sxi >= 0 && sxi < src_w && syi >= 0 && syi < src_h) {
                dst->framebuffer[ty * dst->width + tx] =
                    src[syi * src_w + sxi];
            }
        }
    }
}
