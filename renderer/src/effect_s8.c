/*
 * effect_s8.c -- Opacity effects
 * Split from effect.c (v0.8.2)
 * Contains: fr_effect_set_opacity + fr_effect_apply_opacity
 * Standalone: no cross-section calls to other effect sections
 */

#include "fr_effect.h"
#include "fr_context.h"
#include "stdint.h"
#include "stddef.h"

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
 * fr_effect_apply_opacity - 应用不透明度到帧缓冲区
 *
 * 将每个像素与黑色背景混合以模拟不透明度降低效果
 * 注意: 这简化了透明度效果——正确的做法需要将该区域与它下面的
 * 的内容混合, 而不是与黑色混合。此处作为通用 alpha 缩放处理
 */
void fr_effect_apply_opacity(struct fr_context *ctx,
                             int x, int y, int w, int h,
                             uint8_t opacity)
{
    if (ctx == NULL || ctx->framebuffer == NULL) return;
    if (opacity >= 255) return; /* 无效值 */

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

