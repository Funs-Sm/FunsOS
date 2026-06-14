#ifndef FONT_ENGINE_H
#define FONT_ENGINE_H

#include "stdint.h"
#include "gfx.h"

typedef struct {
    uint32_t codepoint;
    uint32_t width;
    uint32_t height;
    int32_t bearing_x;
    int32_t bearing_y;
    int32_t advance;
    uint8_t *bitmap;  /* Anti-aliased bitmap (8-bit alpha) */
} glyph_t;

typedef struct {
    char family[64];
    char style[32];
    uint32_t size;
    uint32_t ascent;
    uint32_t descent;
    uint32_t line_gap;
    uint32_t units_per_em;
    void *font_data;
} font_face_t;

void font_engine_init(void);
font_face_t *font_load(const char *path, uint32_t size);
void font_free(font_face_t *face);
glyph_t *font_get_glyph(font_face_t *face, uint32_t codepoint);
void font_draw_glyph(gfx_context_t *ctx, glyph_t *glyph, int32_t x, int32_t y, uint32_t fg);
uint32_t font_draw_text_utf8(font_face_t *face, gfx_context_t *ctx, const char *text, int32_t x, int32_t y, uint32_t fg);
uint32_t font_text_width(font_face_t *face, const char *text);

#endif
