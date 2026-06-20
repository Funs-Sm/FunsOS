/* primitive.h - 图元光栅化器
 * 提供 2D 基础图元绘制: 像素、直线(含抗锯齿)、三角形(含Gouraud着色)、
 * 椭圆/圆、圆弧和二次贝塞尔曲线
 */

#ifndef FR_PRIMITIVE_H
#define FR_PRIMITIVE_H

#include "stdint.h"

/* 前向声明 */
struct fr_context;

/* ================================================================
 *  基础像素操作
 * ================================================================ */

void prim_set_pixel(struct fr_context *ctx, int x, int y, uint32_t color);
void prim_draw_hline(struct fr_context *ctx, int x0, int x1, int y,
                     uint32_t color);
void prim_draw_vline(struct fr_context *ctx, int x, int y0, int y1,
                     uint32_t color);

/* ================================================================
 *  直线绘制
 * ================================================================ */

/* Bresenham 直线算法 (整数坐标) */
void prim_draw_line_bresenham(struct fr_context *ctx,
                              int x0, int y0, int x1, int y1,
                              uint32_t color);

/* Xiaolin Wu 反走样直线 (浮点坐标) */
void prim_draw_line_aa(struct fr_context *ctx,
                       float x0, float y0, float x1, float y1,
                       uint32_t color);

/* ================================================================
 *  三角形填充
 * ================================================================ */

/* 单色三角形 (扫描线填充) */
void prim_fill_triangle(struct fr_context *ctx,
                        int x0, int y0, int x1, int y1,
                        int x2, int y2, uint32_t color);

/* Gouraud 着色三角形 (顶点颜色插值) */
void prim_fill_triangle_flat(struct fr_context *ctx,
                             int x0, int y0, uint32_t c0,
                             int x1, int y1, uint32_t c1,
                             int x2, int y2, uint32_t c2);

/* ================================================================
 *  椭圆 / 圆
 * ================================================================ */

/* 中点椭圆算法 (描边) */
void prim_draw_ellipse(struct fr_context *ctx,
                        int cx, int cy, int rx, int ry,
                        uint32_t color);

/* 中点椭圆算法 (填充) */
void prim_fill_ellipse(struct fr_context *ctx,
                        int cx, int cy, int rx, int ry,
                        uint32_t color);

/* 圆弧 (角度范围, 弧度制) */
void prim_draw_arc(struct fr_context *ctx,
                    int cx, int cy, int r,
                    float start_angle, float end_angle,
                    uint32_t color);

/* ================================================================
 *  贝塞尔曲线
 * ================================================================ */

/* 二次贝塞尔曲线 (3个控制点) */
void prim_draw_bezier_quad(struct fr_context *ctx,
                            float x0, float y0,
                            float x1, float y1,
                            float x2, float y2,
                            uint32_t color);

#endif /* FR_PRIMITIVE_H */
