/* texture.c - 纹理管理实现
 * 提供格式转换、mipmap 生成、纹理图集和纹理缓存
 */

#include "../include/fr_texture.h"
#include "../include/funrender.h"
#include "stdlib.h"
#include "string.h"
#include "math.h"
#include "../lib/stdio.h"

/* ---- 静态全局变量 ---- */
static uint32_t g_texture_next_id = 1;

/* ---- 内部辅助函数 ---- */

static uint32_t fr_internal_get_bpp(uint32_t format)
{
    switch (format) {
    case FR_TEX_FMT_RGBA8888:   case FR_TEX_FMT_BGRA8888:   return 4;
    case FR_TEX_FMT_RGB888:                                     return 3;
    case FR_TEX_FMT_RGBA4444:   case FR_TEX_FMT_RGBA5551:
    case FR_TEX_FMT_RGB565:     case FR_TEX_FMT_LA88:          return 2;
    case FR_TEX_FMT_A8:         case FR_TEX_FMT_L8:            return 1;
    case FR_TEX_FMT_DXT1:       case FR_TEX_FMT_ETC1:          return 0; /* 压缩格式 */
    case FR_TEX_FMT_DXT3:       case FR_TEX_FMT_DXT5:
    case FR_TEX_FMT_ETC2:                                       return 0;
    default:                                                     return 4;
    }
}

static uint32_t fr_internal_get_block_size(uint32_t format)
{
    switch (format) {
    case FR_TEX_FMT_DXT1: case FR_TEX_FMT_ETC1: return 8;
    case FR_TEX_FMT_DXT3: case FR_TEX_FMT_DXT5:
    case FR_TEX_FMT_ETC2:                        return 16;
    default:                                      return 0;
    }
}

static int fr_internal_is_compressed(uint32_t format)
{
    return (format >= FR_TEX_FMT_DXT1 && format <= FR_TEX_FMT_ETC2);
}

static uint32_t fr_internal_calculate_mip_size(uint32_t width, uint32_t height,
                                                uint32_t format)
{
    if (fr_internal_is_compressed(format)) {
        uint32_t block_size = fr_internal_get_block_size(format);
        uint32_t blocks_w = (width + 3) / 4;
        uint32_t blocks_h = (height + 3) / 4;
        return blocks_w * blocks_h * block_size;
    }
    return width * height * fr_internal_get_bpp(format);
}

static float fr_internal_clamp(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int fr_internal_wrap_coord(int coord, uint32_t size, uint32_t wrap_mode)
{
    switch (wrap_mode) {
    case FR_TEX_WRAP_REPEAT:
        coord = coord % (int)size;
        if (coord < 0) coord += (int)size;
        return coord;
    case FR_TEX_WRAP_MIRROR: {
        int period = (int)size * 2;
        coord = coord % period;
        if (coord < 0) coord += period;
        if (coord >= (int)size) coord = period - 1 - coord;
        return coord;
    }
    case FR_TEX_WRAP_CLAMP_BORDER:
        return (coord < 0 || coord >= (int)size) ? -1 : coord;
    case FR_TEX_WRAP_CLAMP:
    default:
        if (coord < 0) return 0;
        if (coord >= (int)size) return (int)size - 1;
        return coord;
    }
}

static void fr_internal_blit_pixel(struct fr_context *ctx, int x, int y,
                                    fr_color_t color)
{
    if (!ctx || x < 0 || y < 0) return;

    uint32_t *fb = (uint32_t *)ctx;
    uint32_t fb_width = 0;
    uint32_t fb_height = 0;

    /* 尝试从上下文获取帧缓冲信息 */
    fr_handle_t *handle_ctx = (fr_handle_t *)ctx;
    if (handle_ctx) {
        /* 简化的帧缓冲写入 - 假设 ctx 前几个字段是 width/height/fb */
        uint32_t *ctx_data = (uint32_t *)ctx;
        fb_width = ctx_data[0];
        fb_height = ctx_data[1];
        fb = (uint32_t *)(ctx_data[2]);
    }

    if (!fb || !fb_width || !fb_height) return;
    if (x >= (int)fb_width || y >= (int)fb_height) return;

    /* Alpha 混合 */
    if (color.a == 255) {
        fb[y * fb_width + x] = (color.r << 16) | (color.g << 8) | color.b;
    } else if (color.a > 0) {
        uint32_t dst = fb[y * fb_width + x];
        uint8_t dr = (dst >> 16) & 0xFF;
        uint8_t dg = (dst >> 8) & 0xFF;
        uint8_t db = dst & 0xFF;
        float a = color.a / 255.0f;
        float inv_a = 1.0f - a;
        uint8_t r = (uint8_t)(color.r * a + dr * inv_a);
        uint8_t g = (uint8_t)(color.g * a + dg * inv_a);
        uint8_t b = (uint8_t)(color.b * a + db * inv_a);
        fb[y * fb_width + x] = (r << 16) | (g << 8) | b;
    }
}

/* ===================================================================
 *  纹理创建/销毁
 * =================================================================== */

fr_texture_t *fr_texture_create(uint32_t width, uint32_t height, uint32_t format)
{
    if (width == 0 || height == 0) return NULL;

    fr_texture_t *tex = (fr_texture_t *)malloc(sizeof(fr_texture_t));
    if (!tex) return NULL;

    memset(tex, 0, sizeof(fr_texture_t));
    tex->width = width;
    tex->height = height;
    tex->format = format;
    tex->mip_levels = 1;
    tex->data_size = fr_internal_calculate_mip_size(width, height, format);
    tex->stride = width * fr_internal_get_bpp(format);
    tex->id = g_texture_next_id++;
    tex->ref_count = 1;

    /* 默认采样器 */
    tex->sampler.wrap_s = FR_TEX_WRAP_CLAMP;
    tex->sampler.wrap_t = FR_TEX_WRAP_CLAMP;
    tex->sampler.min_filter = FR_TEX_FILTER_LINEAR;
    tex->sampler.mag_filter = FR_TEX_FILTER_LINEAR;
    tex->sampler.border_color = FR_COLOR_TRANSPARENT;
    tex->sampler.anisotropy = 1.0f;
    tex->sampler.lod_bias = 0.0f;
    tex->sampler.min_lod = 0.0f;
    tex->sampler.max_lod = 1000.0f;

    snprintf(tex->name, sizeof(tex->name), "texture_%u", tex->id);

    if (tex->data_size > 0) {
        tex->data = (uint8_t *)malloc(tex->data_size);
        if (!tex->data) {
            free(tex);
            return NULL;
        }
        memset(tex->data, 0, tex->data_size);
    }

    return tex;
}

fr_texture_t *fr_texture_create_from_data(uint32_t width, uint32_t height,
                                           uint32_t format, const uint8_t *data)
{
    fr_texture_t *tex = fr_texture_create(width, height, format);
    if (!tex) return NULL;

    if (data && tex->data && tex->data_size > 0) {
        memcpy(tex->data, data, tex->data_size);
    }

    return tex;
}

fr_texture_t *fr_texture_create_solid(uint32_t width, uint32_t height,
                                       fr_color_t color)
{
    fr_texture_t *tex = fr_texture_create(width, height, FR_TEX_FMT_RGBA8888);
    if (!tex) return NULL;

    if (tex->data) {
        uint32_t pixel = (color.r << 0) | (color.g << 8) |
                         (color.b << 16) | (color.a << 24);
        uint32_t *p = (uint32_t *)tex->data;
        for (uint32_t i = 0; i < width * height; i++) {
            p[i] = pixel;
        }
    }

    return tex;
}

void fr_texture_destroy(fr_texture_t *tex)
{
    if (!tex) return;

    if (tex->data) {
        free(tex->data);
        tex->data = NULL;
    }
    free(tex);
}

void fr_texture_retain(fr_texture_t *tex)
{
    if (tex) {
        tex->ref_count++;
    }
}

void fr_texture_release(fr_texture_t *tex)
{
    if (tex) {
        tex->ref_count--;
        if (tex->ref_count <= 0) {
            fr_texture_destroy(tex);
        }
    }
}

/* ===================================================================
 *  纹理数据操作
 * =================================================================== */

int fr_texture_set_data(fr_texture_t *tex, const uint8_t *data, uint32_t size)
{
    if (!tex || !data) return -1;

    uint32_t expected = fr_texture_get_size(tex->width, tex->height, tex->format);
    if (size < expected && !fr_internal_is_compressed(tex->format)) return -1;

    if (!tex->data || tex->data_size < size) {
        if (tex->data) free(tex->data);
        tex->data = (uint8_t *)malloc(size);
        if (!tex->data) return -1;
        tex->data_size = size;
    }

    memcpy(tex->data, data, size);
    return 0;
}

int fr_texture_get_data(const fr_texture_t *tex, uint8_t *data, uint32_t size)
{
    if (!tex || !data) return -1;

    uint32_t expected = fr_texture_get_size(tex->width, tex->height, tex->format);
    if (size < expected) return -1;

    if (tex->data) {
        memcpy(data, tex->data, size < tex->data_size ? size : tex->data_size);
    }
    return 0;
}

int fr_texture_set_pixel(fr_texture_t *tex, uint32_t x, uint32_t y,
                          fr_color_t color)
{
    if (!tex || !tex->data) return -1;
    if (x >= tex->width || y >= tex->height) return -1;
    if (fr_internal_is_compressed(tex->format)) return -1;

    uint32_t bpp = fr_internal_get_bpp(tex->format);
    uint32_t offset = (y * tex->stride) + (x * bpp);

    if (offset + bpp > tex->data_size) return -1;

    switch (tex->format) {
    case FR_TEX_FMT_RGBA8888:
        tex->data[offset + 0] = color.r;
        tex->data[offset + 1] = color.g;
        tex->data[offset + 2] = color.b;
        tex->data[offset + 3] = color.a;
        break;
    case FR_TEX_FMT_BGRA8888:
        tex->data[offset + 0] = color.b;
        tex->data[offset + 1] = color.g;
        tex->data[offset + 2] = color.r;
        tex->data[offset + 3] = color.a;
        break;
    case FR_TEX_FMT_RGB888:
        tex->data[offset + 0] = color.r;
        tex->data[offset + 1] = color.g;
        tex->data[offset + 2] = color.b;
        break;
    case FR_TEX_FMT_RGBA4444:
        *((uint16_t *)(tex->data + offset)) =
            (uint16_t)(((color.r >> 4) << 12) | ((color.g >> 4) << 8) |
                       ((color.b >> 4) << 4) | (color.a >> 4));
        break;
    case FR_TEX_FMT_RGBA5551:
        *((uint16_t *)(tex->data + offset)) =
            (uint16_t)(((color.r >> 3) << 11) | ((color.g >> 3) << 6) |
                       ((color.b >> 3) << 1) | (color.a >> 7));
        break;
    case FR_TEX_FMT_RGB565:
        *((uint16_t *)(tex->data + offset)) =
            (uint16_t)(((color.r >> 3) << 11) | ((color.g >> 2) << 5) |
                       (color.b >> 3));
        break;
    case FR_TEX_FMT_A8:
        tex->data[offset] = color.a;
        break;
    case FR_TEX_FMT_L8:
        tex->data[offset] = (uint8_t)((color.r + color.g + color.b) / 3);
        break;
    case FR_TEX_FMT_LA88:
        tex->data[offset + 0] = (uint8_t)((color.r + color.g + color.b) / 3);
        tex->data[offset + 1] = color.a;
        break;
    default:
        return -1;
    }

    return 0;
}

fr_color_t fr_texture_get_pixel(const fr_texture_t *tex, uint32_t x, uint32_t y)
{
    fr_color_t result = {0, 0, 0, 0};
    if (!tex || !tex->data) return result;
    if (x >= tex->width || y >= tex->height) return result;
    if (fr_internal_is_compressed(tex->format)) return result;

    uint32_t bpp = fr_internal_get_bpp(tex->format);
    uint32_t offset = (y * tex->stride) + (x * bpp);

    if (offset + bpp > tex->data_size) return result;

    switch (tex->format) {
    case FR_TEX_FMT_RGBA8888:
        result.r = tex->data[offset + 0];
        result.g = tex->data[offset + 1];
        result.b = tex->data[offset + 2];
        result.a = tex->data[offset + 3];
        break;
    case FR_TEX_FMT_BGRA8888:
        result.b = tex->data[offset + 0];
        result.g = tex->data[offset + 1];
        result.r = tex->data[offset + 2];
        result.a = tex->data[offset + 3];
        break;
    case FR_TEX_FMT_RGB888:
        result.r = tex->data[offset + 0];
        result.g = tex->data[offset + 1];
        result.b = tex->data[offset + 2];
        result.a = 255;
        break;
    case FR_TEX_FMT_RGBA4444: {
        uint16_t val = *((uint16_t *)(tex->data + offset));
        result.r = (uint8_t)(((val >> 12) & 0xF) << 4);
        result.g = (uint8_t)(((val >> 8) & 0xF) << 4);
        result.b = (uint8_t)(((val >> 4) & 0xF) << 4);
        result.a = (uint8_t)((val & 0xF) << 4);
        break;
    }
    case FR_TEX_FMT_RGBA5551: {
        uint16_t val = *((uint16_t *)(tex->data + offset));
        result.r = (uint8_t)(((val >> 11) & 0x1F) << 3);
        result.g = (uint8_t)(((val >> 6) & 0x1F) << 3);
        result.b = (uint8_t)(((val >> 1) & 0x1F) << 3);
        result.a = (uint8_t)((val & 1) * 255);
        break;
    }
    case FR_TEX_FMT_RGB565: {
        uint16_t val = *((uint16_t *)(tex->data + offset));
        result.r = (uint8_t)(((val >> 11) & 0x1F) << 3);
        result.g = (uint8_t)(((val >> 5) & 0x3F) << 2);
        result.b = (uint8_t)((val & 0x1F) << 3);
        result.a = 255;
        break;
    }
    case FR_TEX_FMT_A8:
        result.r = 255;
        result.g = 255;
        result.b = 255;
        result.a = tex->data[offset];
        break;
    case FR_TEX_FMT_L8:
        result.r = tex->data[offset];
        result.g = tex->data[offset];
        result.b = tex->data[offset];
        result.a = 255;
        break;
    case FR_TEX_FMT_LA88:
        result.r = tex->data[offset + 0];
        result.g = tex->data[offset + 0];
        result.b = tex->data[offset + 0];
        result.a = tex->data[offset + 1];
        break;
    default:
        break;
    }

    return result;
}

int fr_texture_set_region(fr_texture_t *tex, uint32_t x, uint32_t y,
                           uint32_t w, uint32_t h, const uint8_t *data)
{
    if (!tex || !tex->data || !data) return -1;
    if (x + w > tex->width || y + h > tex->height) return -1;
    if (fr_internal_is_compressed(tex->format)) return -1;

    uint32_t bpp = fr_internal_get_bpp(tex->format);

    for (uint32_t row = 0; row < h; row++) {
        uint32_t dst_offset = ((y + row) * tex->stride) + (x * bpp);
        uint32_t src_offset = row * w * bpp;
        if (dst_offset + w * bpp <= tex->data_size) {
            memcpy(tex->data + dst_offset, data + src_offset, w * bpp);
        }
    }

    return 0;
}

int fr_texture_get_region(const fr_texture_t *tex, uint32_t x, uint32_t y,
                           uint32_t w, uint32_t h, uint8_t *data)
{
    if (!tex || !tex->data || !data) return -1;
    if (x + w > tex->width || y + h > tex->height) return -1;
    if (fr_internal_is_compressed(tex->format)) return -1;

    uint32_t bpp = fr_internal_get_bpp(tex->format);

    for (uint32_t row = 0; row < h; row++) {
        uint32_t src_offset = ((y + row) * tex->stride) + (x * bpp);
        uint32_t dst_offset = row * w * bpp;
        memcpy(data + dst_offset, tex->data + src_offset, w * bpp);
    }

    return 0;
}

/* ===================================================================
 *  格式转换
 * =================================================================== */

int fr_texture_convert(fr_texture_t *tex, uint32_t new_format)
{
    if (!tex || !tex->data) return -1;
    if (tex->format == new_format) return 0;
    if (fr_internal_is_compressed(tex->format) ||
        fr_internal_is_compressed(new_format)) return -1;

    uint32_t new_bpp = fr_internal_get_bpp(new_format);
    uint32_t new_size = tex->width * tex->height * new_bpp;
    uint8_t *new_data = (uint8_t *)malloc(new_size);
    if (!new_data) return -1;

    for (uint32_t y = 0; y < tex->height; y++) {
        for (uint32_t x = 0; x < tex->width; x++) {
            fr_color_t c = fr_texture_get_pixel(tex, x, y);
            uint32_t offset = (y * tex->width + x) * new_bpp;

            switch (new_format) {
            case FR_TEX_FMT_RGBA8888:
                new_data[offset + 0] = c.r;
                new_data[offset + 1] = c.g;
                new_data[offset + 2] = c.b;
                new_data[offset + 3] = c.a;
                break;
            case FR_TEX_FMT_BGRA8888:
                new_data[offset + 0] = c.b;
                new_data[offset + 1] = c.g;
                new_data[offset + 2] = c.r;
                new_data[offset + 3] = c.a;
                break;
            case FR_TEX_FMT_RGB888:
                new_data[offset + 0] = c.r;
                new_data[offset + 1] = c.g;
                new_data[offset + 2] = c.b;
                break;
            default:
                free(new_data);
                return -1;
            }
        }
    }

    free(tex->data);
    tex->data = new_data;
    tex->data_size = new_size;
    tex->format = new_format;
    tex->stride = tex->width * new_bpp;

    return 0;
}

uint32_t fr_texture_get_bpp(uint32_t format)
{
    return fr_internal_get_bpp(format);
}

uint32_t fr_texture_get_size(uint32_t width, uint32_t height, uint32_t format)
{
    return fr_internal_calculate_mip_size(width, height, format);
}

const char *fr_texture_get_format_name(uint32_t format)
{
    switch (format) {
    case FR_TEX_FMT_RGBA8888:   return "RGBA8888";
    case FR_TEX_FMT_BGRA8888:   return "BGRA8888";
    case FR_TEX_FMT_RGB888:     return "RGB888";
    case FR_TEX_FMT_RGBA4444:   return "RGBA4444";
    case FR_TEX_FMT_RGBA5551:   return "RGBA5551";
    case FR_TEX_FMT_RGB565:     return "RGB565";
    case FR_TEX_FMT_A8:         return "A8";
    case FR_TEX_FMT_L8:         return "L8";
    case FR_TEX_FMT_LA88:       return "LA88";
    case FR_TEX_FMT_DXT1:       return "DXT1";
    case FR_TEX_FMT_DXT3:       return "DXT3";
    case FR_TEX_FMT_DXT5:       return "DXT5";
    case FR_TEX_FMT_ETC1:       return "ETC1";
    case FR_TEX_FMT_ETC2:       return "ETC2";
    default:                     return "Unknown";
    }
}

/* ===================================================================
 *  Mipmap
 * =================================================================== */

int fr_texture_generate_mipmaps(fr_texture_t *tex)
{
    if (!tex || !tex->data) return -1;
    if (fr_internal_is_compressed(tex->format)) return -1;

    /* 计算 mipmap 级别数 */
    uint32_t max_dim = tex->width > tex->height ? tex->width : tex->height;
    uint32_t levels = 1;
    while (max_dim > 1) {
        max_dim >>= 1;
        levels++;
    }

    /* 分配包含所有 mip 级别的数据 */
    uint32_t bpp = fr_internal_get_bpp(tex->format);
    uint32_t total_size = 0;
    uint32_t mip_sizes[32]; /* 假设最多 32 个级别 */
    uint32_t mip_widths[32];
    uint32_t mip_heights[32];

    mip_widths[0] = tex->width;
    mip_heights[0] = tex->height;
    mip_sizes[0] = tex->width * tex->height * bpp;
    total_size += mip_sizes[0];

    for (uint32_t i = 1; i < levels; i++) {
        mip_widths[i] = mip_widths[i - 1] > 1 ? mip_widths[i - 1] >> 1 : 1;
        mip_heights[i] = mip_heights[i - 1] > 1 ? mip_heights[i - 1] >> 1 : 1;
        mip_sizes[i] = mip_widths[i] * mip_heights[i] * bpp;
        total_size += mip_sizes[i];
    }

    uint8_t *new_data = (uint8_t *)malloc(total_size);
    if (!new_data) return -1;

    /* 复制原始数据到 level 0 */
    memcpy(new_data, tex->data, mip_sizes[0]);

    /* 生成每个 mip 级别 (简单 2x2 盒式过滤) */
    uint32_t src_offset = 0;
    for (uint32_t level = 1; level < levels; level++) {
        uint32_t src_w = mip_widths[level - 1];
        uint32_t src_h = mip_heights[level - 1];
        uint32_t dst_w = mip_widths[level];
        uint32_t dst_h = mip_heights[level];
        uint32_t dst_offset = src_offset + mip_sizes[level - 1];

        for (uint32_t y = 0; y < dst_h; y++) {
            for (uint32_t x = 0; x < dst_w; x++) {
                uint32_t sx = x * 2;
                uint32_t sy = y * 2;

                uint32_t r = 0, g = 0, b = 0, a = 0;
                int count = 0;

                for (uint32_t dy = 0; dy < 2 && (sy + dy) < src_h; dy++) {
                    for (uint32_t dx = 0; dx < 2 && (sx + dx) < src_w; dx++) {
                        uint32_t src_idx = ((sy + dy) * src_w + (sx + dx)) * bpp;
                        switch (tex->format) {
                        case FR_TEX_FMT_RGBA8888:
                            r += new_data[src_offset + src_idx + 0];
                            g += new_data[src_offset + src_idx + 1];
                            b += new_data[src_offset + src_idx + 2];
                            a += new_data[src_offset + src_idx + 3];
                            break;
                        case FR_TEX_FMT_BGRA8888:
                            b += new_data[src_offset + src_idx + 0];
                            g += new_data[src_offset + src_idx + 1];
                            r += new_data[src_offset + src_idx + 2];
                            a += new_data[src_offset + src_idx + 3];
                            break;
                        case FR_TEX_FMT_RGB888:
                            r += new_data[src_offset + src_idx + 0];
                            g += new_data[src_offset + src_idx + 1];
                            b += new_data[src_offset + src_idx + 2];
                            a += 255;
                            break;
                        default:
                            break;
                        }
                        count++;
                    }
                }

                uint32_t dst_idx = (y * dst_w + x) * bpp;
                switch (tex->format) {
                case FR_TEX_FMT_RGBA8888:
                    new_data[dst_offset + dst_idx + 0] = (uint8_t)(r / count);
                    new_data[dst_offset + dst_idx + 1] = (uint8_t)(g / count);
                    new_data[dst_offset + dst_idx + 2] = (uint8_t)(b / count);
                    new_data[dst_offset + dst_idx + 3] = (uint8_t)(a / count);
                    break;
                case FR_TEX_FMT_BGRA8888:
                    new_data[dst_offset + dst_idx + 0] = (uint8_t)(b / count);
                    new_data[dst_offset + dst_idx + 1] = (uint8_t)(g / count);
                    new_data[dst_offset + dst_idx + 2] = (uint8_t)(r / count);
                    new_data[dst_offset + dst_idx + 3] = (uint8_t)(a / count);
                    break;
                case FR_TEX_FMT_RGB888:
                    new_data[dst_offset + dst_idx + 0] = (uint8_t)(r / count);
                    new_data[dst_offset + dst_idx + 1] = (uint8_t)(g / count);
                    new_data[dst_offset + dst_idx + 2] = (uint8_t)(b / count);
                    break;
                default:
                    break;
                }
            }
        }
        src_offset += mip_sizes[level - 1];
    }

    free(tex->data);
    tex->data = new_data;
    tex->data_size = total_size;
    tex->mip_levels = levels;

    return 0;
}

int fr_texture_get_mip_level(fr_texture_t *tex, uint32_t level,
                              uint32_t *width, uint32_t *height, uint8_t **data)
{
    if (!tex || !tex->data) return -1;
    if (level >= tex->mip_levels) return -1;

    uint32_t bpp = fr_internal_get_bpp(tex->format);
    uint32_t offset = 0;
    uint32_t cur_w = tex->width;
    uint32_t cur_h = tex->height;

    for (uint32_t i = 0; i < level; i++) {
        offset += cur_w * cur_h * bpp;
        cur_w = cur_w > 1 ? cur_w >> 1 : 1;
        cur_h = cur_h > 1 ? cur_h >> 1 : 1;
    }

    if (width) *width = cur_w;
    if (height) *height = cur_h;
    if (data) *data = tex->data + offset;

    return 0;
}

fr_texture_t *fr_texture_create_mip_level(const fr_texture_t *tex,
                                           uint32_t level)
{
    if (!tex) return NULL;

    uint32_t width, height;
    uint8_t *mip_data = NULL;

    if (fr_texture_get_mip_level((fr_texture_t *)tex, level,
                                  &width, &height, &mip_data) != 0) {
        return NULL;
    }

    return fr_texture_create_from_data(width, height, tex->format, mip_data);
}

/* ===================================================================
 *  采样器
 * =================================================================== */

void fr_texture_set_sampler(fr_texture_t *tex, const fr_sampler_t *sampler)
{
    if (tex && sampler) {
        memcpy(&tex->sampler, sampler, sizeof(fr_sampler_t));
    }
}

void fr_texture_set_filter(fr_texture_t *tex,
                            uint32_t min_filter, uint32_t mag_filter)
{
    if (tex) {
        tex->sampler.min_filter = min_filter;
        tex->sampler.mag_filter = mag_filter;
    }
}

void fr_texture_set_wrap(fr_texture_t *tex, uint32_t wrap_s, uint32_t wrap_t)
{
    if (tex) {
        tex->sampler.wrap_s = wrap_s;
        tex->sampler.wrap_t = wrap_t;
    }
}

/* ===================================================================
 *  纹理采样
 * =================================================================== */

fr_color_t fr_texture_sample(const fr_texture_t *tex, float u, float v)
{
    if (tex->sampler.mag_filter == FR_TEX_FILTER_LINEAR ||
        tex->sampler.mag_filter == FR_TEX_FILTER_BILINEAR ||
        tex->sampler.mag_filter == FR_TEX_FILTER_TRILINEAR) {
        return fr_texture_sample_bilinear(tex, u, v);
    }
    /* 最近邻采样 */
    int x = (int)(u * tex->width);
    int y = (int)(v * tex->height);
    x = fr_internal_wrap_coord(x, tex->width, tex->sampler.wrap_s);
    y = fr_internal_wrap_coord(y, tex->height, tex->sampler.wrap_t);

    if (x < 0 || y < 0) return tex->sampler.border_color;
    return fr_texture_get_pixel(tex, (uint32_t)x, (uint32_t)y);
}

fr_color_t fr_texture_sample_lod(const fr_texture_t *tex, float u, float v,
                                  float lod)
{
    if (!tex) return FR_COLOR_TRANSPARENT;

    if (lod <= 0.0f || tex->mip_levels <= 1) {
        return fr_texture_sample(tex, u, v);
    }

    /* 双线性插值在两个 mip 级别之间 */
    uint32_t level_low = (uint32_t)lod;
    uint32_t level_high = level_low + 1;
    float frac = lod - (float)level_low;

    if (level_low >= tex->mip_levels) level_low = tex->mip_levels - 1;
    if (level_high >= tex->mip_levels) level_high = tex->mip_levels - 1;

    fr_color_t c1 = fr_texture_sample(tex, u, v);
    fr_color_t c2 = c1;

    if (level_low != level_high) {
        /* 为了简单，这里使用 level 0 的颜色 */
        c2 = fr_texture_sample(tex, u, v);
    }

    fr_color_t result;
    result.r = (uint8_t)(c1.r * (1.0f - frac) + c2.r * frac);
    result.g = (uint8_t)(c1.g * (1.0f - frac) + c2.g * frac);
    result.b = (uint8_t)(c1.b * (1.0f - frac) + c2.b * frac);
    result.a = (uint8_t)(c1.a * (1.0f - frac) + c2.a * frac);

    return result;
}

fr_color_t fr_texture_sample_bilinear(const fr_texture_t *tex, float u, float v)
{
    if (!tex) return FR_COLOR_TRANSPARENT;

    float px = u * tex->width - 0.5f;
    float py = v * tex->height - 0.5f;

    int x0 = (int)(float)floor(px);
    int y0 = (int)(float)floor(py);
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    float fx = px - (float)x0;
    float fy = py - (float)y0;

    int wx0 = fr_internal_wrap_coord(x0, tex->width, tex->sampler.wrap_s);
    int wx1 = fr_internal_wrap_coord(x1, tex->width, tex->sampler.wrap_s);
    int wy0 = fr_internal_wrap_coord(y0, tex->height, tex->sampler.wrap_t);
    int wy1 = fr_internal_wrap_coord(y1, tex->height, tex->sampler.wrap_t);

    fr_color_t c00 = (wx0 >= 0 && wy0 >= 0) ?
        fr_texture_get_pixel(tex, (uint32_t)wx0, (uint32_t)wy0) :
        tex->sampler.border_color;
    fr_color_t c10 = (wx1 >= 0 && wy0 >= 0) ?
        fr_texture_get_pixel(tex, (uint32_t)wx1, (uint32_t)wy0) :
        tex->sampler.border_color;
    fr_color_t c01 = (wx0 >= 0 && wy1 >= 0) ?
        fr_texture_get_pixel(tex, (uint32_t)wx0, (uint32_t)wy1) :
        tex->sampler.border_color;
    fr_color_t c11 = (wx1 >= 0 && wy1 >= 0) ?
        fr_texture_get_pixel(tex, (uint32_t)wx1, (uint32_t)wy1) :
        tex->sampler.border_color;

    fr_color_t result;
    result.r = (uint8_t)((c00.r * (1 - fx) + c10.r * fx) * (1 - fy) +
                         (c01.r * (1 - fx) + c11.r * fx) * fy);
    result.g = (uint8_t)((c00.g * (1 - fx) + c10.g * fx) * (1 - fy) +
                         (c01.g * (1 - fx) + c11.g * fx) * fy);
    result.b = (uint8_t)((c00.b * (1 - fx) + c10.b * fx) * (1 - fy) +
                         (c01.b * (1 - fx) + c11.b * fx) * fy);
    result.a = (uint8_t)((c00.a * (1 - fx) + c10.a * fx) * (1 - fy) +
                         (c01.a * (1 - fx) + c11.a * fx) * fy);

    return result;
}

/* ===================================================================
 *  纹理渲染
 * =================================================================== */

void fr_texture_blit(fr_texture_t *tex, struct fr_context *ctx,
                      int dx, int dy)
{
    if (!tex || !ctx) return;
    fr_texture_blit_scaled(tex, ctx, dx, dy, (int)tex->width, (int)tex->height);
}

void fr_texture_blit_scaled(fr_texture_t *tex, struct fr_context *ctx,
                             int dx, int dy, int dw, int dh)
{
    fr_texture_blit_region(tex, ctx, 0, 0,
                            (int)tex->width, (int)tex->height,
                            dx, dy, dw, dh);
}

void fr_texture_blit_region(fr_texture_t *tex, struct fr_context *ctx,
                             int sx, int sy, int sw, int sh,
                             int dx, int dy, int dw, int dh)
{
    if (!tex || !ctx || !tex->data) return;
    if (dw <= 0 || dh <= 0) return;

    float scale_x = (float)sw / (float)dw;
    float scale_y = (float)sh / (float)dh;

    for (int y = 0; y < dh; y++) {
        float v = ((float)y + 0.5f) / (float)dh;
        int tex_y = sy + (int)(v * (float)sh);

        for (int x = 0; x < dw; x++) {
            float u = ((float)x + 0.5f) / (float)dw;
            int tex_x = sx + (int)(u * (float)sw);

            if (tex_x >= 0 && tex_x < (int)tex->width &&
                tex_y >= 0 && tex_y < (int)tex->height) {
                fr_color_t c = fr_texture_get_pixel(tex,
                    (uint32_t)tex_x, (uint32_t)tex_y);
                fr_internal_blit_pixel(ctx, dx + x, dy + y, c);
            }
        }
    }
}

void fr_texture_blit_rotated(fr_texture_t *tex, struct fr_context *ctx,
                              int dx, int dy, float angle, float cx, float cy)
{
    if (!tex || !ctx) return;

    float cos_a = (float)cos(angle);
    float sin_a = (float)sin(angle);

    /* 计算旋转后的包围盒 */
    float corners[4][2] = {
        {-cx, -cy},
        {tex->width - cx, -cy},
        {tex->width - cx, tex->height - cy},
        {-cx, tex->height - cy}
    };

    int min_x = 0x7fffffff, max_x = -0x7fffffff;
    int min_y = 0x7fffffff, max_y = -0x7fffffff;

    for (int i = 0; i < 4; i++) {
        float rx = corners[i][0] * cos_a - corners[i][1] * sin_a;
        float ry = corners[i][0] * sin_a + corners[i][1] * cos_a;
        int ix = (int)rx;
        int iy = (int)ry;
        if (ix < min_x) min_x = ix;
        if (ix > max_x) max_x = ix;
        if (iy < min_y) min_y = iy;
        if (iy > max_y) max_y = iy;
    }

    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            /* 逆变换到纹理坐标 */
            float src_x = x * cos_a + y * sin_a + cx;
            float src_y = -x * sin_a + y * cos_a + cy;

            float u = src_x / tex->width;
            float v = src_y / tex->height;

            if (u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f) {
                fr_color_t c = fr_texture_sample(tex, u, v);
                fr_internal_blit_pixel(ctx, dx + x, dy + y, c);
            }
        }
    }
}

void fr_texture_blit_tinted(fr_texture_t *tex, struct fr_context *ctx,
                             int dx, int dy, fr_color_t tint)
{
    if (!tex || !ctx) return;

    float tr = tint.r / 255.0f;
    float tg = tint.g / 255.0f;
    float tb = tint.b / 255.0f;
    float ta = tint.a / 255.0f;

    for (uint32_t y = 0; y < tex->height; y++) {
        for (uint32_t x = 0; x < tex->width; x++) {
            fr_color_t c = fr_texture_get_pixel(tex, x, y);
            c.r = (uint8_t)(c.r * tr);
            c.g = (uint8_t)(c.g * tg);
            c.b = (uint8_t)(c.b * tb);
            c.a = (uint8_t)(c.a * ta);
            fr_internal_blit_pixel(ctx, dx + (int)x, dy + (int)y, c);
        }
    }
}

/* ===================================================================
 *  纹理操作
 * =================================================================== */

fr_texture_t *fr_texture_flip_horizontal(const fr_texture_t *tex)
{
    if (!tex) return NULL;

    fr_texture_t *result = fr_texture_create(tex->width, tex->height, tex->format);
    if (!result) return NULL;

    uint32_t bpp = fr_internal_get_bpp(tex->format);

    for (uint32_t y = 0; y < tex->height; y++) {
        for (uint32_t x = 0; x < tex->width; x++) {
            uint32_t src_off = y * tex->stride + x * bpp;
            uint32_t dst_off = y * result->stride + (tex->width - 1 - x) * bpp;
            if (src_off + bpp <= tex->data_size &&
                dst_off + bpp <= result->data_size) {
                memcpy(result->data + dst_off, tex->data + src_off, bpp);
            }
        }
    }

    return result;
}

fr_texture_t *fr_texture_flip_vertical(const fr_texture_t *tex)
{
    if (!tex) return NULL;

    fr_texture_t *result = fr_texture_create(tex->width, tex->height, tex->format);
    if (!result) return NULL;

    for (uint32_t y = 0; y < tex->height; y++) {
        uint32_t src_row = y * tex->stride;
        uint32_t dst_row = (tex->height - 1 - y) * result->stride;
        if (src_row + tex->stride <= tex->data_size &&
            dst_row + result->stride <= result->data_size) {
            memcpy(result->data + dst_row, tex->data + src_row, tex->stride);
        }
    }

    return result;
}

fr_texture_t *fr_texture_rotate_90(const fr_texture_t *tex)
{
    if (!tex) return NULL;

    fr_texture_t *result = fr_texture_create(tex->height, tex->width, tex->format);
    if (!result) return NULL;

    uint32_t bpp = fr_internal_get_bpp(tex->format);

    for (uint32_t y = 0; y < tex->height; y++) {
        for (uint32_t x = 0; x < tex->width; x++) {
            uint32_t src_off = y * tex->stride + x * bpp;
            uint32_t dst_x = tex->height - 1 - y;
            uint32_t dst_y = x;
            uint32_t dst_off = dst_y * result->stride + dst_x * bpp;
            if (src_off + bpp <= tex->data_size &&
                dst_off + bpp <= result->data_size) {
                memcpy(result->data + dst_off, tex->data + src_off, bpp);
            }
        }
    }

    return result;
}

fr_texture_t *fr_texture_crop(const fr_texture_t *tex,
                               uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (!tex) return NULL;
    if (x + w > tex->width || y + h > tex->height) return NULL;

    fr_texture_t *result = fr_texture_create(w, h, tex->format);
    if (!result) return NULL;

    uint32_t bpp = fr_internal_get_bpp(tex->format);

    for (uint32_t row = 0; row < h; row++) {
        uint32_t src_off = ((y + row) * tex->stride) + x * bpp;
        uint32_t dst_off = row * result->stride;
        memcpy(result->data + dst_off, tex->data + src_off, w * bpp);
    }

    return result;
}

fr_texture_t *fr_texture_scale(const fr_texture_t *tex,
                                uint32_t new_width, uint32_t new_height)
{
    if (!tex) return NULL;
    if (new_width == 0 || new_height == 0) return NULL;

    fr_texture_t *result = fr_texture_create(new_width, new_height, tex->format);
    if (!result) return NULL;

    /* 双线性缩放 */
    for (uint32_t y = 0; y < new_height; y++) {
        float v = ((float)y + 0.5f) / (float)new_height;
        for (uint32_t x = 0; x < new_width; x++) {
            float u = ((float)x + 0.5f) / (float)new_width;
            fr_color_t c = fr_texture_sample_bilinear(tex, u, v);
            fr_texture_set_pixel(result, x, y, c);
        }
    }

    return result;
}

/* ===================================================================
 *  纹理图集
 * =================================================================== */

fr_texture_atlas_t *fr_atlas_create(uint32_t width, uint32_t height,
                                     uint32_t format)
{
    fr_texture_atlas_t *atlas = (fr_texture_atlas_t *)malloc(
        sizeof(fr_texture_atlas_t));
    if (!atlas) return NULL;

    memset(atlas, 0, sizeof(fr_texture_atlas_t));
    atlas->width = width;
    atlas->height = height;
    atlas->format = format;
    atlas->used_width = 0;
    atlas->used_height = 0;
    atlas->max_height = 0;
    atlas->dirty = 1;

    atlas->texture = fr_texture_create(width, height, format);
    if (!atlas->texture) {
        free(atlas);
        return NULL;
    }

    return atlas;
}

void fr_atlas_destroy(fr_texture_atlas_t *atlas)
{
    if (!atlas) return;

    if (atlas->texture) {
        fr_texture_destroy(atlas->texture);
    }
    free(atlas);
}

fr_tex_region_t *fr_atlas_add(fr_texture_atlas_t *atlas,
                               uint32_t width, uint32_t height,
                               const uint8_t *data)
{
    if (!atlas || !data || width == 0 || height == 0) return NULL;
    if (atlas->region_count >= FR_ATLAS_MAX_TEXTURES) return NULL;

    /* 简单的行式打包 (shelf packing) */
    if (atlas->used_width + width > atlas->width) {
        /* 换行 */
        atlas->used_width = 0;
        atlas->used_height += atlas->max_height;
        atlas->max_height = 0;
    }

    if (atlas->used_height + height > atlas->height) {
        return NULL; /* 图集已满 */
    }

    uint32_t x = atlas->used_width;
    uint32_t y = atlas->used_height;

    /* 复制数据到图集纹理 */
    uint32_t bpp = fr_internal_get_bpp(atlas->format);
    for (uint32_t row = 0; row < height; row++) {
        uint32_t dst_off = ((y + row) * atlas->texture->stride) + x * bpp;
        if (dst_off + width * bpp <= atlas->texture->data_size) {
            memcpy(atlas->texture->data + dst_off,
                   data + row * width * bpp, width * bpp);
        }
    }

    /* 记录区域 */
    fr_tex_region_t *region = &atlas->regions[atlas->region_count];
    region->texture = atlas->texture;
    region->x = x;
    region->y = y;
    region->w = width;
    region->h = height;
    atlas->region_count++;

    /* 更新占用 */
    atlas->used_width += width + 1; /* 1 像素间距 */
    if (height > atlas->max_height) {
        atlas->max_height = height;
    }
    atlas->dirty = 1;

    return region;
}

fr_tex_region_t *fr_atlas_add_texture(fr_texture_atlas_t *atlas,
                                       const fr_texture_t *tex)
{
    if (!atlas || !tex || !tex->data) return NULL;
    return fr_atlas_add(atlas, tex->width, tex->height, tex->data);
}

int fr_atlas_remove(fr_texture_atlas_t *atlas, fr_tex_region_t *region)
{
    if (!atlas || !region) return -1;

    for (int i = 0; i < atlas->region_count; i++) {
        if (&atlas->regions[i] == region) {
            /* 清除纹理区域 */
            uint32_t bpp = fr_internal_get_bpp(atlas->format);
            for (uint32_t row = 0; row < region->h; row++) {
                uint32_t off = ((region->y + row) * atlas->texture->stride) +
                               region->x * bpp;
                if (off + region->w * bpp <= atlas->texture->data_size) {
                    memset(atlas->texture->data + off, 0, region->w * bpp);
                }
            }

            /* 移除条目 */
            if (i < atlas->region_count - 1) {
                memmove(&atlas->regions[i], &atlas->regions[i + 1],
                        (atlas->region_count - i - 1) *
                        sizeof(fr_tex_region_t));
            }
            atlas->region_count--;
            atlas->dirty = 1;
            return 0;
        }
    }

    return -1;
}

void fr_atlas_clear(fr_texture_atlas_t *atlas)
{
    if (!atlas) return;

    if (atlas->texture && atlas->texture->data) {
        memset(atlas->texture->data, 0, atlas->texture->data_size);
    }
    atlas->region_count = 0;
    atlas->used_width = 0;
    atlas->used_height = 0;
    atlas->max_height = 0;
    atlas->dirty = 1;
}

fr_tex_region_t *fr_atlas_find(fr_texture_atlas_t *atlas, uint32_t idx)
{
    if (!atlas || idx >= (uint32_t)atlas->region_count) return NULL;
    return &atlas->regions[idx];
}

void fr_atlas_compact(fr_texture_atlas_t *atlas)
{
    if (!atlas || !atlas->texture) return;

    /* 创建临时纹理 */
    fr_texture_t *tmp = fr_texture_create(atlas->width, atlas->height,
                                           atlas->format);
    if (!tmp) return;

    uint32_t cur_x = 0, cur_y = 0;
    uint32_t row_h = 0;

    for (int i = 0; i < atlas->region_count; i++) {
        fr_tex_region_t *reg = &atlas->regions[i];

        if (cur_x + reg->w > atlas->width) {
            cur_x = 0;
            cur_y += row_h;
            row_h = 0;
        }

        /* 复制像素 */
        uint32_t bpp = fr_internal_get_bpp(atlas->format);
        for (uint32_t row = 0; row < reg->h; row++) {
            uint32_t src_off = ((reg->y + row) * atlas->texture->stride) +
                               reg->x * bpp;
            uint32_t dst_off = ((cur_y + row) * tmp->stride) + cur_x * bpp;
            if (src_off + reg->w * bpp <= atlas->texture->data_size &&
                dst_off + reg->w * bpp <= tmp->data_size) {
                memcpy(tmp->data + dst_off, atlas->texture->data + src_off,
                       reg->w * bpp);
            }
        }

        reg->x = cur_x;
        reg->y = cur_y;
        cur_x += reg->w + 1;
        if (reg->h > row_h) row_h = reg->h;
    }

    /* 替换纹理 */
    free(atlas->texture->data);
    atlas->texture->data = tmp->data;
    atlas->texture->data_size = tmp->data_size;
    tmp->data = NULL;
    fr_texture_destroy(tmp);

    atlas->used_width = cur_x;
    atlas->used_height = cur_y + row_h;
    atlas->dirty = 0;
}

void fr_atlas_pack(fr_texture_atlas_t *atlas)
{
    /* 调用 compact 进行重新打包 */
    fr_atlas_compact(atlas);
}

/* ===================================================================
 *  纹理缓存
 * =================================================================== */

fr_texture_cache_t *fr_cache_create(uint32_t max_size)
{
    fr_texture_cache_t *cache = (fr_texture_cache_t *)malloc(
        sizeof(fr_texture_cache_t));
    if (!cache) return NULL;

    memset(cache, 0, sizeof(fr_texture_cache_t));
    cache->max_size = max_size;

    return cache;
}

void fr_cache_destroy(fr_texture_cache_t *cache)
{
    if (!cache) return;

    for (int i = 0; i < cache->entry_count; i++) {
        if (cache->entries[i].texture) {
            fr_texture_destroy(cache->entries[i].texture);
        }
    }
    free(cache);
}

fr_texture_t *fr_cache_get(fr_texture_cache_t *cache, uint32_t key)
{
    if (!cache) return NULL;

    for (int i = 0; i < cache->entry_count; i++) {
        if (cache->entries[i].key == key) {
            cache->entries[i].last_access = cache->access_counter++;
            cache->hits++;
            return cache->entries[i].texture;
        }
    }

    cache->misses++;
    return NULL;
}

int fr_cache_put(fr_texture_cache_t *cache, uint32_t key, fr_texture_t *tex)
{
    if (!cache || !tex) return -1;

    uint32_t tex_size = tex->data_size;

    /* 检查是否已存在 */
    for (int i = 0; i < cache->entry_count; i++) {
        if (cache->entries[i].key == key) {
            /* 更新 */
            if (cache->entries[i].texture) {
                cache->current_size -= cache->entries[i].size;
                fr_texture_destroy(cache->entries[i].texture);
            }
            cache->entries[i].texture = tex;
            cache->entries[i].size = tex_size;
            cache->entries[i].last_access = cache->access_counter++;
            cache->current_size += tex_size;
            return 0;
        }
    }

    /* 需要空间时驱逐 */
    while (cache->entry_count >= FR_CACHE_MAX_ENTRIES ||
           (cache->current_size + tex_size > cache->max_size &&
            cache->entry_count > 0)) {
        /* 驱逐 LRU (最近最少使用) */
        int lru_idx = 0;
        uint32_t oldest = 0xFFFFFFFF;
        for (int i = 0; i < cache->entry_count; i++) {
            if (!cache->entries[i].locked &&
                cache->entries[i].last_access < oldest) {
                oldest = cache->entries[i].last_access;
                lru_idx = i;
            }
        }

        if (oldest == 0xFFFFFFFF) {
            /* 所有条目都被锁定，尝试驱逐第一个 */
            for (int i = 0; i < cache->entry_count; i++) {
                if (!cache->entries[i].locked) {
                    lru_idx = i;
                    break;
                }
            }
        }

        cache->current_size -= cache->entries[lru_idx].size;
        if (cache->entries[lru_idx].texture) {
            fr_texture_destroy(cache->entries[lru_idx].texture);
        }

        if (lru_idx < cache->entry_count - 1) {
            memmove(&cache->entries[lru_idx], &cache->entries[lru_idx + 1],
                    (cache->entry_count - lru_idx - 1) *
                    sizeof(fr_cache_entry_t));
        }
        cache->entry_count--;
    }

    /* 添加新条目 */
    cache->entries[cache->entry_count].texture = tex;
    cache->entries[cache->entry_count].key = key;
    cache->entries[cache->entry_count].size = tex_size;
    cache->entries[cache->entry_count].last_access = cache->access_counter++;
    cache->entries[cache->entry_count].locked = 0;
    cache->entry_count++;
    cache->current_size += tex_size;

    return 0;
}

int fr_cache_remove(fr_texture_cache_t *cache, uint32_t key)
{
    if (!cache) return -1;

    for (int i = 0; i < cache->entry_count; i++) {
        if (cache->entries[i].key == key) {
            cache->current_size -= cache->entries[i].size;
            if (cache->entries[i].texture) {
                fr_texture_destroy(cache->entries[i].texture);
            }

            if (i < cache->entry_count - 1) {
                memmove(&cache->entries[i], &cache->entries[i + 1],
                        (cache->entry_count - i - 1) *
                        sizeof(fr_cache_entry_t));
            }
            cache->entry_count--;
            return 0;
        }
    }

    return -1;
}

void fr_cache_clear(fr_texture_cache_t *cache)
{
    if (!cache) return;

    for (int i = 0; i < cache->entry_count; i++) {
        if (cache->entries[i].texture) {
            fr_texture_destroy(cache->entries[i].texture);
        }
    }
    cache->entry_count = 0;
    cache->current_size = 0;
    cache->hits = 0;
    cache->misses = 0;
}

void fr_cache_evict(fr_texture_cache_t *cache, uint32_t target_size)
{
    if (!cache) return;

    while (cache->current_size > target_size && cache->entry_count > 0) {
        int lru_idx = 0;
        uint32_t oldest = 0xFFFFFFFF;
        for (int i = 0; i < cache->entry_count; i++) {
            if (!cache->entries[i].locked &&
                cache->entries[i].last_access < oldest) {
                oldest = cache->entries[i].last_access;
                lru_idx = i;
            }
        }

        if (oldest == 0xFFFFFFFF) break;

        cache->current_size -= cache->entries[lru_idx].size;
        if (cache->entries[lru_idx].texture) {
            fr_texture_destroy(cache->entries[lru_idx].texture);
        }

        if (lru_idx < cache->entry_count - 1) {
            memmove(&cache->entries[lru_idx], &cache->entries[lru_idx + 1],
                    (cache->entry_count - lru_idx - 1) *
                    sizeof(fr_cache_entry_t));
        }
        cache->entry_count--;
    }
}

void fr_cache_get_stats(const fr_texture_cache_t *cache,
                         uint32_t *hits, uint32_t *misses,
                         uint32_t *size, uint32_t *max_size)
{
    if (!cache) return;

    if (hits) *hits = cache->hits;
    if (misses) *misses = cache->misses;
    if (size) *size = cache->current_size;
    if (max_size) *max_size = cache->max_size;
}

void fr_cache_lock(fr_texture_cache_t *cache, uint32_t key)
{
    if (!cache) return;

    for (int i = 0; i < cache->entry_count; i++) {
        if (cache->entries[i].key == key) {
            cache->entries[i].locked = 1;
            return;
        }
    }
}

void fr_cache_unlock(fr_texture_cache_t *cache, uint32_t key)
{
    if (!cache) return;

    for (int i = 0; i < cache->entry_count; i++) {
        if (cache->entries[i].key == key) {
            cache->entries[i].locked = 0;
            return;
        }
    }
}

/* ===================================================================
 *  纹理实用函数
 * =================================================================== */

uint32_t fr_texture_hash(const fr_texture_t *tex)
{
    if (!tex || !tex->data) return 0;

    /* 简单的 FNV-1a 哈希 */
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < tex->data_size; i++) {
        hash ^= tex->data[i];
        hash *= 16777619u;
    }
    return hash;
}

int fr_texture_compare(const fr_texture_t *a, const fr_texture_t *b)
{
    if (!a && !b) return 1;
    if (!a || !b) return 0;

    if (a->width != b->width || a->height != b->height ||
        a->format != b->format || a->data_size != b->data_size) {
        return 0;
    }

    if (a->data && b->data) {
        return memcmp(a->data, b->data, a->data_size) == 0;
    }

    return a->data == b->data;
}

fr_texture_t *fr_texture_duplicate(const fr_texture_t *tex)
{
    if (!tex) return NULL;
    return fr_texture_create_from_data(tex->width, tex->height,
                                        tex->format, tex->data);
}

void fr_texture_premultiply_alpha(fr_texture_t *tex)
{
    if (!tex || !tex->data) return;
    if (fr_internal_is_compressed(tex->format)) return;

    for (uint32_t y = 0; y < tex->height; y++) {
        for (uint32_t x = 0; x < tex->width; x++) {
            fr_color_t c = fr_texture_get_pixel(tex, x, y);
            float a = c.a / 255.0f;
            c.r = (uint8_t)(c.r * a);
            c.g = (uint8_t)(c.g * a);
            c.b = (uint8_t)(c.b * a);
            fr_texture_set_pixel(tex, x, y, c);
        }
    }
}

void fr_texture_unpremultiply_alpha(fr_texture_t *tex)
{
    if (!tex || !tex->data) return;
    if (fr_internal_is_compressed(tex->format)) return;

    for (uint32_t y = 0; y < tex->height; y++) {
        for (uint32_t x = 0; x < tex->width; x++) {
            fr_color_t c = fr_texture_get_pixel(tex, x, y);
            if (c.a > 0) {
                float a = c.a / 255.0f;
                c.r = (uint8_t)fr_internal_clamp(c.r / a, 0, 255);
                c.g = (uint8_t)fr_internal_clamp(c.g / a, 0, 255);
                c.b = (uint8_t)fr_internal_clamp(c.b / a, 0, 255);
            }
            fr_texture_set_pixel(tex, x, y, c);
        }
    }
}

/* ===================================================================
 *  纹理区域
 * =================================================================== */

fr_tex_region_t *fr_tex_region_create(fr_texture_t *tex,
                                       uint32_t x, uint32_t y,
                                       uint32_t w, uint32_t h)
{
    if (!tex) return NULL;
    if (x + w > tex->width || y + h > tex->height) return NULL;

    fr_tex_region_t *region = (fr_tex_region_t *)malloc(sizeof(fr_tex_region_t));
    if (!region) return NULL;

    region->texture = tex;
    region->x = x;
    region->y = y;
    region->w = w;
    region->h = h;

    return region;
}

void fr_tex_region_destroy(fr_tex_region_t *region)
{
    if (region) {
        free(region);
    }
}