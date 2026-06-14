#ifndef GFX_H
#define GFX_H

#include "stdint.h"

#define COLOR_BLACK     0x000000
#define COLOR_WHITE     0xFFFFFF
#define COLOR_RED       0xFF0000
#define COLOR_GREEN     0x00FF00
#define COLOR_BLUE      0x0000FF
#define COLOR_YELLOW    0xFFFF00
#define COLOR_CYAN      0x00FFFF
#define COLOR_MAGENTA   0xFF00FF
#define COLOR_GRAY      0x808080
#define COLOR_LIGHT_GRAY 0xC0C0C0
#define COLOR_DARK_GRAY 0x404040
#define COLOR_ORANGE    0xFF8000

typedef uint32_t gfx_color_t;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} gfx_rect_t;

typedef struct {
    int32_t x;
    int32_t y;
} gfx_point_t;

typedef struct {
    uint32_t *buffer;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;       /* bytes per scanline */
    uint32_t bpp;         /* bits per pixel (24 or 32) */
    gfx_rect_t clip;
} gfx_context_t;

#define GFX_ALPHA_BLEND 1
#define GFX_NO_BLEND    0

void gfx_init(gfx_context_t *ctx, uint32_t *buffer, uint32_t w, uint32_t h, uint32_t pitch, uint32_t bpp);
void gfx_set_pixel(gfx_context_t *ctx, int32_t x, int32_t y, gfx_color_t color);
gfx_color_t gfx_get_pixel(gfx_context_t *ctx, int32_t x, int32_t y);
void gfx_draw_line(gfx_context_t *ctx, int32_t x0, int32_t y0, int32_t x1, int32_t y1, gfx_color_t color);
void gfx_draw_rect(gfx_context_t *ctx, gfx_rect_t rect, gfx_color_t color);
void gfx_fill_rect(gfx_context_t *ctx, gfx_rect_t rect, gfx_color_t color);
void gfx_draw_circle(gfx_context_t *ctx, int32_t cx, int32_t cy, int32_t r, gfx_color_t color);
void gfx_fill_circle(gfx_context_t *ctx, int32_t cx, int32_t cy, int32_t r, gfx_color_t color);
void gfx_draw_rounded_rect(gfx_context_t *ctx, gfx_rect_t rect, int32_t radius, gfx_color_t color);
void gfx_fill_rounded_rect(gfx_context_t *ctx, gfx_rect_t rect, int32_t radius, gfx_color_t color);
void gfx_blend_pixel(gfx_context_t *ctx, int32_t x, int32_t y, gfx_color_t color, uint8_t alpha);
void gfx_set_clip(gfx_context_t *ctx, gfx_rect_t clip);
void gfx_reset_clip(gfx_context_t *ctx);
void gfx_blit(gfx_context_t *dst, int32_t dx, int32_t dy, gfx_context_t *src, gfx_rect_t src_rect);

/* ---- Renderer backend selection ---- */

#define GFX_BACKEND_SOFTWARE    0
#define GFX_BACKEND_FUNRENDER   1
#define GFX_BACKEND_AUTO        2

typedef void *gfx_backend_ctx_t;

void gfx_set_backend(uint32_t backend);
uint32_t gfx_get_backend(void);
void gfx_set_backend_context(gfx_backend_ctx_t fr_ctx);
gfx_backend_ctx_t gfx_get_backend_context(void);

/* ---- Compatibility wrappers for FunRender interop ---- */
void gfx_fill_rect_fr(gfx_context_t *ctx, gfx_rect_t rect, gfx_color_t color);
void gfx_draw_rect_fr(gfx_context_t *ctx, gfx_rect_t rect, gfx_color_t color);
void gfx_draw_line_fr(gfx_context_t *ctx, int32_t x0, int32_t y0, int32_t x1, int32_t y1, gfx_color_t color);
void gfx_blit_fr(gfx_context_t *dst, int32_t dx, int32_t dy, gfx_context_t *src, gfx_rect_t src_rect);

/* ---- Canvas surface creation backed by FunRender ---- */
gfx_context_t *gfx_create_fr_surface(uint32_t w, uint32_t h);
void gfx_destroy_fr_surface(gfx_context_t *ctx);

#endif
