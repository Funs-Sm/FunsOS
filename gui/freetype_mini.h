#ifndef FREETYPE_MINI_H
#define FREETYPE_MINI_H

#include "gfx.h"
#include "stdint.h"

typedef struct {
    uint8_t *data;
    uint32_t size;
    uint16_t units_per_em;
    int16_t ascender;
    int16_t descender;
    int16_t line_gap;
    uint32_t cmap_offset;
    uint32_t glyf_offset;
    uint32_t head_offset;
    uint32_t hhea_offset;
    uint32_t hmtx_offset;
    uint32_t loca_offset;
    uint32_t maxp_offset;
    uint16_t num_glyphs;
    uint16_t index_to_loc_format;
} ttf_font_t;

typedef struct {
    int16_t x_min;
    int16_t y_min;
    int16_t x_max;
    int16_t y_max;
    uint16_t advance_width;
    int16_t left_side_bearing;
    uint32_t outline_offset;
    uint16_t outline_length;
} ttf_glyph_t;

int ttf_load(const uint8_t *data, uint32_t size, ttf_font_t *font);
uint16_t ttf_get_glyph_index(ttf_font_t *font, uint32_t codepoint);
int ttf_render_glyph(ttf_font_t *font, uint16_t glyph_index, int32_t x, int32_t y, uint32_t size, gfx_color_t color, gfx_context_t *ctx);
int ttf_render_string(ttf_font_t *font, const char *str, int32_t x, int32_t y, uint32_t size, gfx_color_t color, gfx_context_t *ctx);
void ttf_measure_string(ttf_font_t *font, const char *str, uint32_t font_size, uint32_t *width, uint32_t *height);

#endif
