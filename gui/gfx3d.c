/* gfx3d.c - 3D 图形库实现
 * 软件渲染 3D 图形: 向量/矩阵运算、透视投影、三角形光栅化、
 * Z-buffer 深度测试、Phong 光照模型、预定义几何体
 */

#include "gfx3d.h"
#include "stdlib.h"
#include "string.h"
#include "math.h"

/* ---- Z-buffer 存储 ---- */
static float *zbuffer = NULL;
static uint32_t zb_width = 0;
static uint32_t zb_height = 0;

/* ---- 内联辅助函数 ---- */

static float float_clamp(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
static int32_t i32_min(int32_t a, int32_t b) { return a < b ? a : b; }
static int32_t i32_max(int32_t a, int32_t b) { return a > b ? a : b; }

/* ---- 向量运算 ---- */

vec3_t vec3_create(float x, float y, float z) {
    vec3_t v;
    v.x = x; v.y = y; v.z = z;
    return v;
}

vec3_t vec3_add(vec3_t a, vec3_t b) {
    vec3_t r;
    r.x = a.x + b.x;
    r.y = a.y + b.y;
    r.z = a.z + b.z;
    return r;
}

vec3_t vec3_sub(vec3_t a, vec3_t b) {
    vec3_t r;
    r.x = a.x - b.x;
    r.y = a.y - b.y;
    r.z = a.z - b.z;
    return r;
}

vec3_t vec3_scale(vec3_t v, float s) {
    vec3_t r;
    r.x = v.x * s;
    r.y = v.y * s;
    r.z = v.z * s;
    return r;
}

float vec3_dot(vec3_t a, vec3_t b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

vec3_t vec3_cross(vec3_t a, vec3_t b) {
    vec3_t r;
    r.x = a.y * b.z - a.z * b.y;
    r.y = a.z * b.x - a.x * b.z;
    r.z = a.x * b.y - a.y * b.x;
    return r;
}

float vec3_length(vec3_t v) {
    return (float)sqrt((double)(v.x * v.x + v.y * v.y + v.z * v.z));
}

vec3_t vec3_normalize(vec3_t v) {
    float len = vec3_length(v);
    if (len > 0.0001f) {
        float inv = 1.0f / len;
        v.x *= inv;
        v.y *= inv;
        v.z *= inv;
    }
    return v;
}

/* ---- 矩阵运算 ---- */

mat4_t mat4_identity(void) {
    mat4_t m;
    memset(&m, 0, sizeof(m));
    m.m[0][0] = 1.0f;
    m.m[1][1] = 1.0f;
    m.m[2][2] = 1.0f;
    m.m[3][3] = 1.0f;
    return m;
}

mat4_t mat4_multiply(mat4_t a, mat4_t b) {
    mat4_t r;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            r.m[i][j] = 0.0f;
            for (int k = 0; k < 4; k++) {
                r.m[i][j] += a.m[i][k] * b.m[k][j];
            }
        }
    }
    return r;
}

mat4_t mat4_translate(float x, float y, float z) {
    mat4_t m = mat4_identity();
    m.m[0][3] = x;
    m.m[1][3] = y;
    m.m[2][3] = z;
    return m;
}

mat4_t mat4_scale(float x, float y, float z) {
    mat4_t m = mat4_identity();
    m.m[0][0] = x;
    m.m[1][1] = y;
    m.m[2][2] = z;
    return m;
}

mat4_t mat4_rotate_x(float angle) {
    mat4_t m = mat4_identity();
    float c = (float)cos((double)angle);
    float s = (float)sin((double)angle);
    m.m[1][1] = c;
    m.m[1][2] = -s;
    m.m[2][1] = s;
    m.m[2][2] = c;
    return m;
}

mat4_t mat4_rotate_y(float angle) {
    mat4_t m = mat4_identity();
    float c = (float)cos((double)angle);
    float s = (float)sin((double)angle);
    m.m[0][0] = c;
    m.m[0][2] = s;
    m.m[2][0] = -s;
    m.m[2][2] = c;
    return m;
}

mat4_t mat4_rotate_z(float angle) {
    mat4_t m = mat4_identity();
    float c = (float)cos((double)angle);
    float s = (float)sin((double)angle);
    m.m[0][0] = c;
    m.m[0][1] = -s;
    m.m[1][0] = s;
    m.m[1][1] = c;
    return m;
}

/* ---- 投影矩阵 ---- */

mat4_t mat4_perspective(float fov, float aspect, float near_val, float far_val) {
    mat4_t m;
    memset(&m, 0, sizeof(m));

    float f = 1.0f / (float)tan((double)(fov / 2.0f));
    m.m[0][0] = f / aspect;
    m.m[1][1] = f;
    m.m[2][2] = (far_val + near_val) / (near_val - far_val);
    m.m[2][3] = (2.0f * far_val * near_val) / (near_val - far_val);
    m.m[3][2] = -1.0f;

    return m;
}

mat4_t mat4_orthographic(float left, float right, float bottom, float top, float near_val, float far_val) {
    mat4_t m = mat4_identity();

    m.m[0][0] = 2.0f / (right - left);
    m.m[1][1] = 2.0f / (top - bottom);
    m.m[2][2] = -2.0f / (far_val - near_val);
    m.m[0][3] = -(right + left) / (right - left);
    m.m[1][3] = -(top + bottom) / (top - bottom);
    m.m[2][3] = -(far_val + near_val) / (far_val - near_val);

    return m;
}

mat4_t mat4_lookat(vec3_t eye, vec3_t center, vec3_t up) {
    vec3_t f = vec3_normalize(vec3_sub(center, eye));
    vec3_t s = vec3_normalize(vec3_cross(f, up));
    vec3_t u = vec3_cross(s, f);

    mat4_t m = mat4_identity();
    m.m[0][0] = s.x;
    m.m[0][1] = s.y;
    m.m[0][2] = s.z;
    m.m[0][3] = -vec3_dot(s, eye);
    m.m[1][0] = u.x;
    m.m[1][1] = u.y;
    m.m[1][2] = u.z;
    m.m[1][3] = -vec3_dot(u, eye);
    m.m[2][0] = -f.x;
    m.m[2][1] = -f.y;
    m.m[2][2] = -f.z;
    m.m[2][3] = vec3_dot(f, eye);

    return m;
}

/* ---- 变换 ---- */

vec4_t mat4_transform_vec4(mat4_t m, vec4_t v) {
    vec4_t r;
    r.x = m.m[0][0] * v.x + m.m[0][1] * v.y + m.m[0][2] * v.z + m.m[0][3] * v.w;
    r.y = m.m[1][0] * v.x + m.m[1][1] * v.y + m.m[1][2] * v.z + m.m[1][3] * v.w;
    r.z = m.m[2][0] * v.x + m.m[2][1] * v.y + m.m[2][2] * v.z + m.m[2][3] * v.w;
    r.w = m.m[3][0] * v.x + m.m[3][1] * v.y + m.m[3][2] * v.z + m.m[3][3] * v.w;
    return r;
}

vec3_t mat4_transform_point(mat4_t m, vec3_t p) {
    vec4_t v;
    v.x = p.x; v.y = p.y; v.z = p.z; v.w = 1.0f;
    vec4_t r = mat4_transform_vec4(m, v);
    /* 透视除法 */
    if (r.w != 0.0f) {
        r.x /= r.w;
        r.y /= r.w;
        r.z /= r.w;
    }
    vec3_t result;
    result.x = r.x;
    result.y = r.y;
    result.z = r.z;
    return result;
}

/* ---- 视口变换: NDC [-1,1] -> 屏幕坐标 ---- */

static vec3_t viewport_transform(vec3_t ndc, uint32_t width, uint32_t height) {
    vec3_t screen;
    screen.x = (ndc.x + 1.0f) * 0.5f * (float)width;
    screen.y = (1.0f - ndc.y) * 0.5f * (float)height; /* Y 轴翻转 */
    screen.z = ndc.z; /* 保留深度 */
    return screen;
}

/* ---- Z-buffer 操作 ---- */

void gfx3d_zbuffer_init(gfx_context_t *ctx) {
    if (zbuffer) {
        gfx3d_zbuffer_free(ctx);
    }
    zb_width = ctx->width;
    zb_height = ctx->height;
    zbuffer = (float *)malloc(zb_width * zb_height * sizeof(float));
    if (zbuffer) {
        gfx3d_zbuffer_clear(ctx);
    }
}

void gfx3d_zbuffer_clear(gfx_context_t *ctx) {
    (void)ctx;
    if (zbuffer && zb_width > 0 && zb_height > 0) {
        for (uint32_t i = 0; i < zb_width * zb_height; i++) {
            zbuffer[i] = 1.0f; /* 最远深度 */
        }
    }
}

void gfx3d_zbuffer_free(gfx_context_t *ctx) {
    (void)ctx;
    if (zbuffer) {
        free(zbuffer);
        zbuffer = NULL;
    }
    zb_width = 0;
    zb_height = 0;
}

static int zbuffer_test(int32_t x, int32_t y, float z) {
    if (!zbuffer || x < 0 || x >= (int32_t)zb_width || y < 0 || y >= (int32_t)zb_height) {
        return 0;
    }
    uint32_t idx = (uint32_t)y * zb_width + (uint32_t)x;
    if (z < zbuffer[idx]) {
        zbuffer[idx] = z;
        return 1;
    }
    return 0;
}

/* ---- 渲染 ---- */

void gfx3d_draw_line3d(gfx_context_t *ctx, vec3_t a, vec3_t b, mat4_t mvp, gfx_color_t color) {
    vec3_t pa = mat4_transform_point(mvp, a);
    vec3_t pb = mat4_transform_point(mvp, b);

    /* 裁剪: 简单丢弃超出近平面的线段 */
    if (pa.z < -1.0f || pa.z > 1.0f || pb.z < -1.0f || pb.z > 1.0f) return;

    vec3_t sa = viewport_transform(pa, ctx->width, ctx->height);
    vec3_t sb = viewport_transform(pb, ctx->width, ctx->height);

    gfx_draw_line(ctx, (int32_t)sa.x, (int32_t)sa.y, (int32_t)sb.x, (int32_t)sb.y, color);
}

void gfx3d_draw_triangle(gfx_context_t *ctx, triangle3d_t *tri, mat4_t mvp) {
    for (int i = 0; i < 3; i++) {
        int j = (i + 1) % 3;
        gfx3d_draw_line3d(ctx, tri->v[i], tri->v[j], mvp, tri->color);
    }
}

/* 扫描线三角形光栅化 (带 Z-buffer) */
void gfx3d_fill_triangle(gfx_context_t *ctx, triangle3d_t *tri, mat4_t mvp) {
    /* 变换顶点到裁剪空间 */
    vec3_t p[3];
    for (int i = 0; i < 3; i++) {
        p[i] = mat4_transform_point(mvp, tri->v[i]);
    }

    /* 简单近平面裁剪 */
    if (p[0].z < -1.0f || p[0].z > 1.0f) return;
    if (p[1].z < -1.0f || p[1].z > 1.0f) return;
    if (p[2].z < -1.0f || p[2].z > 1.0f) return;

    /* 视口变换 */
    vec3_t s[3];
    for (int i = 0; i < 3; i++) {
        s[i] = viewport_transform(p[i], ctx->width, ctx->height);
    }

    /* 按Y坐标排序顶点: s[0].y <= s[1].y <= s[2].y */
    if (s[0].y > s[1].y) { vec3_t t = s[0]; s[0] = s[1]; s[1] = t; }
    if (s[1].y > s[2].y) { vec3_t t = s[1]; s[1] = s[2]; s[2] = t; }
    if (s[0].y > s[1].y) { vec3_t t = s[0]; s[0] = s[1]; s[1] = t; }

    int32_t y0 = (int32_t)s[0].y;
    int32_t y1 = (int32_t)s[1].y;
    int32_t y2 = (int32_t)s[2].y;

    if (y0 == y2) return; /* 退化为点 */

    float total_height = s[2].y - s[0].y;
    if (total_height < 0.001f) return;

    /* 扫描线填充 */
    for (int32_t y = i32_max(y0, 0); y <= i32_min(y2, (int32_t)ctx->height - 1); y++) {
        int second_half = (y > y1) || (y1 == y0);
        float segment_height = second_half ? (s[2].y - s[1].y) : (s[1].y - s[0].y);
        if (segment_height < 0.001f) segment_height = 0.001f;

        float alpha = (float)(y - y0) / total_height;
        float beta = second_half ?
            (float)(y - y1) / segment_height :
            (float)(y - y0) / segment_height;

        /* 插值 X 和 Z */
        float ax = s[0].x + (s[2].x - s[0].x) * alpha;
        float az = s[0].z + (s[2].z - s[0].z) * alpha;

        float bx, bz;
        if (second_half) {
            bx = s[1].x + (s[2].x - s[1].x) * beta;
            bz = s[1].z + (s[2].z - s[1].z) * beta;
        } else {
            bx = s[0].x + (s[1].x - s[0].x) * beta;
            bz = s[0].z + (s[1].z - s[0].z) * beta;
        }

        if (ax > bx) {
            float tmp;
            tmp = ax; ax = bx; bx = tmp;
            tmp = az; az = bz; bz = tmp;
        }

        int32_t x_start = i32_max((int32_t)ax, 0);
        int32_t x_end = i32_min((int32_t)bx, (int32_t)ctx->width - 1);
        float span = bx - ax;
        if (span < 0.001f) span = 0.001f;

        for (int32_t x = x_start; x <= x_end; x++) {
            float t = (float)(x - ax) / span;
            float z = az + (bz - az) * t;

            if (zbuffer_test(x, y, z)) {
                gfx_set_pixel(ctx, x, y, tri->color);
            }
        }
    }
}

/* ---- 光照 ---- */

gfx_color_t gfx3d_compute_lighting(vec3_t normal, vec3_t pos, light_t *lights, int nlights, gfx_color_t base_color) {
    float r = (float)((base_color >> 16) & 0xFF);
    float g = (float)((base_color >> 8) & 0xFF);
    float b = (float)(base_color & 0xFF);

    /* 环境光 */
    float ambient_r = r * 0.1f;
    float ambient_g = g * 0.1f;
    float ambient_b = b * 0.1f;

    float total_r = ambient_r;
    float total_g = ambient_g;
    float total_b = ambient_b;

    vec3_t n = vec3_normalize(normal);

    for (int i = 0; i < nlights; i++) {
        light_t *light = &lights[i];
        float lr = (float)((light->color >> 16) & 0xFF);
        float lg = (float)((light->color >> 8) & 0xFF);
        float lb = (float)(light->color & 0xFF);

        vec3_t light_dir;
        float attenuation = 1.0f;

        if (light->type == 0) {
            /* 方向光 */
            light_dir = vec3_normalize(vec3_scale(light->direction, -1.0f));
        } else {
            /* 点光源 */
            vec3_t to_light = vec3_sub(light->position, pos);
            float dist = vec3_length(to_light);
            light_dir = vec3_normalize(to_light);
            /* 简单距离衰减 */
            attenuation = light->intensity / (1.0f + 0.1f * dist * dist);
        }

        /* 漫反射 (Lambert) */
        float ndotl = vec3_dot(n, light_dir);
        if (ndotl < 0.0f) ndotl = 0.0f;

        float diffuse = ndotl * light->intensity * attenuation;

        /* 镜面反射 (Phong) */
        vec3_t view_dir = vec3_normalize(vec3_scale(pos, -1.0f));
        vec3_t reflect = vec3_sub(vec3_scale(n, 2.0f * ndotl), light_dir);
        reflect = vec3_normalize(reflect);
        float rdotv = vec3_dot(reflect, view_dir);
        if (rdotv < 0.0f) rdotv = 0.0f;
        float specular = pow((double)rdotv, 32.0) * light->intensity * attenuation;

        total_r += r * diffuse * lr / 255.0f + specular * lr / 255.0f;
        total_g += g * diffuse * lg / 255.0f + specular * lg / 255.0f;
        total_b += b * diffuse * lb / 255.0f + specular * lb / 255.0f;
    }

    /* 钳位到 [0, 255] */
    uint32_t fr = (uint32_t)float_clamp(total_r, 0.0f, 255.0f);
    uint32_t fg = (uint32_t)float_clamp(total_g, 0.0f, 255.0f);
    uint32_t fb = (uint32_t)float_clamp(total_b, 0.0f, 255.0f);

    return (fr << 16) | (fg << 8) | fb;
}

/* ---- 网格操作 ---- */

mesh3d_t *gfx3d_create_mesh(uint32_t verts, uint32_t indices) {
    mesh3d_t *mesh = (mesh3d_t *)malloc(sizeof(mesh3d_t));
    if (!mesh) return NULL;

    mesh->vertices = (vertex3d_t *)malloc(verts * sizeof(vertex3d_t));
    mesh->indices = (uint32_t *)malloc(indices * sizeof(uint32_t));
    mesh->vertex_count = verts;
    mesh->index_count = indices;

    if (!mesh->vertices || !mesh->indices) {
        gfx3d_destroy_mesh(mesh);
        return NULL;
    }

    memset(mesh->vertices, 0, verts * sizeof(vertex3d_t));
    memset(mesh->indices, 0, indices * sizeof(uint32_t));

    return mesh;
}

void gfx3d_destroy_mesh(mesh3d_t *mesh) {
    if (!mesh) return;
    if (mesh->vertices) free(mesh->vertices);
    if (mesh->indices) free(mesh->indices);
    free(mesh);
}

void gfx3d_draw_mesh(gfx_context_t *ctx, mesh3d_t *mesh, mat4_t mvp, light_t *lights, int nlights) {
    if (!ctx || !mesh) return;

    /* 初始化 Z-buffer (如果尚未初始化) */
    if (!zbuffer || zb_width != ctx->width || zb_height != ctx->height) {
        gfx3d_zbuffer_init(ctx);
    }

    /* 遍历所有三角形 */
    for (uint32_t i = 0; i < mesh->index_count; i += 3) {
        if (i + 2 >= mesh->index_count) break;

        uint32_t i0 = mesh->indices[i];
        uint32_t i1 = mesh->indices[i + 1];
        uint32_t i2 = mesh->indices[i + 2];

        if (i0 >= mesh->vertex_count || i1 >= mesh->vertex_count || i2 >= mesh->vertex_count) continue;

        vertex3d_t *v0 = &mesh->vertices[i0];
        vertex3d_t *v1 = &mesh->vertices[i1];
        vertex3d_t *v2 = &mesh->vertices[i2];

        /* 计算光照 */
        vec3_t face_center;
        face_center.x = (v0->pos.x + v1->pos.x + v2->pos.x) / 3.0f;
        face_center.y = (v0->pos.y + v1->pos.y + v2->pos.y) / 3.0f;
        face_center.z = (v0->pos.z + v1->pos.z + v2->pos.z) / 3.0f;

        vec3_t face_normal;
        face_normal.x = (v0->normal.x + v1->normal.x + v2->normal.x) / 3.0f;
        face_normal.y = (v0->normal.y + v1->normal.y + v2->normal.y) / 3.0f;
        face_normal.z = (v0->normal.z + v1->normal.z + v2->normal.z) / 3.0f;

        gfx_color_t color = v0->color;
        if (lights && nlights > 0) {
            color = gfx3d_compute_lighting(face_normal, face_center, lights, nlights, v0->color);
        }

        triangle3d_t tri;
        tri.v[0] = v0->pos;
        tri.v[1] = v1->pos;
        tri.v[2] = v2->pos;
        tri.color = color;

        gfx3d_fill_triangle(ctx, &tri, mvp);
    }
}

/* ---- 预定义几何体 ---- */

mesh3d_t *gfx3d_create_cube(float size) {
    float hs = size / 2.0f;

    /* 立方体: 24 个顶点 (每面 4 个独立法线), 36 个索引 */
    mesh3d_t *mesh = gfx3d_create_mesh(24, 36);
    if (!mesh) return NULL;

    /* 定义每个面的 4 个顶点 */
    /* 前面 (z+) */
    mesh->vertices[0]  = (vertex3d_t){ { -hs, -hs,  hs }, { 0, 0, 1 }, 0xFFFFFF };
    mesh->vertices[1]  = (vertex3d_t){ {  hs, -hs,  hs }, { 0, 0, 1 }, 0xFFFFFF };
    mesh->vertices[2]  = (vertex3d_t){ {  hs,  hs,  hs }, { 0, 0, 1 }, 0xFFFFFF };
    mesh->vertices[3]  = (vertex3d_t){ { -hs,  hs,  hs }, { 0, 0, 1 }, 0xFFFFFF };
    /* 后面 (z-) */
    mesh->vertices[4]  = (vertex3d_t){ {  hs, -hs, -hs }, { 0, 0, -1 }, 0xCCCCCC };
    mesh->vertices[5]  = (vertex3d_t){ { -hs, -hs, -hs }, { 0, 0, -1 }, 0xCCCCCC };
    mesh->vertices[6]  = (vertex3d_t){ { -hs,  hs, -hs }, { 0, 0, -1 }, 0xCCCCCC };
    mesh->vertices[7]  = (vertex3d_t){ {  hs,  hs, -hs }, { 0, 0, -1 }, 0xCCCCCC };
    /* 上面 (y+) */
    mesh->vertices[8]  = (vertex3d_t){ { -hs,  hs,  hs }, { 0, 1, 0 }, 0xAAAAAA };
    mesh->vertices[9]  = (vertex3d_t){ {  hs,  hs,  hs }, { 0, 1, 0 }, 0xAAAAAA };
    mesh->vertices[10] = (vertex3d_t){ {  hs,  hs, -hs }, { 0, 1, 0 }, 0xAAAAAA };
    mesh->vertices[11] = (vertex3d_t){ { -hs,  hs, -hs }, { 0, 1, 0 }, 0xAAAAAA };
    /* 下面 (y-) */
    mesh->vertices[12] = (vertex3d_t){ { -hs, -hs, -hs }, { 0, -1, 0 }, 0x888888 };
    mesh->vertices[13] = (vertex3d_t){ {  hs, -hs, -hs }, { 0, -1, 0 }, 0x888888 };
    mesh->vertices[14] = (vertex3d_t){ {  hs, -hs,  hs }, { 0, -1, 0 }, 0x888888 };
    mesh->vertices[15] = (vertex3d_t){ { -hs, -hs,  hs }, { 0, -1, 0 }, 0x888888 };
    /* 右面 (x+) */
    mesh->vertices[16] = (vertex3d_t){ {  hs, -hs,  hs }, { 1, 0, 0 }, 0xFF8888 };
    mesh->vertices[17] = (vertex3d_t){ {  hs, -hs, -hs }, { 1, 0, 0 }, 0xFF8888 };
    mesh->vertices[18] = (vertex3d_t){ {  hs,  hs, -hs }, { 1, 0, 0 }, 0xFF8888 };
    mesh->vertices[19] = (vertex3d_t){ {  hs,  hs,  hs }, { 1, 0, 0 }, 0xFF8888 };
    /* 左面 (x-) */
    mesh->vertices[20] = (vertex3d_t){ { -hs, -hs, -hs }, { -1, 0, 0 }, 0x8888FF };
    mesh->vertices[21] = (vertex3d_t){ { -hs, -hs,  hs }, { -1, 0, 0 }, 0x8888FF };
    mesh->vertices[22] = (vertex3d_t){ { -hs,  hs,  hs }, { -1, 0, 0 }, 0x8888FF };
    mesh->vertices[23] = (vertex3d_t){ { -hs,  hs, -hs }, { -1, 0, 0 }, 0x8888FF };

    /* 索引 - 每面 2 个三角形 */
    uint32_t idx = 0;
    for (uint32_t face = 0; face < 6; face++) {
        uint32_t base = face * 4;
        mesh->indices[idx++] = base + 0;
        mesh->indices[idx++] = base + 1;
        mesh->indices[idx++] = base + 2;
        mesh->indices[idx++] = base + 0;
        mesh->indices[idx++] = base + 2;
        mesh->indices[idx++] = base + 3;
    }

    return mesh;
}

mesh3d_t *gfx3d_create_sphere(float radius, int segments, int rings) {
    if (segments < 4) segments = 4;
    if (rings < 2) rings = 2;

    uint32_t vert_count = (uint32_t)((rings + 1) * (segments + 1));
    uint32_t idx_count = (uint32_t)(rings * segments * 6);

    mesh3d_t *mesh = gfx3d_create_mesh(vert_count, idx_count);
    if (!mesh) return NULL;

    /* 生成顶点 */
    for (int r = 0; r <= rings; r++) {
        float phi = 3.14159265f * (float)r / (float)rings;
        float sin_phi = (float)sin((double)phi);
        float cos_phi = (float)cos((double)phi);

        for (int s = 0; s <= segments; s++) {
            float theta = 2.0f * 3.14159265f * (float)s / (float)segments;
            float sin_theta = (float)sin((double)theta);
            float cos_theta = (float)cos((double)theta);

            uint32_t vi = (uint32_t)(r * (segments + 1) + s);
            vec3_t pos;
            pos.x = radius * sin_phi * cos_theta;
            pos.y = radius * cos_phi;
            pos.z = radius * sin_phi * sin_theta;

            vec3_t normal = vec3_normalize(pos);

            mesh->vertices[vi].pos = pos;
            mesh->vertices[vi].normal = normal;
            mesh->vertices[vi].color = 0x44AAFF;
        }
    }

    /* 生成索引 */
    uint32_t idx = 0;
    for (int r = 0; r < rings; r++) {
        for (int s = 0; s < segments; s++) {
            uint32_t i0 = (uint32_t)(r * (segments + 1) + s);
            uint32_t i1 = (uint32_t)(r * (segments + 1) + s + 1);
            uint32_t i2 = (uint32_t)((r + 1) * (segments + 1) + s);
            uint32_t i3 = (uint32_t)((r + 1) * (segments + 1) + s + 1);

            mesh->indices[idx++] = i0;
            mesh->indices[idx++] = i1;
            mesh->indices[idx++] = i2;
            mesh->indices[idx++] = i1;
            mesh->indices[idx++] = i3;
            mesh->indices[idx++] = i2;
        }
    }

    return mesh;
}

mesh3d_t *gfx3d_create_plane(float width, float height) {
    float hw = width / 2.0f;
    float hh = height / 2.0f;

    mesh3d_t *mesh = gfx3d_create_mesh(4, 6);
    if (!mesh) return NULL;

    mesh->vertices[0] = (vertex3d_t){ { -hw, 0, -hh }, { 0, 1, 0 }, 0x44FF44 };
    mesh->vertices[1] = (vertex3d_t){ {  hw, 0, -hh }, { 0, 1, 0 }, 0x44FF44 };
    mesh->vertices[2] = (vertex3d_t){ {  hw, 0,  hh }, { 0, 1, 0 }, 0x44FF44 };
    mesh->vertices[3] = (vertex3d_t){ { -hw, 0,  hh }, { 0, 1, 0 }, 0x44FF44 };

    mesh->indices[0] = 0;
    mesh->indices[1] = 1;
    mesh->indices[2] = 2;
    mesh->indices[3] = 0;
    mesh->indices[4] = 2;
    mesh->indices[5] = 3;

    return mesh;
}
