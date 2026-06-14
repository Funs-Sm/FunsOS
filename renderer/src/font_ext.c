/* font_ext.c - 扩展字体系统实现
 * 实现多字号缩放、粗体/斜体渲染、文本装饰、
 * 对齐、换行、测量和富文本支持
 */

#include "funrender.h"
#include "fr_context.h"
#include "fr_font_ext.h"
#include "string.h"

/* ---- 内部辅助函数 ---- */

/* 绝对值 */
static int iabs_val(int x)
{
    return x < 0 ? -x : x;
}

/* 最大值 */
static int imax_val(int a, int b)
{
    return a > b ? a : b;
}

/* 最小值 */
static int imin_val(int a, int b)
{
    return a < b ? a : b;
}

/* 设置帧缓冲中单个像素 */
static void put_pixel(fr_context_t *ctx, int x, int y, fr_color_t color)
{
    if (ctx == NULL || ctx->framebuffer == NULL) return;
    if (x < 0 || x >= ctx->width || y < 0 || y >= ctx->height) return;

    uint32_t pixel = ((uint32_t)color.r << 16) |
                     ((uint32_t)color.g << 8) |
                     (uint32_t)color.b;

    if (color.a == 255) {
        ctx->framebuffer[y * ctx->width + x] = pixel;
    } else if (color.a > 0) {
        uint32_t bg = ctx->framebuffer[y * ctx->width + x];
        uint8_t bg_r = (bg >> 16) & 0xFF;
        uint8_t bg_g = (bg >> 8) & 0xFF;
        uint8_t bg_b = bg & 0xFF;
        uint8_t inv = 255 - color.a;
        uint8_t r = (uint8_t)(((uint16_t)color.r * color.a +
                                (uint16_t)bg_r * inv) / 255);
        uint8_t g = (uint8_t)(((uint16_t)color.g * color.a +
                                (uint16_t)bg_g * inv) / 255);
        uint8_t b = (uint8_t)(((uint16_t)color.b * color.a +
                                (uint16_t)bg_b * inv) / 255);
        ctx->framebuffer[y * ctx->width + x] =
            ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    }
}

/* 检查 UTF-8 字符的字节长度 */
static int utf8_char_len(uint8_t first_byte)
{
    if (first_byte < 0x80) return 1;
    if (first_byte < 0xC0) return 1; /* 续字节，不应出现在开头 */
    if (first_byte < 0xE0) return 2;
    if (first_byte < 0xF0) return 3;
    return 4;
}

/* 从 UTF-8 字节序列解码为 Unicode 码点 */
static uint32_t utf8_decode(const char *s, int *out_len)
{
    uint8_t c = (uint8_t)s[0];
    *out_len = 1;

    if (c < 0x80) return c;
    if (c < 0xC0) return c;

    if (c < 0xE0 && s[1]) {
        *out_len = 2;
        return ((uint32_t)(c & 0x1F) << 6) | (uint32_t)(s[1] & 0x3F);
    }
    if (c < 0xF0 && s[1] && s[2]) {
        *out_len = 3;
        return ((uint32_t)(c & 0x0F) << 12) |
               ((uint32_t)(s[1] & 0x3F) << 6) |
               (uint32_t)(s[2] & 0x3F);
    }
    if (s[1] && s[2] && s[3]) {
        *out_len = 4;
        return ((uint32_t)(c & 0x07) << 18) |
               ((uint32_t)(s[1] & 0x3F) << 12) |
               ((uint32_t)(s[2] & 0x3F) << 6) |
               (uint32_t)(s[3] & 0x3F);
    }

    return c;
}

/* ================================================================
 *  字体面管理
 * ================================================================ */

/*
 * fr_font_face_create - 创建字体面
 */
fr_font_face_t *fr_font_face_create(const char *family, int base_size,
                                    const fr_font_metrics_t *metrics)
{
    fr_font_face_t *face = (fr_font_face_t *)fr_alloc(
        (uint32_t)sizeof(fr_font_face_t));
    if (face == NULL) return NULL;

    memset(face, 0, sizeof(fr_font_face_t));

    if (family != NULL) {
        int i;
        for (i = 0; i < 63 && family[i] != '\0'; i++) {
            face->family[i] = family[i];
        }
        face->family[i] = '\0';
    }

    face->base_size = base_size;

    if (metrics != NULL) {
        face->metrics = *metrics;
    } else {
        /* 默认度量: 适合 8x16 等宽位图字体 */
        face->metrics.ascent = 12;
        face->metrics.descent = 4;
        face->metrics.line_height = 16;
        face->metrics.x_height = 8;
        face->metrics.cap_height = 10;
        face->metrics.avg_char_width = 8;
        face->metrics.max_char_width = 8;
        face->metrics.underscore_pos = 2;
        face->metrics.underscore_thick = 1;
        face->metrics.strikethrough_pos = 6;
    }

    face->glyphs = NULL;
    face->glyph_count = 0;

    return face;
}

/*
 * fr_font_face_destroy - 销毁字体面
 */
void fr_font_face_destroy(fr_font_face_t *face)
{
    if (face == NULL) return;
    if (face->glyphs != NULL) {
        fr_free(face->glyphs);
    }
    fr_free(face);
}

/*
 * fr_font_face_set_glyphs - 设置字形表
 */
void fr_font_face_set_glyphs(fr_font_face_t *face,
                             fr_glyph_t *glyphs, uint32_t count)
{
    if (face == NULL) return;
    face->glyphs = glyphs;
    face->glyph_count = count;

    /* 清空缓存 */
    memset(face->cache, 0, sizeof(face->cache));
}

/*
 * fr_font_face_find_glyph - 查找指定码点的字形
 */
fr_glyph_t *fr_font_face_find_glyph(fr_font_face_t *face, uint32_t codepoint)
{
    if (face == NULL) return NULL;

    /* 先查缓存 */
    uint32_t cache_idx = codepoint % FR_FONT_CACHE_SIZE;
    if (face->cache[cache_idx] != NULL &&
        face->cache[cache_idx]->codepoint == codepoint) {
        return face->cache[cache_idx];
    }

    /* 线性搜索字形表 */
    for (uint32_t i = 0; i < face->glyph_count; i++) {
        if (face->glyphs[i].codepoint == codepoint) {
            face->cache[cache_idx] = &face->glyphs[i];
            return &face->glyphs[i];
        }
    }

    return NULL;
}

/* ================================================================
 *  字形缩放
 * ================================================================ */

/*
 * fr_glyph_scale - 将字形位图缩放到目标尺寸
 *
 * 使用最近邻插值快速缩放 1bpp 位图。
 * 如果目标尺寸与源尺寸相同, 直接拷贝。
 */
void fr_glyph_scale(const fr_glyph_t *src, int src_size,
                    int dst_size, fr_glyph_t *dst)
{
    if (src == NULL || dst == NULL) return;

    if (src_size == dst_size || src_size <= 0 || dst_size <= 0) {
        /* 同尺寸直接复制 */
        *dst = *src;
        return;
    }

    /* 缩放比例 */
    float scale = (float)dst_size / (float)src_size;

    dst->codepoint = src->codepoint;
    dst->width = (int)((float)src->width * scale);
    dst->height = (int)((float)src->height * scale);
    dst->bearing_x = (int)((float)src->bearing_x * scale);
    dst->bearing_y = (int)((float)src->bearing_y * scale);
    dst->advance = (int)((float)src->advance * scale);

    if (dst->width > FR_GLYPH_MAX_WIDTH) dst->width = FR_GLYPH_MAX_WIDTH;
    if (dst->height > FR_GLYPH_MAX_HEIGHT) dst->height = FR_GLYPH_MAX_HEIGHT;
    if (dst->width <= 0) dst->width = 1;
    if (dst->height <= 0) dst->height = 1;

    /* 清空目标位图 */
    memset(dst->bitmap, 0, sizeof(dst->bitmap));

    /* 最近邻缩放 */
    for (int dy = 0; dy < dst->height; dy++) {
        int sy = (int)((float)dy / scale);
        if (sy >= src->height) sy = src->height - 1;

        for (int dx = 0; dx < dst->width; dx++) {
            int sx = (int)((float)dx / scale);
            if (sx >= src->width) sx = src->width - 1;

            /* 读取源位图对应位 */
            int src_byte = (sy * src->width + sx) / 8;
            int src_bit = (sy * src->width + sx) % 8;
            uint8_t src_val = (src->bitmap[src_byte] >> (7 - src_bit)) & 1;

            if (src_val) {
                /* 写入目标位图对应位 */
                int dst_byte = (dy * dst->width + dx) / 8;
                int dst_bit = (dy * dst->width + dx) % 8;
                dst->bitmap[dst_byte] |= (uint8_t)(1 << (7 - dst_bit));
            }
        }
    }
}

/* ================================================================
 *  字体度量
 * ================================================================ */

/*
 * fr_font_get_metrics - 获取字体度量
 */
const fr_font_metrics_t *fr_font_get_metrics(const fr_font_face_t *face)
{
    if (face == NULL) return NULL;
    return &face->metrics;
}

/*
 * fr_font_get_scaled_metrics - 获取缩放后的字体度量
 *
 * 根据 font_size 与 base_size 的比例缩放度量值。
 */
void fr_font_get_scaled_metrics(const fr_font_face_t *face, int font_size,
                                fr_font_metrics_t *out_metrics)
{
    if (face == NULL || out_metrics == NULL) return;

    if (font_size <= 0) font_size = face->base_size;

    float scale = (float)font_size / (float)face->base_size;

    out_metrics->ascent = (int)((float)face->metrics.ascent * scale);
    out_metrics->descent = (int)((float)face->metrics.descent * scale);
    out_metrics->line_height = (int)((float)face->metrics.line_height * scale);
    out_metrics->x_height = (int)((float)face->metrics.x_height * scale);
    out_metrics->cap_height = (int)((float)face->metrics.cap_height * scale);
    out_metrics->avg_char_width = (int)((float)face->metrics.avg_char_width * scale);
    out_metrics->max_char_width = (int)((float)face->metrics.max_char_width * scale);
    out_metrics->underscore_pos = imax_val(1,
        (int)((float)face->metrics.underscore_pos * scale));
    out_metrics->underscore_thick = imax_val(1,
        (int)((float)face->metrics.underscore_thick * scale));
    out_metrics->strikethrough_pos = (int)((float)face->metrics.strikethrough_pos * scale);

    if (out_metrics->line_height <= 0) out_metrics->line_height = font_size;
}

/* ================================================================
 *  文本绘制核心
 * ================================================================ */

/*
 * 绘制单个字形的位图到帧缓冲
 */
static void draw_glyph_bitmap(fr_context_t *ctx, int x, int y,
                              const fr_glyph_t *glyph, fr_color_t color,
                              int scale_w, int scale_h)
{
    if (ctx == NULL || glyph == NULL) return;

    int gw = glyph->width;
    int gh = glyph->height;

    if (gw <= 0 || gh <= 0) return;

    /* 计算字体颜色像素值 */
    uint32_t font_color = ((uint32_t)color.r << 16) |
                           ((uint32_t)color.g << 8) |
                           (uint32_t)color.b;

    for (int dy = 0; dy < gh; dy++) {
        for (int dx = 0; dx < gw; dx++) {
            int bit_pos = dy * gw + dx;
            int byte_idx = bit_pos / 8;
            int bit_off = bit_pos % 8;
            uint8_t val = (glyph->bitmap[byte_idx] >> (7 - bit_off)) & 1;

            if (val == 0) continue; /* 背景透明 */

            int px = x + dx;
            int py = y + dy;

            if (px < 0 || px >= ctx->width ||
                py < 0 || py >= ctx->height) continue;

            if (color.a == 255) {
                ctx->framebuffer[py * ctx->width + px] = font_color;
            } else {
                uint32_t bg = ctx->framebuffer[py * ctx->width + px];
                uint8_t bg_r = (bg >> 16) & 0xFF;
                uint8_t bg_g = (bg >> 8) & 0xFF;
                uint8_t bg_b = bg & 0xFF;
                uint8_t inv = 255 - color.a;
                uint8_t r = (uint8_t)(((uint16_t)color.r * color.a +
                                        (uint16_t)bg_r * inv) / 255);
                uint8_t g = (uint8_t)(((uint16_t)color.g * color.a +
                                        (uint16_t)bg_g * inv) / 255);
                uint8_t b = (uint8_t)(((uint16_t)color.b * color.a +
                                        (uint16_t)bg_b * inv) / 255);
                ctx->framebuffer[py * ctx->width + px] =
                    ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            }
        }
    }
}

/*
 * fr_font_draw_text - 绘制文本
 *
 * 逐字符查找字形并绘制位图, 支持字形间距。
 */
void fr_font_draw_text(fr_context_t *ctx,
                       int x, int y, const char *text, int len,
                       const fr_font_face_t *face,
                       const fr_text_style_t *style)
{
    if (ctx == NULL || text == NULL || len <= 0 || face == NULL) return;
    if (ctx->framebuffer == NULL) return;

    int cx = x;
    int font_size = (style != NULL && style->font_size > 0) ?
                    style->font_size : face->base_size;
    fr_color_t txt_color = (style != NULL) ? style->color : FR_COLOR_BLACK;

    /* 如果请求粗体, 先画稍偏移位置再画原位置 */
    int do_bold = (style != NULL) && style->bold;
    int do_italic = (style != NULL) && style->italic;

    int i = 0;
    while (i < len) {
        int char_len;
        uint32_t cp = utf8_decode(&text[i], &char_len);
        i += char_len;

        if (cp == '\n') continue; /* 换行符不绘制 */

        fr_glyph_t *glyph = fr_font_face_find_glyph(face, cp);
        if (glyph == NULL) {
            /* 使用默认字形 (空格) 或跳过 */
            cx += font_size * 8 / face->base_size;
            continue;
        }

        /* 缩放字形 */
        fr_glyph_t scaled_glyph;
        fr_glyph_scale(glyph, face->base_size, font_size, &scaled_glyph);

        /* 绘制字形 */
        int sx = cx;
        int sy = y;

        if (do_italic) {
            /* 斜体: 应用剪切变换, 每行的 X 偏移随 Y 递增 */
            sy = y - scaled_glyph.bearing_y;
            /* 对于简单的斜体效果, 在绘制字形时水平错切 */
            /* 这里使用简化方式: 绘制原始字形并额外绘制倾斜偏移版本 */
            for (int dy = 0; dy < scaled_glyph.height; dy++) {
                for (int dx = 0; dx < scaled_glyph.width; dx++) {
                    int bit_pos = dy * scaled_glyph.width + dx;
                    int byte_idx = bit_pos / 8;
                    int bit_off = bit_pos % 8;
                    uint8_t val = (scaled_glyph.bitmap[byte_idx] >>
                                   (7 - bit_off)) & 1;

                    if (val == 0) continue;

                    /* 斜体偏移: 每行偏移 dy/4 像素 */
                    int px = sx + dx + dy / 4;
                    int py = sy + dy;

                    if (px >= 0 && px < ctx->width &&
                        py >= 0 && py < ctx->height) {
                        put_pixel(ctx, px, py, txt_color);
                    }
                }
            }

            if (do_bold) {
                /* 粗斜体: 额外加粗偏移 */
                for (int dy = 0; dy < scaled_glyph.height; dy++) {
                    for (int dx = 0; dx < scaled_glyph.width; dx++) {
                        int bit_pos = dy * scaled_glyph.width + dx;
                        int byte_idx = bit_pos / 8;
                        int bit_off = bit_pos % 8;
                        uint8_t val = (scaled_glyph.bitmap[byte_idx] >>
                                       (7 - bit_off)) & 1;

                        if (val == 0) continue;
                        int px = sx + dx + dy / 4 + 1;
                        int py = sy + dy;
                        if (px >= 0 && px < ctx->width &&
                            py >= 0 && py < ctx->height) {
                            put_pixel(ctx, px, py, txt_color);
                        }
                    }
                }
            }
        } else {
            /* 正常/粗体绘制 */
            sy = y;
            draw_glyph_bitmap(ctx, sx, sy, &scaled_glyph, txt_color,
                              font_size, font_size);

            if (do_bold) {
                /* 粗体: 向右偏移 1 像素再绘制一次 */
                draw_glyph_bitmap(ctx, sx + 1, sy, &scaled_glyph, txt_color,
                                  font_size, font_size);
            }
        }

        cx += scaled_glyph.advance;
    }

    /* 绘制文本装饰 */
    if (style != NULL && style->decor_flags != 0) {
        int text_w = cx - x;
        fr_font_metrics_t scaled_m;
        fr_font_get_scaled_metrics(face, font_size, &scaled_m);
        fr_font_draw_decorations(ctx, x, y, text_w, scaled_m.line_height,
                                  style->decor_flags, txt_color, &scaled_m);
    }
}

/*
 * fr_font_draw_text_bold - 绘制粗体文本
 */
void fr_font_draw_text_bold(fr_context_t *ctx,
                            int x, int y, const char *text, int len,
                            const fr_font_face_t *face,
                            const fr_text_style_t *style)
{
    fr_text_style_t bold_style = *style;
    bold_style.bold = 1;
    fr_font_draw_text(ctx, x, y, text, len, face, &bold_style);
}

/*
 * fr_font_draw_text_italic - 绘制斜体文本
 */
void fr_font_draw_text_italic(fr_context_t *ctx,
                              int x, int y, const char *text, int len,
                              const fr_font_face_t *face,
                              const fr_text_style_t *style)
{
    fr_text_style_t italic_style = *style;
    italic_style.italic = 1;
    fr_font_draw_text(ctx, x, y, text, len, face, &italic_style);
}

/*
 * fr_font_draw_decorations - 绘制文本装饰线
 */
void fr_font_draw_decorations(fr_context_t *ctx,
                              int x, int y, int width, int height,
                              uint8_t decor_flags, fr_color_t color,
                              const fr_font_metrics_t *metrics)
{
    if (ctx == NULL || ctx->framebuffer == NULL) return;
    if (width <= 0) return;

    int baseline = y + (metrics ? metrics->ascent : 12);
    int line_h = (metrics ? metrics->line_height : 16);

    /* 下划线 */
    if (decor_flags & FR_TEXT_DECOR_UNDERLINE) {
        int uy = baseline + (metrics ? metrics->underscore_pos : 2);
        int thick = metrics ? metrics->underscore_thick : 1;
        if (thick <= 0) thick = 1;

        for (int t = 0; t < thick; t++) {
            for (int dx = 0; dx < width; dx++) {
                put_pixel(ctx, x + dx, uy + t, color);
            }
        }
    }

    /* 删除线 */
    if (decor_flags & FR_TEXT_DECOR_STRIKETHROUGH) {
        int sy = baseline - (metrics ? metrics->strikethrough_pos : 6);
        int thick = 1;

        for (int t = 0; t < thick; t++) {
            for (int dx = 0; dx < width; dx++) {
                put_pixel(ctx, x + dx, sy + t, color);
            }
        }
    }

    /* 上划线 */
    if (decor_flags & FR_TEXT_DECOR_OVERLINE) {
        int oy = y - 2;
        for (int dx = 0; dx < width; dx++) {
            put_pixel(ctx, x + dx, oy, color);
        }
    }
}

/* ================================================================
 *  文本对齐
 * ================================================================ */

/*
 * fr_font_align_x - 计算对齐后的 X 坐标
 */
int fr_font_align_x(int container_x, int container_w,
                    int text_width, int alignment)
{
    switch (alignment) {
    case FR_TEXT_ALIGN_CENTER:
        return container_x + (container_w - text_width) / 2;
    case FR_TEXT_ALIGN_RIGHT:
        return container_x + container_w - text_width;
    case FR_TEXT_ALIGN_LEFT:
    default:
        return container_x;
    }
}

/*
 * fr_font_draw_text_aligned - 绘制带对齐的文本
 */
void fr_font_draw_text_aligned(fr_context_t *ctx,
                               int x, int y, int w, int h,
                               const char *text, int len,
                               const fr_font_face_t *face,
                               const fr_text_style_t *style,
                               int halign)
{
    if (ctx == NULL || text == NULL || len <= 0 || face == NULL) return;

    int font_size = (style != NULL && style->font_size > 0) ?
                    style->font_size : face->base_size;
    int text_w = fr_font_measure_text(face, text, len, font_size);
    int ax = fr_font_align_x(x, w, text_w, halign);

    fr_font_draw_text(ctx, ax, y, text, len, face, style);
}

/* ================================================================
 *  文本换行
 * ================================================================ */

/*
 * fr_font_layout_text - 计算文本换行布局
 *
 * 按 word-wrap 规则将文本分成多行, 返回每行的起始/结束位置和宽度。
 */
void fr_font_layout_text(const char *text, int len,
                         const fr_font_face_t *face,
                         int font_size, int max_width,
                         fr_text_layout_t *layout)
{
    if (text == NULL || face == NULL || layout == NULL) return;
    if (max_width <= 0) max_width = 1;

    memset(layout, 0, sizeof(fr_text_layout_t));

    int line_start = 0;
    int last_break = -1; /* 最后一个可断行位置 (空格) */
    int line_width = 0;
    int char_width;
    int i = 0;

    while (i < len && layout->line_count < FR_MAX_TEXT_LINES) {
        int char_len;
        uint32_t cp = utf8_decode(&text[i], &char_len);

        if (cp == '\n') {
            /* 强制换行 */
            layout->lines[layout->line_count].start = line_start;
            layout->lines[layout->line_count].end = i;
            layout->lines[layout->line_count].width_px = line_width;
            layout->line_count++;

            i += char_len;
            line_start = i;
            line_width = 0;
            last_break = -1;
            continue;
        }

        if (cp == ' ' || cp == '\t') {
            last_break = i;
        }

        /* 测量该字符宽度 */
        fr_glyph_t *glyph = fr_font_face_find_glyph(face, cp);
        if (glyph != NULL) {
            char_width = glyph->advance * font_size / face->base_size;
        } else {
            char_width = font_size * face->metrics.avg_char_width /
                         face->base_size;
        }
        if (char_width <= 0) char_width = font_size;

        /* 检查是否需要换行 */
        if (line_width + char_width > max_width && line_width > 0) {
            /* 需要换行 */
            int break_at;

            if (last_break >= line_start) {
                /* 在最后一个空格处断开 */
                break_at = last_break;

                /* 重新计算到该位置的宽度 */
                int recheck_w = 0;
                for (int j = line_start; j <= break_at; j++) {
                    int cl;
                    uint32_t cp2 = utf8_decode(&text[j], &cl);
                    fr_glyph_t *g = fr_font_face_find_glyph(face, cp2);
                    if (j < break_at) {
                        if (g != NULL) {
                            recheck_w += g->advance * font_size / face->base_size;
                        } else {
                            recheck_w += font_size * face->metrics.avg_char_width /
                                         face->base_size;
                        }
                    }
                    j += cl - 1;
                }

                layout->lines[layout->line_count].start = line_start;
                layout->lines[layout->line_count].end = break_at;
                layout->lines[layout->line_count].width_px = recheck_w;
                layout->line_count++;

                /* 下一行: 跳过空格 */
                i = break_at + 1;
                /* 跳过行首空格 */
                while (i < len && (text[i] == ' ' || text[i] == '\t')) i++;
                line_start = i;
                line_width = 0;
            } else {
                /* 没有空格可断, 在当前字符前强制断开 */
                layout->lines[layout->line_count].start = line_start;
                layout->lines[layout->line_count].end = i;
                layout->lines[layout->line_count].width_px = line_width;
                layout->line_count++;

                line_start = i;
                line_width = char_width;
                i += char_len;
            }
            last_break = -1;
            continue;
        }

        line_width += char_width;
        i += char_len;
    }

    /* 最后一行 */
    if (line_start < len && layout->line_count < FR_MAX_TEXT_LINES) {
        layout->lines[layout->line_count].start = line_start;
        layout->lines[layout->line_count].end = len;
        layout->lines[layout->line_count].width_px = line_width;
        layout->line_count++;
    }

    /* 计算总高度和最大宽度 */
    int total_h = 0;
    int max_w = 0;
    fr_font_metrics_t scaled_m;
    fr_font_get_scaled_metrics(face, font_size, &scaled_m);

    for (uint32_t li = 0; li < layout->line_count; li++) {
        total_h += scaled_m.line_height;
        if (layout->lines[li].width_px > max_w) {
            max_w = layout->lines[li].width_px;
        }
    }

    layout->total_height = total_h;
    layout->max_width = max_w;
}

/*
 * fr_font_draw_text_wrapped - 绘制自动换行的文本
 */
void fr_font_draw_text_wrapped(fr_context_t *ctx,
                               int x, int y, int max_width,
                               const char *text, int len,
                               const fr_font_face_t *face,
                               const fr_text_style_t *style,
                               int halign)
{
    if (ctx == NULL || text == NULL || len <= 0 || face == NULL) return;
    if (max_width <= 0) return;

    fr_text_layout_t layout;
    int font_size = (style != NULL && style->font_size > 0) ?
                    style->font_size : face->base_size;
    fr_font_layout_text(text, len, face, font_size, max_width, &layout);

    fr_font_metrics_t scaled_m;
    fr_font_get_scaled_metrics(face, font_size, &scaled_m);
    int cy = y;

    for (uint32_t li = 0; li < layout.line_count; li++) {
        int line_len = layout.lines[li].end - layout.lines[li].start;
        int ax = fr_font_align_x(x, max_width,
                                  layout.lines[li].width_px, halign);

        fr_font_draw_text(ctx, ax, cy,
                         text + layout.lines[li].start, line_len,
                         face, style);
        cy += scaled_m.line_height;
    }
}

/* ================================================================
 *  文本测量
 * ================================================================ */

/*
 * fr_font_measure_text - 测量文本像素宽度
 */
int fr_font_measure_text(const fr_font_face_t *face,
                         const char *text, int len, int font_size)
{
    if (face == NULL || text == NULL || len <= 0) return 0;
    if (font_size <= 0) font_size = face->base_size;

    int total_w = 0;
    int i = 0;

    while (i < len) {
        int char_len;
        uint32_t cp = utf8_decode(&text[i], &char_len);
        i += char_len;

        if (cp == '\n') break;

        fr_glyph_t *glyph = fr_font_face_find_glyph(face, cp);
        if (glyph != NULL) {
            total_w += glyph->advance * font_size / face->base_size;
        } else {
            /* 未知字符使用平均宽度 */
            total_w += face->metrics.avg_char_width * font_size / face->base_size;
        }
    }

    return total_w;
}

/*
 * fr_font_measure_height - 测量文本像素高度 (考虑自动换行)
 */
int fr_font_measure_height(const fr_font_face_t *face,
                           const char *text, int len,
                           int font_size, int max_width)
{
    if (face == NULL || text == NULL || len <= 0) return 0;
    if (font_size <= 0) font_size = face->base_size;

    fr_font_metrics_t scaled_m;
    fr_font_get_scaled_metrics(face, font_size, &scaled_m);

    if (max_width <= 0) {
        /* 不计换行: 统计 \n 数量 */
        int lines = 1;
        for (int i = 0; i < len; i++) {
            if (text[i] == '\n') lines++;
        }
        return lines * scaled_m.line_height;
    }

    /* 使用 layout 计算 */
    fr_text_layout_t layout;
    fr_font_layout_text(text, len, face, font_size, max_width, &layout);
    return layout.total_height > 0 ? layout.total_height : scaled_m.line_height;
}

/*
 * fr_font_measure_char - 测量单个字符的宽度
 */
int fr_font_measure_char(const fr_font_face_t *face, char c, int font_size)
{
    if (face == NULL) return 0;
    if (font_size <= 0) font_size = face->base_size;

    uint32_t cp = (uint32_t)(unsigned char)c;
    fr_glyph_t *glyph = fr_font_face_find_glyph(face, cp);
    if (glyph != NULL) {
        return glyph->advance * font_size / face->base_size;
    }

    return face->metrics.avg_char_width * font_size / face->base_size;
}

/* ================================================================
 *  富文本
 * ================================================================ */

/*
 * fr_rich_text_init - 创建富文本块
 */
void fr_rich_text_init(fr_rich_text_t *rt, const char *text, int len)
{
    if (rt == NULL) return;
    memset(rt, 0, sizeof(fr_rich_text_t));
    rt->text = text;
    rt->text_len = len;
}

/*
 * fr_rich_text_add_run - 向富文本块添加样式运行
 *
 * 返回 0=成功, -1=运行数已满。
 */
int fr_rich_text_add_run(fr_rich_text_t *rt, int start, int end,
                         const fr_text_style_t *style)
{
    if (rt == NULL || style == NULL) return -1;
    if (rt->run_count >= FR_RICH_TEXT_MAX_RUNS) return -1;

    int idx = (int)rt->run_count;
    rt->runs[idx].start = start;
    rt->runs[idx].end = end;
    rt->runs[idx].style = *style;
    rt->run_count++;

    return 0;
}

/*
 * 为指定字符位置查找富文本样式
 */
static const fr_text_style_t *find_rich_text_style(const fr_rich_text_t *rt,
                                                    int pos)
{
    if (rt == NULL) return NULL;

    for (uint32_t i = 0; i < rt->run_count; i++) {
        if (pos >= rt->runs[i].start && pos < rt->runs[i].end) {
            return &rt->runs[i].style;
        }
    }
    return NULL;
}

/*
 * fr_rich_text_draw - 绘制富文本
 *
 * 根据不同的样式运行分段绘制文本。
 */
void fr_rich_text_draw(fr_context_t *ctx,
                       int x, int y, int max_width,
                       const fr_rich_text_t *rt,
                       const fr_font_face_t *face,
                       int halign)
{
    if (ctx == NULL || rt == NULL || face == NULL) return;
    if (rt->text == NULL || rt->text_len <= 0) return;

    /* 先进行换行布局 */
    int font_size = face->base_size;
    /* 使用默认样式作为基准 */
    fr_text_style_t default_style;
    memset(&default_style, 0, sizeof(default_style));
    default_style.font_size = font_size;
    default_style.color = FR_COLOR_BLACK;

    /* 简化实现: 逐行绘制, 每行内按样式运行分段 */
    fr_text_layout_t layout;
    if (max_width > 0) {
        fr_font_layout_text(rt->text, rt->text_len, face,
                           font_size, max_width, &layout);
    } else {
        /* 无宽度限制: 单行 */
        layout.line_count = 1;
        layout.lines[0].start = 0;
        layout.lines[0].end = rt->text_len;

        int total_w = fr_font_measure_text(face, rt->text, rt->text_len,
                                           font_size);
        layout.lines[0].width_px = total_w;
        fr_font_metrics_t scaled_m;
        fr_font_get_scaled_metrics(face, font_size, &scaled_m);
        layout.total_height = scaled_m.line_height;
        layout.max_width = total_w;
    }

    fr_font_metrics_t scaled_m;
    fr_font_get_scaled_metrics(face, font_size, &scaled_m);
    int cy = y;
    int display_w = max_width > 0 ? max_width : layout.max_width;

    for (uint32_t li = 0; li < layout.line_count; li++) {
        int line_start = layout.lines[li].start;
        int line_end = layout.lines[li].end;

        /* 绘制该行中的每个样式运行 (只绘制本行范围) */
        for (uint32_t ri = 0; ri < rt->run_count; ri++) {
            int run_start = rt->runs[ri].start;
            int run_end = rt->runs[ri].end;

            /* 与本行求交集 */
            int seg_start = imax_val(run_start, line_start);
            int seg_end = imin_val(run_end, line_end);

            if (seg_start >= seg_end) continue;

            /* 计算到该段之前的总宽度 (用于对齐和定位) */
            int pre_width = fr_font_measure_text(face,
                rt->text + line_start, seg_start - line_start, font_size);

            int seg_width = fr_font_measure_text(face,
                rt->text + seg_start, seg_end - seg_start, font_size);

            int ax = fr_font_align_x(x, display_w, layout.lines[li].width_px,
                                     halign);

            fr_font_draw_text(ctx, ax + pre_width, cy,
                             rt->text + seg_start, seg_end - seg_start,
                             face, &rt->runs[ri].style);
        }

        cy += scaled_m.line_height;
    }
}

/*
 * fr_rich_text_measure - 测量富文本的宽度和高度
 */
void fr_rich_text_measure(const fr_rich_text_t *rt,
                          const fr_font_face_t *face,
                          int max_width,
                          int *out_width, int *out_height)
{
    if (rt == NULL || face == NULL) {
        if (out_width) *out_width = 0;
        if (out_height) *out_height = 0;
        return;
    }

    int font_size = face->base_size;

    if (max_width <= 0) {
        /* 单行测量 */
        int w = fr_font_measure_text(face, rt->text, rt->text_len, font_size);
        fr_font_metrics_t scaled_m;
        fr_font_get_scaled_metrics(face, font_size, &scaled_m);
        int h = scaled_m.line_height;

        if (out_width) *out_width = w;
        if (out_height) *out_height = h;
    } else {
        /* 多行测量 */
        fr_text_layout_t layout;
        fr_font_layout_text(rt->text, rt->text_len, face,
                           font_size, max_width, &layout);
        if (out_width) *out_width = layout.max_width;
        if (out_height) *out_height = layout.total_height;
    }
}