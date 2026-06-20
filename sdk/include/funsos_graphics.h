#ifndef FUNSOS_GRAPHICS_H
#define FUNSOS_GRAPHICS_H

/*
 * FUNSOS 图形绘制 API
 * 提供 2D 矢量图形和 3D 渲染功能。
 * 2D 部分基于 gui/gfx.h，3D 部分基于 gfx3d 引擎。
 */

#include "stdint.h"

/* 前向声明: funsos_window 完整定义在 funsos_window.h */
struct funsos_window;

/* ---- 颜色定义 ---- */
#define FUNSOS_COLOR_BLACK      0x000000
#define FUNSOS_COLOR_WHITE      0xFFFFFF
#define FUNSOS_COLOR_RED        0xFF0000
#define FUNSOS_COLOR_GREEN      0x00FF00
#define FUNSOS_COLOR_BLUE       0x0000FF
#define FUNSOS_COLOR_YELLOW     0xFFFF00
#define FUNSOS_COLOR_CYAN       0x00FFFF
#define FUNSOS_COLOR_MAGENTA    0xFF00FF
#define FUNSOS_COLOR_GRAY       0x808080
#define FUNSOS_COLOR_LIGHT_GRAY 0xC0C0C0
#define FUNSOS_COLOR_DARK_GRAY  0x404040
#define FUNSOS_COLOR_ORANGE     0xFF8000

/* 颜色类型（32位 ARGB） */
typedef uint32_t funsos_color_t;

/* RGBA 颜色结构体 */
typedef struct {
    uint8_t r, g, b, a;
} funsos_rgba_t;

/* ---- 2D 图形结构体 ---- */

/* 矩形 */
#ifndef FUNSOS_RECT_T_DEFINED
#define FUNSOS_RECT_T_DEFINED
typedef struct {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} funsos_rect_t;
#endif

/* 点 */
typedef struct {
    int32_t x;
    int32_t y;
} funsos_point_t;

/* 图形上下文 */
typedef struct {
    uint32_t *buffer;     /* 帧缓冲区指针 */
    uint32_t width;       /* 宽度（像素） */
    uint32_t height;      /* 高度（像素） */
    uint32_t pitch;       /* 每行字节数 */
    uint32_t bpp;         /* 每像素位数 */
    funsos_rect_t clip;   /* 裁剪区域 */
} funsos_gfx_context_t;

/* 混合模式 */
#define FUNSOS_BLEND_ALPHA  1  /* Alpha 混合 */
#define FUNSOS_BLEND_NONE   0  /* 不混合（直接覆盖） */

/* ---- 2D 绘图 API ---- */

/*
 * 在窗口上绘制填充矩形
 * 参数: win - 窗口句柄; x, y, w, h - 矩形位置和尺寸; color - 颜色
 * 返回: 0 成功, -1 失败
 */
int funsos_draw_rect(uint32_t win_handle, int x, int y, int w, int h, funsos_color_t color);

/*
 * 在窗口上绘制文本
 * 参数: win - 窗口句柄; x, y - 文本位置; text - 文本内容; fg - 前景色
 * 返回: 0 成功, -1 失败
 */
int funsos_draw_text(uint32_t win_handle, int x, int y, const char *text, funsos_color_t fg);

/*
 * 在窗口上绘制直线
 * 参数: win - 窗口句柄; x1, y1 - 起点; x2, y2 - 终点; color - 颜色
 * 返回: 0 成功, -1 失败
 */
int funsos_draw_line(uint32_t win_handle, int x1, int y1, int x2, int y2, funsos_color_t color);

/*
 * 用指定颜色填充整个窗口
 * 参数: win - 窗口句柄; bg - 背景色
 * 返回: 0 成功, -1 失败
 */
int funsos_fill_window(uint32_t win_handle, funsos_color_t bg);

/*
 * 获取窗口的图形上下文
 * 参数: win - 窗口句柄
 * 返回: 图形上下文指针, NULL 表示失败
 */
void *funsos_get_window_context(uint32_t win_handle);

/*
 * 绘制圆形轮廓
 * 参数: ctx - 图形上下文; cx, cy - 圆心; r - 半径; color - 颜色
 */
void funsos_draw_circle(funsos_gfx_context_t *ctx, int cx, int cy, int r, funsos_color_t color);

/*
 * 绘制填充圆形
 * 参数: ctx - 图形上下文; cx, cy - 圆心; r - 半径; color - 颜色
 */
void funsos_fill_circle(funsos_gfx_context_t *ctx, int cx, int cy, int r, funsos_color_t color);

/*
 * 绘制圆角矩形轮廓
 * 参数: ctx - 图形上下文; rect - 矩形; radius - 圆角半径; color - 颜色
 */
void funsos_draw_rounded_rect(funsos_gfx_context_t *ctx, funsos_rect_t rect, int radius, funsos_color_t color);

/*
 * 绘制填充圆角矩形
 * 参数: ctx - 图形上下文; rect - 矩形; radius - 圆角半径; color - 颜色
 */
void funsos_fill_rounded_rect(funsos_gfx_context_t *ctx, funsos_rect_t rect, int radius, funsos_color_t color);

/*
 * 设置单个像素（带 Alpha 混合）
 * 参数: ctx - 图形上下文; x, y - 坐标; color - 颜色; alpha - 透明度 (0-255)
 */
void funsos_blend_pixel(funsos_gfx_context_t *ctx, int x, int y, funsos_color_t color, uint8_t alpha);

/*
 * 设置裁剪区域
 * 参数: ctx - 图形上下文; clip - 裁剪矩形
 */
void funsos_set_clip(funsos_gfx_context_t *ctx, funsos_rect_t clip);

/*
 * 重置裁剪区域（恢复为全画布）
 * 参数: ctx - 图形上下文
 */
void funsos_reset_clip(funsos_gfx_context_t *ctx);

/*
 * 位图传送（从源上下文复制像素到目标上下文）
 * 参数: dst - 目标上下文; dx, dy - 目标位置; src - 源上下文; src_rect - 源区域
 */
void funsos_blit(funsos_gfx_context_t *dst, int dx, int dy,
                 funsos_gfx_context_t *src, funsos_rect_t src_rect);

/* ---- 3D 图形 API ---- */

/* 3D 向量 */
typedef struct {
    float x, y, z;
} funsos_vec3_t;

/* 4x4 矩阵（列主序） */
typedef struct {
    float m[16];
} funsos_mat4_t;

/* 3D 顶点（位置 + 颜色） */
typedef struct {
    funsos_vec3_t pos;
    funsos_rgba_t color;
} funsos_vertex3d_t;

/* 3D 渲染模式 */
#define FUNSOS_RENDER_POINTS     0  /* 点 */
#define FUNSOS_RENDER_LINES      1  /* 线框 */
#define FUNSOS_RENDER_TRIANGLES  2  /* 三角形填充 */

/*
 * 初始化 3D 渲染上下文
 * 参数: ctx - 2D 图形上下文（作为渲染目标）
 */
void funsos_3d_init(funsos_gfx_context_t *ctx);

/*
 * 设置 3D 投影矩阵（透视投影）
 * 参数: fov - 视场角（度）; aspect - 宽高比; near, far - 近远裁剪面
 * 返回: 投影矩阵
 */
funsos_mat4_t funsos_3d_perspective(float fov, float aspect, float near, float far);

/*
 * 创建观察矩阵
 * 参数: eye - 眼睛位置; center - 目标点; up - 上方向
 * 返回: 观察矩阵
 */
funsos_mat4_t funsos_3d_lookat(funsos_vec3_t eye, funsos_vec3_t center, funsos_vec3_t up);

/*
 * 矩阵乘法
 * 参数: a, b - 两个矩阵
 * 返回: 乘积矩阵
 */
funsos_mat4_t funsos_3d_mul_matrix(funsos_mat4_t a, funsos_mat4_t b);

/*
 * 创建旋转矩阵（绕 Y 轴）
 * 参数: angle - 旋转角度（弧度）
 * 返回: 旋转矩阵
 */
funsos_mat4_t funsos_3d_rotate_y(float angle);

/*
 * 创建旋转矩阵（绕 X 轴）
 * 参数: angle - 旋转角度（弧度）
 * 返回: 旋转矩阵
 */
funsos_mat4_t funsos_3d_rotate_x(float angle);

/*
 * 创建旋转矩阵（绕 Z 轴）
 * 参数: angle - 旋转角度（弧度）
 * 返回: 旋转矩阵
 */
funsos_mat4_t funsos_3d_rotate_z(float angle);

/*
 * 创建缩放矩阵
 * 参数: sx, sy, sz - 各轴缩放因子
 * 返回: 缩放矩阵
 */
funsos_mat4_t funsos_3d_scale(float sx, float sy, float sz);

/*
 * 创建平移矩阵
 * 参数: x, y, z - 平移量
 * 返回: 平移矩阵
 */
funsos_mat4_t funsos_3d_translate(float x, float y, float z);

/*
 * 渲染 3D 顶点列表
 * 参数: vertices - 顶点数组; count - 顶点数量;
 *       mvp - 模型-视图-投影矩阵; mode - 渲染模式
 */
void funsos_3d_render(const funsos_vertex3d_t *vertices, uint32_t count,
                      funsos_mat4_t mvp, int mode);

/*
 * 清除 3D 深度缓冲
 */
void funsos_3d_clear_depth(void);

/* ================================================================
 *  Extended 3D Graphics API (Surface / Texture / Mesh / Font)
 * ================================================================ */

/* ---- 表面(Surface)管理 ---- */

/* 图像表面 - 表示一个可操作的像素缓冲区 */
typedef struct {
    uint32_t     *pixels;    /* 像素数据 */
    uint32_t      width;     /* 宽度 */
    uint32_t      height;    /* 高度 */
    uint32_t      pitch;     /* 行跨度（字节） */
    funsos_color_t format;   /* 像素格式 */
} funsos_surface_t;

/* 像素格式常量 */
#define FUNSOS_PIXEL_ARGB8888  0  /* 32位 ARGB */
#define FUNSOS_PIXEL_RGB565    1  /* 16位 RGB565 */
#define FUNSOS_PIXEL_A8        2  /* 8位 Alpha */

/*
 * 创建指定尺寸的空表面
 * 参数: width, height - 尺寸; format - 像素格式
 * 返回: 表面指针, NULL 表示失败
 */
funsos_surface_t *funsos_create_surface(uint32_t width, uint32_t height, funsos_color_t format);

/*
 * 销毁表面并释放内存
 * 参数: surf - 表面指针
 * 返回: 0 成功, -1 失败
 */
int funsos_destroy_surface(funsos_surface_t *surf);

/*
 * 从表面中获取像素颜色
 * 参数: surf - 表面; x, y - 坐标
 * 返回: 像素颜色值
 */
funsos_color_t funsos_surface_get_pixel(funsos_surface_t *surf, int x, int y);

/*
 * 设置表面中的像素颜色
 * 参数: surf - 表面; x, y - 坐标; color - 颜色
 */
void funsos_surface_set_pixel(funsos_surface_t *surf, int x, int y, funsos_color_t color);

/*
 * 将整个表面内容绘制到窗口上
 * 参数: win - 目标窗口; surf - 源表面; dx, dy - 目标位置
 * 返回: 0 成功, -1 失败
 */
int funsos_blit_surface(uint32_t win_handle, funsos_surface_t *surf, int dx, int dy);

/* ---- 纹理(Texture)管理 ---- */

/* 纹理句柄（由渲染器分配的不透明标识符） */
typedef uint32_t funsos_texture_t;

/* 纹理过滤模式 */
#define FUNSOS_TEX_FILTER_NEAREST  0  /* 最近邻滤波（像素风格） */
#define FUNSOS_TEX_FILTER_LINEAR   1  /* 双线性滤波（平滑） */
#define FUNSOS_TEX_FILTER_MIPMAP   2  /* Mipmap 三线性滤波 */

/* 纹理寻址模式 */
#define FUNSOS_TEX_WRAP_CLAMP   0  /* 边缘钳制 */
#define FUNSOS_TEX_WRAP_REPEAT  1  /* 重复平铺 */
#define FUNSOS_TEX_WRAP_MIRROR  2  /* 镜像重复 */

/*
 * 从表面创建纹理
 * 参数: surf - 源表面数据
 * 返回: 纹理句柄, 0 表示失败
 */
funsos_texture_t funsos_create_texture(funsos_surface_t *surf);

/*
 * 销毁纹理并释放 GPU 资源
 * 参数: tex - 纹理句柄
 * 返回: 0 成功, -1 失败
 */
int funsos_destroy_texture(funsos_texture_t tex);

/*
 * 设置当前活动纹理（绑定到纹理单元）
 * 参数: tex - 纹理句柄; unit - 纹理单元索引 (通常为 0)
 * 返回: 0 成功, -1 失败
 */
int funsos_bind_texture(funsos_texture_t tex, uint32_t unit);

/*
 * 设置纹理过滤模式
 * 参数: tex - 纹理句柄; min_filter - 缩小滤波; mag_filter - 放大滤波
 * 返回: 0 成功, -1 失败
 */
int funsos_set_tex_filter(funsos_texture_t tex, int min_filter, int mag_filter);

/*
 * 设置纹理寻址模式
 * 参数: tex - 纹理句柄; wrap_s - S 轴寻址; wrap_t - T 轴寻址
 * 返回: 0 成功, -1 失败
 */
int funsos_set_tex_wrap(funsos_texture_t tex, int wrap_s, int wrap_t);

/* ---- 网格(Mesh)加载与管理 ---- */

/* 网格数据：包含顶点、法线、UV 和索引 */
typedef struct {
    funsos_vec3_t    *positions;    /* 顶点位置数组 */
    funsos_vec3_t    *normals;      /* 法线向量数组 */
    float            *texcoords;    /* UV 纹理坐标数组 (u,v 交错) */
    funsos_rgba_t    *colors;       /* 顶点颜色数组 */
    uint16_t         *indices;      /* 索引数组（三角形列表） */
    uint32_t         vertex_count;  /* 顶点数量 */
    uint32_t         index_count;   /* 索引数量 */
} funsos_mesh_t;

/*
 * 加载网格数据（从内置几何体生成或文件解析）
 * 参数: type - 内置几何体类型 (见下方常量)
 * 返回: 网格数据指针, NULL 表示失败
 *
 * 内置几何体类型:
 *   0 - 单位立方体
 *   1 - 单位球体 (细分级别默认 12)
 *   2 - 平面网格
 *   3 - 圆柱体
 *   4 - 圆锥体
 */
funsos_mesh_t *funsos_create_mesh(uint32_t type);

/*
 * 销毁网格数据并释放内存
 * 参数: mesh - 网格指针
 * 返回: 0 成功, -1 失败
 */
int funsos_destroy_mesh(funsos_mesh_t *mesh);

/*
 * 渲染完整网格（使用当前绑定的纹理和 MVP 矩阵）
 * 参数: mesh - 网格数据; mvp - 变换矩阵
 * 返回: 0 成功, -1 失败
 */
int funsos_render_mesh(const funsos_mesh_t *mesh, funsos_mat4_t mvp);

/* 内置几何体类型常量 */
#define FUNSOS_MESH_CUBE       0
#define FUNSOS_MESH_SPHERE     1
#define FUNSOS_MESH_PLANE      2
#define FUNSOS_MESH_CYLINDER   3
#define FUNSOS_MESH_CONE       4

/* ---- 渲染管线配置 ---- */

/* 渲染管线状态结构体 */
typedef struct {
    int    cull_face;            /* 背面剔除: 0=禁用, 1=启用 */
    int    cull_mode;            /* 剔除模式: 0=背面, 1=正面, 2=双面 */
    int    depth_test;           /* 深度测试: 0=禁用, 1=启用 */
    int    depth_write;          /* 深度写入: 0=禁用, 1=启用 */
    int    blend_enable;         /* Alpha 混合: 0=禁用, 1=启用 */
    int    blend_src;            /* 混合源因子 */
    int    blend_dst;            /* 混合目标因子 */
    funsos_color_t clear_color;  /* 清屏颜色 */
    funsos_color_t fog_color;    /* 雾化颜色 */
    float  fog_start;            /* 雾起始距离 */
    float  fog_end;              /* 雾结束距离 */
    int    fog_enable;           /* 雾效果: 0=禁用, 1=启用 */
} funsos_pipeline_state_t;

/* 混合因子常量 */
#define FUNSOS_BLEND_ZERO             0
#define FUNSOS_BLEND_ONE              1
#define FUNSOS_BLEND_SRC_ALPHA        2
#define FUNSOS_BLEND_ONE_MINUS_SRC_ALPHA 3
#define FUNSOS_BLEND_DST_ALPHA        4
#define FUNSOS_BLEND_ONE_MINUS_DST_ALPHA 5

/*
 * 获取当前渲染管线状态
 * 返回: 管线状态结构体指针（内部静态存储）
 */
funsos_pipeline_state_t *funsos_get_pipeline(void);

/*
 * 应用渲染管线状态
 * 参数: state - 要应用的管线状态
 * 返回: 0 成功, -1 失败
 */
int funsos_set_pipeline(const funsos_pipeline_state_t *state);

/*
 * 重置渲染管线状态为默认值
 */
void funsos_reset_pipeline(void);

/* ---- 字体渲染 API ---- */

/* 字体句柄 */
typedef void *funsos_font_t;

/* 字体样式标志 */
#define FUNSOS_FONT_NORMAL   0x00  /* 正常 */
#define FUNSOS_FONT_BOLD     0x01  /* 粗体 */
#define FUNSOS_FONT_ITALIC   0x02  /* 斜体 */
#define FUNSOS_FONT_UNDERLINE 0x04 /* 下划线 */

/*
 * 加载字体文件（支持 FNT/BDF 格式）
 * 参数: path - 字体文件路径; size - 字号大小
 * 返回: 字体句柄, NULL 表示失败
 */
funsos_font_t funsos_load_font(const char *path, uint32_t size);

/*
 * 卸载字体资源
 * 参数: font - 字体句柄
 * 返回: 0 成功, -1 失败
 */
int funsos_unload_font(funsos_font_t font);

/*
 * 使用自定义字体绘制文本
 * 参数: win - 窗口句柄; font - 字体句柄; x, y - 位置;
 *       text - 文本内容; color - 颜色; style - 样式标志
 * 返回: 绘制的字符数, -1 失败
 */
int funsos_draw_text_ex(uint32_t win_handle, funsos_font_t font,
                        int x, int y, const char *text,
                        funsos_color_t color, uint32_t style);

/*
 * 测量文本尺寸（使用指定字体）
 * 参数: font - 字体句柄; text - 文本内容;
 *       width_out - 输出宽度(像素); height_out - 输出高度(像素)
 * 返回: 0 成功, -1 失败
 */
int funsos_measure_text(funsos_font_t font, const char *text,
                        uint32_t *width_out, uint32_t *height_out);

/*
 * 设置默认字体（后续 funsos_draw_text 将使用此字体）
 * 参数: font - 字体句柄
 * 返回: 之前的默认字体句柄
 */
funsos_font_t funsos_set_default_font(funsos_font_t font);

#endif /* FUNSOS_GRAPHICS_H */
