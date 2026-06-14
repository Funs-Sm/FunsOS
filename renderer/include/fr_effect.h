/* fr_effect.h - 视觉特效引擎
 * 提供阴影、模糊、渐变、混合、圆角矩形、透明、发光、
 * 颜色叠加和透明度控制等视觉特效
 */

#ifndef FR_EFFECT_H
#define FR_EFFECT_H

#include "stdint.h"

/* Ensure fr_color_t is available */
#ifndef FR_COLOR_T_DEFINED
#define FR_COLOR_T_DEFINED
typedef struct { uint8_t r, g, b, a; } fr_color_t;
#endif

/* 前向声明 */
struct fr_context;

/* ---- 阴影效果 ---- */

/* 阴影配置 */
typedef struct {
    int offset_x;           /* 水平偏移 */
    int offset_y;           /* 垂直偏移 */
    int blur_radius;        /* 模糊半径 */
    fr_color_t color;       /* 阴影颜色 */
    uint8_t opacity;        /* 阴影不透明度 (0-255) */
    int spread;             /* 扩展半径 */
} fr_drop_shadow_t;

/* 创建默认阴影 */
#define FR_SHADOW_DEFAULT \
    ((fr_drop_shadow_t){2, 2, 4, FR_RGBA(0, 0, 0, 0), 128, 0})

/* ---- 高斯模糊 ---- */

/* 模糊核大小 */
#define FR_BLUR_3x3     3
#define FR_BLUR_5x5     5
#define FR_BLUR_7x7     7

/* ---- 渐变 ---- */

/* 渐变类型 */
#define FR_GRADIENT_LINEAR      0   /* 线性渐变 */
#define FR_GRADIENT_RADIAL      1   /* 径向渐变 */

/* 渐变方向(线性渐变) */
#define FR_GRADIENT_DIR_HORIZONTAL  0
#define FR_GRADIENT_DIR_VERTICAL    1
#define FR_GRADIENT_DIR_DIAGONAL    2   /* 左上到右下 */
#define FR_GRADIENT_DIR_ANTIDIAG    3   /* 右上到左下 */

/* 颜色停止点 (旧版兼容，fr_gradient.h 有更完整的 fr_grad_stop_t) */
typedef struct {
    float position;         /* 位置 0.0 - 1.0 */
    fr_color_t color;       /* 该位置的颜色 */
} fr_gradient_stop_t;

/* fr_gradient_t 的完整定义在 fr_gradient.h 中 */
typedef struct fr_gradient_full fr_gradient_t;

/* ---- Alpha 混合 ---- */

/* 混合模式 */
#define FR_BLEND_SRC_OVER       0   /* 标准源覆盖 */
#define FR_BLEND_SRC_IN         1   /* 源在目标内 */
#define FR_BLEND_SRC_OUT        2   /* 源在目标外 */
#define FR_BLEND_DST_OVER       3   /* 目标覆盖源 */
#define FR_BLEND_ADDITIVE       4   /* 加法混合 */
#define FR_BLEND_MULTIPLY       5   /* 正片叠底 */
#define FR_BLEND_SCREEN         6   /* 滤色 */
#define FR_BLEND_OVERLAY        7   /* 叠加 */
#define FR_BLEND_COLOR_DODGE    8   /* 颜色减淡 */

/* ---- 圆角矩形 ---- */

/* ---- 透明效果 ---- */

/* 透明效果类型 */
#define FR_TRANSPARENCY_NONE        0   /* 无 */
#define FR_TRANSPARENCY_GLASS       1   /* 玻璃效果 */
#define FR_TRANSPARENCY_FROSTED     2   /* 磨砂玻璃 */
#define FR_TRANSPARENCY_BLUR_BG     3   /* 背景模糊 */

/* 透明效果配置 */
typedef struct {
    uint32_t type;              /* 透明类型 */
    uint8_t intensity;          /* 强度 - 玻璃:毛玻璃感, 0-255 */
    uint8_t tint_r, tint_g, tint_b;  /* 色调 */
    uint8_t tint_alpha;         /* 色调透明度 */
} fr_transparency_t;

/* ---- 发光效果 ---- */

/* 发光配置 */
typedef struct {
    int radius;            /* 发光半径 */
    uint8_t intensity;     /* 发光强度 (0-255) */
    fr_color_t color;      /* 发光颜色 */
    int inner;             /* 0=外发光, 1=内发光 */
} fr_glow_t;

/* ---- 颜色叠加 ---- */

/* 颜色叠加配置 */
typedef struct {
    fr_color_t color;      /* 叠加颜色 */
    uint8_t opacity;       /* 叠加不透明度 (0-255) */
    uint32_t blend_mode;   /* 混合模式 */
} fr_color_overlay_t;

/* ---- 控件透明度 ---- */

/* 控件不透明度控制 */
typedef struct {
    uint8_t opacity;       /* 不透明度 (0-255, 0=完全透明) */
    int enabled;           /* 是否启用 */
} fr_opacity_t;

/* ================================================================
 *  API 函数声明
 * ================================================================ */

/* ---- 阴影效果 ---- */

/* 绘制投影: 在目标表面下绘制一个模糊偏移的阴影 */
void fr_effect_drop_shadow(struct fr_context *ctx,
                           int x, int y, int w, int h,
                           const fr_drop_shadow_t *shadow);

/* 在带Alpha的遮罩上绘制阴影(遮罩决定阴影形状) */
void fr_effect_drop_shadow_masked(struct fr_context *ctx,
                                  int x, int y, int w, int h,
                                  const uint8_t *alpha_mask,
                                  const fr_drop_shadow_t *shadow);

/* ---- 高斯模糊 ---- */

/* 对整个帧缓冲区域执行高斯模糊 */
void fr_effect_gaussian_blur(struct fr_context *ctx,
                             int x, int y, int w, int h,
                             int radius);

/* 对像素缓冲区执行就地高斯模糊 */
void fr_effect_blur_buffer(uint32_t *buffer, int buf_w, int buf_h,
                           int x, int y, int w, int h, int radius);

/* ---- 渐变 ---- */

/* 渲染渐变到帧缓冲 */
void fr_effect_render_gradient(struct fr_context *ctx,
                               int x, int y, int w, int h,
                               const fr_gradient_t *gradient);

/* 从渐变配置中采样颜色 */
fr_color_t fr_effect_gradient_sample(const fr_gradient_t *gradient,
                                     float t);

/* ---- Alpha 混合 ---- */

/* 混合两个像素 (单像素) */
uint32_t fr_effect_blend_pixel(uint32_t src, uint32_t dst,
                                uint8_t alpha, uint32_t mode);

/* 混合源缓冲区到帧缓冲区 */
void fr_effect_blend_buffer(struct fr_context *ctx,
                            int dx, int dy,
                            const uint32_t *src, int src_w, int src_h,
                            int sx, int sy, int sw, int sh,
                            uint8_t alpha, uint32_t mode);

/* 带逐像素 Alpha 的混合 (source-pre multiplied或separate alpha) */
void fr_effect_blend_buffer_alpha(struct fr_context *ctx,
                                  int dx, int dy,
                                  const uint32_t *src, int src_w, int src_h,
                                  const uint8_t *alpha_map,
                                  int sx, int sy, int sw, int sh,
                                  uint8_t global_alpha, uint32_t mode);

/* ---- 圆角矩形 ---- */

/* 绘制填充的圆角矩形(带抗锯齿) */
void fr_effect_fill_rounded_rect(struct fr_context *ctx,
                                 int x, int y, int w, int h,
                                 int radius, fr_color_t color);

/* 绘制圆角矩形边框(带抗锯齿) */
void fr_effect_draw_rounded_rect(struct fr_context *ctx,
                                 int x, int y, int w, int h,
                                 int radius, int border_width,
                                 fr_color_t color);

/* 计算某像素到圆角矩形边缘的距离(用于抗锯齿) */
int fr_effect_rounded_rect_alpha(int px, int py,
                                 int rx, int ry, int rw, int rh,
                                 int radius);

/* ---- 透明效果 ---- */

/* 应用透明效果到帧缓冲区域 */
void fr_effect_transparency(struct fr_context *ctx,
                            int x, int y, int w, int h,
                            const fr_transparency_t *trans);

/* ---- 发光效果 ---- */

/* 绘制发光效果 */
void fr_effect_glow(struct fr_context *ctx,
                    int x, int y, int w, int h,
                    const fr_glow_t *glow);

/* 在带遮罩的区域上绘制发光 */
void fr_effect_glow_masked(struct fr_context *ctx,
                           int x, int y, int w, int h,
                           const uint8_t *mask,
                           const fr_glow_t *glow);

/* ---- 颜色叠加 ---- */

/* 应用颜色叠加到矩形区域 */
void fr_effect_color_overlay(struct fr_context *ctx,
                             int x, int y, int w, int h,
                             const fr_color_overlay_t *overlay);

/* ---- 控件透明度 ---- */

/* 设置控件不透明度 */
void fr_effect_set_opacity(fr_opacity_t *opacity, uint8_t value, int enabled);

/* 渲染时应用不透明度(混合两个缓冲区的对应像素) */
void fr_effect_apply_opacity(struct fr_context *ctx,
                             int x, int y, int w, int h,
                             uint8_t opacity);

#endif /* FR_EFFECT_H */