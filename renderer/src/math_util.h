/* math_util.h - 软件实现的浮点数学库
 * 提供 sin/cos/sqrt 等三角函数、向量运算和 4x4 矩阵运算
 * 不依赖外部数学库，全部自实现
 */

#ifndef FR_MATH_UTIL_H
#define FR_MATH_UTIL_H

#include "stdint.h"

#ifndef M_PI_F
#define M_PI_F  3.14159265358979323846f
#define M_PI_2_F 1.57079632679489661923f
#endif

/* ================================================================
 *  软件实现的三角函数 (Taylor 级数展开)
 * ================================================================ */

float fr_sin(float rad);
float fr_cos(float rad);
float fr_sqrt(float x);
float fr_atan2(float y, float x);
float fr_fabsf(float x);
float fr_floorf(float x);
float fr_ceilf(float x);
float fr_powf(float base, float exp);
float fr_log2f(float x);

/* 角度转换 */
static inline float fr_deg2rad(float deg) { return deg * M_PI_F / 180.0f; }
static inline float fr_rad2deg(float rad) { return rad * 180.0f / M_PI_F; }

/* ================================================================
 *  向量类型和运算 - 2D
 * ================================================================ */

typedef struct { float x, y; } vec2_t;

vec2_t fr_vec2_add(vec2_t a, vec2_t b);
vec2_t fr_vec2_sub(vec2_t a, vec2_t b);
vec2_t fr_vec2_mul(vec2_t v, float s);
float  fr_vec2_dot(vec2_t a, vec2_t b);
float  fr_vec2_len(vec2_t v);
vec2_t fr_vec2_norm(vec2_t v);

/* ================================================================
 *  向量类型和运算 - 3D
 * ================================================================ */

typedef struct { float x, y, z; } vec3_t;

vec3_t fr_vec3_add(vec3_t a, vec3_t b);
vec3_t fr_vec3_sub(vec3_t a, vec3_t b);
vec3_t fr_vec3_mul(vec3_t v, float s);
float  fr_vec3_dot(vec3_t a, vec3_t b);
vec3_t fr_vec3_cross(vec3_t a, vec3_t b);
float  fr_vec3_len(vec3_t v);
vec3_t fr_vec3_norm(vec3_t v);

/* ================================================================
 *  4x4 矩阵运算 (列主序)
 *
 *  内存布局: m[col*4 + row]
 *  索引映射:
 *   [0]  [4]  [8]  [12]   <- 行0
 *   [1]  [5]  [9]  [13]   <- 行1
 *   [2]  [6]  [10] [14]   <- 行2
 *   [3]  [7]  [11] [15]   <- 行3
 * ================================================================ */

typedef struct { float m[16]; } mat4_t;

mat4_t fr_mat4_identity(void);
mat4_t fr_mat4_mul(mat4_t a, mat4_t b);

/* 基本变换矩阵 */
mat4_t fr_mat4_translate(float x, float y, float z);
mat4_t fr_mat4_scale(float x, float y, float z);
mat4_t fr_mat4_rotate_x(float rad);  /* 使用正确的 sin/cos */
mat4_t fr_mat4_rotate_y(float rad);
mat4_t fr_mat4_rotate_z(float rad);

/* 投影矩阵 */
mat4_t fr_mat4_perspective(float fov_deg, float aspect, float near, float far);
mat4_t fr_mat4_ortho(float left, float right, float bottom, float top,
                   float near_val, float far_val);
mat4_t fr_mat4_lookat(vec3_t eye, vec3_t center, vec3_t up);

/* 向量-矩阵乘法 (齐次坐标变换) */
vec3_t fr_mat4_mul_vec3(mat4_t m, vec3_t v);    /* w=1: 点变换 */
vec3_t fr_mat4_mul_dir(mat4_t m, vec3_t v);     /* w=0: 方向变换 */

/* 矩阵工具函数 */
mat4_t fr_mat4_inverse(mat4_t m);
mat4_t fr_mat4_transpose(mat4_t mat);
mat4_t fr_mat4_frustum(float left, float right, float bottom, float top,
                     float near_val, float far_val);

#endif /* FR_MATH_UTIL_H */
