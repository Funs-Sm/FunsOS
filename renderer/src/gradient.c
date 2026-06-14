/* gradient.c - 高级渐变渲染实现
 * 实现线性、径向、锥形、网格渐变及抖动
 */

#include "funrender.h"
#include "fr_context.h"
#include "fr_gradient.h"
#include "string.h"
#include "../kernel/kheap.h"
#include "../lib/stdio.h"
#include "../lib/math.h"

/* Missing math helpers */
static float fr_absf(float x) { return x < 0.0f ? -x : x; }

/* ---- 内部辅助函数 ---- */

static float fr_lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

static uint8_t fr_lerp_u8(uint8_t a, uint8_t b, float t)
{
    float val = (float)a + ((float)b - (float)a) * t;
    if (val < 0.0f) return 0;
    if (val > 255.0f) return 255;
    return (uint8_t)val;
}

static float fr_clampf(float v, float min, float max)
{
    if (v < min) return min;
    if (v > max) return max;
    return v;
}

static float fr_grad_get_t(fr_gradient_t *grad, float t, uint32_t extend_mode)
{
    switch (extend_mode) {
    case FR_GRAD_EXTEND_REPEAT:
        t = t - (float)(int)t;
        if (t < 0.0f) t += 1.0f;
        break;
    case FR_GRAD_EXTEND_REFLECT: {
        int n = (int)t;
        float frac = t - (float)n;
        if (n & 1) frac = 1.0f - frac;
        t = frac;
        break;
    }
    default:
        t = fr_clampf(t, 0.0f, 1.0f);
        break;
    }
    return t;
}

/* ---- 渐变管理 ---- */

fr_gradient_t *fr_gradient_create(uint32_t type)
{
    fr_gradient_t *grad = (fr_gradient_t *)fr_alloc(sizeof(fr_gradient_t));
    if (!grad) return NULL;
    memset(grad, 0, sizeof(fr_gradient_t));
    grad->type = type;
    grad->extend_mode = FR_GRAD_EXTEND_PAD;
    grad->colorspace = FR_GRAD_COLORSPACE_RGB;
    grad->dither = 0;
    fr_gradient_set_identity_transform(grad);
    return grad;
}

void fr_gradient_destroy(fr_gradient_t *grad)
{
    if (grad) fr_free(grad);
}

void fr_gradient_reset(fr_gradient_t *grad)
{
    if (!grad) return;
    memset(grad, 0, sizeof(fr_gradient_t));
    fr_gradient_set_identity_transform(grad);
}

/* ---- 颜色停止点 ---- */

int fr_gradient_add_stop(fr_gradient_t *grad, float position, fr_color_t color)
{
    if (!grad || grad->stop_count >= FR_MAX_GRADIENT_STOPS) return -1;

    /* 保持排序 */
    int insert_at = (int)grad->stop_count;
    for (uint32_t i = 0; i < grad->stop_count; i++) {
        if (position < grad->stops[i].position) {
            insert_at = (int)i;
            break;
        }
    }

    /* 后移 */
    for (int i = (int)grad->stop_count; i > insert_at; i--) {
        grad->stops[i] = grad->stops[i - 1];
    }

    grad->stops[insert_at].position = position;
    grad->stops[insert_at].color = color;
    grad->stops[insert_at].midpoint = 0.5f;
    grad->stop_count++;
    return 0;
}

int fr_gradient_add_stop_rgba(fr_gradient_t *grad, float position,
                               uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    fr_color_t color = {r, g, b, a};
    return fr_gradient_add_stop(grad, position, color);
}

int fr_gradient_remove_stop(fr_gradient_t *grad, int index)
{
    if (!grad || index < 0 || (uint32_t)index >= grad->stop_count) return -1;
    for (uint32_t i = (uint32_t)index; i < grad->stop_count - 1; i++) {
        grad->stops[i] = grad->stops[i + 1];
    }
    grad->stop_count--;
    return 0;
}

void fr_gradient_clear_stops(fr_gradient_t *grad)
{
    if (grad) grad->stop_count = 0;
}

int fr_gradient_set_stop_midpoint(fr_gradient_t *grad, int index, float mid)
{
    if (!grad || index < 0 || (uint32_t)index >= grad->stop_count) return -1;
    grad->stops[index].midpoint = mid;
    return 0;
}

int fr_gradient_get_stop_count(const fr_gradient_t *grad)
{
    return grad ? (int)grad->stop_count : 0;
}

/* ---- 渐变参数设置 ---- */

void fr_gradient_set_linear(fr_gradient_t *grad, float x1, float y1, float x2, float y2)
{
    if (!grad) return;
    grad->type = FR_GRAD_TYPE_LINEAR;
    grad->params.linear.x1 = x1;
    grad->params.linear.y1 = y1;
    grad->params.linear.x2 = x2;
    grad->params.linear.y2 = y2;
}

void fr_gradient_set_radial(fr_gradient_t *grad, float cx, float cy, float fx, float fy, float r)
{
    if (!grad) return;
    grad->type = FR_GRAD_TYPE_RADIAL;
    grad->params.radial.cx = cx;
    grad->params.radial.cy = cy;
    grad->params.radial.fx = fx;
    grad->params.radial.fy = fy;
    grad->params.radial.radius = r;
}

void fr_gradient_set_conical(fr_gradient_t *grad, float cx, float cy,
                              float start_angle, float end_angle)
{
    if (!grad) return;
    grad->type = FR_GRAD_TYPE_CONICAL;
    grad->params.conical.cx = cx;
    grad->params.conical.cy = cy;
    grad->params.conical.start_angle = start_angle;
    grad->params.conical.end_angle = end_angle;
}

void fr_gradient_set_mesh(fr_gradient_t *grad, int cols, int rows,
                           float x, float y, float w, float h)
{
    if (!grad) return;
    grad->type = FR_GRAD_TYPE_MESH;
    grad->params.mesh.cols = cols;
    grad->params.mesh.rows = rows;
    grad->params.mesh.x = x;
    grad->params.mesh.y = y;
    grad->params.mesh.w = w;
    grad->params.mesh.h = h;
}

void fr_gradient_set_mesh_point(fr_gradient_t *grad, int col, int row,
                                 float x, float y, fr_color_t color)
{
    if (!grad || col >= grad->params.mesh.cols || row >= grad->params.mesh.rows) return;
    int idx = row * grad->params.mesh.cols + col;
    if (idx < FR_GRAD_MESH_MAX_COLS * FR_GRAD_MESH_MAX_ROWS) {
        grad->params.mesh.points[idx].x = x;
        grad->params.mesh.points[idx].y = y;
        grad->params.mesh.points[idx].color = color;
    }
}

void fr_gradient_set_diamond(fr_gradient_t *grad, float cx, float cy, float hw, float hh)
{
    if (!grad) return;
    grad->type = FR_GRAD_TYPE_DIAMOND;
    grad->params.diamond.cx = cx;
    grad->params.diamond.cy = cy;
    grad->params.diamond.half_w = hw;
    grad->params.diamond.half_h = hh;
}

void fr_gradient_set_spiral(fr_gradient_t *grad, float cx, float cy,
                             float sr, float er, float start_angle, float revs)
{
    if (!grad) return;
    grad->type = FR_GRAD_TYPE_SPIRAL;
    grad->params.spiral.cx = cx;
    grad->params.spiral.cy = cy;
    grad->params.spiral.start_radius = sr;
    grad->params.spiral.end_radius = er;
    grad->params.spiral.start_angle = start_angle;
    grad->params.spiral.revolutions = revs;
}

void fr_gradient_set_extend_mode(fr_gradient_t *grad, uint32_t mode)
{
    if (grad) grad->extend_mode = mode;
}

void fr_gradient_set_colorspace(fr_gradient_t *grad, uint32_t colorspace)
{
    if (grad) grad->colorspace = colorspace;
}

void fr_gradient_set_dither(fr_gradient_t *grad, int enabled)
{
    if (grad) grad->dither = enabled;
}

void fr_gradient_set_transform(fr_gradient_t *grad, const float matrix[6])
{
    if (grad) memcpy(grad->matrix, matrix, sizeof(float) * 6);
}

void fr_gradient_set_identity_transform(fr_gradient_t *grad)
{
    if (!grad) return;
    grad->matrix[0] = 1.0f; grad->matrix[1] = 0.0f;
    grad->matrix[2] = 0.0f; grad->matrix[3] = 1.0f;
    grad->matrix[4] = 0.0f; grad->matrix[5] = 0.0f;
}

/* ---- 颜色采样 ---- */

fr_color_t fr_gradient_sample(const fr_gradient_t *grad, float t)
{
    fr_color_t result = {0, 0, 0, 255};
    if (!grad || grad->stop_count == 0) return result;

    if (grad->stop_count == 1) return grad->stops[0].color;

    t = fr_grad_get_t((fr_gradient_t *)grad, t, grad->extend_mode);

    /* 查找所在的停止点区间 */
    int idx = 0;
    for (uint32_t i = 0; i < grad->stop_count - 1; i++) {
        if (t >= grad->stops[i].position && t <= grad->stops[i + 1].position) {
            idx = (int)i;
            break;
        }
        if (t < grad->stops[i + 1].position) {
            idx = (int)i;
            break;
        }
    }

    float t0 = grad->stops[idx].position;
    float t1 = grad->stops[idx + 1].position;
    float local_t = (t - t0) / (t1 - t0 + 0.0001f);
    local_t = fr_clampf(local_t, 0.0f, 1.0f);

    fr_color_t c0 = grad->stops[idx].color;
    fr_color_t c1 = grad->stops[idx + 1].color;

    result.r = fr_lerp_u8(c0.r, c1.r, local_t);
    result.g = fr_lerp_u8(c0.g, c1.g, local_t);
    result.b = fr_lerp_u8(c0.b, c1.b, local_t);
    result.a = fr_lerp_u8(c0.a, c1.a, local_t);

    return result;
}

fr_color_t fr_gradient_sample_at(const fr_gradient_t *grad, float px, float py)
{
    fr_color_t result = {0, 0, 0, 255};
    if (!grad || grad->stop_count == 0) return result;

    float t = 0.0f;

    switch (grad->type) {
    case FR_GRAD_TYPE_LINEAR: {
        float dx = grad->params.linear.x2 - grad->params.linear.x1;
        float dy = grad->params.linear.y2 - grad->params.linear.y1;
        float len2 = dx * dx + dy * dy;
        if (len2 < 0.0001f) return grad->stops[0].color;
        t = ((px - grad->params.linear.x1) * dx + (py - grad->params.linear.y1) * dy) / len2;
        break;
    }
    case FR_GRAD_TYPE_RADIAL: {
        float dx = px - grad->params.radial.cx;
        float dy = py - grad->params.radial.cy;
        float dist = (float)sqrt(dx * dx + dy * dy);
        t = dist / (grad->params.radial.radius + 0.0001f);
        break;
    }
    case FR_GRAD_TYPE_CONICAL: {
        float dx = px - grad->params.conical.cx;
        float dy = py - grad->params.conical.cy;
        float angle = (float)atan2(dy, dx);
        float range = grad->params.conical.end_angle - grad->params.conical.start_angle;
        t = (angle - grad->params.conical.start_angle) / (range + 0.0001f);
        break;
    }
    case FR_GRAD_TYPE_DIAMOND: {
        float dx = fr_absf(px - grad->params.diamond.cx);
        float dy = fr_absf(py - grad->params.diamond.cy);
        t = dx / (grad->params.diamond.half_w + 0.0001f) +
            dy / (grad->params.diamond.half_h + 0.0001f);
        t *= 0.5f;
        break;
    }
    case FR_GRAD_TYPE_SPIRAL: {
        float dx = px - grad->params.spiral.cx;
        float dy = py - grad->params.spiral.cy;
        float dist = (float)sqrt(dx * dx + dy * dy);
        float angle = (float)atan2(dy, dx);
        float r_range = grad->params.spiral.end_radius - grad->params.spiral.start_radius;
        float r_t = (dist - grad->params.spiral.start_radius) / (r_range + 0.0001f);
        float a_t = (angle - grad->params.spiral.start_angle) /
                     (grad->params.spiral.revolutions * 6.283185307f + 0.0001f);
        t = (r_t + a_t) * 0.5f;
        break;
    }
    default:
        return grad->stops[0].color;
    }

    return fr_gradient_sample(grad, t);
}

/* ---- 渲染 ---- */

void fr_gradient_render(fr_gradient_t *grad, fr_context_t *ctx, int x, int y, int w, int h)
{
    if (!grad || !ctx || !ctx->framebuffer || w <= 0 || h <= 0) return;

    int end_x = x + w;
    int end_y = y + h;
    if (end_x > ctx->width) end_x = ctx->width;
    if (end_y > ctx->height) end_y = ctx->height;

    for (int py = y; py < end_y; py++) {
        for (int px = x; px < end_x; px++) {
            fr_color_t c = fr_gradient_sample_at(grad, (float)px, (float)py);
            uint32_t pixel = ((uint32_t)c.a << 24) | ((uint32_t)c.r << 16) |
                             ((uint32_t)c.g << 8) | (uint32_t)c.b;
            ctx->framebuffer[py * ctx->width + px] = pixel;
        }
    }
}

void fr_gradient_render_clipped(fr_gradient_t *grad, fr_context_t *ctx,
                                 int x, int y, int w, int h, const uint8_t *clip_mask)
{
    if (!grad || !ctx || !ctx->framebuffer || !clip_mask || w <= 0 || h <= 0) return;

    int end_x = x + w;
    int end_y = y + h;
    if (end_x > ctx->width) end_x = ctx->width;
    if (end_y > ctx->height) end_y = ctx->height;

    for (int py = y; py < end_y; py++) {
        for (int px = x; px < end_x; px++) {
            int idx = (py - y) * w + (px - x);
            if (clip_mask[idx] > 0) {
                fr_color_t c = fr_gradient_sample_at(grad, (float)px, (float)py);
                uint32_t pixel = ((uint32_t)c.a << 24) | ((uint32_t)c.r << 16) |
                                 ((uint32_t)c.g << 8) | (uint32_t)c.b;
                ctx->framebuffer[py * ctx->width + px] = pixel;
            }
        }
    }
}

/* ---- 抖动 ---- */

void fr_dither_init(fr_dither_table_t *table, int size)
{
    if (!table) return;
    /* 经典 Bayer 4x4 抖动矩阵 */
    static const uint8_t bayer4[4][4] = {
        { 0,  8,  2, 10},
        {12,  4, 14,  6},
        { 3, 11,  1,  9},
        {15,  7, 13,  5}
    };
    if (size == 4) {
        memcpy(table->matrix, bayer4, sizeof(bayer4));
    } else {
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++)
                table->matrix[y][x] = (uint8_t)((x ^ y) * 16);
    }
    table->size = size;
}

uint8_t fr_dither_threshold(const fr_dither_table_t *table, int x, int y, int level)
{
    if (!table || table->size <= 0) return (uint8_t)level;
    int mx = x % table->size;
    int my = y % table->size;
    uint8_t threshold = table->matrix[my][mx];
    return (uint8_t)((int)level > (int)threshold ? 255 : 0);
}

/* ---- 预定义渐变 ---- */

fr_gradient_t *fr_gradient_create_sunset(void)
{
    fr_gradient_t *g = fr_gradient_create(FR_GRAD_TYPE_LINEAR);
    if (!g) return NULL;
    fr_gradient_set_linear(g, 0.0f, 0.0f, 0.0f, 1.0f);
    fr_gradient_add_stop_rgba(g, 0.0f, 255, 50, 0, 255);    /* 深橙 */
    fr_gradient_add_stop_rgba(g, 0.3f, 255, 150, 0, 255);   /* 橙 */
    fr_gradient_add_stop_rgba(g, 0.6f, 255, 100, 100, 255); /* 粉 */
    fr_gradient_add_stop_rgba(g, 1.0f, 50, 0, 100, 255);    /* 深紫 */
    return g;
}

fr_gradient_t *fr_gradient_create_ocean(void)
{
    fr_gradient_t *g = fr_gradient_create(FR_GRAD_TYPE_LINEAR);
    if (!g) return NULL;
    fr_gradient_set_linear(g, 0.0f, 0.0f, 0.0f, 1.0f);
    fr_gradient_add_stop_rgba(g, 0.0f, 0, 50, 100, 255);    /* 深蓝 */
    fr_gradient_add_stop_rgba(g, 0.5f, 0, 128, 192, 255);   /* 海蓝 */
    fr_gradient_add_stop_rgba(g, 1.0f, 0, 200, 255, 255);   /* 青 */
    return g;
}

fr_gradient_t *fr_gradient_create_forest(void)
{
    fr_gradient_t *g = fr_gradient_create(FR_GRAD_TYPE_LINEAR);
    if (!g) return NULL;
    fr_gradient_set_linear(g, 0.0f, 0.0f, 0.0f, 1.0f);
    fr_gradient_add_stop_rgba(g, 0.0f, 0, 80, 0, 255);      /* 深绿 */
    fr_gradient_add_stop_rgba(g, 0.5f, 34, 139, 34, 255);   /* 森林绿 */
    fr_gradient_add_stop_rgba(g, 1.0f, 144, 238, 144, 255); /* 浅绿 */
    return g;
}

fr_gradient_t *fr_gradient_create_fire(void)
{
    fr_gradient_t *g = fr_gradient_create(FR_GRAD_TYPE_RADIAL);
    if (!g) return NULL;
    fr_gradient_set_radial(g, 0.5f, 0.5f, 0.5f, 0.5f, 0.7f);
    fr_gradient_add_stop_rgba(g, 0.0f, 255, 255, 100, 255); /* 白黄 */
    fr_gradient_add_stop_rgba(g, 0.3f, 255, 200, 0, 255);   /* 黄 */
    fr_gradient_add_stop_rgba(g, 0.6f, 255, 100, 0, 255);   /* 橙 */
    fr_gradient_add_stop_rgba(g, 1.0f, 200, 0, 0, 255);     /* 红 */
    return g;
}

fr_gradient_t *fr_gradient_create_metal(void)
{
    fr_gradient_t *g = fr_gradient_create(FR_GRAD_TYPE_LINEAR);
    if (!g) return NULL;
    fr_gradient_set_linear(g, 0.0f, 0.0f, 1.0f, 0.0f);
    fr_gradient_add_stop_rgba(g, 0.0f, 80, 80, 80, 255);
    fr_gradient_add_stop_rgba(g, 0.2f, 180, 180, 180, 255);
    fr_gradient_add_stop_rgba(g, 0.4f, 100, 100, 100, 255);
    fr_gradient_add_stop_rgba(g, 0.6f, 200, 200, 200, 255);
    fr_gradient_add_stop_rgba(g, 0.8f, 120, 120, 120, 255);
    fr_gradient_add_stop_rgba(g, 1.0f, 60, 60, 60, 255);
    return g;
}

fr_gradient_t *fr_gradient_create_rainbow(void)
{
    fr_gradient_t *g = fr_gradient_create(FR_GRAD_TYPE_LINEAR);
    if (!g) return NULL;
    fr_gradient_set_linear(g, 0.0f, 0.0f, 1.0f, 0.0f);
    fr_gradient_add_stop_rgba(g, 0.0f, 255, 0, 0, 255);     /* 红 */
    fr_gradient_add_stop_rgba(g, 0.17f, 255, 165, 0, 255);  /* 橙 */
    fr_gradient_add_stop_rgba(g, 0.33f, 255, 255, 0, 255);  /* 黄 */
    fr_gradient_add_stop_rgba(g, 0.5f, 0, 255, 0, 255);     /* 绿 */
    fr_gradient_add_stop_rgba(g, 0.67f, 0, 0, 255, 255);    /* 蓝 */
    fr_gradient_add_stop_rgba(g, 0.83f, 75, 0, 130, 255);   /* 靛 */
    fr_gradient_add_stop_rgba(g, 1.0f, 148, 0, 211, 255);   /* 紫 */
    return g;
}