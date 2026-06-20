/* shape.c - 形状库实现
 * 实现圆角矩形、星形、多边形、箭头、气泡和拼图块等形状
 */

#include "funrender.h"
#include "fr_context.h"
#include "fr_shape.h"
#include "string.h"
#include "../kernel/kheap.h"
#include "../lib/stdio.h"

/* ---- 内部辅助函数 ---- */

static float fr_deg2rad(float deg) { return deg * 3.1415926535f / 180.0f; }

static void fr_draw_pixel_safe(fr_context_t *ctx, int x, int y, uint32_t color)
{
    if (x >= 0 && x < ctx->width && y >= 0 && y < ctx->height) {
        ctx->framebuffer[y * ctx->width + x] = color;
    }
}

static void fr_draw_hline(fr_context_t *ctx, int x1, int x2, int y, uint32_t color)
{
    if (y < 0 || y >= ctx->height) return;
    if (x1 > x2) { int t = x1; x1 = x2; x2 = t; }
    if (x1 < 0) x1 = 0;
    if (x2 >= ctx->width) x2 = ctx->width - 1;
    for (int x = x1; x <= x2; x++) {
        ctx->framebuffer[y * ctx->width + x] = color;
    }
}

static void fr_draw_vline(fr_context_t *ctx, int x, int y1, int y2, uint32_t color)
{
    if (x < 0 || x >= ctx->width) return;
    if (y1 > y2) { int t = y1; y1 = y2; y2 = t; }
    if (y1 < 0) y1 = 0;
    if (y2 >= ctx->height) y2 = ctx->height - 1;
    for (int y = y1; y <= y2; y++) {
        ctx->framebuffer[y * ctx->width + x] = color;
    }
}

/* ---- 形状创建 ---- */

fr_shape_t *fr_shape_create(uint32_t type)
{
    fr_shape_t *shape = (fr_shape_t *)fr_alloc(sizeof(fr_shape_t));
    if (!shape) return NULL;
    memset(shape, 0, sizeof(fr_shape_t));
    shape->type = type;
    shape->style.fill_color = FR_RGB(200, 200, 200);
    shape->style.stroke_color = FR_RGB(0, 0, 0);
    shape->style.stroke_width = 1.0f;
    shape->style.fill_opacity = 255;
    shape->style.stroke_opacity = 255;
    return shape;
}

void fr_shape_destroy(fr_shape_t *shape)
{
    if (shape) fr_free(shape);
}

void fr_shape_set_rect(fr_shape_t *shape, float x, float y, float w, float h)
{
    if (!shape) return;
    shape->x = x; shape->y = y; shape->w = w; shape->h = h;
}

void fr_shape_set_rotation(fr_shape_t *shape, float angle, float cx, float cy)
{
    if (!shape) return;
    shape->rotation = angle;
    shape->center_x = cx;
    shape->center_y = cy;
}

/* ---- 形状特定创建函数 ---- */

fr_shape_t *fr_shape_create_rounded_rect(float x, float y, float w, float h,
                                          float rx, float ry)
{
    fr_shape_t *s = fr_shape_create(FR_SHAPE_ROUNDED_RECT);
    if (!s) return NULL;
    s->x = x; s->y = y; s->w = w; s->h = h;
    s->params[FR_RECT_PARAM_RX] = rx;
    s->params[FR_RECT_PARAM_RY] = ry;
    s->param_count = 2;
    return s;
}

fr_shape_t *fr_shape_create_star(float x, float y, float w, float h,
                                  int points, float inner_ratio)
{
    fr_shape_t *s = fr_shape_create(FR_SHAPE_STAR);
    if (!s) return NULL;
    s->x = x; s->y = y; s->w = w; s->h = h;
    s->params[FR_STAR_PARAM_POINTS] = (float)points;
    s->params[FR_STAR_PARAM_INNER] = inner_ratio;
    s->param_count = 3;
    return s;
}

fr_shape_t *fr_shape_create_polygon(float x, float y, float w, float h, int sides)
{
    fr_shape_t *s = fr_shape_create(FR_SHAPE_POLYGON);
    if (!s) return NULL;
    s->x = x; s->y = y; s->w = w; s->h = h;
    s->params[FR_POLY_PARAM_SIDES] = (float)sides;
    s->param_count = 2;
    return s;
}

fr_shape_t *fr_shape_create_arrow(float x, float y, float w, float h, int direction)
{
    fr_shape_t *s = fr_shape_create(FR_SHAPE_ARROW);
    if (!s) return NULL;
    s->x = x; s->y = y; s->w = w; s->h = h;
    s->params[FR_ARROW_PARAM_DIR] = (float)direction;
    s->params[FR_ARROW_PARAM_STEM_W] = w * 0.3f;
    s->params[FR_ARROW_PARAM_HEAD_W] = w * 0.6f;
    s->params[FR_ARROW_PARAM_HEAD_L] = h * 0.4f;
    s->param_count = 4;
    return s;
}

fr_shape_t *fr_shape_create_speech_bubble(float x, float y, float w, float h,
                                           int point_dir, float px, float py)
{
    fr_shape_t *s = fr_shape_create(FR_SHAPE_SPEECH_BUBBLE);
    if (!s) return NULL;
    s->x = x; s->y = y; s->w = w; s->h = h;
    s->params[FR_BUBBLE_PARAM_DIR] = (float)point_dir;
    s->params[FR_BUBBLE_PARAM_PX] = px;
    s->params[FR_BUBBLE_PARAM_PY] = py;
    s->params[FR_BUBBLE_PARAM_PS] = w * 0.15f;
    s->params[FR_BUBBLE_PARAM_CR] = 8.0f;
    s->param_count = 5;
    return s;
}

fr_shape_t *fr_shape_create_puzzle(float x, float y, float w, float h,
                                    int rows, int cols, int row, int col)
{
    fr_shape_t *s = fr_shape_create(FR_SHAPE_PUZZLE);
    if (!s) return NULL;
    s->x = x; s->y = y; s->w = w; s->h = h;
    s->params[FR_PUZZLE_PARAM_ROWS] = (float)rows;
    s->params[FR_PUZZLE_PARAM_COLS] = (float)cols;
    s->params[FR_PUZZLE_PARAM_ROW] = (float)row;
    s->params[FR_PUZZLE_PARAM_COL] = (float)col;
    s->params[FR_PUZZLE_PARAM_TAB] = w * 0.2f;
    s->param_count = 5;
    return s;
}

fr_shape_t *fr_shape_create_cross(float x, float y, float w, float h, float thickness)
{
    fr_shape_t *s = fr_shape_create(FR_SHAPE_CROSS);
    if (!s) return NULL;
    s->x = x; s->y = y; s->w = w; s->h = h;
    s->params[FR_CROSS_PARAM_THICK] = thickness;
    s->param_count = 2;
    return s;
}

fr_shape_t *fr_shape_create_check(float x, float y, float w, float h, float thickness)
{
    fr_shape_t *s = fr_shape_create(FR_SHAPE_CHECK);
    if (!s) return NULL;
    s->x = x; s->y = y; s->w = w; s->h = h;
    s->params[FR_CHECK_PARAM_THICK] = thickness;
    s->param_count = 1;
    return s;
}

fr_shape_t *fr_shape_create_heart(float x, float y, float w, float h)
{
    fr_shape_t *s = fr_shape_create(FR_SHAPE_HEART);
    if (!s) return NULL;
    s->x = x; s->y = y; s->w = w; s->h = h;
    return s;
}

fr_shape_t *fr_shape_create_diamond(float x, float y, float w, float h)
{
    fr_shape_t *s = fr_shape_create(FR_SHAPE_DIAMOND);
    if (!s) return NULL;
    s->x = x; s->y = y; s->w = w; s->h = h;
    return s;
}

fr_shape_t *fr_shape_create_triangle(float x, float y, float w, float h, int direction)
{
    fr_shape_t *s = fr_shape_create(FR_SHAPE_TRIANGLE);
    if (!s) return NULL;
    s->x = x; s->y = y; s->w = w; s->h = h;
    s->params[0] = (float)direction;
    s->param_count = 1;
    return s;
}

fr_shape_t *fr_shape_create_hexagon(float x, float y, float w, float h)
{
    fr_shape_t *s = fr_shape_create(FR_SHAPE_HEXAGON);
    if (!s) return NULL;
    s->x = x; s->y = y; s->w = w; s->h = h;
    return s;
}

fr_shape_t *fr_shape_create_octagon(float x, float y, float w, float h)
{
    fr_shape_t *s = fr_shape_create(FR_SHAPE_OCTAGON);
    if (!s) return NULL;
    s->x = x; s->y = y; s->w = w; s->h = h;
    return s;
}

fr_shape_t *fr_shape_create_pentagon(float x, float y, float w, float h)
{
    fr_shape_t *s = fr_shape_create(FR_SHAPE_PENTAGON);
    if (!s) return NULL;
    s->x = x; s->y = y; s->w = w; s->h = h;
    return s;
}

/* ---- 样式设置 ---- */

void fr_shape_set_style(fr_shape_t *shape, const fr_shape_style_t *style)
{
    if (shape && style) shape->style = *style;
}

void fr_shape_set_fill(fr_shape_t *shape, fr_color_t color)
{
    if (shape) shape->style.fill_color = color;
}

void fr_shape_set_stroke(fr_shape_t *shape, fr_color_t color, float width)
{
    if (!shape) return;
    shape->style.stroke_color = color;
    shape->style.stroke_width = width;
}

void fr_shape_set_shadow(fr_shape_t *shape, int enabled, fr_color_t color,
                          float ox, float oy, float blur)
{
    if (!shape) return;
    shape->style.shadow = enabled;
    shape->style.shadow_color = color;
    shape->style.shadow_offset_x = ox;
    shape->style.shadow_offset_y = oy;
    shape->style.shadow_blur = blur;
}

void fr_shape_set_corner_radius(fr_shape_t *shape, float radius)
{
    if (shape) shape->style.corner_radius = radius;
}

void fr_shape_set_param(fr_shape_t *shape, int index, float value)
{
    if (shape && index >= 0 && index < 16) {
        shape->params[index] = value;
        if (index >= shape->param_count) shape->param_count = index + 1;
    }
}

float fr_shape_get_param(const fr_shape_t *shape, int index)
{
    if (shape && index >= 0 && index < shape->param_count)
        return shape->params[index];
    return 0.0f;
}

/* ---- 渲染 ---- */

static void fr_shape_fill_rect(fr_context_t *ctx, int x, int y, int w, int h, uint32_t color)
{
    for (int py = y; py < y + h; py++) {
        fr_draw_hline(ctx, x, x + w - 1, py, color);
    }
}

static void fr_shape_fill_ellipse(fr_context_t *ctx, int cx, int cy,
                                   int rx, int ry, uint32_t color)
{
    for (int py = -ry; py <= ry; py++) {
        for (int px = -rx; px <= rx; px++) {
            if ((float)(px * px) / (float)(rx * rx) + (float)(py * py) / (float)(ry * ry) <= 1.0f) {
                fr_draw_pixel_safe(ctx, cx + px, cy + py, color);
            }
        }
    }
}

void fr_shape_fill(fr_shape_t *shape, fr_context_t *ctx)
{
    if (!shape || !ctx || !ctx->framebuffer) return;

    uint32_t color = ((uint32_t)shape->style.fill_opacity << 24) |
                     ((uint32_t)shape->style.fill_color.r << 16) |
                     ((uint32_t)shape->style.fill_color.g << 8) |
                     (uint32_t)shape->style.fill_color.b;

    int sx = (int)shape->x;
    int sy = (int)shape->y;
    int sw = (int)shape->w;
    int sh = (int)shape->h;

    switch (shape->type) {
    case FR_SHAPE_RECT:
        fr_shape_fill_rect(ctx, sx, sy, sw, sh, color);
        break;
    case FR_SHAPE_ROUNDED_RECT: {
        float rx = shape->params[FR_RECT_PARAM_RX];
        float ry = shape->params[FR_RECT_PARAM_RY];
        if (rx <= 0.0f && ry <= 0.0f) {
            fr_shape_fill_rect(ctx, sx, sy, sw, sh, color);
            break;
        }

        /* 限制圆角半径不超过矩形尺寸的一半 */
        if (rx > sw * 0.5f) rx = sw * 0.5f;
        if (ry > sh * 0.5f) ry = sh * 0.5f;
        /* 最小圆角半径为1 */
        if (rx < 1.0f) rx = 1.0f;
        if (ry < 1.0f) ry = 1.0f;

        int irx = (int)rx;
        int iry = (int)ry;

        /* 1. 填充中间矩形 (不含四个角的区域) */
        for (int py = sy + iry; py < sy + sh - iry; py++) {
            if (py < 0 || py >= ctx->height) continue;
            int x_start = sx < 0 ? 0 : sx;
            int x_end = sx + sw - 1;
            if (x_end >= ctx->width) x_end = ctx->width - 1;
            for (int px = x_start; px <= x_end; px++)
                ctx->framebuffer[py * ctx->width + px] = color;
        }

        /* 2. 填充上下水平条带 */
        for (int py = sy; py < sy + iry; py++) {
            if (py < 0 || py >= ctx->height) continue;
            int x_start = sx + irx;
            int x_end = sx + sw - irx - 1;
            if (x_start < 0) x_start = 0;
            if (x_end >= ctx->width) x_end = ctx->width - 1;
            for (int px = x_start; px <= x_end; px++)
                ctx->framebuffer[py * ctx->width + px] = color;
        }
        for (int py = sy + sh - iry; py < sy + sh; py++) {
            if (py < 0 || py >= ctx->height) continue;
            int x_start = sx + irx;
            int x_end = sx + sw - irx - 1;
            if (x_start < 0) x_start = 0;
            if (x_end >= ctx->width) x_end = ctx->width - 1;
            for (int px = x_start; px <= x_end; px++)
                ctx->framebuffer[py * ctx->width + px] = color;
        }

        /* 3. 四个圆角 - 用椭圆方程判断
         * 左上角圆心 (sx+irx, sy+iry)
         * 右上角圆心 (sx+sw-irx-1, sy+iry)
         * 左下角圆心 (sx+irx, sy+sh-iry-1)
         * 右下角圆心 (sx+sw-irx-1, sy+sh-iry-1)
         */
        int corners_x[4] = { sx + irx,           sx + sw - irx - 1, sx + irx,           sx + sw - irx - 1 };
        int corners_y[4] = { sy + iry,           sy + iry,           sy + sh - iry - 1,   sy + sh - iry - 1 };
        int dx_signs[4] = { -1,                   1,                  -1,                   1 };
        int dy_signs[4] = { -1,                  -1,                   1,                    1 };

        for (int c = 0; c < 4; c++) {
            int cx_corner = corners_x[c];
            int cy_corner = corners_y[c];
            int y_start = dy_signs[c] > 0 ? cy_corner : cy_corner - iry + 1;
            int y_end   = dy_signs[c] > 0 ? cy_corner + iry - 1 : cy_corner;

            for (int py = y_start; py <= y_end; py++) {
                if (py < 0 || py >= ctx->height) continue;
                int x_start_c = dx_signs[c] > 0 ? cx_corner : cx_corner - irx + 1;
                int x_end_c   = dx_signs[c] > 0 ? cx_corner + irx - 1 : cx_corner;

                for (int px = x_start_c; px <= x_end_c; px++) {
                    if (px < 0 || px >= ctx->width) continue;
                    float dx = (float)(px - cx_corner);
                    float dy = (float)(py - cy_corner);
                    /* 椭圆方程: (dx/rx)^2 + (dy/ry)^2 <= 1 */
                    if ((dx * dx) / (rx * rx) + (dy * dy) / (ry * ry) <= 1.0f)
                        ctx->framebuffer[py * ctx->width + px] = color;
                }
            }
        }
        break;
    }
    case FR_SHAPE_ELLIPSE:
        fr_shape_fill_ellipse(ctx, sx + sw / 2, sy + sh / 2, sw / 2, sh / 2, color);
        break;
    case FR_SHAPE_DIAMOND: {
        int cx = sx + sw / 2, cy = sy + sh / 2;
        for (int py = 0; py < sh; py++) {
            float t = (float)py / (float)sh;
            int half_w = (int)(sw * 0.5f * (1.0f - 2.0f * (t > 0.5f ? t - 0.5f : 0.5f - t)));
            fr_draw_hline(ctx, cx - half_w, cx + half_w, sy + py, color);
        }
        break;
    }
    case FR_SHAPE_TRIANGLE: {
        int cx = sx + sw / 2;
        int dir = (int)shape->params[0];
        for (int py = 0; py < sh; py++) {
            float t = (float)py / (float)sh;
            int half_w = (int)(sw * 0.5f * t);
            fr_draw_hline(ctx, cx - half_w, cx + half_w, sy + py, color);
        }
        break;
    }
    default:
        /* 默认矩形填充 */
        fr_shape_fill_rect(ctx, sx, sy, sw, sh, color);
        break;
    }
}

void fr_shape_stroke(fr_shape_t *shape, fr_context_t *ctx)
{
    if (!shape || !ctx || !ctx->framebuffer) return;

    uint32_t color = ((uint32_t)shape->style.stroke_opacity << 24) |
                     ((uint32_t)shape->style.stroke_color.r << 16) |
                     ((uint32_t)shape->style.stroke_color.g << 8) |
                     (uint32_t)shape->style.stroke_color.b;

    int sx = (int)shape->x;
    int sy = (int)shape->y;
    int sw = (int)shape->w;
    int sh = (int)shape->h;
    int swi = (int)shape->style.stroke_width;
    if (swi < 1) swi = 1;

    /* 简单矩形边框 */
    fr_draw_hline(ctx, sx, sx + sw - 1, sy, color);
    fr_draw_hline(ctx, sx, sx + sw - 1, sy + sh - 1, color);
    fr_draw_vline(ctx, sx, sy, sy + sh - 1, color);
    fr_draw_vline(ctx, sx + sw - 1, sy, sy + sh - 1, color);

    /* 加粗边框 */
    for (int i = 1; i < swi; i++) {
        fr_draw_hline(ctx, sx, sx + sw - 1, sy + i, color);
        fr_draw_hline(ctx, sx, sx + sw - 1, sy + sh - 1 - i, color);
        fr_draw_vline(ctx, sx + i, sy, sy + sh - 1, color);
        fr_draw_vline(ctx, sx + sw - 1 - i, sy, sy + sh - 1, color);
    }
}

void fr_shape_draw(fr_shape_t *shape, fr_context_t *ctx)
{
    if (!shape || !ctx) return;

    /* 阴影 */
    if (shape->style.shadow) {
        fr_shape_t shadow_shape = *shape;
        shadow_shape.x += shadow_shape.style.shadow_offset_x;
        shadow_shape.y += shadow_shape.style.shadow_offset_y;
        shadow_shape.style.fill_color = shadow_shape.style.shadow_color;
        shadow_shape.style.fill_opacity = 128;
        fr_shape_fill(&shadow_shape, ctx);
    }

    fr_shape_fill(shape, ctx);
    if (shape->style.stroke_width > 0.0f) {
        fr_shape_stroke(shape, ctx);
    }
}

/* ---- 查询 ---- */

int fr_shape_contains_point(const fr_shape_t *shape, float x, float y)
{
    if (!shape) return 0;
    if (x < shape->x || x > shape->x + shape->w ||
        y < shape->y || y > shape->y + shape->h) return 0;

    switch (shape->type) {
    case FR_SHAPE_ELLIPSE: {
        float cx = shape->x + shape->w * 0.5f;
        float cy = shape->y + shape->h * 0.5f;
        float rx = shape->w * 0.5f;
        float ry = shape->h * 0.5f;
        float dx = (x - cx) / rx;
        float dy = (y - cy) / ry;
        return (dx * dx + dy * dy) <= 1.0f;
    }
    default:
        return 1;
    }
}

void fr_shape_get_bounds(const fr_shape_t *shape, float *x, float *y, float *w, float *h)
{
    if (shape && x && y && w && h) {
        *x = shape->x; *y = shape->y;
        *w = shape->w; *h = shape->h;
    }
}

/* ---- 批量绘制 ---- */

void fr_shape_draw_grid(fr_context_t *ctx, fr_shape_t *shape,
                         int cols, int rows, float spacing_x, float spacing_y)
{
    if (!ctx || !shape) return;

    float orig_x = shape->x;
    float orig_y = shape->y;

    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            shape->x = orig_x + (float)c * (shape->w + spacing_x);
            shape->y = orig_y + (float)r * (shape->h + spacing_y);
            fr_shape_draw(shape, ctx);
        }
    }

    shape->x = orig_x;
    shape->y = orig_y;
}