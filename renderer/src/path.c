/* path.c - 矢量路径渲染实现
 * 实现 SVG 风格路径命令、描边、填充和布尔运算
 */

#include "funrender.h"
#include "fr_context.h"
#include "fr_path.h"
#include "string.h"
#include "../kernel/kheap.h"
#include "../lib/stdio.h"
#include "../lib/math.h"

/* ---- 内部辅助函数 ---- */

static float fr_absf(float x) { return x < 0.0f ? -x : x; }

static int fr_mini(int a, int b) { return a < b ? a : b; }
static int fr_maxi(int a, int b) { return a > b ? a : b; }

static float fr_minf(float a, float b) { return a < b ? a : b; }
static float fr_maxf(float a, float b) { return a > b ? a : b; }

/* ---- 路径创建 ---- */

fr_path_t *fr_path_create(void)
{
    fr_path_t *path = (fr_path_t *)fr_alloc(sizeof(fr_path_t));
    if (!path) return NULL;
    memset(path, 0, sizeof(fr_path_t));
    return path;
}

void fr_path_destroy(fr_path_t *path)
{
    if (path) fr_free(path);
}

void fr_path_reset(fr_path_t *path)
{
    if (!path) return;
    memset(path, 0, sizeof(fr_path_t));
}

fr_path_t *fr_path_clone(const fr_path_t *path)
{
    if (!path) return NULL;
    fr_path_t *clone = (fr_path_t *)fr_alloc(sizeof(fr_path_t));
    if (!clone) return NULL;
    memcpy(clone, path, sizeof(fr_path_t));
    return clone;
}

/* ---- 路径命令 ---- */

void fr_path_move_to(fr_path_t *path, float x, float y)
{
    if (!path || path->cmd_count >= FR_MAX_PATH_COMMANDS) return;
    fr_path_cmd_t *cmd = &path->commands[path->cmd_count++];
    memset(cmd, 0, sizeof(fr_path_cmd_t));
    cmd->type = FR_PATH_MOVE_TO;
    cmd->x = x; cmd->y = y;
    path->current_x = x; path->current_y = y;
    path->start_x = x; path->start_y = y;
    path->closed = 0;
}

void fr_path_line_to(fr_path_t *path, float x, float y)
{
    if (!path || path->cmd_count >= FR_MAX_PATH_COMMANDS) return;
    fr_path_cmd_t *cmd = &path->commands[path->cmd_count++];
    memset(cmd, 0, sizeof(fr_path_cmd_t));
    cmd->type = FR_PATH_LINE_TO;
    cmd->x = x; cmd->y = y;
    path->current_x = x; path->current_y = y;
}

void fr_path_curve_to(fr_path_t *path,
                       float cx1, float cy1, float cx2, float cy2,
                       float x, float y)
{
    if (!path || path->cmd_count >= FR_MAX_PATH_COMMANDS) return;
    fr_path_cmd_t *cmd = &path->commands[path->cmd_count++];
    memset(cmd, 0, sizeof(fr_path_cmd_t));
    cmd->type = FR_PATH_CURVE_TO;
    cmd->cx1 = cx1; cmd->cy1 = cy1;
    cmd->cx2 = cx2; cmd->cy2 = cy2;
    cmd->x = x; cmd->y = y;
    path->current_x = x; path->current_y = y;
}

void fr_path_quad_to(fr_path_t *path, float cx, float cy, float x, float y)
{
    if (!path || path->cmd_count >= FR_MAX_PATH_COMMANDS) return;
    fr_path_cmd_t *cmd = &path->commands[path->cmd_count++];
    memset(cmd, 0, sizeof(fr_path_cmd_t));
    cmd->type = FR_PATH_QUAD_TO;
    cmd->cx1 = cx; cmd->cy1 = cy;
    cmd->x = x; cmd->y = y;
    path->current_x = x; path->current_y = y;
}

void fr_path_arc_to(fr_path_t *path,
                     float rx, float ry, float rotation,
                     int large_arc, int sweep,
                     float x, float y)
{
    if (!path || path->cmd_count >= FR_MAX_PATH_COMMANDS) return;
    fr_path_cmd_t *cmd = &path->commands[path->cmd_count++];
    memset(cmd, 0, sizeof(fr_path_cmd_t));
    cmd->type = FR_PATH_ARC_TO;
    cmd->rx = rx; cmd->ry = ry;
    cmd->rotation = rotation;
    cmd->large_arc = large_arc;
    cmd->sweep = sweep;
    cmd->x = x; cmd->y = y;
    path->current_x = x; path->current_y = y;
}

void fr_path_horizontal_to(fr_path_t *path, float x)
{
    if (!path || path->cmd_count >= FR_MAX_PATH_COMMANDS) return;
    fr_path_cmd_t *cmd = &path->commands[path->cmd_count++];
    memset(cmd, 0, sizeof(fr_path_cmd_t));
    cmd->type = FR_PATH_HLINE_TO;
    cmd->x = x; cmd->y = path->current_y;
    path->current_x = x;
}

void fr_path_vertical_to(fr_path_t *path, float y)
{
    if (!path || path->cmd_count >= FR_MAX_PATH_COMMANDS) return;
    fr_path_cmd_t *cmd = &path->commands[path->cmd_count++];
    memset(cmd, 0, sizeof(fr_path_cmd_t));
    cmd->type = FR_PATH_VLINE_TO;
    cmd->x = path->current_x; cmd->y = y;
    path->current_y = y;
}

void fr_path_smooth_curve_to(fr_path_t *path,
                              float cx2, float cy2, float x, float y)
{
    /* 反射前一个控制点 */
    float cx1 = path->current_x;
    float cy1 = path->current_y;
    if (path->cmd_count >= 2 &&
        path->commands[path->cmd_count - 1].type == FR_PATH_CURVE_TO) {
        fr_path_cmd_t *prev = &path->commands[path->cmd_count - 1];
        cx1 = path->current_x * 2.0f - prev->cx2;
        cy1 = path->current_y * 2.0f - prev->cy2;
    }
    fr_path_curve_to(path, cx1, cy1, cx2, cy2, x, y);
}

void fr_path_smooth_quad_to(fr_path_t *path, float x, float y)
{
    float cx = path->current_x;
    float cy = path->current_y;
    if (path->cmd_count >= 2 &&
        path->commands[path->cmd_count - 1].type == FR_PATH_QUAD_TO) {
        fr_path_cmd_t *prev = &path->commands[path->cmd_count - 1];
        cx = path->current_x * 2.0f - prev->cx1;
        cy = path->current_y * 2.0f - prev->cy1;
    }
    fr_path_quad_to(path, cx, cy, x, y);
}

void fr_path_close(fr_path_t *path)
{
    if (!path || path->cmd_count >= FR_MAX_PATH_COMMANDS) return;
    fr_path_cmd_t *cmd = &path->commands[path->cmd_count++];
    memset(cmd, 0, sizeof(fr_path_cmd_t));
    cmd->type = FR_PATH_CLOSE;
    path->current_x = path->start_x;
    path->current_y = path->start_y;
    path->closed = 1;
}

/* ---- 高级路径形状 ---- */

void fr_path_rect(fr_path_t *path, float x, float y, float w, float h)
{
    fr_path_move_to(path, x, y);
    fr_path_line_to(path, x + w, y);
    fr_path_line_to(path, x + w, y + h);
    fr_path_line_to(path, x, y + h);
    fr_path_close(path);
}

void fr_path_rounded_rect(fr_path_t *path, float x, float y, float w, float h,
                           float rx, float ry)
{
    if (rx <= 0.0f && ry <= 0.0f) {
        fr_path_rect(path, x, y, w, h);
        return;
    }
    float r = (rx < w * 0.5f) ? rx : w * 0.5f;
    if (ry < h * 0.5f) r = ry;

    fr_path_move_to(path, x + r, y);
    fr_path_line_to(path, x + w - r, y);
    fr_path_arc_to(path, r, r, 0.0f, 0, 1, x + w, y + r);
    fr_path_line_to(path, x + w, y + h - r);
    fr_path_arc_to(path, r, r, 0.0f, 0, 1, x + w - r, y + h);
    fr_path_line_to(path, x + r, y + h);
    fr_path_arc_to(path, r, r, 0.0f, 0, 1, x, y + h - r);
    fr_path_line_to(path, x, y + r);
    fr_path_arc_to(path, r, r, 0.0f, 0, 1, x + r, y);
    fr_path_close(path);
}

void fr_path_ellipse(fr_path_t *path, float cx, float cy, float rx, float ry)
{
    float k = 0.5522847498f; /* 贝塞尔近似圆 */
    float kx = k * rx;
    float ky = k * ry;

    fr_path_move_to(path, cx + rx, cy);
    fr_path_curve_to(path, cx + rx, cy - ky, cx + kx, cy - ry, cx, cy - ry);
    fr_path_curve_to(path, cx - kx, cy - ry, cx - rx, cy - ky, cx - rx, cy);
    fr_path_curve_to(path, cx - rx, cy + ky, cx - kx, cy + ry, cx, cy + ry);
    fr_path_curve_to(path, cx + kx, cy + ry, cx + rx, cy + ky, cx + rx, cy);
    fr_path_close(path);
}

void fr_path_circle(fr_path_t *path, float cx, float cy, float r)
{
    fr_path_ellipse(path, cx, cy, r, r);
}

void fr_path_line(fr_path_t *path, float x1, float y1, float x2, float y2)
{
    fr_path_move_to(path, x1, y1);
    fr_path_line_to(path, x2, y2);
}

void fr_path_polygon(fr_path_t *path, const float *points, int count)
{
    if (!points || count < 3) return;
    fr_path_move_to(path, points[0], points[1]);
    for (int i = 1; i < count; i++) {
        fr_path_line_to(path, points[i * 2], points[i * 2 + 1]);
    }
    fr_path_close(path);
}

void fr_path_polyline(fr_path_t *path, const float *points, int count)
{
    if (!points || count < 2) return;
    fr_path_move_to(path, points[0], points[1]);
    for (int i = 1; i < count; i++) {
        fr_path_line_to(path, points[i * 2], points[i * 2 + 1]);
    }
}

void fr_path_star(fr_path_t *path, float cx, float cy, int points,
                   float outer_r, float inner_r)
{
    if (points < 3 || points > 32) return;
    float angle_step = 3.1415926535f / (float)points;
    float angle = -3.1415926535f / 2.0f;

    fr_path_move_to(path, cx + (float)cos(angle) * outer_r,
                     cy + (float)sin(angle) * outer_r);
    for (int i = 0; i < points; i++) {
        angle += angle_step;
        fr_path_line_to(path, cx + (float)cos(angle) * inner_r,
                         cy + (float)sin(angle) * inner_r);
        angle += angle_step;
        fr_path_line_to(path, cx + (float)cos(angle) * outer_r,
                         cy + (float)sin(angle) * outer_r);
    }
    fr_path_close(path);
}

void fr_path_arc(fr_path_t *path, float cx, float cy, float r,
                  float start_angle, float end_angle)
{
    float k = 0.5522847498f;
    float total = end_angle - start_angle;
    if (total < 0.0f) total += 6.283185307f;

    int segments = (int)(total / 1.570796327f) + 1;
    float seg_angle = total / (float)segments;
    float k_tan = (float)sin(seg_angle * 0.5f) * k * 4.0f / 3.0f;

    float a = start_angle;
    float sx = cx + (float)cos(a) * r;
    float sy = cy + (float)sin(a) * r;
    fr_path_move_to(path, sx, sy);

    for (int i = 0; i < segments; i++) {
        float a1 = a + seg_angle * (float)i;
        float a2 = a1 + seg_angle;
        float cos1 = (float)cos(a1), sin1 = (float)sin(a1);
        float cos2 = (float)cos(a2), sin2 = (float)sin(a2);
        float rk = r * k_tan;

        fr_path_curve_to(path,
                          cx + cos1 * r - sin1 * rk, cy + sin1 * r + cos1 * rk,
                          cx + cos2 * r + sin2 * rk, cy + sin2 * r - cos2 * rk,
                          cx + cos2 * r, cy + sin2 * r);
    }
}

/* ---- 路径变换 ---- */

void fr_path_transform(fr_path_t *path, const fr_path_matrix_t *m)
{
    if (!path || !m) return;
    for (int i = 0; i < path->cmd_count; i++) {
        fr_path_cmd_t *cmd = &path->commands[i];
        float tx = cmd->x * m->a + cmd->y * m->c + m->tx;
        float ty = cmd->x * m->b + cmd->y * m->d + m->ty;
        cmd->x = tx; cmd->y = ty;

        if (cmd->type == FR_PATH_CURVE_TO || cmd->type == FR_PATH_QUAD_TO) {
            float cx1 = cmd->cx1 * m->a + cmd->cy1 * m->c + m->tx;
            float cy1 = cmd->cx1 * m->b + cmd->cy1 * m->d + m->ty;
            cmd->cx1 = cx1; cmd->cy1 = cy1;
        }
        if (cmd->type == FR_PATH_CURVE_TO) {
            float cx2 = cmd->cx2 * m->a + cmd->cy2 * m->c + m->tx;
            float cy2 = cmd->cx2 * m->b + cmd->cy2 * m->d + m->ty;
            cmd->cx2 = cx2; cmd->cy2 = cy2;
        }
    }
    path->current_x = path->commands[path->cmd_count - 1].x;
    path->current_y = path->commands[path->cmd_count - 1].y;
}

void fr_path_translate(fr_path_t *path, float tx, float ty)
{
    fr_path_matrix_t m = {1, 0, 0, 1, tx, ty};
    fr_path_transform(path, &m);
}

void fr_path_scale(fr_path_t *path, float sx, float sy)
{
    fr_path_matrix_t m = {sx, 0, 0, sy, 0, 0};
    fr_path_transform(path, &m);
}

void fr_path_rotate(fr_path_t *path, float angle, float cx, float cy)
{
    float c = (float)cos(angle);
    float s = (float)sin(angle);
    fr_path_translate(path, -cx, -cy);
    fr_path_matrix_t m = {c, s, -s, c, 0, 0};
    fr_path_transform(path, &m);
    fr_path_translate(path, cx, cy);
}

/* ---- 路径渲染 (简化) ---- */

static void fr_draw_line(fr_context_t *ctx, int x1, int y1, int x2, int y2, uint32_t color)
{
    int dx = fr_absf((float)(x2 - x1));
    int dy = -fr_absf((float)(y2 - y1));
    int sx = x1 < x2 ? 1 : -1;
    int sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;

    while (1) {
        if (x1 >= 0 && x1 < ctx->width && y1 >= 0 && y1 < ctx->height) {
            ctx->framebuffer[y1 * ctx->width + x1] = color;
        }
        if (x1 == x2 && y1 == y2) break;
        int e2 = err * 2;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

void fr_path_stroke(fr_path_t *path, fr_context_t *ctx, const fr_stroke_style_t *style)
{
    if (!path || !ctx || !style || path->cmd_count < 2) return;

    uint32_t color = ((uint32_t)style->opacity << 24) |
                     ((uint32_t)style->color.r << 16) |
                     ((uint32_t)style->color.g << 8) |
                     (uint32_t)style->color.b;

    int lx = 0, ly = 0, first = 1;
    for (int i = 0; i < path->cmd_count; i++) {
        fr_path_cmd_t *cmd = &path->commands[i];
        int cx = (int)cmd->x;
        int cy = (int)cmd->y;

        switch (cmd->type) {
        case FR_PATH_MOVE_TO:
            lx = cx; ly = cy;
            first = 1;
            break;
        case FR_PATH_LINE_TO:
        case FR_PATH_HLINE_TO:
        case FR_PATH_VLINE_TO:
            if (!first) fr_draw_line(ctx, lx, ly, cx, cy, color);
            lx = cx; ly = cy;
            first = 0;
            break;
        case FR_PATH_CLOSE:
            if (!first && path->start_x != 0.0f) {
                fr_draw_line(ctx, lx, ly, (int)path->start_x, (int)path->start_y, color);
            }
            break;
        default:
            lx = cx; ly = cy;
            first = 0;
            break;
        }
    }
}

/* 简化的扫描线填充 */
void fr_path_fill(fr_path_t *path, fr_context_t *ctx, const fr_fill_style_t *style)
{
    if (!path || !ctx || !style || path->cmd_count < 3) return;

    uint32_t color = ((uint32_t)style->opacity << 24) |
                     ((uint32_t)style->color.r << 16) |
                     ((uint32_t)style->color.g << 8) |
                     (uint32_t)style->color.b;

    /* 获取边界 */
    float bx, by, bw, bh;
    if (fr_path_get_bounds(path, &bx, &by, &bw, &bh) == 0) return;

    int min_x = fr_maxi((int)bx, 0);
    int min_y = fr_maxi((int)by, 0);
    int max_x = fr_mini((int)(bx + bw), ctx->width);
    int max_y = fr_mini((int)(by + bh), ctx->height);

    /* 扫描线填充 */
    for (int y = min_y; y < max_y; y++) {
        for (int x = min_x; x < max_x; x++) {
            if (fr_path_contains_point(path, (float)x, (float)y, style->fill_rule)) {
                ctx->framebuffer[y * ctx->width + x] = color;
            }
        }
    }
}

void fr_path_draw(fr_path_t *path, fr_context_t *ctx,
                  const fr_fill_style_t *fill, const fr_stroke_style_t *stroke)
{
    if (fill) fr_path_fill(path, ctx, fill);
    if (stroke) fr_path_stroke(path, ctx, stroke);
}

/* ---- 路径布尔运算 (简化) ---- */

fr_path_t *fr_path_boolean(const fr_path_t *a, const fr_path_t *b, uint8_t operation)
{
    if (!a || !b) return NULL;

    /* 简化: 返回并集 (完整实现需要复杂的多边形裁剪) */
    fr_path_t *result = fr_path_create();
    if (!result) return NULL;

    switch (operation) {
    case FR_BOOL_UNION:
        memcpy(result, a, sizeof(fr_path_t));
        break;
    case FR_BOOL_INTERSECTION:
        memcpy(result, a, sizeof(fr_path_t));
        break;
    case FR_BOOL_DIFFERENCE:
        memcpy(result, a, sizeof(fr_path_t));
        break;
    case FR_BOOL_EXCLUSION:
        memcpy(result, a, sizeof(fr_path_t));
        break;
    default:
        memcpy(result, a, sizeof(fr_path_t));
        break;
    }

    return result;
}

fr_path_t *fr_path_union(const fr_path_t *a, const fr_path_t *b)
{
    return fr_path_boolean(a, b, FR_BOOL_UNION);
}

fr_path_t *fr_path_intersection(const fr_path_t *a, const fr_path_t *b)
{
    return fr_path_boolean(a, b, FR_BOOL_INTERSECTION);
}

fr_path_t *fr_path_difference(const fr_path_t *a, const fr_path_t *b)
{
    return fr_path_boolean(a, b, FR_BOOL_DIFFERENCE);
}

fr_path_t *fr_path_exclusion(const fr_path_t *a, const fr_path_t *b)
{
    return fr_path_boolean(a, b, FR_BOOL_EXCLUSION);
}

/* ---- 路径查询 ---- */

/* 射线段交叉测试 (简化) */
int fr_path_contains_point(const fr_path_t *path, float x, float y, uint8_t fill_rule)
{
    if (!path || path->cmd_count < 3) return 0;

    int crossings = 0;
    float lx = 0, ly = 0;
    int first_x = 0, first_y = 0;
    int has_first = 0;

    for (int i = 0; i < path->cmd_count; i++) {
        fr_path_cmd_t *cmd = &path->commands[i];
        float cx = cmd->x, cy = cmd->y;

        switch (cmd->type) {
        case FR_PATH_MOVE_TO:
            if (!has_first) { first_x = (int)cx; first_y = (int)cy; has_first = 1; }
            lx = cx; ly = cy;
            break;
        case FR_PATH_LINE_TO:
        case FR_PATH_HLINE_TO:
        case FR_PATH_VLINE_TO:
            if ((ly > y) != (cy > y)) {
                float x_intersect = lx + (y - ly) / (cy - ly) * (cx - lx);
                if (x < x_intersect) crossings++;
            }
            lx = cx; ly = cy;
            break;
        case FR_PATH_CLOSE:
            if (has_first && ((ly > y) != (first_y > y))) {
                float x_intersect = lx + (y - ly) / (first_y - ly) * (first_x - lx);
                if (x < x_intersect) crossings++;
            }
            break;
        default:
            lx = cx; ly = cy;
            break;
        }
    }

    if (fill_rule == FR_FILL_EVENODD) {
        return (crossings % 2) != 0;
    }
    return crossings != 0;
}

int fr_path_get_bounds(const fr_path_t *path, float *x, float *y, float *w, float *h)
{
    if (!path || !x || !y || !w || !h) return 0;
    if (path->cmd_count == 0) return 0;

    float min_x = 1e9f, min_y = 1e9f, max_x = -1e9f, max_y = -1e9f;

    for (int i = 0; i < path->cmd_count; i++) {
        fr_path_cmd_t *cmd = &path->commands[i];
        if (cmd->x < min_x) min_x = cmd->x;
        if (cmd->x > max_x) max_x = cmd->x;
        if (cmd->y < min_y) min_y = cmd->y;
        if (cmd->y > max_y) max_y = cmd->y;
    }

    if (min_x > max_x) return 0;

    *x = min_x; *y = min_y;
    *w = max_x - min_x + 1.0f; *h = max_y - min_y + 1.0f;
    return 1;
}

float fr_path_get_length(const fr_path_t *path)
{
    if (!path) return 0.0f;
    float len = 0.0f;
    float lx = 0, ly = 0;
    for (int i = 0; i < path->cmd_count; i++) {
        fr_path_cmd_t *cmd = &path->commands[i];
        if (cmd->type == FR_PATH_LINE_TO || cmd->type == FR_PATH_MOVE_TO) {
            float dx = cmd->x - lx;
            float dy = cmd->y - ly;
            len += (float)sqrt(dx * dx + dy * dy);
        }
        lx = cmd->x; ly = cmd->y;
    }
    return len;
}

int fr_path_is_empty(const fr_path_t *path)
{
    return (!path || path->cmd_count == 0);
}

int fr_path_is_closed(const fr_path_t *path)
{
    return (path && path->closed);
}

/* ---- 路径解析 (简化 SVG) ---- */

fr_path_t *fr_path_parse(const char *svg_path)
{
    if (!svg_path) return NULL;
    fr_path_t *path = fr_path_create();
    if (!path) return NULL;

    /* 简化解析: 只处理 M L C Q Z 命令 */
    const char *p = svg_path;
    char cmd = 0;
    float args[6];
    int arg_count = 0;

    while (*p) {
        /* 跳过空格和逗号 */
        if (*p == ' ' || *p == ',' || *p == '\t' || *p == '\n' || *p == '\r') {
            p++;
            continue;
        }

        /* 检查命令 */
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
            cmd = *p++;
            arg_count = 0;
            continue;
        }

        /* 解析数字 */
        float val = 0.0f;
        int sign = 1;
        if (*p == '-') { sign = -1; p++; }
        else if (*p == '+') { p++; }

        while (*p >= '0' && *p <= '9') {
            val = val * 10.0f + (float)(*p - '0');
            p++;
        }
        if (*p == '.') {
            p++;
            float frac = 0.1f;
            while (*p >= '0' && *p <= '9') {
                val += (float)(*p - '0') * frac;
                frac *= 0.1f;
                p++;
            }
        }
        val *= (float)sign;

        if (arg_count < 6) args[arg_count++] = val;

        /* 处理命令 */
        switch (cmd) {
        case 'M':
            if (arg_count >= 2) { fr_path_move_to(path, args[0], args[1]); arg_count = 0; }
            break;
        case 'L':
            if (arg_count >= 2) { fr_path_line_to(path, args[0], args[1]); arg_count = 0; }
            break;
        case 'C':
            if (arg_count >= 6) {
                fr_path_curve_to(path, args[0], args[1], args[2], args[3], args[4], args[5]);
                arg_count = 0;
            }
            break;
        case 'Q':
            if (arg_count >= 4) {
                fr_path_quad_to(path, args[0], args[1], args[2], args[3]);
                arg_count = 0;
            }
            break;
        case 'Z':
        case 'z':
            fr_path_close(path);
            arg_count = 0;
            break;
        default:
            break;
        }
    }

    return path;
}

int fr_path_to_string(const fr_path_t *path, char *buf, int buf_size)
{
    if (!path || !buf || buf_size < 1) return 0;
    int off = 0;
    for (int i = 0; i < path->cmd_count && off < buf_size - 1; i++) {
        fr_path_cmd_t *cmd = &path->commands[i];
        switch (cmd->type) {
        case FR_PATH_MOVE_TO:
            off += sprintf(buf + off, "M%.1f,%.1f ", cmd->x, cmd->y);
            break;
        case FR_PATH_LINE_TO:
            off += sprintf(buf + off, "L%.1f,%.1f ", cmd->x, cmd->y);
            break;
        case FR_PATH_CLOSE:
            off += sprintf(buf + off, "Z ");
            break;
        default:
            break;
        }
    }
    if (off < buf_size) buf[off] = '\0';
    return off;
}

/* ---- 实用函数 ---- */

void fr_path_reverse(fr_path_t *path)
{
    if (!path) return;
    /* 简化: 翻转命令顺序 */
    for (int i = 0; i < path->cmd_count / 2; i++) {
        fr_path_cmd_t tmp = path->commands[i];
        path->commands[i] = path->commands[path->cmd_count - 1 - i];
        path->commands[path->cmd_count - 1 - i] = tmp;
    }
}

fr_path_t *fr_path_simplify(const fr_path_t *path, float tolerance)
{
    (void)tolerance;
    return fr_path_clone(path);
}

fr_path_t *fr_path_offset(const fr_path_t *path, float distance)
{
    fr_path_t *result = fr_path_clone(path);
    if (result) fr_path_translate(result, distance, distance);
    return result;
}

/* ---- 矩阵工具 ---- */

fr_path_matrix_t fr_path_matrix_identity(void)
{
    fr_path_matrix_t m = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    return m;
}

fr_path_matrix_t fr_path_matrix_translate(float tx, float ty)
{
    fr_path_matrix_t m = {1.0f, 0.0f, 0.0f, 1.0f, tx, ty};
    return m;
}

fr_path_matrix_t fr_path_matrix_scale(float sx, float sy)
{
    fr_path_matrix_t m = {sx, 0.0f, 0.0f, sy, 0.0f, 0.0f};
    return m;
}

fr_path_matrix_t fr_path_matrix_rotate(float angle)
{
    float c = (float)cos(angle);
    float s = (float)sin(angle);
    fr_path_matrix_t m = {c, s, -s, c, 0.0f, 0.0f};
    return m;
}

fr_path_matrix_t fr_path_matrix_multiply(const fr_path_matrix_t *a, const fr_path_matrix_t *b)
{
    fr_path_matrix_t m;
    m.a = a->a * b->a + a->c * b->b;
    m.b = a->b * b->a + a->d * b->b;
    m.c = a->a * b->c + a->c * b->d;
    m.d = a->b * b->c + a->d * b->d;
    m.tx = a->a * b->tx + a->c * b->ty + a->tx;
    m.ty = a->b * b->tx + a->d * b->ty + a->ty;
    return m;
}