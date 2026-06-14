/* fr_shape.h - 形状库
 * 提供圆角矩形、星形、多边形、箭头、气泡和拼图块等形状
 */

#ifndef FR_SHAPE_H
#define FR_SHAPE_H

#include "stdint.h"

/* 前向声明 */
struct fr_context;

/* ---- 形状类型 ---- */
#define FR_SHAPE_RECT               0
#define FR_SHAPE_ROUNDED_RECT       1
#define FR_SHAPE_ELLIPSE            2
#define FR_SHAPE_STAR               3
#define FR_SHAPE_POLYGON            4
#define FR_SHAPE_ARROW              5
#define FR_SHAPE_SPEECH_BUBBLE      6
#define FR_SHAPE_PUZZLE             7
#define FR_SHAPE_CROSS              8
#define FR_SHAPE_CHECK              9
#define FR_SHAPE_HEART              10
#define FR_SHAPE_DIAMOND            11
#define FR_SHAPE_TRIANGLE           12
#define FR_SHAPE_HEXAGON            13
#define FR_SHAPE_OCTAGON            14
#define FR_SHAPE_PENTAGON           15
#define FR_SHAPE_CUSTOM             16

/* ---- 箭头方向 ---- */
#define FR_ARROW_UP             0
#define FR_ARROW_DOWN           1
#define FR_ARROW_LEFT           2
#define FR_ARROW_RIGHT          3
#define FR_ARROW_UP_LEFT        4
#define FR_ARROW_UP_RIGHT       5
#define FR_ARROW_DOWN_LEFT      6
#define FR_ARROW_DOWN_RIGHT     7

/* ---- 气泡方向 ---- */
#define FR_BUBBLE_POINT_LEFT    0
#define FR_BUBBLE_POINT_RIGHT   1
#define FR_BUBBLE_POINT_TOP     2
#define FR_BUBBLE_POINT_BOTTOM  3
#define FR_BUBBLE_POINT_NONE    4

/* ---- 拼图边类型 ---- */
#define FR_PUZZLE_EDGE_FLAT     0
#define FR_PUZZLE_EDGE_TAB      1
#define FR_PUZZLE_EDGE_BLANK    2

/* ---- 形状样式 ---- */
typedef struct {
    fr_color_t fill_color;      /* 填充颜色 */
    fr_color_t stroke_color;    /* 描边颜色 */
    float stroke_width;         /* 描边宽度 */
    uint8_t fill_opacity;       /* 填充不透明度 */
    uint8_t stroke_opacity;     /* 描边不透明度 */
    float corner_radius;        /* 圆角半径 */
    int shadow;                 /* 是否绘制阴影 */
    fr_color_t shadow_color;    /* 阴影颜色 */
    float shadow_offset_x;      /* 阴影偏移X */
    float shadow_offset_y;      /* 阴影偏移Y */
    float shadow_blur;          /* 阴影模糊 */
} fr_shape_style_t;

/* ---- 形状结构 ---- */
typedef struct {
    uint32_t type;              /* 形状类型 */
    float x, y;                 /* 位置 */
    float w, h;                 /* 大小 */
    float params[16];           /* 形状特定参数 */
    int param_count;            /* 参数数量 */
    float rotation;             /* 旋转角度 */
    float center_x, center_y;   /* 旋转中心 */
    fr_shape_style_t style;     /* 样式 */
} fr_shape_t;

/* ---- 形状特定参数索引 ---- */

/* 圆角矩形: params[0]=rx, params[1]=ry */
#define FR_RECT_PARAM_RX        0
#define FR_RECT_PARAM_RY        1

/* 星形: params[0]=points, params[1]=inner_ratio, params[2]=roundness */
#define FR_STAR_PARAM_POINTS    0
#define FR_STAR_PARAM_INNER     1
#define FR_STAR_PARAM_ROUND     2

/* 多边形: params[0]=sides, params[1]=roundness */
#define FR_POLY_PARAM_SIDES     0
#define FR_POLY_PARAM_ROUND     1

/* 箭头: params[0]=direction, params[1]=stem_width, params[2]=head_width, params[3]=head_length */
#define FR_ARROW_PARAM_DIR      0
#define FR_ARROW_PARAM_STEM_W   1
#define FR_ARROW_PARAM_HEAD_W   2
#define FR_ARROW_PARAM_HEAD_L   3

/* 气泡: params[0]=point_dir, params[1]=point_x, params[2]=point_y, params[3]=point_size, params[4]=corner_r */
#define FR_BUBBLE_PARAM_DIR     0
#define FR_BUBBLE_PARAM_PX      1
#define FR_BUBBLE_PARAM_PY      2
#define FR_BUBBLE_PARAM_PS      3
#define FR_BUBBLE_PARAM_CR      4

/* 拼图: params[0]=rows, params[1]=cols, params[2]=row, params[3]=col, params[4]=tab_size */
#define FR_PUZZLE_PARAM_ROWS    0
#define FR_PUZZLE_PARAM_COLS    1
#define FR_PUZZLE_PARAM_ROW     2
#define FR_PUZZLE_PARAM_COL     3
#define FR_PUZZLE_PARAM_TAB     4

/* 十字: params[0]=thickness, params[1]=roundness */
#define FR_CROSS_PARAM_THICK    0
#define FR_CROSS_PARAM_ROUND    1

/* 对勾: params[0]=thickness */
#define FR_CHECK_PARAM_THICK    0

/* ================================================================
 *  API 函数声明
 * ================================================================ */

/* 形状创建 */
fr_shape_t *fr_shape_create(uint32_t type);
void fr_shape_destroy(fr_shape_t *shape);
void fr_shape_set_rect(fr_shape_t *shape, float x, float y, float w, float h);
void fr_shape_set_rotation(fr_shape_t *shape, float angle, float cx, float cy);

/* 形状特定创建函数 */
fr_shape_t *fr_shape_create_rounded_rect(float x, float y, float w, float h,
                                          float rx, float ry);
fr_shape_t *fr_shape_create_star(float x, float y, float w, float h,
                                  int points, float inner_ratio);
fr_shape_t *fr_shape_create_polygon(float x, float y, float w, float h,
                                     int sides);
fr_shape_t *fr_shape_create_arrow(float x, float y, float w, float h,
                                   int direction);
fr_shape_t *fr_shape_create_speech_bubble(float x, float y, float w, float h,
                                           int point_dir, float px, float py);
fr_shape_t *fr_shape_create_puzzle(float x, float y, float w, float h,
                                    int rows, int cols, int row, int col);
fr_shape_t *fr_shape_create_cross(float x, float y, float w, float h,
                                   float thickness);
fr_shape_t *fr_shape_create_check(float x, float y, float w, float h,
                                   float thickness);
fr_shape_t *fr_shape_create_heart(float x, float y, float w, float h);
fr_shape_t *fr_shape_create_diamond(float x, float y, float w, float h);
fr_shape_t *fr_shape_create_triangle(float x, float y, float w, float h,
                                      int direction);
fr_shape_t *fr_shape_create_hexagon(float x, float y, float w, float h);
fr_shape_t *fr_shape_create_octagon(float x, float y, float w, float h);
fr_shape_t *fr_shape_create_pentagon(float x, float y, float w, float h);

/* 样式设置 */
void fr_shape_set_style(fr_shape_t *shape, const fr_shape_style_t *style);
void fr_shape_set_fill(fr_shape_t *shape, fr_color_t color);
void fr_shape_set_stroke(fr_shape_t *shape, fr_color_t color, float width);
void fr_shape_set_shadow(fr_shape_t *shape, int enabled,
                          fr_color_t color, float ox, float oy, float blur);
void fr_shape_set_corner_radius(fr_shape_t *shape, float radius);

/* 参数设置 */
void fr_shape_set_param(fr_shape_t *shape, int index, float value);
float fr_shape_get_param(const fr_shape_t *shape, int index);

/* 渲染 */
void fr_shape_draw(fr_shape_t *shape, struct fr_context *ctx);
void fr_shape_fill(fr_shape_t *shape, struct fr_context *ctx);
void fr_shape_stroke(fr_shape_t *shape, struct fr_context *ctx);

/* 查询 */
int fr_shape_contains_point(const fr_shape_t *shape, float x, float y);
void fr_shape_get_bounds(const fr_shape_t *shape,
                          float *x, float *y, float *w, float *h);

/* 批量绘制 */
void fr_shape_draw_grid(struct fr_context *ctx, fr_shape_t *shape,
                         int cols, int rows, float spacing_x, float spacing_y);

#endif /* FR_SHAPE_H */