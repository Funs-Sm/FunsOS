/*
 * shader.c - 软件可编程着色器管线
 *
 * 提供顶点着色器(VS)、片段着色器(FS)的注册和执行框架。
 * 每个着色器是一个函数指针，通过uniform/varying传递数据。
 */

#include "shader.h"
#include "math_util.h"
#include "stddef.h"
#include "string.h"

/* ================================================================
 *  全局状态
 * ================================================================ */

static shader_program_t programs[MAX_SHADER_PROGRAMS];
static uint32_t next_program_id = 1;
static uint32_t active_program = 0; /* 当前激活的程序索引 (0=无) */

/* ================================================================
 *  内部辅助函数
 * ================================================================ */

/*
 * find_program_by_id - 通过ID查找程序槽位
 *
 * 返回程序指针，未找到返回NULL。
 */
static shader_program_t *find_program_by_id(uint32_t program_id)
{
    for (int i = 0; i < MAX_SHADER_PROGRAMS; i++) {
        if (programs[i].used && programs[i].id == program_id) {
            return &programs[i];
        }
    }
    return NULL;
}

/*
 * find_free_slot - 查找空闲程序槽位
 *
 * 返回槽位索引，-1表示已满。
 */
static int find_free_slot(void)
{
    for (int i = 0; i < MAX_SHADER_PROGRAMS; i++) {
        if (!programs[i].used) {
            return i;
        }
    }
    return -1;
}

/*
 * find_uniform_by_name - 在程序中按名称查找Uniform
 */
static int find_uniform_by_name(shader_program_t *prog, const char *name)
{
    if (prog == NULL || name == NULL) return -1;

    for (int i = 0; i < prog->uniform_count; i++) {
        if (strncmp(prog->uniforms[i].name, name, 31) == 0) {
            return i;
        }
    }
    return -1;
}

/* ================================================================
 *  公共API实现 - 初始化
 * ================================================================ */

void fr_shader_init(void)
{
    memset(programs, 0, sizeof(programs));
    next_program_id = 1;
    active_program = 0;
}

/* ================================================================
 *  公共API实现 - 程序管理
 * ================================================================ */

uint32_t fr_program_create(void)
{
    int slot = find_free_slot();
    if (slot < 0) return 0; /* 已满 */

    shader_program_t *prog = &programs[slot];
    memset(prog, 0, sizeof(shader_program_t));

    prog->id = next_program_id++;
    prog->used = 1;
    prog->linked = 0;
    prog->depth_test = 1;   /* 默认开启深度测试 */
    prog->depth_write = 1;
    prog->blend_mode = 0;   /* 默认无混合 */

    return prog->id;
}

int fr_program_delete(uint32_t program_id)
{
    shader_program_t *prog = find_program_by_id(program_id);
    if (prog == NULL) return -1;

    /* 如果是当前激活的程序，取消激活 */
    if (active_program == program_id) {
        active_program = 0;
    }

    memset(prog, 0, sizeof(shader_program_t));
    return 0;
}

int fr_program_link(uint32_t program_id)
{
    shader_program_t *prog = find_program_by_id(program_id);
    if (prog == NULL) return -1;

    /* 检查VS和FS是否都已绑定 */
    if (prog->vertex_shader == NULL || prog->fragment_shader == NULL) {
        return -1; /* 着色器不完整 */
    }

    /* 标记为已链接 */
    prog->linked = 1;
    return 0;
}

int fr_program_use(uint32_t program_id)
{
    if (program_id == 0) {
        /* 取消激活 */
        active_program = 0;
        return 0;
    }

    shader_program_t *prog = find_program_by_id(program_id);
    if (prog == NULL) return -1;

    if (!prog->linked) {
        /* 尝试自动链接 */
        if (fr_program_link(program_id) != 0) {
            return -1; /* 链接失败 */
        }
    }

    active_program = program_id;
    return 0;
}

/* ================================================================
 *  公共API实现 - 着色器绑定
 * ================================================================ */

int fr_attach_vertex_shader(uint32_t program_id,
    void (*vs)(varying_t*, const vertex_t*, const uniform_t*, int))
{
    shader_program_t *prog = find_program_by_id(program_id);
    if (prog == NULL) return -1;

    prog->vertex_shader = vs;
    prog->linked = 0; /* 需要重新链接 */
    return 0;
}

int fr_attach_fragment_shader(uint32_t program_id,
    void (*fs)(color_t*, const varying_t*, const uniform_t*, int))
{
    shader_program_t *prog = find_program_by_id(program_id);
    if (prog == NULL) return -1;

    prog->fragment_shader = fs;
    prog->linked = 0; /* 需要重新链接 */
    return 0;
}

/* ================================================================
 *  公共API实现 - Uniform操作
 * ================================================================ */

int fr_uniform_location(uint32_t program_id, const char *name)
{
    shader_program_t *prog = find_program_by_id(program_id);
    if (prog == NULL || name == NULL) return -1;

    /* 先查找是否已存在 */
    int idx = find_uniform_by_name(prog, name);
    if (idx >= 0) return idx;

    /* 不存在则创建新的Uniform条目 */
    if (prog->uniform_count >= MAX_UNIFORMS) return -1;

    idx = prog->uniform_count++;
    strncpy(prog->uniforms[idx].name, name, 31);
    prog->uniforms[idx].name[31] = '\0';
    prog->uniforms[idx].type = UNIFORM_FLOAT; /* 默认类型 */
    prog->uniforms[idx].dirty = 1;

    return idx;
}

int fr_uniform_1f(int location, float v)
{
    shader_program_t *prog = find_program_by_id(active_program);
    if (prog == NULL || location < 0 || location >= prog->uniform_count)
        return -1;

    prog->uniforms[location].type = UNIFORM_FLOAT;
    prog->uniforms[location].data.f = v;
    prog->uniforms[location].dirty = 1;
    return 0;
}

int fr_uniform_2f(int location, float x, float y)
{
    shader_program_t *prog = find_program_by_id(active_program);
    if (prog == NULL || location < 0 || location >= prog->uniform_count)
        return -1;

    prog->uniforms[location].type = UNIFORM_VEC2;
    prog->uniforms[location].data.v2[0] = x;
    prog->uniforms[location].data.v2[1] = y;
    prog->uniforms[location].dirty = 1;
    return 0;
}

int fr_uniform_3f(int location, float x, float y, float z)
{
    shader_program_t *prog = find_program_by_id(active_program);
    if (prog == NULL || location < 0 || location >= prog->uniform_count)
        return -1;

    prog->uniforms[location].type = UNIFORM_VEC3;
    prog->uniforms[location].data.v3[0] = x;
    prog->uniforms[location].data.v3[1] = y;
    prog->uniforms[location].data.v3[2] = z;
    prog->uniforms[location].dirty = 1;
    return 0;
}

int fr_uniform_4f(int location, float x, float y, float z, float w)
{
    shader_program_t *prog = find_program_by_id(active_program);
    if (prog == NULL || location < 0 || location >= prog->uniform_count)
        return -1;

    prog->uniforms[location].type = UNIFORM_VEC4;
    prog->uniforms[location].data.v4[0] = x;
    prog->uniforms[location].data.v4[1] = y;
    prog->uniforms[location].data.v4[2] = z;
    prog->uniforms[location].data.v4[3] = w;
    prog->uniforms[location].dirty = 1;
    return 0;
}

int fr_uniform_mat4(int location, const float *m)
{
    shader_program_t *prog = find_program_by_id(active_program);
    if (prog == NULL || location < 0 || location >= prog->uniform_count || m == NULL)
        return -1;

    prog->uniforms[location].type = UNIFORM_MAT4;
    memcpy(prog->uniforms[location].data.m16, m, 16 * sizeof(float));
    prog->uniforms[location].dirty = 1;
    return 0;
}

int fr_uniform_1i(int location, int i)
{
    shader_program_t *prog = find_program_by_id(active_program);
    if (prog == NULL || location < 0 || location >= prog->uniform_count)
        return -1;

    prog->uniforms[location].type = UNIFORM_INT;
    prog->uniforms[location].data.i = i;
    prog->uniforms[location].dirty = 1;
    return 0;
}

/* ================================================================
 *  内置着色器实现
 * ================================================================ */

/*
 * fr_builtin_solid_vs - 纯色顶点着色器
 *
 * 直接传递位置到gl_Position，颜色到varying[0..3]。
 */
void fr_builtin_solid_vs(varying_t *o, const vertex_t *i,
                         const uniform_t *u, int n)
{
    (void)u; (void)n;

    /* 输出裁剪空间坐标 (假设输入已经是NDC或已经变换) */
    o->gl_Position[0] = i->x;
    o->gl_Position[1] = i->y;
    o->gl_Position[2] = i->z;
    o->gl_Position[3] = 1.0f;

    /* 传递颜色 */
    o->floats[0] = i->r;
    o->floats[1] = i->g;
    o->floats[2] = i->b;
    o->floats[3] = i->a;
    o->float_count = 4;
}

/*
 * fr_builtin_solid_fs - 纯色片段着色器
 *
 * 从varying读取颜色并输出。
 */
void fr_builtin_solid_fs(color_t *o, const varying_t *i,
                         const uniform_t *u, int n)
{
    (void)u; (void)n;

    if (i->float_count >= 4) {
        o->r = (uint8_t)i->floats[0];
        o->g = (uint8_t)i->floats[1];
        o->b = (uint8_t)i->floats[2];
        o->a = (uint8_t)i->floats[3];
    } else {
        o->r = o->g = o->b = o->a = 255;
    }
}

/*
 * builtin_color_fs - 颜色片段着色器 (兼容别名)
 *
 * 与 builtin_solid_fs 功能相同。
 */
void builtin_color_fs(color_t *o, const varying_t *i,
                      const uniform_t *u, int n)
{
    fr_builtin_solid_fs(o, i, u, n);
}

/*
 * builtin_texture_fs - 纹理片段着色器
 *
 * 从varying读取UV坐标，采样纹理后输出颜色。
 * 注意: 实际纹理采样需要纹理管理器的支持，
 * 这里使用简化实现: 将UV编码到颜色中用于调试。
 */
void builtin_texture_fs(color_t *o, const varying_t *i,
                        const uniform_t *u, int n)
{
    (void)u; (void)n;

    if (i->float_count >= 6) {
        /* floats[4..5] 是UV坐标 */
        float u_coord = i->floats[4];
        float v_coord = i->floats[5];

        /* 简化: 用UV生成棋盘格图案作为占位纹理 */
        int check = ((int)(u_coord * 8.0f) + (int)(v_coord * 8.0f)) & 1;
        if (check) {
            o->r = o->g = o->b = 200;
        } else {
            o->r = o->g = o->b = 100;
        }
        o->a = 255;
    } else {
        /* 无UV数据时输出品红色(调试用) */
        o->r = 255; o->g = 0; o->b = 255; o->a = 255;
    }
}

/*
 * builtin_phong_vs - Phong光照顶点着色器
 *
 * 计算世界空间的位置、法线、视线方向和光方向，
 * 存储到varying中供FS插值使用。
 *
 * 假设的Uniform布局:
 *   u_mat4[0]: ModelViewProjection矩阵
 *   u_mat4[1]: ModelView矩阵 (用于法线变换)
 *   u_vec4[0]: 相机位置 (world space)
 *   u_vec4[1]: 光源位置 (world space)
 */
void builtin_phong_vs(varying_t *o, const vertex_t *i,
                      const uniform_t *u, int n)
{
    /* 查找MVP矩阵 (通常在location 0) */
    mat4_t mvp = {{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}};
    for (int j = 0; j < n; j++) {
        if (u[j].type == UNIFORM_MAT4 && j == 0) {
            memcpy(mvp.m, u[j].data.m16, 16 * sizeof(float));
            break;
        }
    }

    /* 变换顶点位置 */
    vec3_t pos = {i->x, i->y, i->z};
    vec3_t transformed = fr_mat4_mul_vec3(mvp, pos);

    o->gl_Position[0] = transformed.x;
    o->gl_Position[1] = transformed.y;
    o->gl_Position[2] = transformed.z;
    o->gl_Position[3] = 1.0f;

    /* 传递世界空间法线 (简化: 假设法线矩阵=单位阵) */
    o->floats[0] = i->nx;
    o->floats[1] = i->ny;
    o->floats[2] = i->nz;
    /* 归一化法线 */
    float len = fr_sqrt(o->floats[0]*o->floats[0] +
                        o->floats[1]*o->floats[1] +
                        o->floats[2]*o->floats[2]);
    if (len > 0.0001f) {
        o->floats[0] /= len;
        o->floats[1] /= len;
        o->floats[2] /= len;
    }

    /* 传递世界空间位置 (用于计算视线方向) */
    o->floats[3] = i->x;
    o->floats[4] = i->y;
    o->floats[5] = i->z;

    /* 传递顶点颜色 */
    o->floats[6] = i->r / 255.0f;
    o->floats[7] = i->g / 255.0f;
    o->floats[8] = i->b / 255.0f;

    o->float_count = 9;
}

/*
 * builtin_phong_fs - Phong光照片段着色器
 *
 * 实现标准的Phong反射模型:
 *   FinalColor = Ambient + Diffuse + Specular
 *
 * Varying数据布局:
 *   [0..2]: 法线 (归一化)
 *   [3..5]: 世界空间位置
 *   [6..8]: 顶点颜色 (归一化)
 */
void builtin_phong_fs(color_t *o, const varying_t *i,
                      const uniform_t *u, int n)
{
    /* 从varying读取插值后的数据 */
    float nx = i->floats[0], ny = i->floats[1], nz = i->floats[2];
    float px = i->floats[3], py = i->floats[4], pz = i->floats[5];
    float vr = i->floats[6], vg = i->floats[7], vb = i->floats[8];

    /* 再次归一化插值后的法线 */
    float nlen = fr_sqrt(nx*nx + ny*ny + nz*nz);
    if (nlen > 0.0001f) { nx /= nlen; ny /= nlen; nz /= nlen; }

    /* 查找光源位置 (默认从正前方照射) */
    float lx = 0.0f, ly = 0.0f, lz = 10.0f;
    float light_r = 1.0f, light_g = 1.0f, light_b = 1.0f;
    for (int j = 0; j < n; j++) {
        if (u[j].type == UNIFORM_VEC4 && j == 1) {
            lx = u[j].data.v4[0]; ly = u[j].data.v4[1]; lz = u[j].data.v4[2];
            break;
        }
    }

    /* 计算光线方向 L = normalize(lightPos - fragPos) */
    float ldx = lx - px, ldy = ly - py, ldz = lz - pz;
    float llen = fr_sqrt(ldx*ldx + ldy*ldy + ldz*ldz);
    if (llen > 0.0001f) { ldx /= llen; ldy /= llen; ldz /= llen; }

    /* 漫反射: max(dot(N, L), 0) */
    float ndotl = nx * ldx + ny * ldy + nz * ldz;
    float diffuse = (ndotl > 0.0f) ? ndotl : 0.0f;

    /* 环境光系数 */
    float ambient = 0.15f;

    /* 高光反射 (Blinn-Phong简化版) */
    float specular = 0.0f;
    float shininess = 32.0f;

    /* 查找相机位置 */
    float camx = 0.0f, camy = 0.0f, camz = 5.0f;
    for (int j = 0; j < n; j++) {
        if (u[j].type == UNIFORM_VEC4 && j == 0) {
            camx = u[j].data.v4[0]; camy = u[j].data.v4[1]; camz = u[j].data.v4[2];
            break;
        }
    }

    /* 视线方向 V = normalize(camPos - fragPos) */
    float vdx = camx - px, vdy = camy - py, vdz = camz - pz;
    float vlen = fr_sqrt(vdx*vdx + vdy*vdy + vdz*vdz);
    if (vlen > 0.0001f) { vdx /= vlen; vdy /= vlen; vdz /= vlen; }

    /* 半程向量 H = normalize(L + V) */
    float hx = ldx + vdx, hy = ldy + vdy, hz = ldz + vdz;
    float hlen = fr_sqrt(hx*hx + hy*hy + hz*hz);
    if (hlen > 0.0001f) { hx /= hlen; hy /= hlen; hz /= hlen; }

    /* 高光: pow(max(dot(N, H), 0), shininess) */
    float ndoth = nx * hx + ny * hy + nz * hz;
    if (ndoth > 0.0f) {
        /* 使用powf近似: 多次乘法 */
        float s = ndoth;
        for (int p = 0; p < (int)shininess / 4; p++) s *= s;
        specular = s;
    }

    /* 组合最终颜色 */
    float final_r = (ambient + diffuse * 0.7f + specular * 0.5f) * vr * light_r;
    float final_g = (ambient + diffuse * 0.7f + specular * 0.5f) * vg * light_g;
    float final_b = (ambient + diffuse * 0.7f + specular * 0.5f) * vb * light_b;

    /* 钳制到 [0, 1] 并转换为8位 */
    o->r = (final_r > 1.0f) ? 255 : (final_r < 0.0f) ? 0 : (uint8_t)(final_r * 255.0f);
    o->g = (final_g > 1.0f) ? 255 : (final_g < 0.0f) ? 0 : (uint8_t)(final_g * 255.0f);
    o->b = (final_b > 1.0f) ? 255 : (final_b < 0.0f) ? 0 : (uint8_t)(final_b * 255.0f);
    o->a = 255;
}

/* ================================================================
 *  渲染执行
 * ================================================================ */

int fr_run_vertex_stage(shader_program_t *prog, vertex_t *vertices,
                        int count, varying_t *out_varyings)
{
    if (prog == NULL || vertices == NULL || out_varyings == NULL) return -1;
    if (prog->vertex_shader == NULL) return -1;
    if (count <= 0) return 0;

    for (int i = 0; i < count; i++) {
        memset(&out_varyings[i], 0, sizeof(varying_t));
        prog->vertex_shader(&out_varyings[i], &vertices[i],
                           prog->uniforms, prog->uniform_count);
    }

    return count;
}

int fr_run_fragment_stage(shader_program_t *prog, const varying_t *v,
                          color_t *out_color)
{
    if (prog == NULL || v == NULL || out_color == NULL) return -1;
    if (prog->fragment_shader == NULL) return -1;

    prog->fragment_shader(out_color, v, prog->uniforms, prog->uniform_count);
    return 0;
}
