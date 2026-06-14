/* transform.c - 2D 变换系统实现
 * 实现平移、旋转、缩放、变换矩阵栈、
 * 包围盒计算、点变换和坐标系统转换
 */

#include "funrender.h"
#include "fr_context.h"
#include "fr_transform.h"
#include "string.h"

/* ---- 恒等矩阵 ---- */

const fr_matrix_t FR_MATRIX_IDENTITY = {
    {FR_FIXED_ONE, 0, 0,
     0, FR_FIXED_ONE, 0,
     0, 0, FR_FIXED_ONE}
};

/* ---- 内部辅助函数 ---- */

/* 快速整数绝对值 */
static int32_t iabs(int32_t x)
{
    return x < 0 ? -x : x;
}

/* ---- 定点数三角函数近似 ---- */

/* 使用BPM表近似计算 sin */
/* 我们将角度转为 0-360 度的整数, 然后用查表法 */
/* 360 度固定点表 (采样 256 个点) */

static const fr_fixed_t sin_table[256] = {
    0,       1608,    3216,    4821,    6424,    8022,    9616,   11204,
    12785,   14359,   15924,   17479,   19024,   20557,   22078,   23586,
    25079,   26557,   28020,   29465,   30893,   32302,   33692,   35061,
    36409,   37735,   39039,   40319,   41575,   42806,   44011,   45190,
    46340,   47463,   48558,   49623,   50659,   51665,   52639,   53582,
    54492,   55370,   56214,   57024,   57800,   58541,   59247,   59916,
    60550,   61146,   61706,   62228,   62712,   63158,   63566,   63934,
    64264,   64553,   64804,   65014,   65185,   65315,   65406,   65456,
    65467,   65437,   65367,   65257,   65107,   64916,   64686,   64415,
    64105,   63754,   63364,   62934,   62465,   61957,   61409,   60823,
    60198,   59534,   58833,   58093,   57315,   56500,   55647,   54757,
    53830,   52867,   51867,   50832,   49760,   48654,   47513,   46337,
    45128,   43885,   42609,   41301,   39961,   38589,   37187,   35753,
    34290,   32798,   31276,   29726,   28149,   26544,   24912,   23255,
    21572,   19865,   18134,   16379,   14602,   12803,   10982,    9141,
    7280,    5400,    3502,    1586,    -348,   -2297,   -4263,   -6244,
    -8238,  -10247,  -12268,  -14301,  -16345,  -18399,  -20462,  -22533,
    -24610, -26693, -28780, -30870, -32962, -35054, -37146, -39236,
    -41322, -43404, -45480, -47548, -49608, -51657, -53695, -55720,
    -57730, -59725, -61703, -63662, -65601, -67519, -69415, -71286,
    -73132, -74952, -76745, -78508, -80240, -81941, -83609, -85243,
    -86841, -88403, -89927, -91413, -92859, -94265, -95629, -96951,
    -98229, -99464, -100653, -101796, -102894, -103944, -104946, -105901,
    -106807, -107664, -108471, -109228, -109935, -110591, -111196, -111750,
    -112252, -112703, -113101, -113447, -113741, -113982, -114170, -114306,
    -114388, -114418, -114395, -114319, -114191, -114010, -113777, -113491,
    -113153, -112763, -112321, -111828, -111283, -110687, -110040, -109343,
    -108596, -107799, -106953, -106058, -105114, -104122, -103083, -101996,
    -100863, -99683,  -98458,  -97188,  -95874,  -94515,  -93114,  -91670,
    -90184,  -88657,  -87090,  -85483,  -83837,  -82153,  -80431,  -78673,
    -76879,  -75050,  -73187,  -71291,  -69362,  -67402,  -65411,  -63391,
    -61343,  -59267,  -57164,  -55037,  -52885,  -50709,  -48511,  -46292,
    -44053,  -41795,  -39519,  -37226,  -34918,  -32596,  -30260,  -27913,
    -25555,  -23187,  -20811,  -18428,  -16039,  -13645,  -11248,  -8848
};

/* sin_table 的一个额外值用于 cos 查找 */
/* cos(θ) = sin(θ + 90°) */

/* 从度数计算 sin (定点数) */
static fr_fixed_t fast_sin(int32_t degrees)
{
    /* 规范化到 0-359 */
    while (degrees < 0) degrees += 360;
    degrees = degrees % 360;

    /* 将 360 度映射到 256 个表项 */
    int idx = (degrees * 256) / 360;

    /* 处理角度 > 180: sin(θ+180) = -sin(θ) */
    int neg = 0;
    if (idx >= 128) {
        neg = 1;
        idx -= 128;
    }

    if (idx >= 256) idx = 0;

    fr_fixed_t val = sin_table[idx];
    return neg ? -val : val;
}

/* 从度数计算 cos (定点数) */
static fr_fixed_t fast_cos(int32_t degrees)
{
    return fast_sin(degrees + 90);
}

/* ---- 矩阵操作实现 ---- */

/*
 * fr_transform_matrix_identity - 置矩阵为单位矩阵
 */
void fr_transform_matrix_identity(fr_matrix_t *mat)
{
    if (mat == NULL) return;
    mat->m[0] = FR_FIXED_ONE;  mat->m[1] = 0;  mat->m[2] = 0;
    mat->m[3] = 0;  mat->m[4] = FR_FIXED_ONE;  mat->m[5] = 0;
    mat->m[6] = 0;  mat->m[7] = 0;  mat->m[8] = FR_FIXED_ONE;
}

/*
 * fr_transform_translate - 创建平移矩阵
 */
void fr_transform_translate(fr_matrix_t *mat, int32_t dx, int32_t dy)
{
    if (mat == NULL) return;
    mat->m[0] = FR_FIXED_ONE;  mat->m[1] = 0;  mat->m[2] = FR_INT_TO_FIXED(dx);
    mat->m[3] = 0;  mat->m[4] = FR_FIXED_ONE;  mat->m[5] = FR_INT_TO_FIXED(dy);
    mat->m[6] = 0;  mat->m[7] = 0;  mat->m[8] = FR_FIXED_ONE;
}

/*
 * fr_transform_rotate - 创建旋转矩阵 (绕原点)
 */
void fr_transform_rotate(fr_matrix_t *mat, float angle_degrees)
{
    if (mat == NULL) return;
    int32_t angle_int = (int32_t)angle_degrees;
    fr_fixed_t c = fast_cos(angle_int);
    fr_fixed_t s = fast_sin(angle_int);

    mat->m[0] = c;  mat->m[1] = -s; mat->m[2] = 0;
    mat->m[3] = s;  mat->m[4] = c;  mat->m[5] = 0;
    mat->m[6] = 0;  mat->m[7] = 0;  mat->m[8] = FR_FIXED_ONE;
}

/*
 * fr_transform_rotate_around - 创建绕指定点旋转的矩阵
 *
 * 等价于 T(cx,cy) * R(angle) * T(-cx,-cy)
 */
void fr_transform_rotate_around(fr_matrix_t *mat, float angle_degrees,
                                int32_t cx, int32_t cy)
{
    if (mat == NULL) return;
    int32_t angle_int = (int32_t)angle_degrees;
    fr_fixed_t c = fast_cos(angle_int);
    fr_fixed_t s = fast_sin(angle_int);

    fr_fixed_t fcx = FR_INT_TO_FIXED(cx);
    fr_fixed_t fcy = FR_INT_TO_FIXED(cy);

    /* m[2] = cx - cx*cos + cy*sin */
    /* m[5] = cy - cx*sin - cy*cos */
    mat->m[0] = c;
    mat->m[1] = -s;
    mat->m[2] = fcx - FR_FIXED_MUL(fcx, c) + FR_FIXED_MUL(fcy, s);
    mat->m[3] = s;
    mat->m[4] = c;
    mat->m[5] = fcy - FR_FIXED_MUL(fcx, s) - FR_FIXED_MUL(fcy, c);
    mat->m[6] = 0;
    mat->m[7] = 0;
    mat->m[8] = FR_FIXED_ONE;
}

/*
 * fr_transform_scale - 创建缩放矩阵
 */
void fr_transform_scale(fr_matrix_t *mat, float sx, float sy)
{
    if (mat == NULL) return;
    mat->m[0] = FR_FLOAT_TO_FIXED(sx);  mat->m[1] = 0;  mat->m[2] = 0;
    mat->m[3] = 0;  mat->m[4] = FR_FLOAT_TO_FIXED(sy);  mat->m[5] = 0;
    mat->m[6] = 0;  mat->m[7] = 0;  mat->m[8] = FR_FIXED_ONE;
}

/*
 * fr_transform_scale_nonuniform - 创建非均匀缩放矩阵 (同 scale)
 */
void fr_transform_scale_nonuniform(fr_matrix_t *mat, float sx, float sy)
{
    fr_transform_scale(mat, sx, sy);
}

/*
 * fr_transform_matrix_multiply - 3x3 矩阵乘法: result = a * b
 */
void fr_transform_matrix_multiply(fr_matrix_t *result,
                                  const fr_matrix_t *a,
                                  const fr_matrix_t *b)
{
    if (result == NULL || a == NULL || b == NULL) return;

    fr_fixed_t tmp[9];

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            int64_t sum = 0;
            for (int k = 0; k < 3; k++) {
                sum += (int64_t)a->m[i * 3 + k] * (int64_t)b->m[k * 3 + j];
            }
            tmp[i * 3 + j] = (fr_fixed_t)(sum >> FR_FIXED_SHIFT);
        }
    }

    for (int i = 0; i < 9; i++) {
        result->m[i] = tmp[i];
    }
}

/*
 * fr_transform_matrix_inverse - 计算 3x3 矩阵的逆矩阵
 *
 * 使用伴随矩阵法。
 * 返回 0=成功, -1=不可逆 (行列式为 0)
 */
int fr_transform_matrix_inverse(fr_matrix_t *result,
                                const fr_matrix_t *mat)
{
    if (result == NULL || mat == NULL) return -1;

    /* 计算行列式 */
    /* det = m[0]*m[4]*m[8] + m[1]*m[5]*m[6] + m[2]*m[3]*m[7]
     *     - m[2]*m[4]*m[6] - m[1]*m[3]*m[8] - m[0]*m[5]*m[7] */

    int64_t a = (int64_t)mat->m[0];
    int64_t b = (int64_t)mat->m[1];
    int64_t c = (int64_t)mat->m[2];
    int64_t d = (int64_t)mat->m[3];
    int64_t e = (int64_t)mat->m[4];
    int64_t f = (int64_t)mat->m[5];
    int64_t g = (int64_t)mat->m[6];
    int64_t h = (int64_t)mat->m[7];
    int64_t i = (int64_t)mat->m[8];

    int64_t det = (a*e*i + b*f*g + c*d*h
                    - c*e*g - b*d*i - a*f*h) >> FR_FIXED_SHIFT;

    if (det == 0) return -1; /* 不可逆 */

    /* 对于仿射变换, 最后一行通常为 [0,0,1],
     * 可以用简化公式计算逆矩阵 */
    if (g == 0 && h == 0 && i == FR_FIXED_ONE) {
        /* 仿射矩阵的逆 */
        /* 缩放部分: [a b; d e] 的逆 = 1/(ae-bd) * [e -b; -d a] */
        int64_t scale_det = a*e - b*d;

        /* 检查是否为整数/标准变换 */
        if (scale_det == 0) {
            /* 使用一般方法 */
        } else {
            fr_fixed_t inv_a = (fr_fixed_t)((e << FR_FIXED_SHIFT * 2) / scale_det);
            /* 实际上我们需要: result = 1/(ae-bd) * |  e  -b   b*f - c*e |
             *                                         | -d   a   c*d - a*f | */
            /* 简化计算: 使用定点除法 */

            fr_fixed_t scl_inv = (fr_fixed_t)(
                ((int64_t)FR_FIXED_SCALE * FR_FIXED_SCALE) / scale_det);

            result->m[0] = (fr_fixed_t)(((int64_t)e * scl_inv) >> FR_FIXED_SHIFT);
            result->m[1] = (fr_fixed_t)(((int64_t)(-b) * scl_inv) >> FR_FIXED_SHIFT);
            result->m[2] = (fr_fixed_t)(((int64_t)(b*f - c*e) * scl_inv) >> FR_FIXED_SHIFT);
            result->m[3] = (fr_fixed_t)(((int64_t)(-d) * scl_inv) >> FR_FIXED_SHIFT);
            result->m[4] = (fr_fixed_t)(((int64_t)a * scl_inv) >> FR_FIXED_SHIFT);
            result->m[5] = (fr_fixed_t)(((int64_t)(c*d - a*f) * scl_inv) >> FR_FIXED_SHIFT);
            result->m[6] = 0;
            result->m[7] = 0;
            result->m[8] = FR_FIXED_ONE;
            return 0;
        }
    }

    /* 一般 3x3 逆矩阵计算 (使用伴随矩阵) */
    int64_t inv_det = ((int64_t)FR_FIXED_SCALE * FR_FIXED_SCALE) / det;

    /* 伴随矩阵的每个元素 */
    result->m[0] = (fr_fixed_t)(((e*i - f*h) * inv_det) >> FR_FIXED_SHIFT);
    result->m[1] = (fr_fixed_t)(((c*h - b*i) * inv_det) >> FR_FIXED_SHIFT);
    result->m[2] = (fr_fixed_t)(((b*f - c*e) * inv_det) >> FR_FIXED_SHIFT);
    result->m[3] = (fr_fixed_t)(((f*g - d*i) * inv_det) >> FR_FIXED_SHIFT);
    result->m[4] = (fr_fixed_t)(((a*i - c*g) * inv_det) >> FR_FIXED_SHIFT);
    result->m[5] = (fr_fixed_t)(((c*d - a*f) * inv_det) >> FR_FIXED_SHIFT);
    result->m[6] = (fr_fixed_t)(((d*h - e*g) * inv_det) >> FR_FIXED_SHIFT);
    result->m[7] = (fr_fixed_t)(((b*g - a*h) * inv_det) >> FR_FIXED_SHIFT);
    result->m[8] = (fr_fixed_t)(((a*e - b*d) * inv_det) >> FR_FIXED_SHIFT);

    return 0;
}

/* ================================================================
 *  变换栈实现
 * ================================================================ */

/*
 * fr_transform_stack_init - 初始化变换栈
 */
void fr_transform_stack_init(fr_transform_stack_t *stack)
{
    if (stack == NULL) return;
    memset(stack, 0, sizeof(fr_transform_stack_t));
    stack->depth = 0;
    stack->stack[0] = FR_MATRIX_IDENTITY;
    stack->clip.enabled = 0;
    stack->clip.x = 0;
    stack->clip.y = 0;
    stack->clip.w = 0;
    stack->clip.h = 0;
}

/*
 * fr_transform_stack_push - 压入新变换层级
 *
 * 复制当前栈顶的变换矩阵到下一层,
 * 使得子控件可以在此基础上继续变换。
 */
void fr_transform_stack_push(fr_transform_stack_t *stack)
{
    if (stack == NULL) return;
    if (stack->depth >= FR_TRANSFORM_STACK_MAX_DEPTH - 1) return;

    stack->depth++;
    stack->stack[stack->depth] = stack->stack[stack->depth - 1];

    /* 保存当前裁剪区域 */
    stack->clip_stack[stack->depth] = stack->clip;
}

/*
 * fr_transform_stack_pop - 弹出变换层级
 */
void fr_transform_stack_pop(fr_transform_stack_t *stack)
{
    if (stack == NULL) return;
    if (stack->depth <= 0) return;

    /* 恢复裁剪区域 */
    stack->clip = stack->clip_stack[stack->depth];

    stack->depth--;
}

/*
 * fr_transform_stack_top - 获取栈顶变换矩阵
 */
const fr_matrix_t *fr_transform_stack_top(fr_transform_stack_t *stack)
{
    if (stack == NULL) return &FR_MATRIX_IDENTITY;
    return &stack->stack[stack->depth];
}

/*
 * fr_transform_stack_apply - 应用变换到栈顶
 */
void fr_transform_stack_apply(fr_transform_stack_t *stack,
                              const fr_matrix_t *mat)
{
    if (stack == NULL || mat == NULL) return;

    fr_matrix_t tmp;
    fr_transform_matrix_multiply(&tmp, &stack->stack[stack->depth], mat);
    stack->stack[stack->depth] = tmp;
}

/*
 * fr_transform_stack_reset - 重置栈顶到恒等变换
 */
void fr_transform_stack_reset(fr_transform_stack_t *stack)
{
    if (stack == NULL) return;
    stack->stack[stack->depth] = FR_MATRIX_IDENTITY;
}

/* ================================================================
 *  点变换实现
 * ================================================================ */

/*
 * fr_transform_point - 将点通过矩阵变换
 */
void fr_transform_point(const fr_matrix_t *mat,
                        int32_t sx, int32_t sy,
                        int32_t *dx, int32_t *dy)
{
    if (mat == NULL || dx == NULL || dy == NULL) return;

    fr_fixed_t fsx = FR_INT_TO_FIXED(sx);
    fr_fixed_t fsy = FR_INT_TO_FIXED(sy);

    fr_fixed_t fdx, fdy;
    fr_transform_point_fixed(mat, fsx, fsy, &fdx, &fdy);

    *dx = FR_FIXED_TO_INT(fdx);
    *dy = FR_FIXED_TO_INT(fdy);
}

/*
 * fr_transform_point_fixed - 定点数版本的点变换
 */
void fr_transform_point_fixed(const fr_matrix_t *mat,
                              fr_fixed_t sx, fr_fixed_t sy,
                              fr_fixed_t *dx, fr_fixed_t *dy)
{
    if (mat == NULL || dx == NULL || dy == NULL) return;

    /* 齐次坐标变换: [x'] = [m0 m1 m2] * [x]
     *               [y'] = [m3 m4 m5] * [y]
     *                               [1] */
    int64_t rx = (int64_t)mat->m[0] * sx +
                 (int64_t)mat->m[1] * sy +
                 (int64_t)mat->m[2] * FR_FIXED_SCALE;

    int64_t ry = (int64_t)mat->m[3] * sx +
                 (int64_t)mat->m[4] * sy +
                 (int64_t)mat->m[5] * FR_FIXED_SCALE;

    /* 透视除法 (第三个分量) */
    /* 对于仿射变换, w = m6*x + m7*y + m8*1 */
    int64_t w = (int64_t)mat->m[6] * sx +
                (int64_t)mat->m[7] * sy +
                (int64_t)mat->m[8] * FR_FIXED_SCALE;

    if (w != 0 && w != ((int64_t)FR_FIXED_SCALE * FR_FIXED_SCALE)) {
        *dx = (fr_fixed_t)((rx * FR_FIXED_SCALE) / w);
        *dy = (fr_fixed_t)((ry * FR_FIXED_SCALE) / w);
    } else if (w == ((int64_t)FR_FIXED_SCALE * FR_FIXED_SCALE)) {
        *dx = (fr_fixed_t)(rx / FR_FIXED_SCALE);
        *dy = (fr_fixed_t)(ry / FR_FIXED_SCALE);
    } else {
        *dx = (fr_fixed_t)(rx / FR_FIXED_SCALE);
        *dy = (fr_fixed_t)(ry / FR_FIXED_SCALE);
    }
}

/*
 * fr_transform_stack_point - 使用变换栈变换点
 */
void fr_transform_stack_point(fr_transform_stack_t *stack,
                              int32_t sx, int32_t sy,
                              int32_t *dx, int32_t *dy)
{
    if (stack == NULL || dx == NULL || dy == NULL) return;
    fr_transform_point(&stack->stack[stack->depth], sx, sy, dx, dy);
}

/* ================================================================
 *  包围盒计算实现
 * ================================================================ */

/*
 * fr_transform_bounding_box - 计算矩形变换后的包围盒
 *
 * 算法: 对矩形的 4 个角点分别应用变换矩阵,
 * 然后取最小/最大的 x, y 值作为包围盒。
 */
void fr_transform_bounding_box(const fr_matrix_t *mat,
                               int32_t x, int32_t y,
                               int32_t w, int32_t h,
                               int32_t *out_x, int32_t *out_y,
                               int32_t *out_w, int32_t *out_h)
{
    if (mat == NULL || out_x == NULL || out_y == NULL ||
        out_w == NULL || out_h == NULL) return;

    /* 4 个角点 */
    int32_t corners_x[4], corners_y[4];

    /* 左上 */
    fr_transform_point(mat, x, y, &corners_x[0], &corners_y[0]);
    /* 右上 */
    fr_transform_point(mat, x + w, y, &corners_x[1], &corners_y[1]);
    /* 右下 */
    fr_transform_point(mat, x + w, y + h, &corners_x[2], &corners_y[2]);
    /* 左下 */
    fr_transform_point(mat, x, y + h, &corners_x[3], &corners_y[3]);

    /* 寻找包围盒 */
    int32_t min_x = corners_x[0], max_x = corners_x[0];
    int32_t min_y = corners_y[0], max_y = corners_y[0];

    for (int i = 1; i < 4; i++) {
        if (corners_x[i] < min_x) min_x = corners_x[i];
        if (corners_x[i] > max_x) max_x = corners_x[i];
        if (corners_y[i] < min_y) min_y = corners_y[i];
        if (corners_y[i] > max_y) max_y = corners_y[i];
    }

    *out_x = min_x;
    *out_y = min_y;
    *out_w = max_x - min_x;
    *out_h = max_y - min_y;
}

/*
 * fr_transform_stack_bounding_box - 使用变换栈计算包围盒
 */
void fr_transform_stack_bounding_box(fr_transform_stack_t *stack,
                                     int32_t x, int32_t y,
                                     int32_t w, int32_t h,
                                     int32_t *out_x, int32_t *out_y,
                                     int32_t *out_w, int32_t *out_h)
{
    if (stack == NULL) return;
    fr_transform_bounding_box(&stack->stack[stack->depth],
                              x, y, w, h, out_x, out_y, out_w, out_h);
}

/* ================================================================
 *  坐标系统转换实现
 * ================================================================ */

/*
 * fr_coord_screen_to_widget - 屏幕坐标转控件坐标
 *
 * 简单的偏移减法: widget坐标 = 屏幕坐标 - 控件位置
 */
void fr_coord_screen_to_widget(int32_t screen_x, int32_t screen_y,
                               int32_t widget_x, int32_t widget_y,
                               int32_t *out_x, int32_t *out_y)
{
    if (out_x == NULL || out_y == NULL) return;
    *out_x = screen_x - widget_x;
    *out_y = screen_y - widget_y;
}

/*
 * fr_coord_widget_to_screen - 控件坐标转屏幕坐标
 *
 * 简单的偏移加法: 屏幕坐标 = 控件坐标 + 控件位置
 */
void fr_coord_widget_to_screen(int32_t widget_local_x, int32_t widget_local_y,
                               int32_t widget_screen_x, int32_t widget_screen_y,
                               int32_t *out_x, int32_t *out_y)
{
    if (out_x == NULL || out_y == NULL) return;
    *out_x = widget_local_x + widget_screen_x;
    *out_y = widget_local_y + widget_screen_y;
}

/*
 * fr_coord_widget_to_local - 控件坐标转局部坐标
 *
 * 应用当前变换栈的逆变换, 将控件空间中的坐标
 * 转换为局部坐标系中的坐标。
 */
void fr_coord_widget_to_local(fr_transform_stack_t *stack,
                              int32_t wx, int32_t wy,
                              int32_t *lx, int32_t *ly)
{
    if (stack == NULL || lx == NULL || ly == NULL) return;

    /* 求当前变换矩阵的逆 */
    fr_matrix_t inv;
    if (fr_transform_matrix_inverse(&inv, &stack->stack[stack->depth]) != 0) {
        /* 不可逆: 返回原始坐标 */
        *lx = wx;
        *ly = wy;
        return;
    }

    fr_transform_point(&inv, wx, wy, lx, ly);
}

/*
 * fr_coord_local_to_widget - 局部坐标转控件坐标
 *
 * 应用当前变换栈的正向变换, 将局部坐标转换为控件坐标。
 */
void fr_coord_local_to_widget(fr_transform_stack_t *stack,
                              int32_t lx, int32_t ly,
                              int32_t *wx, int32_t *wy)
{
    if (stack == NULL || wx == NULL || wy == NULL) return;
    fr_transform_point(&stack->stack[stack->depth], lx, ly, wx, wy);
}

/* ================================================================
 *  裁剪区域管理实现
 * ================================================================ */

/*
 * fr_clip_set - 设置裁剪区域
 */
void fr_clip_set(fr_clip_region_t *clip, int x, int y, int w, int h)
{
    if (clip == NULL) return;
    clip->x = x;
    clip->y = y;
    clip->w = w;
    clip->h = h;
    clip->enabled = 1;
}

/*
 * fr_clip_disable - 禁用裁剪
 */
void fr_clip_disable(fr_clip_region_t *clip)
{
    if (clip == NULL) return;
    clip->enabled = 0;
}

/*
 * fr_clip_enable - 启用裁剪
 */
void fr_clip_enable(fr_clip_region_t *clip)
{
    if (clip == NULL) return;
    clip->enabled = 1;
}

/*
 * fr_clip_test_point - 测试点是否在裁剪区域内
 */
int fr_clip_test_point(const fr_clip_region_t *clip, int x, int y)
{
    if (clip == NULL || !clip->enabled) return 1; /* 无裁剪: 全部可见 */

    if (x < clip->x || x >= clip->x + clip->w) return 0;
    if (y < clip->y || y >= clip->y + clip->h) return 0;
    return 1;
}

/*
 * fr_clip_test_rect - 测试矩形是否与裁剪区域相交
 *
 * 返回: 0=完全在外, 1=部分或完全可见
 */
int fr_clip_test_rect(const fr_clip_region_t *clip,
                      int x, int y, int w, int h)
{
    if (clip == NULL || !clip->enabled) return 1; /* 无裁剪: 全部可见 */

    /* 检查完全在外部 */
    if (x + w <= clip->x || x >= clip->x + clip->w) return 0;
    if (y + h <= clip->y || y >= clip->y + clip->h) return 0;

    return 1; /* 至少部分可见 */
}

/*
 * fr_clip_intersect - 求两个裁剪区域的交集
 */
void fr_clip_intersect(fr_clip_region_t *result,
                       const fr_clip_region_t *a,
                       const fr_clip_region_t *b)
{
    if (result == NULL) return;

    /* 至少有一个不启用时, 结果等于另一个 */
    if (a == NULL || !a->enabled) {
        if (b != NULL) {
            *result = *b;
        } else {
            fr_clip_disable(result);
        }
        return;
    }

    if (b == NULL || !b->enabled) {
        *result = *a;
        return;
    }

    /* 计算交集矩形 */
    int x1 = a->x > b->x ? a->x : b->x;
    int y1 = a->y > b->y ? a->y : b->y;
    int x2 = (a->x + a->w < b->x + b->w) ? (a->x + a->w) : (b->x + b->w);
    int y2 = (a->y + a->h < b->y + b->h) ? (a->y + a->h) : (b->y + b->h);

    if (x2 <= x1 || y2 <= y1) {
        /* 不相交: 设置一个空区域 */
        result->x = 0;
        result->y = 0;
        result->w = 0;
        result->h = 0;
        result->enabled = 1; /* 启用但为空 */
        return;
    }

    result->x = x1;
    result->y = y1;
    result->w = x2 - x1;
    result->h = y2 - y1;
    result->enabled = 1;
}

/* ================================================================
 *  变换栈裁剪集成
 * ================================================================ */

/*
 * fr_transform_stack_push_clip - 压入变换层级并设置裁剪
 *
 * 将当前裁剪区域与传入的裁剪区域求交集后作为新的裁剪区域保存。
 */
void fr_transform_stack_push_clip(fr_transform_stack_t *stack,
                                  int x, int y, int w, int h)
{
    if (stack == NULL) return;
    if (stack->depth >= FR_TRANSFORM_STACK_MAX_DEPTH - 1) return;

    /* 先压入变换 */
    fr_transform_stack_push(stack);

    /* 计算新的裁剪区域 */
    fr_clip_region_t new_clip;
    fr_clip_set(&new_clip, x, y, w, h);

    /* 求交集 */
    fr_clip_region_t intersected;
    fr_clip_intersect(&intersected, &stack->clip, &new_clip);

    stack->clip = intersected;
}

/*
 * fr_transform_stack_pop_clip - 弹出变换层级并恢复裁剪
 */
void fr_transform_stack_pop_clip(fr_transform_stack_t *stack)
{
    fr_transform_stack_pop(stack);
}