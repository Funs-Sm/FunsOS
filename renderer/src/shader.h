/*
 * shader.h - 软件可编程着色器管线
 *
 * 提供顶点着色器(VS)、片段着色器(FS)的注册和执行框架。
 * 每个着色器是一个函数指针，通过uniform/varying传递数据。
 */

#ifndef FR_SHADER_H
#define FR_SHADER_H

#include "stdint.h"
#include "stddef.h"

#ifndef FR_COLOR_T_DEFINED
#define FR_COLOR_T_DEFINED
typedef struct { uint8_t r, g, b, a; } fr_color_t;
#endif

/* 前向声明 */
struct fr_context;

/* ================================================================
 *  基础类型定义
 * ================================================================ */

/* 颜色类型 (用于片段着色器输出) */
typedef fr_color_t color_t;

/* 顶点数据 */
typedef struct {
    float x, y, z;        /* 位置 */
    float nx, ny, nz;     /* 法线 */
    float u, v;           /* 纹理坐标 */
    float r, g, b, a;     /* 颜色 */
} vertex_t;

/* Varying 变量 (VS->FS插值数据) */
#define MAX_VARYING_FLOATS 32

typedef struct {
    float floats[MAX_VARYING_FLOATS]; /* 插值数据池 */
    float gl_Position[4];             /* 裁剪空间坐标 */
    int   float_count;                /* 已使用的浮点数数量 */
} varying_t;

/* ================================================================
 *  Uniform 变量 (全局常量)
 * ================================================================ */

#define MAX_UNIFORMS 64

/* Uniform 类型枚举 */
typedef enum {
    UNIFORM_FLOAT,
    UNIFORM_VEC2,
    UNIFORM_VEC3,
    UNIFORM_VEC4,
    UNIFORM_MAT4,
    UNIFORM_INT,
    UNIFORM_SAMPLER2D
} uniform_type_t;

/* Uniform 变量结构 */
typedef struct uniform {
    char          name[32];
    uniform_type_t type;
    union {
        float f;
        float v2[2];
        float v3[3];
        float v4[4];
        float m16[16];
        int   i;
    } data;
    uint8_t dirty; /* 标记是否已更新到GPU */
} uniform_t;

/* ================================================================
 *  Varying 声明 (VS->FS插值)
 * ================================================================ */

#define MAX_VARYINGS 16

/* Varying 类型 */
#define VARYING_FLOAT 0
#define VARYING_VEC2  1
#define VARYING_VEC3  2
#define VARYING_VEC4  3

/* Varying 声明 */
typedef struct varying_decl {
    char name[32];
    uint8_t type;
    uint8_t location;
} varying_decl_t;

/* ================================================================
 *  着色器程序
 * ================================================================ */

#define MAX_SHADER_PROGRAMS 16

/* 着色器程序结构 */
typedef struct shader_program {
    uint32_t id;

    /* 顶点着色器函数指针 */
    void (*vertex_shader)(varying_t *output, const vertex_t *input,
                          const uniform_t *uniforms, int uniform_count);

    /* 片段着色器函数指针 */
    void (*fragment_shader)(color_t *output, const varying_t *input,
                            const uniform_t *uniforms, int uniform_count);

    /* Uniform表 */
    uniform_t uniforms[MAX_UNIFORMS];
    int uniform_count;

    /* Varying声明 */
    varying_decl_t varyings[MAX_VARYINGS];
    int varying_count;

    /* 深度测试配置 */
    uint8_t depth_test;
    uint8_t depth_write;
    uint8_t blend_mode; /* 0=none, 1=alpha, 2=additive */

    uint8_t linked;
    uint8_t used;
} shader_program_t;

/* ================================================================
 *  矩形区域 (用于效果处理)
 * ================================================================ */

typedef struct rect_t {
    int x, y, w, h;
} rect_t;

/* ================================================================
 *  公共API - 初始化
 * ================================================================ */

/*
 * fr_shader_init - 初始化着色器系统
 *
 * 清空所有程序槽位，重置状态。
 */
void fr_shader_init(void);

/* ================================================================
 *  公共API - 程序管理
 * ================================================================ */

/*
 * fr_program_create - 创建新的着色器程序
 *
 * 返回新程序的ID，0表示失败。
 */
uint32_t fr_program_create(void);

/*
 * fr_program_delete - 删除指定程序
 *
 * 返回 0=成功, -1=未找到。
 */
int fr_program_delete(uint32_t program_id);

/*
 * fr_program_link - 链接着色器程序
 *
 * 验证VS/FS是否已绑定，检查varying匹配性。
 * 返回 0=成功, -1=失败。
 */
int fr_program_link(uint32_t program_id);

/*
 * fr_program_use - 激活指定程序
 *
 * 后续的渲染调用将使用此程序。
 * 返回 0=成功, -1=失败。
 */
int fr_program_use(uint32_t program_id);

/* ================================================================
 *  公共API - 着色器绑定
 * ================================================================ */

/*
 * fr_attach_vertex_shader - 绑定顶点着色器到程序
 *
 * 返回 0=成功, -1=失败。
 */
int fr_attach_vertex_shader(uint32_t program_id,
    void (*vs)(varying_t*, const vertex_t*, const uniform_t*, int));

/*
 * fr_attach_fragment_shader - 绑定片段着色器到程序
 *
 * 返回 0=成功, -1=失败。
 */
int fr_attach_fragment_shader(uint32_t program_id,
    void (*fs)(color_t*, const varying_t*, const uniform_t*, int));

/* ================================================================
 *  公共API - Uniform操作
 * ================================================================ */

/*
 * fr_uniform_location - 获取Uniform位置索引
 *
 * 返回 >=0 的位置索引, -1=未找到。
 */
int fr_uniform_location(uint32_t program_id, const char *name);

/*
 * fr_uniform_1f - 设置float类型的Uniform值
 */
int fr_uniform_1f(int location, float v);

/*
 * fr_uniform_2f - 设置vec2类型的Uniform值
 */
int fr_uniform_2f(int location, float x, float y);

/*
 * fr_uniform_3f - 设置vec3类型的Uniform值
 */
int fr_uniform_3f(int location, float x, float y, float z);

/*
 * fr_uniform_4f - 设置vec4类型的Uniform值
 */
int fr_uniform_4f(int location, float x, float y, float z, float w);

/*
 * fr_uniform_mat4 - 设置mat4类型的Uniform值
 */
int fr_uniform_mat4(int location, const float *m);

/*
 * fr_uniform_1i - 设置int/sampler类型的Uniform值
 */
int fr_uniform_1i(int location, int i);

/* ================================================================
 *  内置着色器
 * ================================================================ */

/*
 * fr_builtin_solid_vs - 内置纯色顶点着色器
 *
 * 仅传递位置和颜色，不做变换。
 */
void fr_builtin_solid_vs(varying_t *o, const vertex_t *i,
                         const uniform_t *u, int n);

/*
 * fr_builtin_solid_fs - 内置纯色片段着色器
 *
 * 直接输出顶点颜色。
 */
void fr_builtin_solid_fs(color_t *o, const varying_t *i,
                         const uniform_t *u, int n);

/*
 * builtin_color_fs - 内置颜色片段着色器 (兼容命名)
 */
void builtin_color_fs(color_t *o, const varying_t *i,
                      const uniform_t *u, int n);

/*
 * builtin_texture_fs - 内置纹理片段着色器
 *
 * 使用纹理坐标采样纹理并输出颜色。
 */
void builtin_texture_fs(color_t *o, const varying_t *i,
                        const uniform_t *u, int n);

/*
 * builtin_phong_vs - 内置Phong光照顶点着色器
 *
 * 计算世界空间位置、法线和视线方向。
 */
void builtin_phong_vs(varying_t *o, const vertex_t *i,
                      const uniform_t *u, int n);

/*
 * builtin_phong_fs - 内置Phong光照片段着色器
 *
 * 实现环境光+漫反射+高光的Phong反射模型。
 */
void builtin_phong_fs(color_t *o, const varying_t *i,
                      const uniform_t *u, int n);

/* ================================================================
 *  渲染执行
 * ================================================================ */

/*
 * fr_run_vertex_stage - 执行顶点着色器阶段
 *
 * 对每个顶点调用绑定的VS，输出变换后的varying数据。
 * 返回处理的顶点数，-1表示错误。
 */
int fr_run_vertex_stage(shader_program_t *prog, vertex_t *vertices,
                        int count, varying_t *out_varyings);

/*
 * fr_run_fragment_stage - 执行片段着色器阶段
 *
 * 对单个像素(插值后的varying)调用绑定的FS，输出最终颜色。
 * 返回 0=成功, -1=失败。
 */
int fr_run_fragment_stage(shader_program_t *prog, const varying_t *v,
                          color_t *out_color);

#endif /* FR_SHADER_H */
