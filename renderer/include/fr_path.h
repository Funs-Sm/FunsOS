/* fr_path.h - 矢量路径渲染
 * 提供 SVG 风格路径命令、描边、填充和布尔运算
 */

#ifndef FR_PATH_H
#define FR_PATH_H

#include "stdint.h"

/* 前向声明 */
struct fr_context;

/* ---- 路径命令类型 ---- */
#define FR_PATH_MOVE_TO         0   /* M: 移动到 */
#define FR_PATH_LINE_TO         1   /* L: 画线到 */
#define FR_PATH_CURVE_TO        2   /* C: 三次贝塞尔曲线 */
#define FR_PATH_QUAD_TO         3   /* Q: 二次贝塞尔曲线 */
#define FR_PATH_ARC_TO          4   /* A: 弧线 */
#define FR_PATH_CLOSE           5   /* Z: 闭合路径 */
#define FR_PATH_HLINE_TO        6   /* H: 水平线 */
#define FR_PATH_VLINE_TO        7   /* V: 垂直线 */
#define FR_PATH_SMOOTH_CURVE_TO 8   /* S: 平滑三次贝塞尔 */
#define FR_PATH_SMOOTH_QUAD_TO  9   /* T: 平滑二次贝塞尔 */

/* ---- 填充规则 ---- */
#define FR_FILL_NONZERO         0
#define FR_FILL_EVENODD         1

/* ---- 描边端点样式 ---- */
#define FR_CAP_BUTT             0
#define FR_CAP_ROUND            1
#define FR_CAP_SQUARE           2

/* ---- 描边连接样式 ---- */
#define FR_JOIN_MITER           0
#define FR_JOIN_ROUND           1
#define FR_JOIN_BEVEL           2

/* ---- 布尔运算 ---- */
#define FR_BOOL_UNION           0
#define FR_BOOL_INTERSECTION    1
#define FR_BOOL_DIFFERENCE      2
#define FR_BOOL_EXCLUSION       3
#define FR_BOOL_DIVISION        4

/* ---- 路径命令 ---- */
#define FR_MAX_PATH_COMMANDS    1024

typedef struct {
    uint8_t type;               /* 命令类型 */
    float x, y;                 /* 目标点 */
    float cx1, cy1;             /* 控制点1 */
    float cx2, cy2;             /* 控制点2 */
    float rx, ry;               /* 弧度参数 (弧线) */
    float rotation;             /* 旋转 (弧线) */
    int large_arc;              /* 大弧标志 */
    int sweep;                  /* 扫描标志 */
} fr_path_cmd_t;

/* ---- 路径结构 ---- */
typedef struct {
    fr_path_cmd_t commands[FR_MAX_PATH_COMMANDS];
    int cmd_count;
    float current_x, current_y; /* 当前点 */
    float start_x, start_y;     /* 子路径起始点 */
    int closed;                 /* 是否闭合 */
} fr_path_t;

/* ---- 描边样式 ---- */
typedef struct {
    float width;                /* 描边宽度 */
    uint8_t cap;                /* 端点样式 */
    uint8_t join;               /* 连接样式 */
    float miter_limit;          /* 斜接限制 */
    float dash_array[8];        /* 虚线数组 */
    int dash_count;             /* 虚线数组元素数 */
    float dash_offset;          /* 虚线偏移 */
    fr_color_t color;           /* 描边颜色 */
    uint8_t opacity;            /* 描边不透明度 */
} fr_stroke_style_t;

/* ---- 填充样式 ---- */
typedef struct {
    uint8_t fill_rule;          /* 填充规则 */
    fr_color_t color;           /* 填充颜色 */
    uint8_t opacity;            /* 填充不透明度 */
    void *gradient;             /* 渐变填充(可选) */
    void *pattern;              /* 图案填充(可选) */
} fr_fill_style_t;

/* ---- 路径变换矩阵 ---- */
typedef struct {
    float a, b, c, d;           /* 2x2 仿射矩阵 */
    float tx, ty;               /* 平移 */
} fr_path_matrix_t;

/* ================================================================
 *  API 函数声明
 * ================================================================ */

/* 路径创建与操作 */
fr_path_t *fr_path_create(void);
void fr_path_destroy(fr_path_t *path);
void fr_path_reset(fr_path_t *path);
fr_path_t *fr_path_clone(const fr_path_t *path);

/* 路径命令 */
void fr_path_move_to(fr_path_t *path, float x, float y);
void fr_path_line_to(fr_path_t *path, float x, float y);
void fr_path_curve_to(fr_path_t *path,
                       float cx1, float cy1, float cx2, float cy2,
                       float x, float y);
void fr_path_quad_to(fr_path_t *path,
                      float cx, float cy, float x, float y);
void fr_path_arc_to(fr_path_t *path,
                     float rx, float ry, float rotation,
                     int large_arc, int sweep,
                     float x, float y);
void fr_path_horizontal_to(fr_path_t *path, float x);
void fr_path_vertical_to(fr_path_t *path, float y);
void fr_path_smooth_curve_to(fr_path_t *path,
                              float cx2, float cy2, float x, float y);
void fr_path_smooth_quad_to(fr_path_t *path, float x, float y);
void fr_path_close(fr_path_t *path);

/* 高级路径形状 */
void fr_path_rect(fr_path_t *path, float x, float y, float w, float h);
void fr_path_rounded_rect(fr_path_t *path, float x, float y, float w, float h,
                           float rx, float ry);
void fr_path_ellipse(fr_path_t *path, float cx, float cy, float rx, float ry);
void fr_path_circle(fr_path_t *path, float cx, float cy, float r);
void fr_path_line(fr_path_t *path, float x1, float y1, float x2, float y2);
void fr_path_polygon(fr_path_t *path, const float *points, int count);
void fr_path_polyline(fr_path_t *path, const float *points, int count);
void fr_path_star(fr_path_t *path, float cx, float cy, int points,
                   float outer_r, float inner_r);
void fr_path_arc(fr_path_t *path, float cx, float cy, float r,
                  float start_angle, float end_angle);

/* 路径变换 */
void fr_path_transform(fr_path_t *path, const fr_path_matrix_t *matrix);
void fr_path_translate(fr_path_t *path, float tx, float ty);
void fr_path_scale(fr_path_t *path, float sx, float sy);
void fr_path_rotate(fr_path_t *path, float angle, float cx, float cy);

/* 路径渲染 */
void fr_path_fill(fr_path_t *path, struct fr_context *ctx,
                  const fr_fill_style_t *style);
void fr_path_stroke(fr_path_t *path, struct fr_context *ctx,
                    const fr_stroke_style_t *style);
void fr_path_draw(fr_path_t *path, struct fr_context *ctx,
                  const fr_fill_style_t *fill,
                  const fr_stroke_style_t *stroke);

/* 路径布尔运算 */
fr_path_t *fr_path_boolean(const fr_path_t *a, const fr_path_t *b,
                            uint8_t operation);
fr_path_t *fr_path_union(const fr_path_t *a, const fr_path_t *b);
fr_path_t *fr_path_intersection(const fr_path_t *a, const fr_path_t *b);
fr_path_t *fr_path_difference(const fr_path_t *a, const fr_path_t *b);
fr_path_t *fr_path_exclusion(const fr_path_t *a, const fr_path_t *b);

/* 路径查询 */
int fr_path_contains_point(const fr_path_t *path, float x, float y,
                            uint8_t fill_rule);
int fr_path_get_bounds(const fr_path_t *path,
                        float *x, float *y, float *w, float *h);
float fr_path_get_length(const fr_path_t *path);
int fr_path_is_empty(const fr_path_t *path);
int fr_path_is_closed(const fr_path_t *path);

/* 路径解析 (SVG 路径字符串) */
fr_path_t *fr_path_parse(const char *svg_path);
int fr_path_to_string(const fr_path_t *path, char *buf, int buf_size);

/* 实用函数 */
void fr_path_reverse(fr_path_t *path);
fr_path_t *fr_path_simplify(const fr_path_t *path, float tolerance);
fr_path_t *fr_path_offset(const fr_path_t *path, float distance);

/* 矩阵工具 */
fr_path_matrix_t fr_path_matrix_identity(void);
fr_path_matrix_t fr_path_matrix_translate(float tx, float ty);
fr_path_matrix_t fr_path_matrix_scale(float sx, float sy);
fr_path_matrix_t fr_path_matrix_rotate(float angle);
fr_path_matrix_t fr_path_matrix_multiply(const fr_path_matrix_t *a,
                                          const fr_path_matrix_t *b);

#endif /* FR_PATH_H */