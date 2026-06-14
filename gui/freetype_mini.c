#include "freetype_mini.h"
#include "kheap.h"
#include "string.h"

static uint16_t ttf_read_uint16(const uint8_t *data, uint32_t off) {
    return (uint16_t)((data[off] << 8) | data[off + 1]);
}

static int16_t ttf_read_int16(const uint8_t *data, uint32_t off) {
    return (int16_t)((data[off] << 8) | data[off + 1]);
}

static uint32_t ttf_read_uint32(const uint8_t *data, uint32_t off) {
    return (uint32_t)((data[off] << 24) | (data[off + 1] << 16) | (data[off + 2] << 8) | data[off + 3]);
}

static int32_t ttf_read_int32(const uint8_t *data, uint32_t off) {
    return (int32_t)((data[off] << 24) | (data[off + 1] << 16) | (data[off + 2] << 8) | data[off + 3]);
}

static uint32_t ttf_find_table(ttf_font_t *font, const char *tag) {
    uint16_t num_tables = ttf_read_uint16(font->data, 4);
    for (uint16_t i = 0; i < num_tables; i++) {
        uint32_t entry = 12 + i * 16;
        if (font->data[entry] == tag[0] && font->data[entry + 1] == tag[1] &&
            font->data[entry + 2] == tag[2] && font->data[entry + 3] == tag[3]) {
            return ttf_read_uint32(font->data, entry + 8);
        }
    }
    return 0;
}

int ttf_load(const uint8_t *data, uint32_t size, ttf_font_t *font) {
    font->data = (uint8_t *)data;
    font->size = size;

    uint32_t sfnt = ttf_read_uint32(data, 0);
    if (sfnt != 0x00010000 && sfnt != 0x74727565) return -1;

    font->cmap_offset = ttf_find_table(font, "cmap");
    font->glyf_offset = ttf_find_table(font, "glyf");
    font->head_offset = ttf_find_table(font, "head");
    font->hhea_offset = ttf_find_table(font, "hhea");
    font->hmtx_offset = ttf_find_table(font, "hmtx");
    font->loca_offset = ttf_find_table(font, "loca");
    font->maxp_offset = ttf_find_table(font, "maxp");

    if (!font->cmap_offset || !font->glyf_offset || !font->head_offset ||
        !font->hhea_offset || !font->hmtx_offset || !font->loca_offset || !font->maxp_offset)
        return -1;

    font->units_per_em = ttf_read_uint16(data, font->head_offset + 18);
    font->index_to_loc_format = ttf_read_int16(data, font->head_offset + 50);

    font->ascender = ttf_read_int16(data, font->hhea_offset + 4);
    font->descender = ttf_read_int16(data, font->hhea_offset + 6);
    font->line_gap = ttf_read_int16(data, font->hhea_offset + 8);

    font->num_glyphs = ttf_read_uint16(data, font->maxp_offset + 4);

    return 0;
}

uint16_t ttf_get_glyph_index(ttf_font_t *font, uint32_t codepoint) {
    uint32_t off = font->cmap_offset;
    uint16_t num_subtables = ttf_read_uint16(font->data, off + 2);

    for (uint16_t i = 0; i < num_subtables; i++) {
        uint32_t entry = off + 4 + i * 8;
        uint16_t platform = ttf_read_uint16(font->data, entry);
        uint16_t encoding = ttf_read_uint16(font->data, entry + 2);
        uint32_t subtable_off = ttf_read_uint32(font->data, entry + 4);

        if (platform == 3 && encoding == 1) {
            uint32_t fmt_off = off + subtable_off;
            uint16_t fmt = ttf_read_uint16(font->data, fmt_off);

            if (fmt == 4) {
                uint16_t seg_count = ttf_read_uint16(font->data, fmt_off + 6) / 2;
                uint32_t end_off = fmt_off + 14;
                uint32_t start_off = end_off + seg_count * 2 + 2;
                uint32_t delta_off = start_off + seg_count * 2;
                uint32_t range_off = delta_off + seg_count * 2;

                for (uint16_t seg = 0; seg < seg_count; seg++) {
                    uint16_t end = ttf_read_uint16(font->data, end_off + seg * 2);
                    uint16_t start = ttf_read_uint16(font->data, start_off + seg * 2);
                    if (codepoint >= start && codepoint <= end) {
                        uint16_t delta = ttf_read_uint16(font->data, delta_off + seg * 2);
                        uint16_t range = ttf_read_uint16(font->data, range_off + seg * 2);
                        if (range == 0) {
                            return (uint16_t)(codepoint + delta);
                        } else {
                            uint32_t idx_off = range_off + seg * 2 + range + (codepoint - start) * 2;
                            return ttf_read_uint16(font->data, idx_off);
                        }
                    }
                }
            }
        }
    }
    return 0;
}

static uint32_t ttf_get_glyph_offset(ttf_font_t *font, uint16_t idx) {
    if (font->index_to_loc_format == 0) {
        return ttf_read_uint16(font->data, font->loca_offset + idx * 2) * 2;
    } else {
        return ttf_read_uint32(font->data, font->loca_offset + idx * 4);
    }
}

static void ttf_fill_scanline(gfx_context_t *ctx, int32_t y, int32_t x0, int32_t x1, gfx_color_t color, float alpha) {
    if (y < 0 || y >= (int32_t)ctx->height) return;
    if (x0 < 0) x0 = 0;
    if (x1 >= (int32_t)ctx->width) x1 = ctx->width - 1;
    if (x0 > x1) return;

    uint8_t a = (uint8_t)(alpha * 255.0f);
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;

    for (int32_t x = x0; x <= x1; x++) {
        uint32_t *pixel = (uint32_t *)((uint8_t *)ctx->buffer + y * ctx->pitch + x * 4);
        uint8_t pr = (*pixel >> 16) & 0xFF;
        uint8_t pg = (*pixel >> 8) & 0xFF;
        uint8_t pb = *pixel & 0xFF;
        pr = (pr * (255 - a) + r * a) / 255;
        pg = (pg * (255 - a) + g * a) / 255;
        pb = (pb * (255 - a) + b * a) / 255;
        *pixel = (pr << 16) | (pg << 8) | pb;
    }
}

int ttf_render_glyph(ttf_font_t *font, uint16_t glyph_index, int32_t x, int32_t y, uint32_t size, gfx_color_t color, gfx_context_t *ctx) {
    if (glyph_index == 0) return -1;

    float scale = (float)size / font->units_per_em;

    uint32_t glyph_off = ttf_get_glyph_offset(font, glyph_index);
    uint32_t abs_off = font->glyf_offset + glyph_off;

    int16_t num_contours = ttf_read_int16(font->data, abs_off);
    ttf_glyph_t glyph;
    glyph.x_min = ttf_read_int16(font->data, abs_off + 2);
    glyph.y_min = ttf_read_int16(font->data, abs_off + 4);
    glyph.x_max = ttf_read_int16(font->data, abs_off + 6);
    glyph.y_max = ttf_read_int16(font->data, abs_off + 8);

    uint16_t num_long_hor_metrics = ttf_read_uint16(font->data, font->hhea_offset + 34);
    if (glyph_index < num_long_hor_metrics) {
        glyph.advance_width = ttf_read_uint16(font->data, font->hmtx_offset + glyph_index * 4);
        glyph.left_side_bearing = ttf_read_int16(font->data, font->hmtx_offset + glyph_index * 4 + 2);
    } else {
        glyph.advance_width = ttf_read_uint16(font->data, font->hmtx_offset + (num_long_hor_metrics - 1) * 4);
        glyph.left_side_bearing = ttf_read_int16(font->data, font->hmtx_offset + num_long_hor_metrics * 4 + (glyph_index - num_long_hor_metrics) * 2);
    }

    if (num_contours == 0) return 0;
    if (num_contours < 0) return 0;

    uint32_t cur = abs_off + 10;
    uint16_t *end_pts = (uint16_t *)kmalloc(num_contours * sizeof(uint16_t));
    for (int i = 0; i < num_contours; i++) {
        end_pts[i] = ttf_read_uint16(font->data, cur);
        cur += 2;
    }

    uint16_t num_points = end_pts[num_contours - 1] + 1;

    cur += 2;

    uint8_t *flags = (uint8_t *)kmalloc(num_points);
    for (uint16_t i = 0; i < num_points; ) {
        uint8_t f = font->data[cur++];
        flags[i++] = f;
        if (f & 0x08) {
            uint8_t rep = font->data[cur++];
            for (uint8_t j = 0; j < rep && i < num_points; j++) {
                flags[i++] = f;
            }
        }
    }

    int16_t *x_coords = (int16_t *)kmalloc(num_points * sizeof(int16_t));
    int16_t *y_coords = (int16_t *)kmalloc(num_points * sizeof(int16_t));

    int16_t val = 0;
    for (uint16_t i = 0; i < num_points; i++) {
        if (flags[i] & 0x02) {
            int8_t dx = (int8_t)font->data[cur++];
            val += (flags[i] & 0x10) ? dx : -dx;
        } else if (!(flags[i] & 0x10)) {
            val += ttf_read_int16(font->data, cur);
            cur += 2;
        }
        x_coords[i] = val;
    }

    val = 0;
    for (uint16_t i = 0; i < num_points; i++) {
        if (flags[i] & 0x04) {
            int8_t dy = (int8_t)font->data[cur++];
            val += (flags[i] & 0x20) ? dy : -dy;
        } else if (!(flags[i] & 0x20)) {
            val += ttf_read_int16(font->data, cur);
            cur += 2;
        }
        y_coords[i] = val;
    }

    int32_t *scanline = (int32_t *)kmalloc(ctx->height * sizeof(int32_t) * 2);
    int32_t *scanline_count = (int32_t *)kmalloc(ctx->height * sizeof(int32_t));
    memset(scanline_count, 0, ctx->height * sizeof(int32_t));

    int32_t base_y = y - (int32_t)(font->ascender * scale);

    for (int c = 0; c < num_contours; c++) {
        int start = (c == 0) ? 0 : end_pts[c - 1] + 1;
        int end = end_pts[c];

        for (int i = start; i <= end; i++) {
            int next = (i == end) ? start : i + 1;

            int32_t x0s = x + (int32_t)(x_coords[i] * scale);
            int32_t y0s = base_y + (int32_t)((font->ascender - y_coords[i]) * scale);
            int32_t x1s = x + (int32_t)(x_coords[next] * scale);
            int32_t y1s = base_y + (int32_t)((font->ascender - y_coords[next]) * scale);

            if (y0s == y1s) continue;

            if (y0s > y1s) {
                int32_t tmp;
                tmp = x0s; x0s = x1s; x1s = tmp;
                tmp = y0s; y0s = y1s; y1s = tmp;
            }

            for (int32_t sy = y0s; sy < y1s; sy++) {
                if (sy < 0 || sy >= (int32_t)ctx->height) continue;
                float t = (float)(sy - y0s) / (float)(y1s - y0s);
                int32_t ix = (int32_t)(x0s + t * (x1s - x0s));
                uint32_t si = scanline_count[sy];
                scanline[sy * 2 + si] = ix;
                scanline_count[sy] = (si + 1) % 2;
            }
        }
    }

    for (uint32_t sy = 0; sy < ctx->height; sy++) {
        if (scanline_count[sy] == 0 && scanline[(sy * 2) + 0] != 0) {
            int32_t x0s = scanline[sy * 2 + 0];
            int32_t x1s = scanline[sy * 2 + 1];
            if (x0s > x1s) { int32_t tmp = x0s; x0s = x1s; x1s = tmp; }
            ttf_fill_scanline(ctx, sy, x0s, x1s, color, 1.0f);
        }
    }

    kfree(end_pts);
    kfree(flags);
    kfree(x_coords);
    kfree(y_coords);
    kfree(scanline);
    kfree(scanline_count);

    return 0;
}

int ttf_render_string(ttf_font_t *font, const char *str, int32_t x, int32_t y, uint32_t size, gfx_color_t color, gfx_context_t *ctx) {
    int32_t cur_x = x;
    float scale = (float)size / font->units_per_em;

    while (*str) {
        uint8_t ch = (uint8_t)*str;
        if (ch < 0x20) { str++; continue; }
        uint16_t idx = ttf_get_glyph_index(font, ch);
        ttf_render_glyph(font, idx, cur_x, y, size, color, ctx);

        uint16_t advance = 0;
        uint16_t num_long = ttf_read_uint16(font->data, font->hhea_offset + 34);
        if (idx < num_long) {
            advance = ttf_read_uint16(font->data, font->hmtx_offset + idx * 4);
        } else if (num_long > 0) {
            advance = ttf_read_uint16(font->data, font->hmtx_offset + (num_long - 1) * 4);
        }
        cur_x += (int32_t)(advance * scale);
        str++;
    }
    return 0;
}

void ttf_measure_string(ttf_font_t *font, const char *str, uint32_t font_size, uint32_t *width, uint32_t *height) {
    float scale = (float)font_size / font->units_per_em;
    uint32_t w = 0;
    while (*str) {
        uint8_t ch = (uint8_t)*str;
        if (ch < 0x20) { str++; continue; }
        uint16_t idx = ttf_get_glyph_index(font, ch);
        uint16_t advance = 0;
        uint16_t num_long = ttf_read_uint16(font->data, font->hhea_offset + 34);
        if (idx < num_long) {
            advance = ttf_read_uint16(font->data, font->hmtx_offset + idx * 4);
        } else if (num_long > 0) {
            advance = ttf_read_uint16(font->data, font->hmtx_offset + (num_long - 1) * 4);
        }
        w += (uint32_t)(advance * scale);
        str++;
    }
    *width = w;
    *height = (uint32_t)((font->ascender - font->descender) * scale);
}
