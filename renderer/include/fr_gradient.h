/* fr_gradient.h - 高级渐变渲染
 * 提供线性、径向、锥形、网格渐变以及多色停止点和抖动
 */

#ifndef FR_GRADIENT_H
#define FR_GRADIENT_H

#include "stdint.h"

/* Ensure fr_color_t is available */
#ifndef FR_COLOR_T_DEFINED
#define FR_COLOR_T_DEFINED
typedef struct { uint8_t r, g, b, a; } fr_color_t;
#endif

/* 前向声明 */
struct fr_context;

/* ---- 渐变类型 ---- */
#define FR_GRAD_TYPE_LINEAR     0
#define FR_GRAD_TYPE_RADIAL     1
#define FR_GRAD_TYPE_CONICAL    2
#define FR_GRAD_TYPE_MESH       3
#define FR_GRAD_TYPE_DIAMOND    4
#define FR_GRAD_TYPE_SPIRAL     5
#define FR_GRAD_TYPE_REPEAT     6
#define FR_GRAD_TYPE_REFLECT    7

/* ---- 渐变扩展模式 ---- */
#define FR_GRAD_EXTEND_PAD      0   /* 边界填充 */
#define FR_GRAD_EXTEND_REPEAT   1   /* 重复 */
#define FR_GRAD_EXTEND_REFLECT  2   /* 反射 */

/* ---- 渐变插值颜色空间 ---- */
#define FR_GRAD_COLORSPACE_RGB  0
#define FR_GRAD_COLORSPACE_HSL  1
#define FR_GRAD_COLORSPACE_LAB  2
#define FR_GRAD_COLORSPACE_OKLAB 3

/* ---- 颜色停止点 ---- */
#define FR_MAX_GRADIENT_STOPS   32

typedef struct {
    float position;             /* 位置 0.0 - 1.0 */
    fr_color_t color;           /* 停止点颜色 */
    float midpoint;             /* 中点 (默认 0.5) */
} fr_grad_stop_t;

/* ---- 线性渐变参数 ---- */
typedef struct {
    float x1, y1;               /* 起点 */
    float x2, y2;               /* 终点 */
} fr_grad_linear_t;

/* ---- 径向渐变参数 ---- */
typedef struct {
    float cx, cy;               /* 圆心 */
    float fx, fy;               /* 焦点 */
    float radius;               /* 半径 */
} fr_grad_radial_t;

/* ---- 锥形渐变参数 ---- */
typedef struct {
    float cx, cy;               /* 中心 */
    float start_angle;          /* 起始角度 */
    float end_angle;            /* 结束角度 */
} fr_grad_conical_t;

/* ---- 网格渐变参数 ---- */
#define FR_GRAD_MESH_MAX_COLS 8
#define FR_GRAD_MESH_MAX_ROWS 8

typedef struct {
    float x, y;                 /* 网格点位置 */
    fr_color_t color;           /* 网格点颜色 */
} fr_grad_mesh_point_t;

typedef struct {
    int cols, rows;             /* 网格尺寸 */
    float x, y, w, h;           /* 网格区域 */
    fr_grad_mesh_point_t points[FR_GRAD_MESH_MAX_COLS * FR_GRAD_MESH_MAX_ROWS];
} fr_grad_mesh_t;

/* ---- 渐变配置 ---- */
typedef struct fr_gradient_full {
    uint32_t type;                              /* 渐变类型 */
    uint32_t extend_mode;                       /* 扩展模式 */
    uint32_t colorspace;                        /* 插值颜色空间 */
    int dither;                                 /* 是否启用抖动 */
    uint32_t stop_count;                        /* 停止点数量 */
    fr_grad_stop_t stops[FR_MAX_GRADIENT_STOPS];

    /* 类型特定参数 */
    union {
        fr_grad_linear_t linear;
        fr_grad_radial_t radial;
        fr_grad_conical_t conical;
        fr_grad_mesh_t mesh;
        struct {
            float cx, cy;
            float half_w, half_h;               /* 菱形半宽/半高 */
        } diamond;
        struct {
            float cx, cy;
            float start_radius, end_radius;
            float start_angle, revolutions;
        } spiral;
    } params;

    /* 变换矩阵 */
    float matrix[6];                            /* 仿射矩阵 */
} fr_gradient_t;

/* ---- 抖动表 ---- */
typedef struct {
    uint8_t matrix[4][4];       /* 4x4 抖动矩阵 */
    int size;                   /* 矩阵大小 */
} fr_dither_table_t;

/* ================================================================
 *  API 函数声明
 * ================================================================ */

/* 渐变管理 */
fr_gradient_t *fr_gradient_create(uint32_t type);
void fr_gradient_destroy(fr_gradient_t *grad);
void fr_gradient_reset(fr_gradient_t *grad);

/* 颜色停止点 */
int fr_gradient_add_stop(fr_gradient_t *grad, float position,
                          fr_color_t color);
int fr_gradient_add_stop_rgba(fr_gradient_t *grad, float position,
                               uint8_t r, uint8_t g, uint8_t b, uint8_t a);
int fr_gradient_remove_stop(fr_gradient_t *grad, int index);
void fr_gradient_clear_stops(fr_gradient_t *grad);
int fr_gradient_set_stop_midpoint(fr_gradient_t *grad, int index, float mid);
int fr_gradient_get_stop_count(const fr_gradient_t *grad);

/* 渐变参数设置 */
void fr_gradient_set_linear(fr_gradient_t *grad,
                             float x1, float y1, float x2, float y2);
void fr_gradient_set_radial(fr_gradient_t *grad,
                             float cx, float cy, float fx, float fy, float r);
void fr_gradient_set_conical(fr_gradient_t *grad,
                              float cx, float cy, float start_angle, float end_angle);
void fr_gradient_set_mesh(fr_gradient_t *grad,
                           int cols, int rows, float x, float y, float w, float h);
void fr_gradient_set_mesh_point(fr_gradient_t *grad,
                                 int col, int row, float x, float y, fr_color_t color);
void fr_gradient_set_diamond(fr_gradient_t *grad,
                              float cx, float cy, float hw, float hh);
void fr_gradient_set_spiral(fr_gradient_t *grad,
                             float cx, float cy, float sr, float er,
                             float start_angle, float revs);

/* 渐变属性 */
void fr_gradient_set_extend_mode(fr_gradient_t *grad, uint32_t mode);
void fr_gradient_set_colorspace(fr_gradient_t *grad, uint32_t colorspace);
void fr_gradient_set_dither(fr_gradient_t *grad, int enabled);
void fr_gradient_set_transform(fr_gradient_t *grad, const float matrix[6]);
void fr_gradient_set_identity_transform(fr_gradient_t *grad);

/* 渲染 */
void fr_gradient_render(fr_gradient_t *grad, struct fr_context *ctx,
                         int x, int y, int w, int h);
void fr_gradient_render_clipped(fr_gradient_t *grad, struct fr_context *ctx,
                                 int x, int y, int w, int h,
                                 const uint8_t *clip_mask);

/* 颜色采样 */
fr_color_t fr_gradient_sample(const fr_gradient_t *grad, float t);
fr_color_t fr_gradient_sample_at(const fr_gradient_t *grad, float px, float py);

/* 抖动 */
void fr_dither_init(fr_dither_table_t *table, int size);
uint8_t fr_dither_threshold(const fr_dither_table_t *table, int x, int y, int level);

/* 预定义渐变 */
fr_gradient_t *fr_gradient_create_sunset(void);
fr_gradient_t *fr_gradient_create_ocean(void);
fr_gradient_t *fr_gradient_create_forest(void);
fr_gradient_t *fr_gradient_create_fire(void);
fr_gradient_t *fr_gradient_create_metal(void);
fr_gradient_t *fr_gradient_create_rainbow(void);

#endif /* FR_GRADIENT_H */