/* effect.c - 视觉特效引擎实现
 * 实现阴影、模糊、渐变、Alpha混合、圆角矩形、透明效果、
 * 发光效果、颜色叠加和透明度控制
 */

#include "funrender.h"
#include "fr_context.h"
#include "fr_effect.h"
#include "string.h"

/* ---- 内部辅助函数 ---- */

/* 限制值到范围 */
static int clamp_int(int val, int min, int max)
{
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

/* 限制浮点数到范围 */
static float clamp_float(float val, float min, float max)
{
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

/* 在两点间线性插值 */
static float lerp_float(float a, float b, float t)
{
    return a + (b - a) * t;
}

/* 在两点间线性插值(uint8_t) */
static uint8_t lerp_uint8(uint8_t a, uint8_t b, float t)
{
    return (uint8_t)((float)a + (float)(b - a) * t);
}

/* 复制像素矩形到临时缓冲区 */
static uint32_t *copy_rect_to_buffer(fr_context_t *ctx,
                                      int x, int y, int w, int h)
{
    if (ctx == NULL || ctx->framebuffer == NULL) return NULL;
    if (w <= 0 || h <= 0) return NULL;

    uint32_t *buf = (uint32_t *)fr_alloc((uint32_t)(w * h * 4));
    if (buf == NULL) return NULL;

    for (int py = 0; py < h; py++) {
        int sy = y + py;
        if (sy < 0 || sy >= ctx->height) {
            /* 超出屏幕范围的行填 0 */
            memset(&buf[py * w], 0, (size_t)(w * 4));
            continue;
        }
        for (int px = 0; px < w; px++) {
            int sx = x + px;
            if (sx >= 0 && sx < ctx->width) {
                buf[py * w + px] = ctx->framebuffer[sy * ctx->width + sx];
            } else {
                buf[py * w + px] = 0;
            }
        }
    }
    return buf;
}

/* ================================================================
 *  阴影效果
 * ================================================================ */

/*
 * fr_effect_drop_shadow - 绘制投影
 *
 * 在当前帧缓冲上进行绘制: 先根据阴影配置生成一个偏移的模糊阴影区域,
 * 然后在对应位置进行 Alpha 混合。
 *
 * 算法步骤:
 *   1. 提取并扩展需要绘制阴影的区域(包含偏移和模糊扩展)
 *   2. 创建一个临时缓冲区, 将阴影颜色填充到对应的形状中
 *   3. 对临时缓冲区执行高斯模糊
 *   4. 将模糊后的阴影混合回帧缓冲
 */
void fr_effect_drop_shadow(fr_context_t *ctx,
                           int x, int y, int w, int h,
                           const fr_drop_shadow_t *shadow)
{
    if (ctx == NULL || ctx->framebuffer == NULL) return;
    if (shadow == NULL) return;
    if (shadow->opacity == 0) return;

    int blur_r = shadow->blur_radius;
    int spread = shadow->spread;

    /* 计算阴影包围盒 */
    int sx = x + shadow->offset_x - blur_r - spread;
    int sy = y + shadow->offset_y - blur_r - spread;
    int sw = w + 2 * (blur_r + spread);
    int sh = h + 2 * (blur_r + spread);

    /* 裁剪到屏幕 */
    if (sx < 0) { sw += sx; sx = 0; }
    if (sy < 0) { sh += sy; sy = 0; }
    if (sx + sw > ctx->width)  sw = ctx->width - sx;
    if (sy + sh > ctx->height) sh = ctx->height - sy;
    if (sw <= 0 || sh <= 0) return;

    /* 在阴影包围盒内, 填充阴影颜色到"目标区域" */
    /* 使用简化方法: 直接在帧缓冲上绘制模糊效果 */
    int ofx = shadow->offset_x;
    int ofy = shadow->offset_y;

    /* 创建一个临时缓冲区用于生成阴影 */
    uint32_t *shadow_buf = (uint32_t *)fr_alloc((uint32_t)(sw * sh * 4));
    if (shadow_buf == NULL) return;

    /* 填充阴影形状到临时缓冲区 */
    int shape_x = blur_r + spread;
    int shape_y = blur_r + spread;
    uint32_t shadow_color = ((uint32_t)shadow->color.r << 16) |
                             ((uint32_t)shadow->color.g << 8) |
                             (uint32_t)shadow->color.b;

    for (int py = 0; py < sh; py++) {
        for (int px = 0; px < sw; px++) {
            /* 检查是否在形状区域内 */
            if (px >= shape_x && px < shape_x + w &&
                py >= shape_y && py < shape_y + h) {
                shadow_buf[py * sw + px] = shadow_color;
            } else {
                shadow_buf[py * sw + px] = 0;
            }
        }
    }

    /* 对阴影缓冲区执行模糊 */
    fr_effect_blur_buffer(shadow_buf, sw, sh, 0, 0, sw, sh, blur_r);

    /* 将阴影混合回帧缓冲 */
    if (shadow->opacity == 255) {
        /* 完全覆盖模式 - 直接复制非零像素, 使用 Alpha 混合 */
        for (int py = 0; py < sh; py++) {
            for (int px = 0; px < sw; px++) {
                uint32_t sp = shadow_buf[py * sw + px];
                if (sp == 0) continue;

                int tx = sx + px;
                int ty = sy + py;
                if (tx < 0 || tx >= ctx->width ||
                    ty < 0 || ty >= ctx->height) continue;

                uint32_t dst_pixel = ctx->framebuffer[ty * ctx->width + tx];
                /* 阴影像素本身已经有模糊衰减, 直接进行 Alpha 混合 */
                uint8_t sb = sp & 0xFF;
                uint8_t sg = (sp >> 8) & 0xFF;
                uint8_t sr = (sp >> 16) & 0xFF;

                uint8_t db = dst_pixel & 0xFF;
                uint8_t dg = (dst_pixel >> 8) & 0xFF;
                uint8_t dr = (dst_pixel >> 16) & 0xFF;

                /* 计算源像素的有效 Alpha */
                uint8_t sa = (uint8_t)(((uint16_t)sr + (uint16_t)sg + (uint16_t)sb) / 3);
                sa = (uint8_t)((uint16_t)sa * shadow->opacity / 255);

                uint8_t inv_a = 255 - sa;
                uint8_t rr = (uint8_t)(((uint16_t)sr * sa + (uint16_t)dr * inv_a) / 255);
                uint8_t rg = (uint8_t)(((uint16_t)sg * sa + (uint16_t)dg * inv_a) / 255);
                uint8_t rb = (uint8_t)(((uint16_t)sb * sa + (uint16_t)db * inv_a) / 255);

                ctx->framebuffer[ty * ctx->width + tx] =
                    ((uint32_t)rr << 16) | ((uint32_t)rg << 8) | rb;
            }
        }
    } else {
        /* 带整体不透明度的混合 */
        for (int py = 0; py < sh; py++) {
            for (int px = 0; px < sw; px++) {
                uint32_t sp = shadow_buf[py * sw + px];
                if (sp == 0) continue;

                int tx = sx + px;
                int ty = sy + py;
                if (tx < 0 || tx >= ctx->width ||
                    ty < 0 || ty >= ctx->height) continue;

                uint32_t dst_pixel = ctx->framebuffer[ty * ctx->width + tx];
                uint8_t sb = sp & 0xFF;
                uint8_t sg = (sp >> 8) & 0xFF;
                uint8_t sr = (sp >> 16) & 0xFF;

                uint8_t db = dst_pixel & 0xFF;
                uint8_t dg = (dst_pixel >> 8) & 0xFF;
                uint8_t dr = (dst_pixel >> 16) & 0xFF;

                /* 基于亮度计算 Alpha */
                uint8_t sa = (uint8_t)(((uint16_t)sr + (uint16_t)sg + (uint16_t)sb) / 3);
                sa = (uint8_t)((uint16_t)sa * shadow->opacity / 255);

                uint8_t inv_a = 255 - sa;
                uint8_t rr = (uint8_t)(((uint16_t)sr * sa + (uint16_t)dr * inv_a) / 255);
                uint8_t rg = (uint8_t)(((uint16_t)sg * sa + (uint16_t)dg * inv_a) / 255);
                uint8_t rb = (uint8_t)(((uint16_t)sb * sa + (uint16_t)db * inv_a) / 255);

                ctx->framebuffer[ty * ctx->width + tx] =
                    ((uint32_t)rr << 16) | ((uint32_t)rg << 8) | rb;
            }
        }
    }

    fr_free(shadow_buf);
}

/*
 * fr_effect_drop_shadow_masked - 在带遮罩的区域上绘制阴影
 *
 * 与 fr_effect_drop_shadow 类似, 但使用一个 Alpha 遮罩来决定阴影的形状。
 * 遮罩中非零区域作为阴影的"发射区域"。
 */
void fr_effect_drop_shadow_masked(fr_context_t *ctx,
                                  int x, int y, int w, int h,
                                  const uint8_t *alpha_mask,
                                  const fr_drop_shadow_t *shadow)
{
    if (ctx == NULL || ctx->framebuffer == NULL) return;
    if (shadow == NULL || alpha_mask == NULL) return;
    if (shadow->opacity == 0) return;

    int blur_r = shadow->blur_radius;
    int spread = shadow->spread;
    int ofx = shadow->offset_x;
    int ofy = shadow->offset_y;

    int sx = x + ofx - blur_r - spread;
    int sy = y + ofy - blur_r - spread;
    int sw = w + 2 * (blur_r + spread);
    int sh = h + 2 * (blur_r + spread);

    if (sx < 0) { sw += sx; sx = 0; }
    if (sy < 0) { sh += sy; sy = 0; }
    if (sx + sw > ctx->width)  sw = ctx->width - sx;
    if (sy + sh > ctx->height) sh = ctx->height - sy;
    if (sw <= 0 || sh <= 0) return;

    uint32_t *shadow_buf = (uint32_t *)fr_alloc((uint32_t)(sw * sh * 4));
    if (shadow_buf == NULL) return;

    uint32_t shadow_color = ((uint32_t)shadow->color.r << 16) |
                             ((uint32_t)shadow->color.g << 8) |
                             (uint32_t)shadow->color.b;

    int shape_x = blur_r + spread;
    int shape_y = blur_r + spread;

    /* 使用遮罩填充阴影形状 */
    for (int py = 0; py < sh; py++) {
        for (int px = 0; px < sw; px++) {
            if (px < shape_x || px >= shape_x + w ||
                py < shape_y || py >= shape_y + h) {
                shadow_buf[py * sw + px] = 0;
                continue;
            }

            int mask_idx = (py - shape_y) * w + (px - shape_x);
            uint8_t mask_alpha = alpha_mask[mask_idx];

            if (mask_alpha > 0) {
                /* 按遮罩 Alpha 缩放阴影颜色 */
                uint8_t r = (uint8_t)((uint16_t)shadow->color.r * mask_alpha / 255);
                uint8_t g = (uint8_t)((uint16_t)shadow->color.g * mask_alpha / 255);
                uint8_t b = (uint8_t)((uint16_t)shadow->color.b * mask_alpha / 255);
                shadow_buf[py * sw + px] =
                    ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            } else {
                shadow_buf[py * sw + px] = 0;
            }
        }
    }

    /* 模糊 */
    fr_effect_blur_buffer(shadow_buf, sw, sh, 0, 0, sw, sh, blur_r);

    /* 混合回帧缓冲 */
    for (int py = 0; py < sh; py++) {
        for (int px = 0; px < sw; px++) {
            uint32_t sp = shadow_buf[py * sw + px];
            if (sp == 0) continue;

            int tx = sx + px;
            int ty = sy + py;
            if (tx < 0 || tx >= ctx->width ||
                ty < 0 || ty >= ctx->height) continue;

            uint32_t dst_pixel = ctx->framebuffer[ty * ctx->width + tx];
            uint8_t sb = sp & 0xFF;
            uint8_t sg = (sp >> 8) & 0xFF;
            uint8_t sr = (sp >> 16) & 0xFF;
            uint8_t db = dst_pixel & 0xFF;
            uint8_t dg = (dst_pixel >> 8) & 0xFF;
            uint8_t dr = (dst_pixel >> 16) & 0xFF;

            uint8_t sa = (uint8_t)(((uint16_t)sr + (uint16_t)sg + (uint16_t)sb) / 3);
            sa = (uint8_t)((uint16_t)sa * shadow->opacity / 255);

            uint8_t inv_a = 255 - sa;
            uint8_t rr = (uint8_t)(((uint16_t)sr * sa + (uint16_t)dr * inv_a) / 255);
            uint8_t rg = (uint8_t)(((uint16_t)sg * sa + (uint16_t)dg * inv_a) / 255);
            uint8_t rb = (uint8_t)(((uint16_t)sb * sa + (uint16_t)db * inv_a) / 255);

            ctx->framebuffer[ty * ctx->width + tx] =
                ((uint32_t)rr << 16) | ((uint32_t)rg << 8) | rb;
        }
    }

    fr_free(shadow_buf);
}

/* ================================================================
 *  高斯模糊
 * ================================================================ */

/*
 * 预计算的高斯模糊核
 * 使用 2σ^2 = radius^2 的近似高斯分布
 */

/* 3x3 核 */
static const int gauss_kernel_3[9] = {1, 2, 1, 2, 4, 2, 1, 2, 1};
static const int gauss_kernel_3_sum = 16;

/* 5x5 核 */
static const int gauss_kernel_5[25] = {
    1,  4,  7,  4, 1,
    4, 16, 26, 16, 4,
    7, 26, 41, 26, 7,
    4, 16, 26, 16, 4,
    1,  4,  7,  4, 1
};
static const int gauss_kernel_5_sum = 273;

/* 7x7 核 */
static const int gauss_kernel_7[49] = {
     1,  4,  7, 10,  7,  4,  1,
     4, 12, 26, 33, 26, 12,  4,
     7, 26, 55, 71, 55, 26,  7,
    10, 33, 71, 91, 71, 33, 10,
     7, 26, 55, 71, 55, 26,  7,
     4, 12, 26, 33, 26, 12,  4,
     1,  4,  7, 10,  7,  4,  1
};
static const int gauss_kernel_7_sum = 1003;

/* 单遍水平模糊 */
static void blur_horizontal(uint32_t *buf, int buf_w, int buf_h,
                            int x0, int y0, int w, int h,
                            const int *kernel, int ksize, int ksum)
{
    int half = ksize / 2;

    /* 需要读写分离, 在此使用临时行缓冲 */
    uint32_t *row_temp = (uint32_t *)fr_alloc((uint32_t)(w * 4));
    if (row_temp == NULL) return;

    for (int py = y0; py < y0 + h && py < buf_h; py++) {
        for (int px_off = 0; px_off < w; px_off++) {
            int px = x0 + px_off;
            int sum_r = 0, sum_g = 0, sum_b = 0;

            for (int kx = 0; kx < ksize; kx++) {
                int sx = px + kx - half;
                if (sx < 0) sx = 0;
                if (sx >= buf_w) sx = buf_w - 1;
                uint32_t p = buf[py * buf_w + sx];
                int weight = kernel[kx];
                sum_r += (int)((p >> 16) & 0xFF) * weight;
                sum_g += (int)((p >> 8) & 0xFF) * weight;
                sum_b += (int)(p & 0xFF) * weight;
            }

            row_temp[px_off] =
                ((uint32_t)clamp_int(sum_r / ksum, 0, 255) << 16) |
                ((uint32_t)clamp_int(sum_g / ksum, 0, 255) << 8) |
                (uint32_t)clamp_int(sum_b / ksum, 0, 255);
        }

        /* 回写 */
        for (int px_off = 0; px_off < w; px_off++) {
            int px = x0 + px_off;
            buf[py * buf_w + px] = row_temp[px_off];
        }
    }

    fr_free(row_temp);
}

/* 单遍垂直模糊 */
static void blur_vertical(uint32_t *buf, int buf_w, int buf_h,
                          int x0, int y0, int w, int h,
                          const int *kernel, int ksize, int ksum)
{
    int half = ksize / 2;

    uint32_t *col_temp = (uint32_t *)fr_alloc((uint32_t)(h * 4));
    if (col_temp == NULL) return;

    for (int px_off = 0; px_off < w; px_off++) {
        int px = x0 + px_off;

        for (int py_off = 0; py_off < h; py_off++) {
            int py = y0 + py_off;
            int sum_r = 0, sum_g = 0, sum_b = 0;

            for (int ky = 0; ky < ksize; ky++) {
                int sy = py + ky - half;
                if (sy < 0) sy = 0;
                if (sy >= buf_h) sy = buf_h - 1;
                uint32_t p = buf[sy * buf_w + px];
                int weight = kernel[ky * ksize];
                sum_r += (int)((p >> 16) & 0xFF) * weight;
                sum_g += (int)((p >> 8) & 0xFF) * weight;
                sum_b += (int)(p & 0xFF) * weight;
            }

            col_temp[py_off] =
                ((uint32_t)clamp_int(sum_r / ksum, 0, 255) << 16) |
                ((uint32_t)clamp_int(sum_g / ksum, 0, 255) << 8) |
                (uint32_t)clamp_int(sum_b / ksum, 0, 255);
        }

        for (int py_off = 0; py_off < h; py_off++) {
            int py = y0 + py_off;
            buf[py * buf_w + px] = col_temp[py_off];
        }
    }

    fr_free(col_temp);
}

/*
 * fr_effect_gaussian_blur - 对帧缓冲区域执行高斯模糊
 *
 * 使用分离卷积: 先水平方向再垂直方向各执行一遍, 减少计算量。
 * 支持 3x3、5x5、7x7 三种模糊核。
 */
void fr_effect_gaussian_blur(fr_context_t *ctx,
                             int x, int y, int w, int h,
                             int radius)
{
    if (ctx == NULL || ctx->framebuffer == NULL) return;
    if (w <= 0 || h <= 0) return;

    const int *kernel;
    int ksize, ksum;

    /* 选择合适的模糊核 */
    if (radius <= 1) {
        ksize = 3;
        kernel = gauss_kernel_3;
        ksum = gauss_kernel_3_sum;
    } else if (radius <= 2) {
        ksize = 5;
        kernel = gauss_kernel_5;
        ksum = gauss_kernel_5_sum;
    } else {
        ksize = 7;
        kernel = gauss_kernel_7;
        ksum = gauss_kernel_7_sum;
    }

    /* 裁剪到帧缓冲范围 */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > ctx->width)  w = ctx->width - x;
    if (y + h > ctx->height) h = ctx->height - y;
    if (w <= 0 || h <= 0) return;

    /* 将帧缓冲当作正确宽度的高缓冲处理 */
    int buf_w = ctx->width;
    int buf_h = ctx->height;

    /* 水平模糊 */
    blur_horizontal(ctx->framebuffer, buf_w, buf_h,
                    x, y, w, h, kernel, ksize, ksum);

    /* 垂直模糊 */
    blur_vertical(ctx->framebuffer, buf_w, buf_h,
                  x, y, w, h, kernel, ksize, ksum);
}

/*
 * fr_effect_blur_buffer - 对独立缓冲区执行高斯模糊
 */
void fr_effect_blur_buffer(uint32_t *buffer, int buf_w, int buf_h,
                           int x, int y, int w, int h, int radius)
{
    if (buffer == NULL) return;
    if (w <= 0 || h <= 0) return;

    const int *kernel;
    int ksize, ksum;

    if (radius <= 1) {
        ksize = 3;
        kernel = gauss_kernel_3;
        ksum = gauss_kernel_3_sum;
    } else if (radius <= 2) {
        ksize = 5;
        kernel = gauss_kernel_5;
        ksum = gauss_kernel_5_sum;
    } else {
        ksize = 7;
        kernel = gauss_kernel_7;
        ksum = gauss_kernel_7_sum;
    }

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > buf_w)  w = buf_w - x;
    if (y + h > buf_h) h = buf_h - y;
    if (w <= 0 || h <= 0) return;

    blur_horizontal(buffer, buf_w, buf_h, x, y, w, h,
                    kernel, ksize, ksum);
    blur_vertical(buffer, buf_w, buf_h, x, y, w, h,
                  kernel, ksize, ksum);
}

/* ================================================================
 *  渐变渲染器
 * ================================================================ */

/*
 * fr_effect_gradient_sample - 从渐变配置中采样颜色
 *
 * 根据位置 t (0.0-1.0) 在停止点之间进行线性插值。
 */
fr_color_t fr_effect_gradient_sample(const fr_gradient_t *gradient, float t)
{
    if (gradient == NULL || gradient->stop_count == 0) {
        return FR_COLOR_TRANSPARENT;
    }

    t = clamp_float(t, 0.0f, 1.0f);

    if (gradient->stop_count == 1) {
        return gradient->stops[0].color;
    }

    /* 查找 t 落在哪两个停止点之间 */
    /* 如果 t 小于第一个停止点 */
    if (t <= gradient->stops[0].position) {
        return gradient->stops[0].color;
    }

    /* 如果 t 大于最后一个停止点 */
    uint32_t last = gradient->stop_count - 1;
    if (t >= gradient->stops[last].position) {
        return gradient->stops[last].color;
    }

    /* 在停止点之间插值 */
    for (uint32_t i = 0; i < gradient->stop_count - 1; i++) {
        float p0 = gradient->stops[i].position;
        float p1 = gradient->stops[i + 1].position;

        if (t >= p0 && t <= p1) {
            float local_t = (p1 - p0 > 0.0001f) ?
                            (t - p0) / (p1 - p0) : 0.0f;

            fr_color_t c0 = gradient->stops[i].color;
            fr_color_t c1 = gradient->stops[i + 1].color;

            fr_color_t result;
            result.r = lerp_uint8(c0.r, c1.r, local_t);
            result.g = lerp_uint8(c0.g, c1.g, local_t);
            result.b = lerp_uint8(c0.b, c1.b, local_t);
            result.a = lerp_uint8(c0.a, c1.a, local_t);
            return result;
        }
    }

    return gradient->stops[last].color;
}

/*
 * fr_effect_render_gradient - 渲染渐变到帧缓冲
 *
 * 支持线性渐变(四个方向)和径向渐变。
 */
void fr_effect_render_gradient(fr_context_t *ctx,
                               int x, int y, int w, int h,
                               const fr_gradient_t *gradient)
{
    if (ctx == NULL || ctx->framebuffer == NULL) return;
    if (gradient == NULL || gradient->stop_count == 0) return;
    if (w <= 0 || h <= 0) return;

    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            int tx = x + px;
            int ty = y + py;

            if (tx < 0 || tx >= ctx->width ||
                ty < 0 || ty >= ctx->height) continue;

            float t;
            if (gradient->type == FR_GRADIENT_LINEAR) {
                /* 线性渐变 */
                switch (gradient->direction) {
                case FR_GRADIENT_DIR_HORIZONTAL:
                    t = (float)px / (float)(w > 1 ? w - 1 : 1);
                    break;
                case FR_GRADIENT_DIR_VERTICAL:
                    t = (float)py / (float)(h > 1 ? h - 1 : 1);
                    break;
                case FR_GRADIENT_DIR_DIAGONAL:
                    t = (float)(px + py) /
                        (float)((w + h) > 1 ? (w + h - 2) : 1);
                    break;
                case FR_GRADIENT_DIR_ANTIDIAG:
                    /* 右上到左下: t = ((w-px-1) + py) / (w+h-2) */
                    t = (float)((w - px - 1) + py) /
                        (float)((w + h - 2) > 1 ? (w + h - 2) : 1);
                    break;
                default:
                    t = (float)px / (float)(w > 1 ? w - 1 : 1);
                    break;
                }
            } else {
                /* 径向渐变 */
                int cx = gradient->cx;
                int cy = gradient->cy;
                int radius = gradient->radius;
                if (radius <= 0) radius = 1;

                int dx = px - cx;
                int dy = py - cy;
                float dist = 0.0f;

                /* 使用整数 sqrt 近似 */
                if (dx < 0) dx = -dx;
                if (dy < 0) dy = -dy;
                dist = (float)dx + (float)dy; /* 曼哈顿距离近似 */
                t = dist / (float)radius;
                if (t > 1.0f) t = 1.0f;
            }

            fr_color_t c = fr_effect_gradient_sample(gradient, t);
            uint32_t pixel = ((uint32_t)c.r << 16) |
                             ((uint32_t)c.g << 8) |
                             (uint32_t)c.b;

            if (c.a == 255) {
                ctx->framebuffer[ty * ctx->width + tx] = pixel;
            } else {
                /* Alpha 混合 */
                uint32_t bg = ctx->framebuffer[ty * ctx->width + tx];
                uint8_t bg_r = (bg >> 16) & 0xFF;
                uint8_t bg_g = (bg >> 8) & 0xFF;
                uint8_t bg_b = bg & 0xFF;
                uint8_t inv = 255 - c.a;
                uint8_t r = (uint8_t)(((uint16_t)c.r * c.a +
                                       (uint16_t)bg_r * inv) / 255);
                uint8_t g = (uint8_t)(((uint16_t)c.g * c.a +
                                       (uint16_t)bg_g * inv) / 255);
                uint8_t b = (uint8_t)(((uint16_t)c.b * c.a +
                                       (uint16_t)bg_b * inv) / 255);
                ctx->framebuffer[ty * ctx->width + tx] =
                    ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            }
        }
    }
}

/* ================================================================
 *  Alpha 混合
 * ================================================================ */

/*
 * fr_effect_blend_pixel - 混合单个像素
 *
 * 支持多种混合模式 (Porter-Duff + Photoshop 风格)。
 */
uint32_t fr_effect_blend_pixel(uint32_t src, uint32_t dst,
                                uint8_t alpha, uint32_t mode)
{
    if (alpha == 0) return dst;

    uint8_t sr = (src >> 16) & 0xFF;
    uint8_t sg = (src >> 8) & 0xFF;
    uint8_t sb = src & 0xFF;
    uint8_t dr = (dst >> 16) & 0xFF;
    uint8_t dg = (dst >> 8) & 0xFF;
    uint8_t db = dst & 0xFF;

    uint8_t rr, rg, rb;

    switch (mode) {
    case FR_BLEND_SRC_OVER:
    default: {
        uint16_t inv = 255 - alpha;
        rr = (uint8_t)(((uint16_t)sr * alpha + (uint16_t)dr * inv) / 255);
        rg = (uint8_t)(((uint16_t)sg * alpha + (uint16_t)dg * inv) / 255);
        rb = (uint8_t)(((uint16_t)sb * alpha + (uint16_t)db * inv) / 255);
        break;
    }
    case FR_BLEND_SRC_IN: {
        rr = (uint8_t)((uint16_t)sr * alpha / 255);
        rg = (uint8_t)((uint16_t)sg * alpha / 255);
        rb = (uint8_t)((uint16_t)sb * alpha / 255);
        break;
    }
    case FR_BLEND_ADDITIVE: {
        uint16_t ar = (uint16_t)sr * alpha / 255;
        uint16_t ag = (uint16_t)sg * alpha / 255;
        uint16_t ab = (uint16_t)sb * alpha / 255;
        rr = (uint8_t)(ar + dr > 255 ? 255 : ar + dr);
        rg = (uint8_t)(ag + dg > 255 ? 255 : ag + dg);
        rb = (uint8_t)(ab + db > 255 ? 255 : ab + db);
        break;
    }
    case FR_BLEND_MULTIPLY: {
        uint16_t temp_r = (uint16_t)sr * dr / 255;
        uint16_t temp_g = (uint16_t)sg * dg / 255;
        uint16_t temp_b = (uint16_t)sb * db / 255;
        uint16_t inv = 255 - alpha;
        rr = (uint8_t)((temp_r * alpha + (uint16_t)dr * inv) / 255);
        rg = (uint8_t)((temp_g * alpha + (uint16_t)dg * inv) / 255);
        rb = (uint8_t)((temp_b * alpha + (uint16_t)db * inv) / 255);
        break;
    }
    case FR_BLEND_SCREEN: {
        /* 滤色: 1 - (1-src)(1-dst) = 1 - 1 + src + dst - src*dst */
        uint16_t scr_r = (uint16_t)sr + dr -
                         (uint16_t)sr * dr / 255;
        uint16_t scr_g = (uint16_t)sg + dg -
                         (uint16_t)sg * dg / 255;
        uint16_t scr_b = (uint16_t)sb + db -
                         (uint16_t)sb * db / 255;
        uint16_t inv = 255 - alpha;
        rr = (uint8_t)((scr_r * alpha + (uint16_t)dr * inv) / 255);
        rg = (uint8_t)((scr_g * alpha + (uint16_t)dg * inv) / 255);
        rb = (uint8_t)((scr_b * alpha + (uint16_t)db * inv) / 255);
        break;
    }
    case FR_BLEND_OVERLAY: {
        /* 叠加: 混合 Multiply 和 Screen */
        uint16_t ov_r, ov_g, ov_b;

        if (dr < 128) ov_r = (uint16_t)sr * dr * 2 / 255;
        else ov_r = 510 - (uint16_t)(255 - sr) * (255 - dr) * 2 / 255;

        if (dg < 128) ov_g = (uint16_t)sg * dg * 2 / 255;
        else ov_g = 510 - (uint16_t)(255 - sg) * (255 - dg) * 2 / 255;

        if (db < 128) ov_b = (uint16_t)sb * db * 2 / 255;
        else ov_b = 510 - (uint16_t)(255 - sb) * (255 - db) * 2 / 255;

        uint16_t inv = 255 - alpha;
        rr = (uint8_t)((ov_r * alpha + (uint16_t)dr * inv) / 255);
        rg = (uint8_t)((ov_g * alpha + (uint16_t)dg * inv) / 255);
        rb = (uint8_t)((ov_b * alpha + (uint16_t)db * inv) / 255);
        break;
    }
    case FR_BLEND_COLOR_DODGE: {
        /* 颜色减淡 */
        uint16_t cd_r, cd_g, cd_b;

        if (sr == 255) cd_r = 255;
        else cd_r = (uint16_t)dr * 255 / (255 - sr > 0 ? 255 - sr : 1);
        if (cd_r > 255) cd_r = 255;

        if (sg == 255) cd_g = 255;
        else cd_g = (uint16_t)dg * 255 / (255 - sg > 0 ? 255 - sg : 1);
        if (cd_g > 255) cd_g = 255;

        if (sb == 255) cd_b = 255;
        else cd_b = (uint16_t)db * 255 / (255 - sb > 0 ? 255 - sb : 1);
        if (cd_b > 255) cd_b = 255;

        uint16_t inv = 255 - alpha;
        rr = (uint8_t)((cd_r * alpha + (uint16_t)dr * inv) / 255);
        rg = (uint8_t)((cd_g * alpha + (uint16_t)dg * inv) / 255);
        rb = (uint8_t)((cd_b * alpha + (uint16_t)db * inv) / 255);
        break;
    }
    }

    return ((uint32_t)rr << 16) | ((uint32_t)rg << 8) | rb;
}

/*
 * fr_effect_blend_buffer - 混合源缓冲区到帧缓冲
 */
void fr_effect_blend_buffer(fr_context_t *ctx,
                            int dx, int dy,
                            const uint32_t *src, int src_w, int src_h,
                            int sx, int sy, int sw, int sh,
                            uint8_t alpha, uint32_t mode)
{
    if (ctx == NULL || src == NULL || ctx->framebuffer == NULL) return;
    if (alpha == 0) return;

    for (int py = 0; py < sh; py++) {
        for (int px = 0; px < sw; px++) {
            int tx = dx + px;
            int ty = dy + py;

            if (tx < 0 || tx >= ctx->width ||
                ty < 0 || ty >= ctx->height) continue;

            int sxi = sx + px;
            int syi = sy + py;

            if (sxi < 0 || sxi >= src_w ||
                syi < 0 || syi >= src_h) continue;

            uint32_t sp = src[syi * src_w + sxi];
            uint32_t dp = ctx->framebuffer[ty * ctx->width + tx];

            ctx->framebuffer[ty * ctx->width + tx] =
                fr_effect_blend_pixel(sp, dp, alpha, mode);
        }
    }
}

/*
 * fr_effect_blend_buffer_alpha - 带逐像素 Alpha 的混合
 *
 * 每个源像素有一个独立的 Alpha 通道(来自 alpha_map),
 * 与全局 global_alpha 相乘后作为该像素的最终不透明度。
 */
void fr_effect_blend_buffer_alpha(fr_context_t *ctx,
                                  int dx, int dy,
                                  const uint32_t *src, int src_w, int src_h,
                                  const uint8_t *alpha_map,
                                  int sx, int sy, int sw, int sh,
                                  uint8_t global_alpha, uint32_t mode)
{
    if (ctx == NULL || src == NULL || ctx->framebuffer == NULL) return;
    if (global_alpha == 0) return;

    for (int py = 0; py < sh; py++) {
        for (int px = 0; px < sw; px++) {
            int tx = dx + px;
            int ty = dy + py;

            if (tx < 0 || tx >= ctx->width ||
                ty < 0 || ty >= ctx->height) continue;

            int sxi = sx + px;
            int syi = sy + py;

            if (sxi < 0 || sxi >= src_w ||
                syi < 0 || syi >= src_h) continue;

            uint8_t sa;
            if (alpha_map != NULL) {
                sa = alpha_map[syi * src_w + sxi];
            } else {
                sa = 255;
            }

            /* 合并全局和逐像素透明度 */
            uint8_t final_alpha = (uint8_t)((uint16_t)sa * global_alpha / 255);
            if (final_alpha == 0) continue;

            uint32_t sp = src[syi * src_w + sxi];
            uint32_t dp = ctx->framebuffer[ty * ctx->width + tx];

            ctx->framebuffer[ty * ctx->width + tx] =
                fr_effect_blend_pixel(sp, dp, final_alpha, mode);
        }
    }
}

/* ================================================================
 *  圆角矩形渲染 (带抗锯齿)
 * ================================================================ */

/*
 * fr_effect_rounded_rect_alpha - 计算像素对应的圆角矩形 Alpha 值
 *
 * 返回 0-255 的 Alpha 值, 表示该像素在圆角矩形内的覆盖比例。
 * 圆心区域返回 255 (完全不透明), 圆角边缘区域返回 0-255 (抗锯齿),
 * 圆角外返回 0 (完全透明)。
 */
int fr_effect_rounded_rect_alpha(int px, int py,
                                 int rx, int ry, int rw, int rh,
                                 int radius)
{
    if (radius <= 0) {
        /* 无圆角：完全在内部返回 255 */
        if (px >= rx && px < rx + rw && py >= ry && py < ry + rh)
            return 255;
        return 0;
    }

    /* 保证 radius 不超过宽度/高度的一半 */
    if (radius > rw / 2) radius = rw / 2;
    if (radius > rh / 2) radius = rh / 2;
    if (radius <= 0) {
        if (px >= rx && px < rx + rw && py >= ry && py < ry + rh)
            return 255;
        return 0;
    }

    /* 判断像素在哪个区域 */
    int inside = 1;
    int corner = 0;
    int cx = 0, cy = 0;

    if (px < rx) inside = 0;
    if (px >= rx + rw) inside = 0;
    if (py < ry) inside = 0;
    if (py >= ry + rh) inside = 0;

    if (!inside) return 0;

    /* 检查是否在圆角区域 */
    /* 左上角 */
    if (px < rx + radius && py < ry + radius) {
        corner = 1;
        cx = rx + radius;
        cy = ry + radius;
    }
    /* 右上角 */
    else if (px >= rx + rw - radius && py < ry + radius) {
        corner = 1;
        cx = rx + rw - radius;
        cy = ry + radius;
    }
    /* 左下角 */
    else if (px < rx + radius && py >= ry + rh - radius) {
        corner = 1;
        cx = rx + radius;
        cy = ry + rh - radius;
    }
    /* 右下角 */
    else if (px >= rx + rw - radius && py >= ry + rh - radius) {
        corner = 1;
        cx = rx + rw - radius;
        cy = ry + rh - radius;
    }

    if (!corner) return 255;

    /* 计算像素到圆角中心的距离, 并据此计算 Alpha */
    int dx = px - cx;
    int dy = py - cy;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;

    /* 使用整数距离近似: 对于像素级抗锯齿,
     * 使用距离的平方与半径的平方比较, 并在边缘做平滑过渡 */
    int dist_sq = dx * dx + dy * dy;
    int r_sq = radius * radius;

    if (dist_sq >= r_sq + radius) {
        /* 远在圆角外 */
        return 0;
    }

    if (dist_sq <= (radius - 1) * (radius - 1)) {
        /* 完全在圆角内 */
        return 255;
    }

    /* 边缘抗锯齿: 使用像素到理想边缘的距离 */
    int dist = 0;
    /* 近似 sqrt: 使用牛顿法的一步近似 */
    if (dist_sq > 0) {
        dist = dist_sq; /* 初始猜测 */
        dist = (dist + dist_sq / (dist > 0 ? dist : 1)) / 2;
    }

    /* 计算到边缘的距离 (正=内部, 负=外部) */
    int edge_dist = radius - dist;
    /* 映射到 Alpha 范围 [0, 255] */
    int alpha = (edge_dist + 1) * 128;
    if (alpha > 255) alpha = 255;
    if (alpha < 0) alpha = 0;

    return alpha;
}

/*
 * fr_effect_fill_rounded_rect - 绘制填充的圆角矩形(带抗锯齿)
 */
void fr_effect_fill_rounded_rect(fr_context_t *ctx,
                                 int x, int y, int w, int h,
                                 int radius, fr_color_t color)
{
    if (ctx == NULL || ctx->framebuffer == NULL) return;
    if (w <= 0 || h <= 0) return;

    uint32_t pixel_color = ((uint32_t)color.r << 16) |
                            ((uint32_t)color.g << 8) |
                            (uint32_t)color.b;

    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            int tx = x + px;
            int ty = y + py;

            if (tx < 0 || tx >= ctx->width ||
                ty < 0 || ty >= ctx->height) continue;

            int alpha = fr_effect_rounded_rect_alpha(
                tx, ty, x, y, w, h, radius);

            if (alpha == 0) continue;

            if (alpha == 255 && color.a == 255) {
                ctx->framebuffer[ty * ctx->width + tx] = pixel_color;
            } else {
                /* 结合圆角抗锯齿 Alpha 和颜色 Alpha */
                uint8_t final_alpha = (uint8_t)((uint16_t)alpha *
                                                color.a / 255);
                if (final_alpha == 0) continue;

                uint32_t bg = ctx->framebuffer[ty * ctx->width + tx];
                uint8_t bg_r = (bg >> 16) & 0xFF;
                uint8_t bg_g = (bg >> 8) & 0xFF;
                uint8_t bg_b = bg & 0xFF;
                uint8_t inv = 255 - final_alpha;

                uint8_t r = (uint8_t)(((uint16_t)color.r * final_alpha +
                                       (uint16_t)bg_r * inv) / 255);
                uint8_t g = (uint8_t)(((uint16_t)color.g * final_alpha +
                                       (uint16_t)bg_g * inv) / 255);
                uint8_t b = (uint8_t)(((uint16_t)color.b * final_alpha +
                                       (uint16_t)bg_b * inv) / 255);
                ctx->framebuffer[ty * ctx->width + tx] =
                    ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            }
        }
    }
}

/*
 * fr_effect_draw_rounded_rect - 绘制圆角矩形边框(带抗锯齿)
 */
void fr_effect_draw_rounded_rect(fr_context_t *ctx,
                                 int x, int y, int w, int h,
                                 int radius, int border_width,
                                 fr_color_t color)
{
    if (ctx == NULL || ctx->framebuffer == NULL) return;
    if (w <= 0 || h <= 0 || border_width <= 0) return;

    /* 绘制外边框: 对于每像素, 检查它是否在边框区域内 */
    uint32_t pixel_color = ((uint32_t)color.r << 16) |
                            ((uint32_t)color.g << 8) |
                            (uint32_t)color.b;

    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            int tx = x + px;
            int ty = y + py;

            if (tx < 0 || tx >= ctx->width ||
                ty < 0 || ty >= ctx->height) continue;

            /* 外矩形的 Alpha */
            int outer = fr_effect_rounded_rect_alpha(
                tx, ty, x, y, w, h, radius);

            /* 内矩形的 Alpha */
            int inner = fr_effect_rounded_rect_alpha(
                tx, ty, x + border_width, y + border_width,
                w - 2 * border_width, h - 2 * border_width,
                radius - border_width > 0 ? radius - border_width : 0);

            /* 边框区域的 Alpha = 外部Alpha - 内部Alpha */
            int alpha = outer - inner;
            if (alpha <= 0) continue;

            if (alpha == 255 && color.a == 255) {
                ctx->framebuffer[ty * ctx->width + tx] = pixel_color;
            } else {
                uint8_t final_alpha = (uint8_t)((uint16_t)alpha *
                                                color.a / 255);
                if (final_alpha == 0) continue;

                uint32_t bg = ctx->framebuffer[ty * ctx->width + tx];
                uint8_t bg_r = (bg >> 16) & 0xFF;
                uint8_t bg_g = (bg >> 8) & 0xFF;
                uint8_t bg_b = bg & 0xFF;
                uint8_t inv = 255 - final_alpha;

                uint8_t r = (uint8_t)(((uint16_t)color.r * final_alpha +
                                       (uint16_t)bg_r * inv) / 255);
                uint8_t g = (uint8_t)(((uint16_t)color.g * final_alpha +
                                       (uint16_t)bg_g * inv) / 255);
                uint8_t b = (uint8_t)(((uint16_t)color.b * final_alpha +
                                       (uint16_t)bg_b * inv) / 255);
                ctx->framebuffer[ty * ctx->width + tx] =
                    ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            }
        }
    }
}

/* ================================================================
 *  透明效果 (玻璃/磨砂/背景模糊)
 * ================================================================ */

/*
 * fr_effect_transparency - 应用透明效果到帧缓冲区域
 *
 * 玻璃效果: 对背景部分进行采样并叠加轻微亮色
 * 磨砂玻璃: 对背景进行模糊, 然后叠加色调
 * 背景模糊: 仅对背景进行模糊, 不做额外处理
 */
void fr_effect_transparency(fr_context_t *ctx,
                            int x, int y, int w, int h,
                            const fr_transparency_t *trans)
{
    if (ctx == NULL || ctx->framebuffer == NULL) return;
    if (trans == NULL) return;

    if (trans->type == FR_TRANSPARENCY_NONE) return;

    /* 裁剪到屏幕范围 */
    int cx = x, cy = y, cw = w, ch = h;
    if (cx < 0) { cw += cx; cx = 0; }
    if (cy < 0) { ch += cy; cy = 0; }
    if (cx + cw > ctx->width)  cw = ctx->width - cx;
    if (cy + ch > ctx->height) ch = ctx->height - cy;
    if (cw <= 0 || ch <= 0) return;

    if (trans->type == FR_TRANSPARENCY_BLUR_BG) {
        /* 仅背景模糊 */
        fr_effect_gaussian_blur(ctx, cx, cy, cw, ch, 3);
        return;
    }

    if (trans->type == FR_TRANSPARENCY_FROSTED) {
        /* 磨砂玻璃: 模糊 + 变亮 + 色调 */
        /* 先模糊背景 */
        fr_effect_gaussian_blur(ctx, cx, cy, cw, ch, 5);

        /* 叠加色调 */
        uint8_t intensity = trans->intensity;

        for (int py = 0; py < ch; py++) {
            for (int px = 0; px < cw; px++) {
                int tx = cx + px;
                int ty = cy + py;

                uint32_t p = ctx->framebuffer[ty * ctx->width + tx];
                uint8_t pr = (p >> 16) & 0xFF;
                uint8_t pg = (p >> 8) & 0xFF;
                uint8_t pb = p & 0xFF;

                /* 磨砂效果: 变亮 + 色调 */
                uint16_t r = (uint16_t)pr + (uint8_t)((uint16_t)intensity / 2);
                uint16_t g = (uint16_t)pg + (uint8_t)((uint16_t)intensity / 2);
                uint16_t b = (uint16_t)pb + (uint8_t)((uint16_t)intensity / 2);

                /* 混合色调 */
                if (trans->tint_alpha > 0) {
                    r = (r * (255 - trans->tint_alpha) +
                         trans->tint_r * trans->tint_alpha) / 255;
                    g = (g * (255 - trans->tint_alpha) +
                         trans->tint_g * trans->tint_alpha) / 255;
                    b = (b * (255 - trans->tint_alpha) +
                         trans->tint_b * trans->tint_alpha) / 255;
                }

                if (r > 255) r = 255;
                if (g > 255) g = 255;
                if (b > 255) b = 255;

                ctx->framebuffer[ty * ctx->width + tx] =
                    ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
        }
        return;
    }

    if (trans->type == FR_TRANSPARENCY_GLASS) {
        /* 玻璃效果: 轻微变亮 + Alpha 混合 + 高光色调 */
        uint8_t intensity = trans->intensity;

        for (int py = 0; py < ch; py++) {
            for (int px = 0; px < cw; px++) {
                int tx = cx + px;
                int ty = cy + py;

                uint32_t p = ctx->framebuffer[ty * ctx->width + tx];
                uint8_t pr = (p >> 16) & 0xFF;
                uint8_t pg = (p >> 8) & 0xFF;
                uint8_t pb = p & 0xFF;

                /* 玻璃效果: 混合白色和背景 */
                uint16_t inv = 255 - intensity;
                uint16_t r = ((uint16_t)pr * inv + 255 * intensity) / 255;
                uint16_t g = ((uint16_t)pg * inv + 255 * intensity) / 255;
                uint16_t b = ((uint16_t)pb * inv + 255 * intensity) / 255;

                /* 叠加色调 */
                if (trans->tint_alpha > 0) {
                    r = (r * (255 - trans->tint_alpha) +
                         trans->tint_r * trans->tint_alpha) / 255;
                    g = (g * (255 - trans->tint_alpha) +
                         trans->tint_g * trans->tint_alpha) / 255;
                    b = (b * (255 - trans->tint_alpha) +
                         trans->tint_b * trans->tint_alpha) / 255;
                }

                if (r > 255) r = 255;
                if (g > 255) g = 255;
                if (b > 255) b = 255;

                ctx->framebuffer[ty * ctx->width + tx] =
                    ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
        }
        return;
    }
}

/* ================================================================
 *  发光效果
 * ================================================================ */

/*
 * fr_effect_glow - 绘制发光效果
 *
 * 在目标矩形周围绘制一个模糊的光晕。
 * 工作原理: 创建临时缓冲区, 将矩形"点亮", 然后模糊, 再混合回帧缓冲。
 */
void fr_effect_glow(fr_context_t *ctx,
                    int x, int y, int w, int h,
                    const fr_glow_t *glow)
{
    if (ctx == NULL || ctx->framebuffer == NULL) return;
    if (glow == NULL || glow->intensity == 0) return;

    int r = glow->radius;
    if (r <= 0) r = 1;

    /* 计算发光包围盒 */
    int gx = x - r;
    int gy = y - r;
    int gw = w + 2 * r;
    int gh = h + 2 * r;

    /* 裁剪 */
    if (gx < 0) { gw += gx; gx = 0; }
    if (gy < 0) { gh += gy; gy = 0; }
    if (gx + gw > ctx->width)  gw = ctx->width - gx;
    if (gy + gh > ctx->height) gh = ctx->height - gy;
    if (gw <= 0 || gh <= 0) return;

    /* 创建临时发光缓冲区 */
    uint32_t *glow_buf = (uint32_t *)fr_alloc((uint32_t)(gw * gh * 4));
    if (glow_buf == NULL) return;

    uint32_t glow_color = ((uint32_t)glow->color.r << 16) |
                           ((uint32_t)glow->color.g << 8) |
                           (uint32_t)glow->color.b;

    /* 填充发光源 */
    int inner_x = x - gx;
    int inner_y = y - gy;

    for (int py = 0; py < gh; py++) {
        for (int px = 0; px < gw; px++) {
            if (px >= inner_x && px < inner_x + w &&
                py >= inner_y && py < inner_y + h) {
                glow_buf[py * gw + px] = glow_color;
            } else {
                glow_buf[py * gw + px] = 0;
            }
        }
    }

    /* 高斯模糊 */
    fr_effect_blur_buffer(glow_buf, gw, gh, 0, 0, gw, gh, r / 2);
    if (r > 2) {
        fr_effect_blur_buffer(glow_buf, gw, gh, 0, 0, gw, gh, r / 2);
    }

    /* 混合回帧缓冲 */
    for (int py = 0; py < gh; py++) {
        for (int px = 0; px < gw; px++) {
            uint32_t sp = glow_buf[py * gw + px];
            if (sp == 0) continue;

            int tx = gx + px;
            int ty = gy + py;

            uint32_t dp = ctx->framebuffer[ty * ctx->width + tx];
            uint8_t sb = sp & 0xFF;
            uint8_t sg = (sp >> 8) & 0xFF;
            uint8_t sr = (sp >> 16) & 0xFF;

            /* 基于亮度计算 Alpha */
            uint8_t sa = (uint8_t)(((uint16_t)sr + (uint16_t)sg +
                                    (uint16_t)sb) / 3);
            sa = (uint8_t)((uint16_t)sa * glow->intensity / 255);
            if (sa == 0) continue;

            ctx->framebuffer[ty * ctx->width + tx] =
                fr_effect_blend_pixel(sp, dp, sa, FR_BLEND_ADDITIVE);
        }
    }

    fr_free(glow_buf);
}

/*
 * fr_effect_glow_masked - 在带遮罩的区域上绘制发光
 */
void fr_effect_glow_masked(fr_context_t *ctx,
                           int x, int y, int w, int h,
                           const uint8_t *mask,
                           const fr_glow_t *glow)
{
    if (ctx == NULL || ctx->framebuffer == NULL) return;
    if (glow == NULL || mask == NULL || glow->intensity == 0) return;

    int r = glow->radius;
    if (r <= 0) r = 1;

    int gx = x - r;
    int gy = y - r;
    int gw = w + 2 * r;
    int gh = h + 2 * r;

    if (gx < 0) { gw += gx; gx = 0; }
    if (gy < 0) { gh += gy; gy = 0; }
    if (gx + gw > ctx->width)  gw = ctx->width - gx;
    if (gy + gh > ctx->height) gh = ctx->height - gy;
    if (gw <= 0 || gh <= 0) return;

    uint32_t *glow_buf = (uint32_t *)fr_alloc((uint32_t)(gw * gh * 4));
    if (glow_buf == NULL) return;

    uint32_t glow_color = ((uint32_t)glow->color.r << 16) |
                           ((uint32_t)glow->color.g << 8) |
                           (uint32_t)glow->color.b;

    int inner_x = x - gx;
    int inner_y = y - gy;

    /* 使用遮罩填充发光源 */
    for (int py = 0; py < gh; py++) {
        for (int px = 0; px < gw; px++) {
            int mx = px - inner_x;
            int my = py - inner_y;

            if (mx >= 0 && mx < w && my >= 0 && my < h) {
                uint8_t ma = mask[my * w + mx];
                if (ma > 0) {
                    uint8_t r = (uint8_t)((uint16_t)glow->color.r * ma / 255);
                    uint8_t g = (uint8_t)((uint16_t)glow->color.g * ma / 255);
                    uint8_t b = (uint8_t)((uint16_t)glow->color.b * ma / 255);
                    glow_buf[py * gw + px] =
                        ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
                } else {
                    glow_buf[py * gw + px] = 0;
                }
            } else {
                glow_buf[py * gw + px] = 0;
            }
        }
    }

    /* 模糊 */
    fr_effect_blur_buffer(glow_buf, gw, gh, 0, 0, gw, gh, r / 2);
    if (r > 2) {
        fr_effect_blur_buffer(glow_buf, gw, gh, 0, 0, gw, gh, r / 2);
    }

    /* 加法混合回帧缓冲 */
    for (int py = 0; py < gh; py++) {
        for (int px = 0; px < gw; px++) {
            uint32_t sp = glow_buf[py * gw + px];
            if (sp == 0) continue;

            int tx = gx + px;
            int ty = gy + py;

            uint32_t dp = ctx->framebuffer[ty * ctx->width + tx];
            uint8_t sr = (sp >> 16) & 0xFF;
            uint8_t sg = (sp >> 8) & 0xFF;
            uint8_t sb = sp & 0xFF;
            uint8_t sa = (uint8_t)(((uint16_t)sr + (uint16_t)sg +
                                    (uint16_t)sb) / 3);
            sa = (uint8_t)((uint16_t)sa * glow->intensity / 255);
            if (sa == 0) continue;

            ctx->framebuffer[ty * ctx->width + tx] =
                fr_effect_blend_pixel(sp, dp, sa, FR_BLEND_ADDITIVE);
        }
    }

    fr_free(glow_buf);
}

/* ================================================================
 *  颜色叠加
 * ================================================================ */

/*
 * fr_effect_color_overlay - 应用颜色叠加
 *
 * 使用指定的混合模式将颜色叠加到矩形区域上。
 */
void fr_effect_color_overlay(fr_context_t *ctx,
                             int x, int y, int w, int h,
                             const fr_color_overlay_t *overlay)
{
    if (ctx == NULL || ctx->framebuffer == NULL) return;
    if (overlay == NULL || overlay->opacity == 0) return;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > ctx->width)  w = ctx->width - x;
    if (y + h > ctx->height) h = ctx->height - y;
    if (w <= 0 || h <= 0) return;

    uint32_t overlay_color = ((uint32_t)overlay->color.r << 16) |
                              ((uint32_t)overlay->color.g << 8) |
                              (uint32_t)overlay->color.b;

    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            int tx = x + px;
            int ty = y + py;

            uint32_t dp = ctx->framebuffer[ty * ctx->width + tx];

            ctx->framebuffer[ty * ctx->width + tx] =
                fr_effect_blend_pixel(overlay_color, dp,
                                      overlay->opacity,
                                      overlay->blend_mode);
        }
    }
}

/* ================================================================
 *  控件透明度
 * ================================================================ */

/*
 * fr_effect_set_opacity - 设置不透明度控制
 */
void fr_effect_set_opacity(fr_opacity_t *opacity, uint8_t value, int enabled)
{
    if (opacity == NULL) return;
    opacity->opacity = value;
    opacity->enabled = enabled ? 1 : 0;
}

/*
 * fr_effect_apply_opacity - 应用不透明度到帧缓冲区域
 *
 * 将每个像素与黑色背景混合以模拟不透明度降低效果。
 * 注意: 这简化了透明度效果——正确的做法需要将该区域与它下面
 * 的内容混合, 而不是与黑色混合。此处作为通用 alpha 缩放处理。
 */
void fr_effect_apply_opacity(struct fr_context *ctx,
                             int x, int y, int w, int h,
                             uint8_t opacity)
{
    if (ctx == NULL || ctx->framebuffer == NULL) return;
    if (opacity >= 255) return; /* 无效果 */

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > ctx->width)  w = ctx->width - x;
    if (y + h > ctx->height) h = ctx->height - y;
    if (w <= 0 || h <= 0) return;

    /* 将每个像素的 RGB 值按比例缩放, 模拟不透明度 */
    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            int tx = x + px;
            int ty = y + py;

            uint32_t p = ctx->framebuffer[ty * ctx->width + tx];
            uint8_t pr = (p >> 16) & 0xFF;
            uint8_t pg = (p >> 8) & 0xFF;
            uint8_t pb = p & 0xFF;

            /* 向黑色 (0) 混合 */
            uint8_t r = (uint8_t)((uint16_t)pr * opacity / 255);
            uint8_t g = (uint8_t)((uint16_t)pg * opacity / 255);
            uint8_t b = (uint8_t)((uint16_t)pb * opacity / 255);

            ctx->framebuffer[ty * ctx->width + tx] =
                ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }
}