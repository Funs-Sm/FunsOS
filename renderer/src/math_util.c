/* math_util.c - 软件实现的浮点数学库实现
 * Taylor 级数 sin/cos, Newton-Raphson sqrt, 向量/矩阵运算
 */

#include "math_util.h"
#include "string.h"

/* ================================================================
 *  内部辅助: 角度归一化到 [-PI, PI]
 * ================================================================ */

static float fr_normalize_angle(float x)
{
    while (x > M_PI_F)   x -= 2.0f * M_PI_F;
    while (x < -M_PI_F)  x += 2.0f * M_PI_F;
    return x;
}

/* ================================================================
 *  三角函数 (Taylor 级数, 10项展开)
 * ================================================================ */

float fr_sin(float rad)
{
    rad = fr_normalize_angle(rad);
    float result = 0.0f;
    float term = rad;           /* x^1 / 1! */
    for (int i = 1; i <= 10; i++) {
        result += term;
        /* 下一项: term * (-x^2) / ((2i)*(2i+1)) */
        term *= -rad * rad / (float)(2 * i * (2 * i + 1));
    }
    return result;
}

float fr_cos(float rad)
{
    rad = fr_normalize_angle(rad);
    float result = 0.0f;
    float term = 1.0f;          /* x^0 / 0! */
    for (int i = 1; i <= 10; i++) {
        result += term;
        /* 下一项: term * (-x^2) / ((2i-1)*(2i)) */
        term *= -rad * rad / (float)((2 * i - 1) * (2 * i));
    }
    return result;
}

/* ================================================================
 *  平方根 (Newton-Raphson 迭代)
 *  x(n+1) = (x(n) + a/x(n)) / 2
 * ================================================================ */

float fr_sqrt(float x)
{
    if (x < 0.0f) return 0.0f;
    if (x == 0.0f) return 0.0f;

    float guess = x * 0.5f;
    for (int i = 0; i < 30; i++) {
        float next = (guess + x / guess) * 0.5f;
        if (next == guess) break;
        guess = next;
    }
    return guess;
}

/* ================================================================
 *  atan2 (基于 arctan 级数展开)
 * ================================================================ */

static float fr_atan_internal(float x)
{
    if (x > 1.0f) return M_PI_2_F - fr_atan_internal(1.0f / x);
    if (x < -1.0f) return -M_PI_2_F - fr_atan_internal(1.0f / x);
    float result = 0.0f;
    float term = x;
    float x2 = x * x;
    for (int i = 1; i <= 20; i++) {
        result += term / (float)(2 * i - 1);
        term *= -x2;
    }
    return result;
}

float fr_atan2(float y, float x)
{
    if (x > 0.0f)       return fr_atan_internal(y / x);
    if (x < 0.0f && y >= 0.0f) return fr_atan_internal(y / x) + M_PI_F;
    if (x < 0.0f && y < 0.0f)  return fr_atan_internal(y / x) - M_PI_F;
    if (x == 0.0f && y > 0.0f) return M_PI_2_F;
    if (x == 0.0f && y < 0.0f) return -M_PI_2_F;
    return 0.0f;
}

/* ================================================================
 *  基础数学函数
 * ================================================================ */

float fr_fabsf(float x)
{
    return x < 0.0f ? -x : x;
}

float fr_floorf(float x)
{
    int i = (int)x;
    if (x < 0.0f && (float)i != x) i--;
    return (float)i;
}

float fr_ceilf(float x)
{
    int i = (int)x;
    if (x > 0.0f && (float)i != x) i++;
    return (float)i;
}

float fr_powf(float base, float exp_val)
{
    if (base <= 0.0f) return 0.0f;
    /* 使用 e^(exp*ln(base)) */
    float ln_base = 0.0f;
    float z = (base - 1.0f) / (base + 1.0f);
    float z2 = z * z;
    float term = z;
    for (int i = 1; i <= 40; i += 2) {
        ln_base += term / (float)i;
        term *= z2;
    }
    ln_base *= 2.0f;

    /* e^(exp * ln_base) via Taylor series */
    float t = exp_val * ln_base;
    float result = 1.0f;
    float pterm = 1.0f;
    for (int i = 1; i <= 25; i++) {
        pterm *= t / (float)i;
        result += pterm;
    }
    return result;
}

float fr_log2f(float x)
{
    if (x <= 0.0f) return 0.0f;
    /* log2(x) = ln(x) / ln(2) */
    float y = (x - 1.0f) / (x + 1.0f);
    float y2 = y * y;
    float result = 0.0f;
    float term = y;
    for (int i = 1; i <= 40; i += 2) {
        result += term / (float)i;
        term *= y2;
    }
    return 2.0f * result / 0.69314718056f;  /* / ln(2) */
}

/* ================================================================
 *  vec2 运算
 * ================================================================ */

vec2_t fr_vec2_add(vec2_t a, vec2_t b)
{
    vec2_t r = {a.x + b.x, a.y + b.y};
    return r;
}

vec2_t fr_vec2_sub(vec2_t a, vec2_t b)
{
    vec2_t r = {a.x - b.x, a.y - b.y};
    return r;
}

vec2_t fr_vec2_mul(vec2_t v, float s)
{
    vec2_t r = {v.x * s, v.y * s};
    return r;
}

float fr_vec2_dot(vec2_t a, vec2_t b)
{
    return a.x * b.x + a.y * b.y;
}

float fr_vec2_len(vec2_t v)
{
    return fr_sqrt(v.x * v.x + v.y * v.y);
}

vec2_t fr_vec2_norm(vec2_t v)
{
    float len = fr_vec2_len(v);
    if (len < 0.0001f) {
        vec2_t zero = {0.0f, 0.0f};
        return zero;
    }
    vec2_t r = {v.x / len, v.y / len};
    return r;
}

/* ================================================================
 *  vec3 运算
 * ================================================================ */

vec3_t fr_vec3_add(vec3_t a, vec3_t b)
{
    vec3_t r = {a.x + b.x, a.y + b.y, a.z + b.z};
    return r;
}

vec3_t fr_vec3_sub(vec3_t a, vec3_t b)
{
    vec3_t r = {a.x - b.x, a.y - b.y, a.z - b.z};
    return r;
}

vec3_t fr_vec3_mul(vec3_t v, float s)
{
    vec3_t r = {v.x * s, v.y * s, v.z * s};
    return r;
}

float fr_vec3_dot(vec3_t a, vec3_t b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

vec3_t fr_vec3_cross(vec3_t a, vec3_t b)
{
    vec3_t r = {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
    return r;
}

float fr_vec3_len(vec3_t v)
{
    return fr_sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

vec3_t fr_vec3_norm(vec3_t v)
{
    float len = fr_vec3_len(v);
    if (len < 0.0001f) {
        vec3_t zero = {0.0f, 0.0f, 0.0f};
        return zero;
    }
    vec3_t r = {v.x / len, v.y / len, v.z / len};
    return r;
}

/* ================================================================
 *  mat4 运算 (列主序)
 *
 *  内存布局: m[col*4 + row]
 *  即 m[0..3] 是第0列, m[4..7] 是第1列, ...
 * ================================================================ */

mat4_t fr_mat4_identity(void)
{
    mat4_t m;
    memset(&m, 0, sizeof(m));
    m.m[0]  = 1.0f;  m.m[5]  = 1.0f;
    m.m[10] = 1.0f;  m.m[15] = 1.0f;
    return m;
}

mat4_t fr_mat4_mul(mat4_t a, mat4_t b)
{
    mat4_t r;
    memset(&r, 0, sizeof(r));
    for (int col = 0; col < 4; col++)
        for (int row = 0; row < 4; row++)
            for (int k = 0; k < 4; k++)
                r.m[col * 4 + row] += a.m[k * 4 + row] * b.m[col * 4 + k];
    return r;
}

mat4_t fr_mat4_translate(float x, float y, float z)
{
    mat4_t m = fr_mat4_identity();
    m.m[12] = x;
    m.m[13] = y;
    m.m[14] = z;
    return m;
}

mat4_t fr_mat4_scale(float x, float y, float z)
{
    mat4_t m = fr_mat4_identity();
    m.m[0]  = x;
    m.m[5]  = y;
    m.m[10] = z;
    return m;
}

mat4_t fr_mat4_rotate_x(float rad)
{
    float c = fr_cos(rad), s = fr_sin(rad);
    mat4_t m = fr_mat4_identity();
    m.m[5]  = c;   m.m[6]  = s;
    m.m[9]  = -s;  m.m[10] = c;
    return m;
}

mat4_t fr_mat4_rotate_y(float rad)
{
    float c = fr_cos(rad), s = fr_sin(rad);
    mat4_t m = fr_mat4_identity();
    m.m[0]  = c;   m.m[2]  = -s;
    m.m[8]  = s;   m.m[10] = c;
    return m;
}

mat4_t fr_mat4_rotate_z(float rad)
{
    float c = fr_cos(rad), s = fr_sin(rad);
    mat4_t m = fr_mat4_identity();
    m.m[0] = c;   m.m[1] = s;
    m.m[4] = -s;  m.m[5] = c;
    return m;
}

mat4_t fr_mat4_frustum(float left, float right, float bottom, float top,
                     float near_val, float far_val)
{
    float rl = right - left;
    float tb = top - bottom;
    float fn = far_val - near_val;
    mat4_t m;
    memset(&m, 0, sizeof(m));

    m.m[0]  = 2.0f * near_val / rl;
    m.m[5]  = 2.0f * near_val / tb;
    m.m[8]  = (right + left) / rl;
    m.m[9]  = (top + bottom) / tb;
    m.m[10] = -(far_val + near_val) / fn;
    m.m[11] = -1.0f;
    m.m[14] = -2.0f * far_val * near_val / fn;

    return m;
}

mat4_t fr_mat4_perspective(float fov_deg, float aspect, float near_val, float far_val)
{
    float fov_rad = fr_deg2rad(fov_deg);
    float tan_half_fov = fr_sin(fov_rad * 0.5f) / fr_cos(fov_rad * 0.5f);

    mat4_t m;
    memset(&m, 0, sizeof(m));

    m.m[0]  = 1.0f / (aspect * tan_half_fov);
    m.m[5]  = 1.0f / tan_half_fov;
    m.m[10] = -(far_val + near_val) / (far_val - near_val);
    m.m[11] = -1.0f;
    m.m[14] = -2.0f * far_val * near_val / (far_val - near_val);

    return m;
}

mat4_t fr_mat4_ortho(float left, float right, float bottom, float top,
                   float near_val, float far_val)
{
    mat4_t m;
    memset(&m, 0, sizeof(m));

    m.m[0]  = 2.0f / (right - left);
    m.m[5]  = 2.0f / (top - bottom);
    m.m[10] = -2.0f / (far_val - near_val);
    m.m[12] = -(right + left) / (right - left);
    m.m[13] = -(top + bottom) / (top - bottom);
    m.m[14] = -(far_val + near_val) / (far_val - near_val);
    m.m[15] = 1.0f;

    return m;
}

mat4_t fr_mat4_lookat(vec3_t eye, vec3_t center, vec3_t up)
{
    vec3_t f = fr_vec3_norm(fr_vec3_sub(center, eye));  /* forward */
    vec3_t s = fr_vec3_norm(fr_vec3_cross(f, up));      /* side */
    vec3_t u = fr_vec3_cross(s, f);                   /* true up */

    mat4_t m;
    memset(&m, 0, sizeof(m));

    m.m[0]  = s.x;     m.m[4]  = s.y;     m.m[8]  = s.z;
    m.m[1]  = u.x;     m.m[5]  = u.y;     m.m[9]  = u.z;
    m.m[2]  = -f.x;    m.m[6]  = -f.y;    m.m[10] = -f.z;
    m.m[12] = -fr_vec3_dot(s, eye);
    m.m[13] = -fr_vec3_dot(u, eye);
    m.m[14] =  fr_vec3_dot(f, eye);
    m.m[15] = 1.0f;

    return m;
}

vec3_t fr_mat4_mul_vec3(mat4_t m, vec3_t v)
{
    vec3_t r;
    r.x = m.m[0]*v.x + m.m[4]*v.y + m.m[8]*v.z  + m.m[12];
    r.y = m.m[1]*v.x + m.m[5]*v.y + m.m[9]*v.z  + m.m[13];
    r.z = m.m[2]*v.x + m.m[6]*v.y + m.m[10]*v.z + m.m[14];

    /* 齐次坐标透视除法 */
    float w = m.m[3]*v.x + m.m[7]*v.y + m.m[11]*v.z + m.m[15];
    if (fr_fabsf(w) > 0.0001f) {
        r.x /= w;
        r.y /= w;
        r.z /= w;
    }

    return r;
}

vec3_t fr_mat4_mul_dir(mat4_t m, vec3_t v)
{
    vec3_t r;
    r.x = m.m[0]*v.x + m.m[4]*v.y + m.m[8]*v.z;
    r.y = m.m[1]*v.x + m.m[5]*v.y + m.m[9]*v.z;
    r.z = m.m[2]*v.x + m.m[6]*v.y + m.m[10]*v.z;
    return r;
}

/* ================================================================
 *  矩阵求逆 (Gauss-Jordan 消元法, 4x4)
 * ================================================================ */

mat4_t fr_mat4_inverse(mat4_t m)
{
    mat4_t inv;
    int idxc[4], idxr[4], ipiv[4] = {0, 0, 0, 0};

    memcpy(inv.m, m.m, sizeof(inv.m));

    for (int i = 0; i < 4; i++) {
        float big = 0.0f;
        int irow = 0, icol = 0;

        for (int j = 0; j < 4; j++) {
            if (ipiv[j] != 1) {
                for (int k = 0; k < 4; k++) {
                    if (ipiv[k] == 0) {
                        float val = fr_fabsf(inv.m[j * 4 + k]);
                        if (val >= big) {
                            big = val;
                            irow = j;
                            icol = k;
                        }
                    } else if (ipiv[k] > 1) {
                        /* 奇异矩阵, 返回零矩阵 */
                        mat4_t zero;
                        memset(&zero, 0, sizeof(zero));
                        return zero;
                    }
                }
            }
        }

        ipiv[icol]++;
        if (irow != icol) {
            for (int k = 0; k < 4; k++) {
                float tmp = inv.m[irow * 4 + k];
                inv.m[irow * 4 + k] = inv.m[icol * 4 + k];
                inv.m[icol * 4 + k] = tmp;
            }
        }

        idxr[i] = irow;
        idxc[i] = icol;

        float pivinv = 1.0f / inv.m[icol * 4 + icol];
        inv.m[icol * 4 + icol] = 1.0f;

        for (int k = 0; k < 4; k++)
            inv.m[icol * 4 + k] *= pivinv;

        for (int j = 0; j < 4; j++) {
            if (j != icol) {
                float dum = inv.m[j * 4 + icol];
                inv.m[j * 4 + icol] = 0.0f;
                for (int k = 0; k < 4; k++)
                    inv.m[j * 4 + k] -= dum * inv.m[icol * 4 + k];
            }
        }
    }

    for (int i = 3; i >= 0; i--) {
        if (idxr[i] != idxc[i]) {
            for (int k = 0; k < 4; k++) {
                float tmp = inv.m[k * 4 + idxr[i]];
                inv.m[k * 4 + idxr[i]] = inv.m[k * 4 + idxc[i]];
                inv.m[k * 4 + idxc[i]] = tmp;
            }
        }
    }

    return inv;
}

mat4_t fr_mat4_transpose(mat4_t mat)
{
    mat4_t r;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            r.m[i * 4 + j] = mat.m[j * 4 + i];
    return r;
}
