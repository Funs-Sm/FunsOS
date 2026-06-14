/* fr_texture.h - 纹理管理
 * 提供格式转换、mipmap 生成、纹理图集和纹理缓存
 */

#ifndef FR_TEXTURE_H
#define FR_TEXTURE_H

#include "stdint.h"

/* Ensure fr_color_t is available */
#ifndef FR_COLOR_T_DEFINED
#define FR_COLOR_T_DEFINED
typedef struct { uint8_t r, g, b, a; } fr_color_t;
#endif

/* 前向声明 */
struct fr_context;

/* ---- 纹理格式 ---- */
#define FR_TEX_FMT_RGBA8888     0
#define FR_TEX_FMT_BGRA8888     1
#define FR_TEX_FMT_RGB888       2
#define FR_TEX_FMT_RGBA4444     3
#define FR_TEX_FMT_RGBA5551     4
#define FR_TEX_FMT_RGB565       5
#define FR_TEX_FMT_A8           6
#define FR_TEX_FMT_L8           7
#define FR_TEX_FMT_LA88         8
#define FR_TEX_FMT_DXT1         9
#define FR_TEX_FMT_DXT3         10
#define FR_TEX_FMT_DXT5         11
#define FR_TEX_FMT_ETC1         12
#define FR_TEX_FMT_ETC2         13

/* ---- 纹理环绕模式 ---- */
#define FR_TEX_WRAP_CLAMP        0
#define FR_TEX_WRAP_REPEAT       1
#define FR_TEX_WRAP_MIRROR       2
#define FR_TEX_WRAP_CLAMP_BORDER 3

/* ---- 纹理过滤 ---- */
#define FR_TEX_FILTER_NEAREST    0
#define FR_TEX_FILTER_LINEAR     1
#define FR_TEX_FILTER_BILINEAR   2
#define FR_TEX_FILTER_TRILINEAR  3
#define FR_TEX_FILTER_ANISO      4

/* ---- 纹理采样器 ---- */
typedef struct {
    uint32_t wrap_s;            /* S 轴环绕 */
    uint32_t wrap_t;            /* T 轴环绕 */
    uint32_t min_filter;        /* 缩小过滤 */
    uint32_t mag_filter;        /* 放大过滤 */
    fr_color_t border_color;    /* 边界颜色 */
    float anisotropy;           /* 各向异性级别 */
    float lod_bias;             /* LOD 偏移 */
    float min_lod;              /* 最小 LOD */
    float max_lod;              /* 最大 LOD */
} fr_sampler_t;

/* ---- 纹理结构 ---- */
typedef struct {
    uint32_t width;             /* 宽度 */
    uint32_t height;            /* 高度 */
    uint32_t format;            /* 格式 */
    uint32_t mip_levels;        /* mipmap 级别数 */
    uint8_t *data;              /* 像素数据 */
    uint32_t data_size;         /* 数据大小 (字节) */
    uint32_t stride;            /* 每行字节数 */
    fr_sampler_t sampler;       /* 采样器 */
    char name[64];              /* 纹理名称 */
    uint32_t id;                /* 唯一 ID */
    int managed;                /* 是否托管(由系统管理) */
    int ref_count;              /* 引用计数 */
} fr_texture_t;

/* ---- 纹理区域 ---- */
typedef struct {
    fr_texture_t *texture;      /* 所属纹理 */
    uint32_t x, y;              /* 区域偏移 */
    uint32_t w, h;              /* 区域大小 */
} fr_tex_region_t;

/* ---- 纹理图集 ---- */
#define FR_ATLAS_MAX_TEXTURES 256

typedef struct {
    uint32_t width;             /* 图集总宽度 */
    uint32_t height;            /* 图集总高度 */
    uint32_t format;            /* 图集格式 */
    uint32_t used_width;        /* 已用宽度 */
    uint32_t used_height;       /* 已用高度 */
    uint32_t max_height;        /* 当前行最大高度 */
    fr_tex_region_t regions[FR_ATLAS_MAX_TEXTURES];
    int region_count;
    fr_texture_t *texture;      /* 内部纹理 */
    int dirty;                  /* 需要更新 */
} fr_texture_atlas_t;

/* ---- 纹理缓存 ---- */
#define FR_CACHE_MAX_ENTRIES 256

typedef struct {
    fr_texture_t *texture;      /* 缓存的纹理 */
    uint32_t key;               /* 查找键 */
    uint32_t last_access;       /* 最后访问时间 */
    uint32_t size;              /* 纹理大小 (字节) */
    int locked;                 /* 是否锁定 */
} fr_cache_entry_t;

typedef struct {
    fr_cache_entry_t entries[FR_CACHE_MAX_ENTRIES];
    int entry_count;
    uint32_t max_size;          /* 最大缓存大小 (字节) */
    uint32_t current_size;      /* 当前缓存大小 */
    uint32_t access_counter;    /* 访问计数器 */
    uint32_t hits;              /* 命中次数 */
    uint32_t misses;            /* 未命中次数 */
} fr_texture_cache_t;

/* ---- 纹理着色器 (简化) ---- */
typedef fr_color_t (*fr_tex_shader)(const fr_texture_t *tex,
                                     float u, float v, void *user_data);

/* ================================================================
 *  API 函数声明
 * ================================================================ */

/* 纹理创建/销毁 */
fr_texture_t *fr_texture_create(uint32_t width, uint32_t height, uint32_t format);
fr_texture_t *fr_texture_create_from_data(uint32_t width, uint32_t height,
                                           uint32_t format, const uint8_t *data);
fr_texture_t *fr_texture_create_solid(uint32_t width, uint32_t height,
                                       fr_color_t color);
void fr_texture_destroy(fr_texture_t *tex);
void fr_texture_retain(fr_texture_t *tex);
void fr_texture_release(fr_texture_t *tex);

/* 纹理数据操作 */
int fr_texture_set_data(fr_texture_t *tex, const uint8_t *data, uint32_t size);
int fr_texture_get_data(const fr_texture_t *tex, uint8_t *data, uint32_t size);
int fr_texture_set_pixel(fr_texture_t *tex, uint32_t x, uint32_t y,
                          fr_color_t color);
fr_color_t fr_texture_get_pixel(const fr_texture_t *tex, uint32_t x, uint32_t y);
int fr_texture_set_region(fr_texture_t *tex, uint32_t x, uint32_t y,
                           uint32_t w, uint32_t h, const uint8_t *data);
int fr_texture_get_region(const fr_texture_t *tex, uint32_t x, uint32_t y,
                           uint32_t w, uint32_t h, uint8_t *data);

/* 格式转换 */
int fr_texture_convert(fr_texture_t *tex, uint32_t new_format);
uint32_t fr_texture_get_bpp(uint32_t format);
uint32_t fr_texture_get_size(uint32_t width, uint32_t height, uint32_t format);
const char *fr_texture_get_format_name(uint32_t format);

/* Mipmap */
int fr_texture_generate_mipmaps(fr_texture_t *tex);
int fr_texture_get_mip_level(fr_texture_t *tex, uint32_t level,
                              uint32_t *width, uint32_t *height, uint8_t **data);
fr_texture_t *fr_texture_create_mip_level(const fr_texture_t *tex,
                                           uint32_t level);

/* 采样器 */
void fr_texture_set_sampler(fr_texture_t *tex, const fr_sampler_t *sampler);
void fr_texture_set_filter(fr_texture_t *tex,
                            uint32_t min_filter, uint32_t mag_filter);
void fr_texture_set_wrap(fr_texture_t *tex, uint32_t wrap_s, uint32_t wrap_t);

/* 纹理采样 */
fr_color_t fr_texture_sample(const fr_texture_t *tex, float u, float v);
fr_color_t fr_texture_sample_lod(const fr_texture_t *tex, float u, float v,
                                  float lod);
fr_color_t fr_texture_sample_bilinear(const fr_texture_t *tex, float u, float v);

/* 纹理渲染 */
void fr_texture_blit(fr_texture_t *tex, struct fr_context *ctx,
                      int dx, int dy);
void fr_texture_blit_scaled(fr_texture_t *tex, struct fr_context *ctx,
                             int dx, int dy, int dw, int dh);
void fr_texture_blit_region(fr_texture_t *tex, struct fr_context *ctx,
                             int sx, int sy, int sw, int sh,
                             int dx, int dy, int dw, int dh);
void fr_texture_blit_rotated(fr_texture_t *tex, struct fr_context *ctx,
                              int dx, int dy, float angle, float cx, float cy);
void fr_texture_blit_tinted(fr_texture_t *tex, struct fr_context *ctx,
                             int dx, int dy, fr_color_t tint);

/* 纹理操作 */
fr_texture_t *fr_texture_flip_horizontal(const fr_texture_t *tex);
fr_texture_t *fr_texture_flip_vertical(const fr_texture_t *tex);
fr_texture_t *fr_texture_rotate_90(const fr_texture_t *tex);
fr_texture_t *fr_texture_crop(const fr_texture_t *tex,
                               uint32_t x, uint32_t y, uint32_t w, uint32_t h);
fr_texture_t *fr_texture_scale(const fr_texture_t *tex,
                                uint32_t new_width, uint32_t new_height);

/* 纹理图集 */
fr_texture_atlas_t *fr_atlas_create(uint32_t width, uint32_t height,
                                     uint32_t format);
void fr_atlas_destroy(fr_texture_atlas_t *atlas);
fr_tex_region_t *fr_atlas_add(fr_texture_atlas_t *atlas,
                               uint32_t width, uint32_t height,
                               const uint8_t *data);
fr_tex_region_t *fr_atlas_add_texture(fr_texture_atlas_t *atlas,
                                       const fr_texture_t *tex);
int fr_atlas_remove(fr_texture_atlas_t *atlas, fr_tex_region_t *region);
void fr_atlas_clear(fr_texture_atlas_t *atlas);
fr_tex_region_t *fr_atlas_find(fr_texture_atlas_t *atlas, uint32_t idx);
void fr_atlas_compact(fr_texture_atlas_t *atlas);
void fr_atlas_pack(fr_texture_atlas_t *atlas);

/* 纹理缓存 */
fr_texture_cache_t *fr_cache_create(uint32_t max_size);
void fr_cache_destroy(fr_texture_cache_t *cache);
fr_texture_t *fr_cache_get(fr_texture_cache_t *cache, uint32_t key);
int fr_cache_put(fr_texture_cache_t *cache, uint32_t key, fr_texture_t *tex);
int fr_cache_remove(fr_texture_cache_t *cache, uint32_t key);
void fr_cache_clear(fr_texture_cache_t *cache);
void fr_cache_evict(fr_texture_cache_t *cache, uint32_t target_size);
void fr_cache_get_stats(const fr_texture_cache_t *cache,
                         uint32_t *hits, uint32_t *misses,
                         uint32_t *size, uint32_t *max_size);
void fr_cache_lock(fr_texture_cache_t *cache, uint32_t key);
void fr_cache_unlock(fr_texture_cache_t *cache, uint32_t key);

/* 纹理实用函数 */
uint32_t fr_texture_hash(const fr_texture_t *tex);
int fr_texture_compare(const fr_texture_t *a, const fr_texture_t *b);
fr_texture_t *fr_texture_duplicate(const fr_texture_t *tex);
void fr_texture_premultiply_alpha(fr_texture_t *tex);
void fr_texture_unpremultiply_alpha(fr_texture_t *tex);

/* 纹理区域 */
fr_tex_region_t *fr_tex_region_create(fr_texture_t *tex,
                                       uint32_t x, uint32_t y,
                                       uint32_t w, uint32_t h);
void fr_tex_region_destroy(fr_tex_region_t *region);

#endif /* FR_TEXTURE_H */