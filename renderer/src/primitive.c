/* primitive.c - 图元光栅化器实现
 * Bresenham 直线 / Xiaolin Wu 反走样直线
 * 扫描线三角形填充 (单色 + Gouraud 着色)
 * 中点椭圆算法 / 圆弧
 * 二次贝塞尔曲线 (de Casteljau 细分)
 */

#include "primitive.h"
#include "fr_context.h"
#include "math_util.h"
#include "string.h"

/* ================================================================
 *  内部辅助: 安全像素写入 (带边界和裁剪检测)
 * ================================================================ */

static void prim_put_pixel(fr_context_t *ctx, int x, int y, uint32_t color)
{
    if (!ctx || !ctx->framebuffer) return;
    if (x >= 0 && x < ctx->width && y >= 0 && y < ctx->height) {
        if (x >= ctx->clip_x && x < ctx->clip_x + ctx->clip_w &&
            y >= ctx->clip_y && y < ctx->clip_y + ctx->clip_h) {
            ctx->framebuffer[y * ctx->width + x] = color;
        }
    }
}

/* ================================================================
 *  基础像素操作
 * ================================================================ */

void prim_set_pixel(fr_context_t *ctx, int x, int y, uint32_t color)
{
    prim_put_pixel(ctx, x, y, color);
}

void prim_draw_hline(fr_context_t *ctx, int x0, int x1, int y,
                     uint32_t color)
{
    if (!ctx || !ctx->framebuffer) return;
    if (y < 0 || y >= ctx->height) return;

    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (x0 < 0) x0 = 0;
    if (x1 >= ctx->width) x1 = ctx->width - 1;

    for (int x = x0; x <= x1; x++) {
        if (x >= ctx->clip_x && x < ctx->clip_x + ctx->clip_w &&
            y >= ctx->clip_y && y < ctx->clip_y + ctx->clip_h) {
            ctx->framebuffer[y * ctx->width + x] = color;
        }
    }
}

void prim_draw_vline(fr_context_t *ctx, int x, int y0, int y1,
                     uint32_t color)
{
    if (!ctx || !ctx->framebuffer) return;
    if (x < 0 || x >= ctx->width) return;

    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (y0 < 0) y0 = 0;
    if (y1 >= ctx->height) y1 = ctx->height - 1;

    for (int y = y0; y <= y1; y++) {
        if (x >= ctx->clip_x && x < ctx->clip_x + ctx->clip_w &&
            y >= ctx->clip_y && y < ctx->clip_y + ctx->clip_h) {
            ctx->framebuffer[y * ctx->width + x] = color;
        }
    }
}

/* ================================================================
 *  Bresenham 直线算法 (经典整数实现)
 *
 *  核心思想: 用决策参数 p 判断下一个像素位置
 *  |m| <= 1 时: 每步 x+1, 根据 p 决定 y 是否 +1
 *  |m| > 1  时: 交换角色, 每步 y+1
 * ================================================================ */

void prim_draw_line_bresenham(fr_context_t *ctx,
                              int x0, int y0, int x1, int y1,
                              uint32_t color)
{
    if (!ctx || !ctx->framebuffer) return;

    int dx = x1 - x0;
    int dy = y1 - y0;
    int sx = dx >= 0 ? 1 : -1;
    int sy = dy >= 0 ? 1 : -1;
    dx = dx > 0 ? dx : -dx;
    dy = dy > 0 ? dy : -dy;

    /* |m| <= 1: 以 x 为驱动轴 */
    if (dx >= dy) {
        int err = 2 * dy - dx;  /* 初始决策参数 */
        int y = y0;
        for (int x = x0; sx > 0 ? x <= x1 : x >= x1; x += sx) {
            prim_put_pixel(ctx, x, y, color);
            if (err > 0) {
                y += sy;
                err -= 2 * dx;
            }
            err += 2 * dy;
        }
    } else {
        /* |m| > 1: 以 y 为驱动轴 */
        int err = 2 * dx - dy;
        int x = x0;
        for (int y = y0; sy > 0 ? y <= y1 : y >= y1; y += sy) {
            prim_put_pixel(ctx, x, y, color);
            if (err > 0) {
                x += sx;
                err -= 2 * dy;
            }
            err += 2 * dx;
        }
    }
}

/* ================================================================
 *  Xiaolin Wu 反走样直线
 *
 *  对每个像素计算其到理想直线的距离, 用距离决定亮度
 *  距离越近 -> alpha越高
 * ================================================================ */

/* 内部辅助: 将颜色与背景混合 (简化 alpha blend) */
static uint32_t prim_blend_color(uint32_t fg, float alpha)
{
    if (alpha <= 0.0f) return 0xFF000000;  /* 完全透明(不绘制) */
    if (alpha >= 1.0f) return fg;

    uint8_t r = (uint8_t)((fg >> 16) & 0xFF);
    uint8_t g = (uint8_t)((fg >> 8) & 0xFF);
    uint8_t b = (uint8_t)(fg & 0xFF);

    r = (uint8_t)(r * alpha);
    g = (uint8_t)(g * alpha);
    b = (uint8_t)(b * alpha);

    return ((uint32_t)0xFF << 24) | ((uint32_t)r << 16) |
           ((uint32_t)g << 8) | (uint32_t)b;
}

static void prim_plot_aa(fr_context_t *ctx, float x, float y,
                          uint32_t color, float brightness)
{
    uint32_t blended = prim_blend_color(color, brightness);
    prim_put_pixel(ctx, (int)x, (int)y, blended);
}

void prim_draw_line_aa(fr_context_t *ctx,
                       float x0, float y0, float x1, float y1,
                       uint32_t color)
{
    if (!ctx || !ctx->framebuffer) return;

    int steep = fr_fabsf(y1 - y0) > fr_fabsf(x1 - x0);

    if (steep) {
        float tmp = x0; x0 = y0; y0 = tmp;
        tmp = x1;   x1 = y1;   y1 = tmp;
    }

    if (x0 > x1) {
        float tmp = x0; x0 = x1; x1 = tmp;
        tmp = y0;   y0 = y1; y1 = tmp;
    }

    float dx = x1 - x0;
    float dy = y1 - y0;
    float gradient = (dx == 0.0f) ? 1.0f : dy / dx;

    /* 第一个端点 */
    float xend = (float)(int)(x0 + 0.5f);
    float yend = y0 + gradient * (xend - x0);
    float xgap = 1.0f - ((x0 + 0.5f) - (float)(int)(x0 + 0.5f));
    int xpxl1 = (int)xend;
    int ypxl1 = (int)yend;

    if (steep) {
        prim_plot_aa(ctx, (float)ypxl1, (float)xpxl1, color,
                     (1.0f - fr_fabsf(yend - (float)ypxl1)) * xgap);
        prim_plot_aa(ctx, (float)(ypxl1 + 1), (float)xpxl1, color,
                     fr_fabsf(yend - (float)ypxl1) * xgap);
    } else {
        prim_plot_aa(ctx, (float)xpxl1, (float)ypxl1, color,
                     (1.0f - fr_fabsf(yend - (float)ypxl1)) * xgap);
        prim_plot_aa(ctx, (float)xpxl1, (float)(ypxl1 + 1), color,
                     fr_fabsf(yend - (float)ypxl1) * xgap);
    }

    float intery = yend + gradient;

    /* 第二个端点 */
    xend = (float)(int)(x1 + 0.5f);
    yend = y1 + gradient * (xend - x1);
    xgap = (x1 + 0.5f) - (float)(int)(x1 + 0.5f);
    int xpxl2 = (int)xend;
    int ypxl2 = (int)yend;

    if (steep) {
        prim_plot_aa(ctx, (float)ypxl2, (float)xpxl2, color,
                     (1.0f - fr_fabsf(yend - (float)ypxl2)) * xgap);
        prim_plot_aa(ctx, (float)(ypxl2 + 1), (float)xpxl2, color,
                     fr_fabsf(yend - (float)ypxl2) * xgap);
    } else {
        prim_plot_aa(ctx, (float)xpxl2, (float)ypxl2, color,
                     (1.0f - fr_fabsf(yend - (float)ypxl2)) * xgap);
        prim_plot_aa(ctx, (float)xpxl2, (float)(ypxl2 + 1), color,
                     fr_fabsf(yend - (float)ypxl2) * xgap);
    }

    /* 主循环 */
    if (steep) {
        for (int x = xpxl1 + 1; x < xpxl2; x++) {
            prim_plot_aa(ctx, (float)(int)intery, (float)x, color,
                         1.0f - fr_fabsf(intery - (float)(int)intery));
            prim_plot_aa(ctx, (float)((int)intery + 1), (float)x, color,
                         fr_fabsf(intery - (float)(int)intery));
            intery += gradient;
        }
    } else {
        for (int x = xpxl1 + 1; x < xpxl2; x++) {
            prim_plot_aa(ctx, (float)x, (float)(int)intery, color,
                         1.0f - fr_fabsf(intery - (float)(int)intery));
            prim_plot_aa(ctx, (float)x, (float)((int)intery + 1), color,
                         fr_fabsf(intery - (float)(int)intery));
            intery += gradient;
        }
    }
}

/* ================================================================
 *  三角形填充 (扫描线算法)
 * ================================================================ */

/* 内部辅助: 扫描线三角形边函数插值 */
typedef struct {
    int x_min, x_max;  /* 当前扫描线的左右边界 */
} scan_edge_t;

void prim_fill_triangle(fr_context_t *ctx,
                        int x0, int y0, int x1, int y1,
                        int x2, int y2, uint32_t color)
{
    if (!ctx || !ctx->framebuffer) return;

    /* 按 Y 坐标排序顶点: v0.y <= v1.y <= v2.y */
    struct { int x, y; } v[3];
    v[0].x = x0; v[0].y = y0;
    v[1].x = x1; v[1].y = y1;
    v[2].x = x2; v[2].y = y2;

    for (int i = 0; i < 3; i++)
        for (int j = i + 1; j < 3; j++)
            if (v[j].y < v[i].y) {
                int tx = v[i].x, ty = v[i].y;
                v[i].x = v[j].x; v[i].y = v[j].y;
                v[j].x = tx; v[j].y = ty;
            }

    int total_height = v[2].y - v[0].y;
    if (total_height == 0) return;  /* 退化三角形 */

    /* 上半部分 (v0 -> v1) 和 下半部分 (v1 -> v2) */
    for (int y = v[0].y; y < v[2].y; y++) {
        int second_half = (y >= v[1].y) || (v[1].y == v[0].y);
        int target_y = second_half ? v[2].y : v[1].y;
        int seg_height = target_y - (second_half ? v[1].y : v[0].y);
        if (seg_height == 0) continue;

        /* 归一化进度 [0, 1] */
        float alpha = (float)(y - v[0].y) / (float)total_height;
        float beta  = (float)(y - (second_half ? v[1].y : v[0].y)) /
                      (float)seg_height;

        /* A 点: 从 v0 到 v2 的长边插值 */
        int ax = (int)(v[0].x + (v[2].x - v[0].x) * alpha);

        /* B 点: 在短边上插值 */
        int bx;
        if (second_half)
            bx = (int)(v[1].x + (v[2].x - v[1].x) * beta);
        else
            bx = (int)(v[0].x + (v[1].x - v[0].x) * beta);

        if (ax > bx) { int t = ax; ax = bx; bx = t; }

        /* 填充当前扫描线 */
        prim_draw_hline(ctx, ax, bx, y, color);
    }
}

/* ================================================================
 *  Gouraud 着色三角形 (顶点颜色扫描线插值)
 * ================================================================ */

/* 颜色分量线性插值 */
static uint32_t prim_lerp_color(uint32_t c0, uint32_t c1, float t)
{
    uint8_t r0 = (c0 >> 16) & 0xFF, g0 = (c0 >> 8) & 0xFF, b0 = c0 & 0xFF;
    uint8_t r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;

    uint8_t r = (uint8_t)(r0 + (r1 - r0) * t);
    uint8_t g = (uint8_t)(g0 + (g1 - g0) * t);
    uint8_t b = (uint8_t)(b0 + (b1 - b0) * t);

    return ((uint32_t)0xFF << 24) | ((uint32_t)r << 16) |
           ((uint32_t)g << 8) | (uint32_t)b;
}

void prim_fill_triangle_flat(fr_context_t *ctx,
                             int x0, int y0, uint32_t c0,
                             int x1, int y1, uint32_t c1,
                             int x2, int y2, uint32_t c2)
{
    if (!ctx || !ctx->framebuffer) return;

    /* 按 Y 排序 */
    struct { int x, y; } v[3];
    uint32_t vc[3];
    v[0].x = x0; v[0].y = y0; vc[0] = c0;
    v[1].x = x1; v[1].y = y1; vc[1] = c1;
    v[2].x = x2; v[2].y = y2; vc[2] = c2;

    for (int i = 0; i < 3; i++)
        for (int j = i + 1; j < 3; j++)
            if (v[j].y < v[i].y) {
                int tx = v[i].x, ty = v[i].y;
                uint32_t tc = vc[i];
                v[i].x = v[j].x; v[i].y = v[j].y; vc[i] = vc[j];
                v[j].x = tx; v[j].y = ty; vc[j] = tc;
            }

    int total_height = v[2].y - v[0].y;
    if (total_height == 0) return;

    for (int y = v[0].y; y < v[2].y; y++) {
        int second_half = (y >= v[1].y) || (v[1].y == v[0].y);
        int target_y = second_half ? v[2].y : v[1].y;
        int seg_height = target_y - (second_half ? v[1].y : v[0].y);
        if (seg_height == 0) continue;

        float alpha = (float)(y - v[0].y) / (float)total_height;
        float beta  = (float)(y - (second_half ? v[1].y : v[0].y)) /
                      (float)seg_height;

        /* 边界 X 插值 */
        int ax = (int)(v[0].x + (v[2].x - v[0].x) * alpha);
        int bx;
        if (second_half)
            bx = (int)(v[1].x + (v[2].x - v[1].x) * beta);
        else
            bx = (int)(v[0].x + (v[1].x - v[0].x) * beta);

        /* 边界颜色插值 */
        uint32_t ca = prim_lerp_color(vc[0], vc[2], alpha);
        uint32_t cb;
        if (second_half)
            cb = prim_lerp_color(vc[1], vc[2], beta);
        else
            cb = prim_lerp_color(vc[0], vc[1], beta);

        if (ax > bx) {
            int tx = ax; ax = bx; bx = tx;
            uint32_t tc = ca; ca = cb; cb = tc;
        }

        /* 逐像素颜色插值并绘制 */
        int span = bx - ax;
        if (span <= 0) continue;
        for (int px = ax; px <= bx; px++) {
            float st = (span > 0) ? (float)(px - ax) / (float)span : 0.0f;
            uint32_t pixel_color = prim_lerp_color(ca, cb, st);
            prim_put_pixel(ctx, px, y, pixel_color);
        }
    }
}

/* ================================================================
 *  中点椭圆算法 (Midpoint Ellipse Algorithm)
 *  分为两个区域:
 *   Region 1: 斜率 |m| < 1, 以 x 为步进
 *   Region 2: 斜率 |m| >= 1, 以 y 为步进
 * ================================================================ */

void prim_draw_ellipse(fr_context_t *ctx,
                        int cx, int cy, int rx, int ry,
                        uint32_t color)
{
    if (!ctx || !ctx->framebuffer) return;
    if (rx <= 0 || ry <= 0) return;

    int rx2 = rx * rx;
    int ry2 = ry * ry;
    int two_rx2 = 2 * rx2;
    int two_ry2 = 2 * ry2;

    int x = 0;
    int y = ry;
    int px = 0;
    int py = two_rx2 * y;

    /* Region 1: |m| < 1 */
    int p1 = ry2 - (rx2 * ry) + (rx2 >> 2);  /* rx2/4 四舍五入近似 */
    while (px * ry2 < py * rx2) {
        /* 4路对称绘制 */
        prim_put_pixel(ctx, cx + x, cy + y, color);
        prim_put_pixel(ctx, cx - x, cy + y, color);
        prim_put_pixel(ctx, cx + x, cy - y, color);
        prim_put_pixel(ctx, cx - x, cy - y, color);

        x++;
        px += two_ry2;
        if (p1 < 0) {
            p1 += ry2 + px;
        } else {
            y--;
            py -= two_rx2;
            p1 += ry2 + px - py;
        }
    }

    /* Region 2: |m| >= 1 */
    int p2 = (ry2)*((2 * x + 1) * (2 * x + 1))
             + (rx2)*(ry2 - 2 * y + 1) * (ry2 - 2 * y + 1)
             - (long)rx2 * ry2 * rx2 * ry2 / ((long)rx2 * rx2 + (long)ry2 * ry2);

    /* 更精确的 Region 2 初始决策参数 */
    p2 = ry2 * (2*x + 1)*(2*x + 1) + rx2 * (2*y - 1)*(2*y - 1) - 2L*rx2*rx2*ry2;

    while (y >= 0) {
        prim_put_pixel(ctx, cx + x, cy + y, color);
        prim_put_pixel(ctx, cx - x, cy + y, color);
        prim_put_pixel(ctx, cx + x, cy - y, color);
        prim_put_pixel(ctx, cx - x, cy - y, color);

        y--;
        py -= two_rx2;
        if (p2 > 0) {
            p2 -= rx2 + py;
        } else {
            x++;
            px += two_ry2;
            p2 += rx2 + py + px;
        }
    }
}

void prim_fill_ellipse(fr_context_t *ctx,
                        int cx, int cy, int rx, int ry,
                        uint32_t color)
{
    if (!ctx || !ctx->framebuffer) return;
    if (rx <= 0 || ry <= 0) return;

    float rx_f = (float)rx;
    float ry_f = (float)ry;
    float rx2 = rx_f * rx_f;
    float ry2 = ry_f * ry_f;

    for (int py = -ry; py <= ry; py++) {
        int hline_start = -1, hline_end = -1;
        for (int px = -rx; px <= rx; px++) {
            float dx = (float)px;
            float dy = (float)py;
            if ((dx * dx) / rx2 + (dy * dy) / ry2 <= 1.0f) {
                if (hline_start < 0) hline_start = px;
                hline_end = px;
            } else if (hline_start >= 0) {
                break;
            }
        }
        if (hline_start >= 0) {
            prim_draw_hline(ctx, cx + hline_start, cx + hline_end,
                            cy + py, color);
        }
    }
}

/* ================================================================
 *  圆弧绘制 (基于中点圆算法 + 角度裁剪)
 * ================================================================ */

void prim_draw_arc(fr_context_t *ctx,
                    int cx, int cy, int r,
                    float start_angle, float end_angle,
                    uint32_t color)
{
    if (!ctx || !ctx->framebuffer) return;
    if (r <= 0) return;

    /* 归一化角度范围 */
    while (start_angle < 0.0f) start_angle += 2.0f * M_PI_F;
    while (end_angle < start_angle) end_angle += 2.0f * M_PI_F;

    /* 中点圆算法遍历所有八分圆像素 */
    int x = 0;
    int y = r;
    int d = 1 - r;

    while (x <= y) {
        /* 8个对称点, 检查每个点的角度是否在范围内 */
        int pts[][2] = {
            { x,  y}, {-x,  y}, { x, -y}, {-x, -y},
            { y,  x}, {-y,  x}, { y, -x}, {-y, -x}
        };

        for (int i = 0; i < 8; i++) {
            int px = cx + pts[i][0];
            int py = cy + pts[i][1];

            /* 计算该点的角度 */
            float angle = fr_atan2((float)pts[i][1], (float)pts[i][0]);
            if (angle < 0.0f) angle += 2.0f * M_PI_F;

            /* 检查角度是否在范围内 */
            int in_range = 0;
            if (end_angle >= start_angle + 2.0f * M_PI_F) {
                in_range = 1;  /* 全圆 */
            } else {
                float a = angle;
                if (a < start_angle) a += 2.0f * M_PI_F;
                in_range = (a >= start_angle && a <= end_angle);
            }

            if (in_range)
                prim_put_pixel(ctx, px, py, color);
        }

        x++;
        if (d < 0) {
            d += 2 * x + 1;
        } else {
            y--;
            d += 2 * (x - y) + 1;
        }
    }
}

/* ================================================================
 *  二次贝塞尔曲线 (de Casteljau 细分 + 逐段 Bresenham)
 *
 *  P(t) = (1-t)^2*P0 + 2*(1-t)*t*P1 + t^2*P2
 *  使用递归细分直到曲线足够平坦, 再用直线连接
 * ================================================================ */

#define PRIM_BEZIER_FLATNESS  0.5f  /* 平坦度阈值 */
#define PRIM_BEZIER_MAX_DEPTH 10     /* 最大递归深度 */

static void prim_bezier_subdivide(fr_context_t *ctx,
                                   float x0, float y0,
                                   float x1, float y1,
                                   float x2, float y2,
                                   uint32_t color, int depth)
{
    /* 计算控制点到弦的距离作为平坦度度量 */
    float mid_x = (x0 + x2) * 0.5f;
    float mid_y = (y0 + y2) * 0.5f;
    float dist = fr_fabsf(x1 - mid_x) + fr_fabsf(y1 - mid_y);

    if (dist <= PRIM_BEZIER_FLATNESS || depth >= PRIM_BEZIER_MAX_DEPTH) {
        /* 足够平坦, 直接画直线 */
        prim_draw_line_bresenham(ctx,
                                 (int)x0, (int)y0,
                                 (int)x2, (int)y2, color);
        return;
    }

    /* de Casteljau 细分: t=0.5 处分割成两段子曲线 */
    float x01 = (x0 + x1) * 0.5f;
    float y01 = (y0 + y1) * 0.5f;
    float x12 = (x1 + x2) * 0.5f;
    float y12 = (y1 + y2) * 0.5f;
    float xm  = (x01 + x12) * 0.5f;
    float ym  = (y01 + y12) * 0.5f;

    /* 左半部分: P0 -> P01 -> Pm */
    prim_bezier_subdivide(ctx, x0, y0, x01, y01, xm, ym, color, depth + 1);
    /* 右半部分: Pm -> P12 -> P2 */
    prim_bezier_subdivide(ctx, xm, ym, x12, y12, x2, y2, color, depth + 1);
}

void prim_draw_bezier_quad(fr_context_t *ctx,
                            float x0, float y0,
                            float x1, float y1,
                            float x2, float y2,
                            uint32_t color)
{
    if (!ctx || !ctx->framebuffer) return;
    prim_bezier_subdivide(ctx, x0, y0, x1, y1, x2, y2, color, 0);
}
