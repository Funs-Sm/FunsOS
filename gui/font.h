#ifndef FONT_H
#define FONT_H

#include "stdint.h"

#define FONT_GLYPH_WIDTH   8
#define FONT_GLYPH_HEIGHT  16
#define FONT_FIRST_CHAR    32
#define FONT_LAST_CHAR     127
#define FONT_GLYPH_COUNT   (FONT_LAST_CHAR - FONT_FIRST_CHAR + 1)

#include "gfx.h"

extern const uint8_t font_data[FONT_GLYPH_COUNT][FONT_GLYPH_HEIGHT];

void font_init(void);
void font_draw_char(gfx_context_t *ctx, char c, int32_t x, int32_t y, gfx_color_t fg, gfx_color_t bg);
void font_draw_string(gfx_context_t *ctx, const char *str, int32_t x, int32_t y, gfx_color_t fg, gfx_color_t bg);
void font_measure_string(const char *str, uint32_t *width, uint32_t *height);

#endif
