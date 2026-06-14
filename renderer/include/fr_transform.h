/* fr_transform.h - 2D 变换系统
 * 提供平移、旋转、缩放和变换矩阵栈,
 * 支持坐标系转换、包围盒计算和裁剪区域管理
 */

#ifndef FR_TRANSFORM_H
#define FR_TRANSFORM_H

#include "stdint.h"

/* ---- 定点数表示 ---- */

/*
 * 使用 32 位定点数: 高 16 位为整数部分, 低 16 位为小数部分。
 * 这提供了足够的精度用于 2D 渲染变换。
 */

/* 定点数类型 */
typedef int32_t fr_fixed_t;

/* 定点数常量 */
#define FR_FIXED_SHIFT      16
#define FR_FIXED_SCALE      (1 << FR_FIXED_SHIFT)  /* 65536 */
#define FR_FIXED_ONE        (1 << FR_FIXED_SHIFT)
#define FR_FIXED_HALF       (1 << (FR_FIXED_SHIFT - 1))
#define FR_FIXED_PI         205887  /* 3.14159 * 65536 */
#define FR_FIXED_2PI        411775  /* 2 * PI */
#define FR_FIXED_PI_2       102944  /* PI/2 */

/* 定点数转换宏 */
#define FR_FLOAT_TO_FIXED(f)   ((fr_fixed_t)((f) * FR_FIXED_SCALE))
#define FR_FIXED_TO_FLOAT(x)   ((float)(x) / FR_FIXED_SCALE)
#define FR_INT_TO_FIXED(i)     ((fr_fixed_t)((i) << FR_FIXED_SHIFT))
#define FR_FIXED_TO_INT(x)     ((int32_t)((x) >> FR_FIXED_SHIFT))
#define FR_FIXED_FRAC(x)       ((x) & 0xFFFF)

/* 定点数乘法 (64 位中间结果防止溢出) */
#define FR_FIXED_MUL(a, b) \
    ((fr_fixed_t)(((int64_t)(a) * (int64_t)(b)) >> FR_FIXED_SHIFT))

/* 定点数除法 */
#define FR_FIXED_DIV(a, b) \
    ((fr_fixed_t)(((int64_t)(a) << FR_FIXED_SHIFT) / (b)))

/* ---- 2D 点和矩形 ---- */

/* 2D 点 (定点数) */
typedef struct {
    fr_fixed_t x;
    fr_fixed_t y;
} fr_point_fixed_t;

/* 2D 点 (整数) */
typedef struct {
    int32_t x;
    int32_t y;
} fr_point_t;

/* 包围盒 (定点数) */
typedef struct {
    fr_fixed_t x;
    fr_fixed_t y;
    fr_fixed_t w;
    fr_fixed_t h;
} fr_bbox_fixed_t;

/* ---- 3x3 变换矩阵 ---- */

/*
 * 3x3 仿射变换矩阵 (行主序):
 *   | m[0] m[1] m[2] |   | sx  shy tx  |
 *   | m[3] m[4] m[5] | = | shx sy  ty  |
 *   | m[6] m[7] m[8] |   | 0   0   1   |
 *
 * 对于 2D 仿射变换, 最后一行始终为 [0, 0, 1],
 * 因此只需存储前 6 个分量 (m[0]..m[5]) 加上恒等最后一行。
 */

typedef struct {
    fr_fixed_t m[9];   /* 3x3 矩阵, 行主序, 定点数 */
} fr_matrix_t;

/* 恒等矩阵 */
extern const fr_matrix_t FR_MATRIX_IDENTITY;

/* 前缀声明 */
struct fr_context;

/* ---- 变换栈 ---- */

/* 变换栈最大深度 */
#define FR_TRANSFORM_STACK_MAX_DEPTH  32

/* 裁剪区域 */
typedef struct {
    int x, y, w, h;          /* 裁剪矩形 */
    int enabled;             /* 是否启用 */
} fr_clip_region_t;

/* 变换栈 */
typedef struct {
    fr_matrix_t stack[FR_TRANSFORM_STACK_MAX_DEPTH];
    int depth;               /* 当前深度 (0 = 基础变换) */

    /* 当前裁剪区域 */
    fr_clip_region_t clip;
    fr_clip_region_t clip_stack[FR_TRANSFORM_STACK_MAX_DEPTH];
} fr_transform_stack_t;

/* ---- 坐标系统类型 ---- */

/* 坐标空间 */
#define FR_COORD_SCREEN     0   /* 屏幕坐标 (帧缓冲坐标) */
#define FR_COORD_WIDGET     1   /* 控件坐标 (相对于父控件) */
#define FR_COORD_LOCAL      2   /* 局部坐标 (当前变换空间) */

/* ================================================================
 *  API 函数声明
 * ================================================================ */

/* ---- 基础变换函数 ---- */

/* 创建平移矩阵 */
void fr_transform_translate(fr_matrix_t *mat, int32_t dx, int32_t dy);

/* 创建旋转矩阵 (绕原点旋转, angle 为度数) */
void fr_transform_rotate(fr_matrix_t *mat, float angle_degrees);

/* 创建绕指定点旋转的矩阵 */
void fr_transform_rotate_around(fr_matrix_t *mat, float angle_degrees,
                                int32_t cx, int32_t cy);

/* 创建均匀缩放矩阵 */
void fr_transform_scale(fr_matrix_t *mat, float sx, float sy);

/* 创建非均匀缩放矩阵 */
void fr_transform_scale_nonuniform(fr_matrix_t *mat, float sx, float sy);

/* ---- 矩阵操作 ---- */

/* 矩阵乘法: result = a * b */
void fr_transform_matrix_multiply(fr_matrix_t *result,
                                  const fr_matrix_t *a,
                                  const fr_matrix_t *b);

/* 矩阵求逆 */
int fr_transform_matrix_inverse(fr_matrix_t *result,
                                const fr_matrix_t *mat);

/* 矩阵置为恒等矩阵 */
void fr_transform_matrix_identity(fr_matrix_t *mat);

/* ---- 变换栈 ---- */

/* 初始化变换栈 */
void fr_transform_stack_init(fr_transform_stack_t *stack);

/* 压入新变换层级 (复制当前变换, 供子控件使用) */
void fr_transform_stack_push(fr_transform_stack_t *stack);

/* 弹出变换层级 */
void fr_transform_stack_pop(fr_transform_stack_t *stack);

/* 获取栈顶变换矩阵 */
const fr_matrix_t *fr_transform_stack_top(fr_transform_stack_t *stack);

/* 应用变换到栈顶 (矩阵乘法) */
void fr_transform_stack_apply(fr_transform_stack_t *stack,
                              const fr_matrix_t *mat);

/* 重置栈顶到恒等变换 */
void fr_transform_stack_reset(fr_transform_stack_t *stack);

/* ---- 点变换 ---- */

/* 将一个点通过矩阵变换 */
void fr_transform_point(const fr_matrix_t *mat,
                        int32_t sx, int32_t sy,
                        int32_t *dx, int32_t *dy);

/* 将一个点通过矩阵变换 (定点数输入/输出) */
void fr_transform_point_fixed(const fr_matrix_t *mat,
                              fr_fixed_t sx, fr_fixed_t sy,
                              fr_fixed_t *dx, fr_fixed_t *dy);

/* 使用当前变换栈变换点 */
void fr_transform_stack_point(fr_transform_stack_t *stack,
                              int32_t sx, int32_t sy,
                              int32_t *dx, int32_t *dy);

/* ---- 包围盒计算 ---- */

/* 计算矩形经过变换后的包围盒 */
void fr_transform_bounding_box(const fr_matrix_t *mat,
                               int32_t x, int32_t y,
                               int32_t w, int32_t h,
                               int32_t *out_x, int32_t *out_y,
                               int32_t *out_w, int32_t *out_h);

/* 使用当前变换栈计算包围盒 */
void fr_transform_stack_bounding_box(fr_transform_stack_t *stack,
                                     int32_t x, int32_t y,
                                     int32_t w, int32_t h,
                                     int32_t *out_x, int32_t *out_y,
                                     int32_t *out_w, int32_t *out_h);

/* ---- 坐标系统转换 ---- */

/* 屏幕坐标转控件坐标 */
void fr_coord_screen_to_widget(int32_t screen_x, int32_t screen_y,
                               int32_t widget_x, int32_t widget_y,
                               int32_t *out_x, int32_t *out_y);

/* 控件坐标转屏幕坐标 */
void fr_coord_widget_to_screen(int32_t widget_local_x, int32_t widget_local_y,
                               int32_t widget_screen_x, int32_t widget_screen_y,
                               int32_t *out_x, int32_t *out_y);

/* 控件坐标转局部坐标 (应用当前变换) */
void fr_coord_widget_to_local(fr_transform_stack_t *stack,
                              int32_t wx, int32_t wy,
                              int32_t *lx, int32_t *ly);

/* 局部坐标转控件坐标 (应用逆变换) */
void fr_coord_local_to_widget(fr_transform_stack_t *stack,
                              int32_t lx, int32_t ly,
                              int32_t *wx, int32_t *wy);

/* ---- 裁剪区域管理 ---- */

/* 设置当前裁剪区域 */
void fr_clip_set(fr_clip_region_t *clip, int x, int y, int w, int h);

/* 禁用裁剪 */
void fr_clip_disable(fr_clip_region_t *clip);

/* 启用裁剪 */
void fr_clip_enable(fr_clip_region_t *clip);

/* 测试点是否在裁剪区域内 */
int fr_clip_test_point(const fr_clip_region_t *clip, int x, int y);

/* 测试矩形是否与裁剪区域相交 */
int fr_clip_test_rect(const fr_clip_region_t *clip,
                      int x, int y, int w, int h);

/* 求两个裁剪区域的交集 */
void fr_clip_intersect(fr_clip_region_t *result,
                       const fr_clip_region_t *a,
                       const fr_clip_region_t *b);

/* ---- 变换栈裁剪集成 ---- */

/* 压入变换层级并设置裁剪区域 */
void fr_transform_stack_push_clip(fr_transform_stack_t *stack,
                                  int x, int y, int w, int h);

/* 弹出变换层级并恢复裁剪 */
void fr_transform_stack_pop_clip(fr_transform_stack_t *stack);

#endif /* FR_TRANSFORM_H */