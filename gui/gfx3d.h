/* gfx3d.h - 3D 图形库 */
#ifndef GFX3D_H
#define GFX3D_H

#include "gfx.h"
#include "math.h"

/* ---- 向量/矩阵类型 ---- */
typedef struct { float x, y, z; } vec3_t;
typedef struct { float x, y, z, w; } vec4_t;
typedef struct { float m[4][4]; } mat4_t;

/* ---- 向量运算 ---- */
vec3_t vec3_create(float x, float y, float z);
vec3_t vec3_add(vec3_t a, vec3_t b);
vec3_t vec3_sub(vec3_t a, vec3_t b);
vec3_t vec3_scale(vec3_t v, float s);
float  vec3_dot(vec3_t a, vec3_t b);
vec3_t vec3_cross(vec3_t a, vec3_t b);
float  vec3_length(vec3_t v);
vec3_t vec3_normalize(vec3_t v);

/* ---- 矩阵运算 ---- */
mat4_t mat4_identity(void);
mat4_t mat4_multiply(mat4_t a, mat4_t b);
mat4_t mat4_translate(float x, float y, float z);
mat4_t mat4_scale(float x, float y, float z);
mat4_t mat4_rotate_x(float angle);
mat4_t mat4_rotate_y(float angle);
mat4_t mat4_rotate_z(float angle);

/* ---- 投影矩阵 ---- */
mat4_t mat4_perspective(float fov, float aspect, float near, float far);
mat4_t mat4_orthographic(float left, float right, float bottom, float top, float near, float far);
mat4_t mat4_lookat(vec3_t eye, vec3_t center, vec3_t up);

/* ---- 变换 ---- */
vec4_t mat4_transform_vec4(mat4_t m, vec4_t v);
vec3_t mat4_transform_point(mat4_t m, vec3_t p);

/* ---- 3D 图元 ---- */
typedef struct {
    vec3_t v[3];           /* 三个顶点 */
    gfx_color_t color;     /* 三角形颜色 */
} triangle3d_t;

typedef struct {
    vec3_t pos;
    vec3_t normal;
    gfx_color_t color;
} vertex3d_t;

/* ---- 渲染 ---- */
void gfx3d_draw_triangle(gfx_context_t *ctx, triangle3d_t *tri, mat4_t mvp);
void gfx3d_draw_line3d(gfx_context_t *ctx, vec3_t a, vec3_t b, mat4_t mvp, gfx_color_t color);
void gfx3d_fill_triangle(gfx_context_t *ctx, triangle3d_t *tri, mat4_t mvp);

/* ---- Z-buffer ---- */
void gfx3d_zbuffer_init(gfx_context_t *ctx);
void gfx3d_zbuffer_clear(gfx_context_t *ctx);
void gfx3d_zbuffer_free(gfx_context_t *ctx);

/* ---- 光照 ---- */
typedef struct {
    vec3_t position;
    vec3_t direction;
    gfx_color_t color;
    float intensity;
    int type;  /* 0=方向光, 1=点光源 */
} light_t;

gfx_color_t gfx3d_compute_lighting(vec3_t normal, vec3_t pos, light_t *lights, int nlights, gfx_color_t base_color);

/* ---- 网格 ---- */
typedef struct {
    vertex3d_t *vertices;
    uint32_t    vertex_count;
    uint32_t   *indices;       /* 三角形索引 */
    uint32_t    index_count;
} mesh3d_t;

mesh3d_t *gfx3d_create_mesh(uint32_t verts, uint32_t indices);
void gfx3d_destroy_mesh(mesh3d_t *mesh);
void gfx3d_draw_mesh(gfx_context_t *ctx, mesh3d_t *mesh, mat4_t mvp, light_t *lights, int nlights);

/* ---- 预定义几何体 ---- */
mesh3d_t *gfx3d_create_cube(float size);
mesh3d_t *gfx3d_create_sphere(float radius, int segments, int rings);
mesh3d_t *gfx3d_create_plane(float width, float height);

#endif
