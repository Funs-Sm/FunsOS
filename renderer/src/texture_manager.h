/*
 * texture_manager.h - GPU纹理管理
 *
 * 管理纹理的生命周期：加载、上传、缓存、卸载。
 * 支持Mipmap生成、纹理压缩(简化DXT1)、纹理流式加载。
 */

#ifndef FR_TEXTURE_MANAGER_H
#define FR_TEXTURE_MANAGER_H

#include "stdint.h"
#include "stddef.h"

#ifndef FR_COLOR_T_DEFINED
#define FR_COLOR_T_DEFINED
typedef struct { uint8_t r, g, b, a; } fr_color_t;
#endif

/* 前向声明 */
struct fr_context;

/* ================================================================
 *  纹理格式枚举
 * ================================================================ */

typedef enum {
    TEX_FORMAT_RGBA8888,    /* 32位RGBA */
    TEX_FORMAT_RGB888,      /* 24位RGB (3字节对齐) */
    TEX_FORMAT_RGB565,      /* 16位RGB */
    TEX_FORMAT_RGBA4444,    /* 16位RGBA */
    TEX_FORMAT_A8,          /* 8位Alpha */
    TEX_FORMAT_DXT1,        /* BC1压缩 (无Alpha) */
    TEX_FORMAT_DXT5         /* BC3压缩 (有Alpha) */
} tex_format_t;

/* ================================================================
 *  纹理过滤模式
 * ================================================================ */

typedef enum {
    TEX_FILTER_NEAREST,     /* 最近邻插值 (像素风格) */
    TEX_FILTER_LINEAR,      /* 双线性插值 */
    TEX_FILTER_BILINEAR,    /* 完整双线性过滤 */
    TEX_FILTER_TRILINEAR    /* 三线性过滤 (Mipmap) */
} tex_filter_t;

/* ================================================================
 *  纹理地址模式 (环绕)
 * ================================================================ */

typedef enum {
    TEX_WRAP_CLAMP,         /* 钳制到边缘 */
    TEX_WRAP_REPEAT,        /* 重复平铺 */
    TEX_WRAP_MIRROR         /* 镜像重复 */
} tex_wrap_t;

/* ================================================================
 *  Mipmap配置
 * ================================================================ */

#define MIP_LEVELS_MAX 10   /* 最大Mipmap层数: 1024->1 */

/* ================================================================
 *  纹理槽位结构
 * ================================================================ */

#define TEX_MAX_SLOTS       128
#define TEX_NAME_MAX        64

typedef struct texture_slot {
    uint32_t    handle;            /* 硬件句柄/唯一ID */
    char        name[TEX_NAME_MAX]; /* 纹理名称 (用于查找) */
    uint16_t    width, height;      /* 基础层尺寸 */
    tex_format_t format;            /* 像素格式 */
    tex_filter_t mag_filter;        /* 放大过滤模式 */
    tex_filter_t min_filter;        /* 缩小过滤模式 */
    tex_wrap_t  wrap_s;             /* S轴环绕模式 */
    tex_wrap_t  wrap_t;             /* T轴环绕模式 */
    uint8_t     mip_levels;         /* Mipmap层数 */
    uint8_t     mipmap_generated;   /* 是否已生成Mipmap */
    uint8_t     locked;             /* 锁定标记 (防止LRU驱逐) */
    uint8_t     resident;           /* 是否驻留显存 */
    uint32_t    access_time;        /* LRU时间戳 */
    uint32_t    size_bytes;         /* 占用显存大小 (字节) */
    void       *mip_data[MIP_LEVELS_MAX]; /* 各级Mipmap数据指针 */
    uint8_t     used;               /* 槽位是否占用 */
} texture_slot_t;

/* ================================================================
 *  公共API - 初始化与全局状态
 * ================================================================ */

/*
 * texmgr_init - 初始化纹理管理器
 *
 * 设置显存预算上限，清空所有槽位。
 * memory_budget_mb: 允许使用的最大显存(MB)，0=不限制。
 */
void texmgr_init(uint32_t memory_budget_mb);

/* ================================================================
 *  公共API - 纹理生命周期
 * ================================================================ */

/*
 * texmgr_create - 创建新纹理
 *
 * 分配一个纹理槽位，设置尺寸和格式。
 * 返回纹理句柄(>0)，0表示失败。
 */
uint32_t texmgr_create(const char *name, uint16_t w, uint16_t h, tex_format_t fmt);

/*
 * texmgr_destroy - 销毁纹理
 *
 * 释放纹理占用的所有资源(包括Mipmap数据)。
 * 返回 0=成功, -1=未找到。
 */
int texmgr_destroy(uint32_t handle);

/*
 * texmgr_upload - 上传像素数据到纹理
 *
 * 将CPU端的数据上传到GPU纹理对象。
 * level: Mipmap层级 (0=基础层)。
 * 返回 0=成功, -1=失败。
 */
int texmgr_upload(uint32_t handle, const void *data, uint32_t level);

/*
 * texmgr_download - 从纹理下载像素数据
 *
 * 将GPU纹理数据读回到CPU内存。
 * 调用方负责释放返回的缓冲区。
 * 返回数据指针(需调用方free)，NULL表示失败。
 */
void *texmgr_download(uint32_t handle, uint32_t level, uint32_t *out_size);

/* ================================================================
 *  公共API - 参数设置
 * ================================================================ */

/*
 * texmgr_set_filter - 设置纹理过滤模式
 *
 * mag: 放大时的过滤方式。
 * min: 缩小时的过滤方式。
 * 返回 0=成功, -1=失败。
 */
int texmgr_set_filter(uint32_t handle, tex_filter_t mag, tex_filter_t min);

/*
 * texmgr_set_wrap - 设置纹理环绕模式
 *
 * s: 水平方向环绕模式。
 * t: 垂直方向环绕模式。
 * 返回 0=成功, -1=失败。
 */
int texmgr_set_wrap(uint32_t handle, tex_wrap_t s, tex_wrap_t t);

/*
 * texmgr_lock - 锁定纹理防止LRU驱逐
 *
 * 锁定后的纹理不会被自动清理。
 * 返回 0=成功, -1=失败。
 */
int texmgr_lock(uint32_t handle);

/*
 * texmgr_unlock - 解锁纹理
 *
 * 解锁后纹理可被LRU机制驱逐。
 * 返回 0=成功, -1=失败。
 */
int texmgr_unlock(uint32_t handle);

/* ================================================================
 *  公共API - Mipmap操作
 * ================================================================ */

/*
 * texmgr_generate_mipmaps - 自动生成完整的Mipmap链
 *
 * 从基础层级(0)开始，逐级下采样生成所有Mipmap层。
 * 使用简单的2x2盒式滤波进行下采样。
 * 返回生成的Mipmap层数，-1表示失败。
 */
int texmgr_generate_mipmaps(uint32_t handle);

/*
 * texmgr_bind_mipmap - 绑定指定Mipmap层级用于渲染
 *
 * 返回 0=成功, -1=失败或层级不存在。
 */
int texmgr_bind_mipmap(uint32_t handle, uint32_t level);

/* ================================================================
 *  公共API - 纹理查找
 * ================================================================ */

/*
 * texmgr_find - 通过句柄查找纹理
 *
 * 返回纹理槽位指针，未找到返回NULL。
 */
texture_slot_t *texmgr_find(uint32_t handle);

/*
 * texmgr_find_name - 通过名称查找纹理
 *
 * 返回纹理槽位指针，未找到返回NULL。
 */
texture_slot_t *texmgr_find_name(const char *name);

/*
 * texmgr_used_memory - 获取当前已用显存总量
 *
 * 返回字节数。
 */
uint32_t texmgr_used_memory(void);

/*
 * texmgr_slot_count - 获取当前已用槽位数
 */
uint32_t texmgr_slot_count(void);

/* ================================================================
 *  公共API - LRU清理
 * ================================================================ */

/*
 * texmgr_evict_lru - 驱逐最近最少使用的未锁定纹理
 *
 * 释放一个纹理以回收显存。
 * 返回释放的字节数，-1表示无可驱逐的纹理。
 */
int texmgr_evict_lru(void);

/*
 * texmgr_cleanup_unused - 清理长时间未访问的纹理
 *
 * timeout_ticks: 超过此时长未访问的纹理将被清理。
 * 返回清理的纹理数量。
 */
void texmgr_cleanup_unused(uint32_t timeout_ticks);

/* ================================================================
 *  公共API - DXT压缩 (简化实现)
 * ================================================================ */

/*
 * texmgr_compress_dxt1 - 将RGBA数据压缩为DXT1格式
 *
 * DXT1是BC1块压缩，每个4x4像素块使用64位(8字节)存储。
 * 这是有损压缩，适合漫反射纹理。
 *
 * rgba_data: 输入的RGBA8888像素数据。
 * w, h: 尺寸 (必须是4的倍数，否则内部填充)。
 * out_dxt: 输出的DXT1数据缓冲区 (由函数分配，调用方释放)。
 * out_size: 输出数据大小(字节)。
 *
 * 返回 0=成功, -1=失败。
 */
int texmgr_compress_dxt1(const void *rgba_data, uint16_t w, uint16_t h,
                          void **out_dxt, uint32_t *out_size);

/*
 * texmgr_decompress_dxt1 - 将DXT1数据解压为RGBA
 *
 * dxt_data: 输入的DXT1压缩数据。
 * w, h: 原始尺寸。
 * out_rgba: 输出的RGBA8888像素数据 (由函数分配，调用方释放)。
 * out_size: 输出数据大小(字节)。
 *
 * 返回 0=成功, -1=失败。
 */
int texmgr_decompress_dxt1(const void *dxt_data, uint16_t w, uint16_t h,
                            void **out_rgba, uint32_t *out_size);

#endif /* FR_TEXTURE_MANAGER_H */
