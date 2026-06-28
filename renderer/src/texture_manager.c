/*
 * texture_manager.c - GPU纹理管理
 *
 * 管理纹理的生命周期：加载、上传、缓存、卸载。
 * 支持Mipmap生成、纹理压缩(简化DXT1)、纹理流式加载。
 */

#include "texture_manager.h"
#include "fr_texture.h"
#include "fr_context.h"
#include "stddef.h"
#include "string.h"

#ifndef abs
#define abs(x) ((x) >= 0 ? (x) : -(x))
#endif

/* ================================================================
 *  全局状态
 * ================================================================ */

static texture_slot_t tex_slots[TEX_MAX_SLOTS];
static uint32_t tex_next_handle = 1;
static uint32_t tex_lru_clock = 0;       /* 全局LRU时钟 */
static uint32_t tex_total_memory = 0;    /* 当前已用显存 */
static uint32_t tex_memory_limit = 0;    /* 显存预算上限 (字节) */

/* ================================================================
 *  内部辅助函数
 * ================================================================ */

/*
 * get_format_bpp - 获取格式的每像素字节数
 */
static int get_format_bpp(tex_format_t fmt)
{
    switch (fmt) {
    case TEX_FORMAT_RGBA8888: return 4;
    case TEX_FORMAT_RGB888:   return 3;
    case TEX_FORMAT_RGB565:   return 2;
    case TEX_FORMAT_RGBA4444: return 2;
    case TEX_FORMAT_A8:       return 1;
    case TEX_FORMAT_DXT1:     return 0; /* 压缩格式特殊处理 */
    case TEX_FORMAT_DXT5:     return 0;
    default:                  return 4;
    }
}

/*
 * find_free_tex_slot - 查找空闲纹理槽位
 *
 * 返回槽位索引，-1表示已满。
 */
static int find_free_tex_slot(void)
{
    for (int i = 0; i < TEX_MAX_SLOTS; i++) {
        if (!tex_slots[i].used) {
            return i;
        }
    }
    return -1;
}

/*
 * find_slot_by_handle - 通过句柄查找槽位
 */
static texture_slot_t *find_slot_by_handle(uint32_t handle)
{
    for (int i = 0; i < TEX_MAX_SLOTS; i++) {
        if (tex_slots[i].used && tex_slots[i].handle == handle) {
            return &tex_slots[i];
        }
    }
    return NULL;
}

/*
 * update_lru - 更新纹理的LRU时间戳
 */
static void update_lru(texture_slot_t *slot)
{
    if (slot == NULL) return;
    slot->access_time = ++tex_lru_clock;
}

/* ================================================================
 *  公共API实现 - 初始化与全局状态
 * ================================================================ */

void texmgr_init(uint32_t memory_budget_mb)
{
    memset(tex_slots, 0, sizeof(tex_slots));
    tex_next_handle = 1;
    tex_lru_clock = 0;
    tex_total_memory = 0;

    /* 设置显存预算 (MB转字节) */
    tex_memory_limit = memory_budget_mb * 1024 * 1024;
}

/* ================================================================
 *  公共API实现 - 纹理生命周期
 * ================================================================ */

uint32_t texmgr_create(const char *name, uint16_t w, uint16_t h, tex_format_t fmt)
{
    int slot = find_free_tex_slot();
    if (slot < 0) return 0; /* 已满 */

    texture_slot_t *ts = &tex_slots[slot];
    memset(ts, 0, sizeof(texture_slot_t));

    ts->handle = tex_next_handle++;
    ts->width = w;
    ts->height = h;
    ts->format = fmt;
    ts->mag_filter = TEX_FILTER_LINEAR;
    ts->min_filter = TEX_FILTER_LINEAR;
    ts->wrap_s = TEX_WRAP_CLAMP;
    ts->wrap_t = TEX_WRAP_CLAMP;
    ts->mip_levels = 1;       /* 默认只有基础层 */
    ts->mipmap_generated = 0;
    ts->locked = 0;
    ts->resident = 1;         /* 创建即驻留 */
    ts->used = 1;

    /* 设置名称 */
    if (name != NULL) {
        strncpy(ts->name, name, TEX_NAME_MAX - 1);
        ts->name[TEX_NAME_MAX - 1] = '\0';
    }

    /* 计算基础层大小 */
    int bpp = get_format_bpp(fmt);
    if (bpp > 0) {
        ts->size_bytes = (uint32_t)w * h * bpp;
        tex_total_memory += ts->size_bytes;
    } else {
        /* DXT压缩格式: 每4x4块8字节(DXT1)或16字节(DXT5) */
        int block_size = (fmt == TEX_FORMAT_DXT5) ? 16 : 8;
        uint16_t bw = (w + 3) / 4;
        uint16_t bh = (h + 3) / 4;
        ts->size_bytes = (uint32_t)bw * bh * block_size;
        tex_total_memory += ts->size_bytes;
    }

    update_lru(ts);
    return ts->handle;
}

int texmgr_destroy(uint32_t handle)
{
    texture_slot_t *ts = find_slot_by_handle(handle);
    if (ts == NULL) return -1;

    /* 释放Mipmap数据 */
    for (int i = 0; i < MIP_LEVELS_MAX && i < ts->mip_levels; i++) {
        if (ts->mip_data[i] != NULL) {
            fr_free(ts->mip_data[i]);
            ts->mip_data[i] = NULL;
        }
    }

    /* 更新全局内存计数 */
    tex_total_memory -= ts->size_bytes;

    /* 清空槽位 */
    memset(ts, 0, sizeof(texture_slot_t));
    return 0;
}

int texmgr_upload(uint32_t handle, const void *data, uint32_t level)
{
    texture_slot_t *ts = find_slot_by_handle(handle);
    if (ts == NULL || data == NULL) return -1;
    if (level >= MIP_LEVELS_MAX) return -1;

    /* 计算该层级的尺寸和大小 */
    uint16_t lw = ts->width >> level;
    uint16_t lh = ts->height >> level;
    if (lw == 0) lw = 1;
    if (lh == 0) lh = 1;

    int bpp = get_format_bpp(ts->format);
    uint32_t data_size;

    if (bpp > 0) {
        data_size = (uint32_t)lw * lh * bpp;
    } else {
        int block_size = (ts->format == TEX_FORMAT_DXT5) ? 16 : 8;
        uint16_t bw = (lw + 3) / 4;
        uint16_t bh = (lh + 3) / 4;
        data_size = (uint32_t)bw * bh * block_size;
    }

    /* 如果该层级已有数据，先释放 */
    if (ts->mip_data[level] != NULL) {
        fr_free(ts->mip_data[level]);
    }

    /* 分配并复制数据 */
    void *buf = fr_alloc(data_size);
    if (buf == NULL) return -1;
    memcpy(buf, data, data_size);

    ts->mip_data[level] = buf;

    /* 更新Mipmap层数 */
    if (level + 1 > ts->mip_levels) {
        ts->mip_levels = level + 1;
    }

    update_lru(ts);
    return 0;
}

void *texmgr_download(uint32_t handle, uint32_t level, uint32_t *out_size)
{
    texture_slot_t *ts = find_slot_by_handle(handle);
    if (ts == NULL) return NULL;
    if (level >= MIP_LEVELS_MAX || level >= ts->mip_levels) return NULL;
    if (ts->mip_data[level] == NULL) return NULL;

    /* 计算数据大小 */
    uint16_t lw = ts->width >> level;
    uint16_t lh = ts->height >> level;
    if (lw == 0) lw = 1;
    if (lh == 0) lh = 1;

    int bpp = get_format_bpp(ts->format);
    uint32_t data_size;

    if (bpp > 0) {
        data_size = (uint32_t)lw * lh * bpp;
    } else {
        int block_size = (ts->format == TEX_FORMAT_DXT5) ? 16 : 8;
        uint16_t bw = (lw + 3) / 4;
        uint16_t bh = (lh + 3) / 4;
        data_size = (uint32_t)bw * bh * block_size;
    }

    if (out_size != NULL) *out_size = data_size;

    /* 复制一份返回 (调用方负责释放) */
    void *copy = fr_alloc(data_size);
    if (copy == NULL) return NULL;
    memcpy(copy, ts->mip_data[level], data_size);

    update_lru(ts);
    return copy;
}

/* ================================================================
 *  公共API实现 - 参数设置
 * ================================================================ */

int texmgr_set_filter(uint32_t handle, tex_filter_t mag, tex_filter_t min)
{
    texture_slot_t *ts = find_slot_by_handle(handle);
    if (ts == NULL) return -1;

    ts->mag_filter = mag;
    ts->min_filter = min;
    return 0;
}

int texmgr_set_wrap(uint32_t handle, tex_wrap_t s, tex_wrap_t t)
{
    texture_slot_t *ts = find_slot_by_handle(handle);
    if (ts == NULL) return -1;

    ts->wrap_s = s;
    ts->wrap_t = t;
    return 0;
}

int texmgr_lock(uint32_t handle)
{
    texture_slot_t *ts = find_slot_by_handle(handle);
    if (ts == NULL) return -1;

    ts->locked = 1;
    return 0;
}

int texmgr_unlock(uint32_t handle)
{
    texture_slot_t *ts = find_slot_by_handle(handle);
    if (ts == NULL) return -1;

    ts->locked = 0;
    return 0;
}

/* ================================================================
 *  公共API实现 - Mipmap操作
 * ================================================================ */

int texmgr_generate_mipmaps(uint32_t handle)
{
    texture_slot_t *ts = find_slot_by_handle(handle);
    if (ts == NULL) return -1;
    if (ts->mip_data[0] == NULL) return -1; /* 无基础层数据 */

    /* 只支持非压缩格式生成Mipmap */
    int bpp = get_format_bpp(ts->format);
    if (bpp <= 0) return -1;

    uint16_t w = ts->width;
    uint16_t h = ts->height;
    int levels = 0;

    /* 计算可生成的最大层数 */
    {
        uint16_t dim = (w > h) ? w : h;
        while (dim > 1) { dim >>= 1; levels++; }
        if (levels > MIP_LEVELS_MAX - 1) levels = MIP_LEVELS_MAX - 1;
    }

    /* 逐级下采样生成Mipmap */
    for (int level = 1; level <= levels; level++) {
        uint16_t prev_w = ts->width >> (level - 1);
        uint16_t prev_h = ts->height >> (level - 1);
        if (prev_w == 0) prev_w = 1;
        if (prev_h == 0) prev_h = 1;

        uint16_t cur_w = ts->width >> level;
        uint16_t cur_h = ts->height >> level;
        if (cur_w == 0) cur_w = 1;
        if (cur_h == 0) cur_h = 1;

        uint32_t cur_size = (uint32_t)cur_w * cur_h * bpp;
        const uint8_t *src = (const uint8_t *)ts->mip_data[level - 1];

        if (src == NULL) break; /* 上层不存在，停止生成 */

        uint8_t *dst = (uint8_t *)fr_alloc(cur_size);
        if (dst == NULL) break;

        /* 2x2盒式滤波下采样 */
        for (uint16_t y = 0; y < cur_h; y++) {
            for (uint16_t x = 0; x < cur_w; x++) {
                /* 源坐标 (在上一层中取2x2区域) */
                int sx = x * 2;
                int sy = y * 2;

                /* 对每个通道进行平均 */
                for (int c = 0; c < bpp; c++) {
                    int sum = 0;
                    int count = 0;

                    /* 2x2区域内采样 (处理边界) */
                    for (int dy = 0; dy < 2; dy++) {
                        for (int dx = 0; dx < 2; dx++) {
                            int cx = sx + dx;
                            int cy = sy + dy;
                            if (cx < (int)prev_w && cy < (int)prev_h) {
                                sum += src[(cy * prev_w + cx) * bpp + c];
                                count++;
                            }
                        }
                    }

                    dst[(y * cur_w + x) * bpp + c] =
                        (count > 0) ? (uint8_t)(sum / count) : 0;
                }
            }
        }

        /* 存储生成的Mipmap层 */
        if (ts->mip_data[level] != NULL) {
            fr_free(ts->mip_data[level]);
        }
        ts->mip_data[level] = dst;
    }

    ts->mip_levels = levels + 1;
    ts->mipmap_generated = 1;

    /* 更新总内存统计 (新增的Mipmap层) */
    for (int l = 1; l < ts->mip_levels; l++) {
        uint16_t lw = ts->width >> l;
        uint16_t lh = ts->height >> l;
        if (lw == 0) lw = 1;
        if (lh == 0) lh = 1;
        tex_total_memory += (uint32_t)lw * lh * bpp;
        ts->size_bytes += (uint32_t)lw * lh * bpp;
    }

    update_lru(ts);
    return ts->mip_levels;
}

int texmgr_bind_mipmap(uint32_t handle, uint32_t level)
{
    texture_slot_t *ts = find_slot_by_handle(handle);
    if (ts == NULL) return -1;
    if (level >= ts->mip_levels) return -1;
    if (ts->mip_data[level] == NULL) return -1;

    /* 在软件渲染器中，"绑定"Mipmap只是标记当前使用哪一层 */
    (void)level; /* 当前为无操作，实际采样时由调用方指定 */
    update_lru(ts);
    return 0;
}

/* ================================================================
 *  公共API实现 - 纹理查找
 * ================================================================ */

texture_slot_t *texmgr_find(uint32_t handle)
{
    texture_slot_t *ts = find_slot_by_handle(handle);
    if (ts != NULL) update_lru(ts);
    return ts;
}

texture_slot_t *texmgr_find_name(const char *name)
{
    if (name == NULL) return NULL;

    for (int i = 0; i < TEX_MAX_SLOTS; i++) {
        if (tex_slots[i].used &&
            strncmp(tex_slots[i].name, name, TEX_NAME_MAX - 1) == 0) {
            update_lru(&tex_slots[i]);
            return &tex_slots[i];
        }
    }
    return NULL;
}

uint32_t texmgr_used_memory(void)
{
    return tex_total_memory;
}

uint32_t texmgr_slot_count(void)
{
    uint32_t count = 0;
    for (int i = 0; i < TEX_MAX_SLOTS; i++) {
        if (tex_slots[i].used) count++;
    }
    return count;
}

/* ================================================================
 *  公共API实现 - LRU清理
 * ================================================================ */

int texmgr_evict_lru(void)
{
    texture_slot_t *oldest = NULL;
    uint32_t oldest_time = 0xFFFFFFFF;

    /* 找到最近最少使用的未锁定纹理 */
    for (int i = 0; i < TEX_MAX_SLOTS; i++) {
        texture_slot_t *ts = &tex_slots[i];
        if (!ts->used) continue;
        if (ts->locked) continue; /* 跳过锁定的纹理 */

        if (ts->access_time < oldest_time) {
            oldest_time = ts->access_time;
            oldest = ts;
        }
    }

    if (oldest == NULL) return -1; /* 无可驱逐的纹理 */

    uint32_t freed = oldest->size_bytes;
    texmgr_destroy(oldest->handle);
    return (int)freed;
}

void texmgr_cleanup_unused(uint32_t timeout_ticks)
{
    uint32_t current_time = tex_lru_clock;
    uint32_t cleaned = 0;

    for (int i = 0; i < TEX_MAX_SLOTS; i++) {
        texture_slot_t *ts = &tex_slots[i];
        if (!ts->used) continue;
        if (ts->locked) continue;

        /* 检查是否超时未访问 */
        if ((current_time - ts->access_time) > timeout_ticks) {
            texmgr_destroy(ts->handle);
            cleaned++;
        }
    }

    (void)cleaned; /* 静默警告 */
}

/* ================================================================
 *  公共API实现 - DXT压缩 (简化BC1/DXT1)
 * ================================================================ */

/*
 * DXT1块结构:
 *   - 2个16位RGB565端点颜色 (c0, c1)
 *   - 32位索引表 (每像素2位，16个像素)
 *
 * 这是简化实现，不追求最佳质量，重点在于正确性。
 */

/* 将RGB888转换为RGB516 */
static uint16_t rgb_to_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((uint16_t)(r >> 3) << 11) |
           ((uint16_t)(g >> 2) << 5) |
           (uint16_t)(b >> 3);
}

/* 从RGB565提取分量 */
static void rgb565_to_rgb(uint16_t c, uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = (uint8_t)((c >> 11) & 0x1F);
    *g = (uint8_t)((c >> 5) & 0x3F);
    *b = c & 0x1F;
    /* 放大到8位 */
    *r = (*r << 3) | (*r >> 2);
    *g = (*g << 2) | (*g >> 4);
    *b = (*b << 3) | (*b >> 2);
}

/* 在两个颜色间插值 (用于DXT1调色板生成) */
static uint16_t lerp_rgb565(uint16_t c0, uint16_t c1, int index)
{
    uint8_t r0, g0, b0, r1, g1, b1;
    rgb565_to_rgb(c0, &r0, &g0, &b0);
    rgb565_to_rgb(c1, &r1, &g1, &b1);

    float t;
    switch (index) {
    case 0: t = 0.0f; break;
    case 1: t = 1.0f / 3.0f; break;
    case 2: t = 2.0f / 3.0f; break;
    default: t = 1.0f; break;
    }

    uint8_t rr = (uint8_t)(r0 + (r1 - r0) * t);
    uint8_t gg = (uint8_t)(g0 + (g1 - g0) * t);
    uint8_t bb = (uint8_t)(b0 + (b1 - b0) * t);

    return rgb_to_rgb565(rr, gg, bb);
}

int texmgr_compress_dxt1(const void *rgba_data, uint16_t w, uint16_t h,
                          void **out_dxt, uint32_t *out_size)
{
    if (rgba_data == NULL || out_dxt == NULL || out_size == NULL) return -1;
    if (w <= 0 || h <= 0) return -1;

    const uint8_t *src = (const uint8_t *)rgba_data;

    /* 尺寸对齐到4的倍数 */
    uint16_t aligned_w = (w + 3) & ~3;
    uint16_t aligned_h = (h + 3) & ~3;

    /* DXT1每个4x4块占8字节 */
    uint16_t blocks_x = aligned_w / 4;
    uint16_t blocks_y = aligned_h / 4;
    uint32_t total_blocks = (uint32_t)blocks_x * blocks_y;
    uint32_t dxt_size = total_blocks * 8;

    /* 分配输出缓冲区 */
    uint8_t *dxt = (uint8_t *)fr_alloc(dxt_size);
    if (dxt == NULL) return -1;
    memset(dxt, 0, dxt_size);

    /* 逐块压缩 */
    for (uint16_t by = 0; by < blocks_y; by++) {
        for (uint16_t bx = 0; bx < blocks_x; bx++) {
            uint8_t block[4][4][4]; /* 4x4 RGBA */
            memset(block, 0, sizeof(block));

            /* 提取4x4块的像素 */
            for (int py = 0; py < 4; py++) {
                for (int px = 0; px < 4; px++) {
                    int ix = bx * 4 + px;
                    int iy = by * 4 + py;

                    if (ix < (int)w && iy < (int)h) {
                        const uint8_t *pixel = src + (iy * w + ix) * 4;
                        block[py][px][0] = pixel[0]; /* R */
                        block[py][px][1] = pixel[1]; /* G */
                        block[py][px][2] = pixel[2]; /* B */
                        block[py][px][3] = pixel[3]; /* A */
                    }
                }
            }

            /* 寻找极值颜色作为端点 (简化: 取最亮和最暗) */
            int max_lum = -1, min_lum = 999999;
            uint8_t max_c[3], min_c[3];

            for (int py = 0; py < 4; py++) {
                for (int px = 0; px < 4; px++) {
                    int lum = block[py][px][0] + block[py][px][1] + block[py][px][2];
                    if (lum > max_lum) {
                        max_lum = lum;
                        max_c[0] = block[py][px][0];
                        max_c[1] = block[py][px][1];
                        max_c[2] = block[py][px][2];
                    }
                    if (lum < min_lum) {
                        min_lum = lum;
                        min_c[0] = block[py][px][0];
                        min_c[1] = block[py][px][1];
                        min_c[2] = block[py][px][2];
                    }
                }
            }

            uint16_t c0 = rgb_to_rgb565(max_c[0], max_c[1], max_c[2]);
            uint16_t c1 = rgb_to_rgb565(min_c[0], min_c[1], min_c[2]);

            /* 写入块数据 */
            uint32_t block_offset = (by * blocks_x + bx) * 8;
            dxt[block_offset + 0] = c0 & 0xFF;
            dxt[block_offset + 1] = (c0 >> 8) & 0xFF;
            dxt[block_offset + 2] = c1 & 0xFF;
            dxt[block_offset + 3] = (c1 >> 8) & 0xFF;

            /* 为每个像素分配索引 (选择最近的调色板颜色) */
            uint32_t indices = 0;
            for (int p = 15; p >= 0; p--) {
                int px = p % 4;
                int py = p / 4;

                int dr0 = abs((int)block[py][px][0] - (int)max_c[0]) +
                         abs((int)block[py][px][1] - (int)max_c[1]) +
                         abs((int)block[py][px][2] - (int)max_c[2]);
                int dr1 = abs((int)block[py][px][0] - (int)min_c[0]) +
                         abs((int)block[py][px][1] - (int)min_c[1]) +
                         abs((int)block[py][px][2] - (int)min_c[2]);

                int idx = (dr0 <= dr1) ? 0 : 1;
                indices = (indices << 2) | idx;
            }

            /* 写入索引 (小端序) */
            dxt[block_offset + 4] = indices & 0xFF;
            dxt[block_offset + 5] = (indices >> 8) & 0xFF;
            dxt[block_offset + 6] = (indices >> 16) & 0xFF;
            dxt[block_offset + 7] = (indices >> 24) & 0xFF;
        }
    }

    *out_dxt = dxt;
    *out_size = dxt_size;
    return 0;
}

int texmgr_decompress_dxt1(const void *dxt_data, uint16_t w, uint16_t h,
                            void **out_rgba, uint32_t *out_size)
{
    if (dxt_data == NULL || out_rgba == NULL || out_size == NULL) return -1;
    if (w <= 0 || h <= 0) return -1;

    const uint8_t *dxt = (const uint8_t *)dxt_data;

    /* 对齐到4 */
    uint16_t aligned_w = (w + 3) & ~3;
    uint16_t aligned_h = (h + 3) & ~3;

    uint16_t blocks_x = aligned_w / 4;
    uint16_t blocks_y = aligned_h / 4;

    /* 分配RGBA输出缓冲区 */
    uint32_t rgba_size = (uint32_t)aligned_w * aligned_h * 4;
    uint8_t *rgba = (uint8_t *)fr_alloc(rgba_size);
    if (rgba == NULL) return -1;
    memset(rgba, 0, rgba_size);

    /* 逐块解压 */
    for (uint16_t by = 0; by < blocks_y; by++) {
        for (uint16_t bx = 0; bx < blocks_x; bx++) {
            uint32_t block_offset = (by * blocks_x + bx) * 8;

            /* 读取端点颜色 */
            uint16_t c0 = dxt[block_offset + 0] |
                          ((uint16_t)dxt[block_offset + 1] << 8);
            uint16_t c1 = dxt[block_offset + 2] |
                          ((uint16_t)dxt[block_offset + 3] << 8);

            /* 读取索引 */
            uint32_t indices = (uint32_t)dxt[block_offset + 4] |
                              ((uint32_t)dxt[block_offset + 5] << 8) |
                              ((uint32_t)dxt[block_offset + 6] << 16) |
                              ((uint32_t)dxt[block_offset + 7] << 24);

            /* 生成4色调色板 */
            uint8_t colors[4][3];
            rgb565_to_rgb(c0, &colors[0][0], &colors[0][1], &colors[0][2]);
            rgb565_to_rgb(c1, &colors[1][0], &colors[1][1], &colors[1][2]);

            /* 调色板中间色 (线性插值) */
            colors[2][0] = (uint8_t)(((int)colors[0][0] * 2 + (int)colors[1][0]) / 3);
            colors[2][1] = (uint8_t)(((int)colors[0][1] * 2 + (int)colors[1][1]) / 3);
            colors[2][2] = (uint8_t)(((int)colors[0][2] * 2 + (int)colors[1][2]) / 3);

            colors[3][0] = (uint8_t)(((int)colors[0][0] + (int)colors[1][0] * 2) / 3);
            colors[3][1] = (uint8_t)(((int)colors[0][1] + (int)colors[1][1] * 2) / 3);
            colors[3][2] = (uint8_t)(((int)colors[0][2] + (int)colors[1][2] * 2) / 3);

            /* 解码像素 */
            for (int p = 0; p < 16; p++) {
                int px = p % 4;
                int py = p / 2; /* 注意: DXT1像素顺序是特殊的 */

                /* 正确的像素位置映射 */
                int real_px = bx * 4 + (p % 4);
                int real_py = by * 4 + (p / 4);

                if (real_px >= (int)aligned_w || real_py >= (int)aligned_h)
                    continue;

                /* 从高位到低位读取索引 (bits 30-31 是像素0) */
                int shift = 30 - (p * 2);
                int idx = (indices >> shift) & 0x3;

                uint8_t *dst = rgba + (real_py * aligned_w + real_px) * 4;
                dst[0] = colors[idx][0]; /* R */
                dst[1] = colors[idx][1]; /* G */
                dst[2] = colors[idx][2]; /* B */
                dst[3] = 255;             /* A (DXT1无Alpha) */
            }
        }
    }

    *out_rgba = rgba;
    *out_size = rgba_size;
    return 0;
}
