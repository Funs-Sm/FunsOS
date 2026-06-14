#include "gfx.h"
#include "kheap.h"
#include "string.h"

static uint32_t gfx_current_backend = GFX_BACKEND_SOFTWARE;
static gfx_backend_ctx_t gfx_fr_context = 0;

static int32_t clamp_int(int32_t v, int32_t lo, int32_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int in_clip(gfx_context_t *ctx, int32_t x, int32_t y) {
    return x >= ctx->clip.x && x < ctx->clip.x + ctx->clip.w &&
           y >= ctx->clip.y && y < ctx->clip.y + ctx->clip.h;
}

void gfx_init(gfx_context_t *ctx, uint32_t *buffer, uint32_t w, uint32_t h, uint32_t pitch, uint32_t bpp) {
    ctx->buffer = buffer;
    ctx->width = w;
    ctx->height = h;
    ctx->pitch = pitch;
    ctx->bpp = bpp;
    ctx->clip.x = 0;
    ctx->clip.y = 0;
    ctx->clip.w = w;
    ctx->clip.h = h;
}

/* Write a pixel respecting the current bpp */
static void gfx_write_pixel(gfx_context_t *ctx, int32_t x, int32_t y, gfx_color_t color) {
    if (ctx->bpp == 32) {
        ctx->buffer[y * (ctx->pitch / 4) + x] = color;
    } else if (ctx->bpp == 24) {
        uint8_t *fb8 = (uint8_t *)ctx->buffer;
        uint32_t byte_offset = y * ctx->pitch + x * 3;
        fb8[byte_offset]     = color & 0xFF;
        fb8[byte_offset + 1] = (color >> 8) & 0xFF;
        fb8[byte_offset + 2] = (color >> 16) & 0xFF;
    } else if (ctx->bpp == 16 || ctx->bpp == 15) {
        uint16_t *fb16 = (uint16_t *)ctx->buffer;
        /* Convert RGB888 -> RGB565 (works for both 16 and 15 bpp; top bit ignored in 15bpp) */
        uint16_t r5 = (uint16_t)((color >> 19) & 0x1F);
        uint16_t g6 = (uint16_t)((color >> 10) & 0x3F);
        uint16_t b5 = (uint16_t)((color >> 3) & 0x1F);
        fb16[y * (ctx->pitch / 2) + x] = (r5 << 11) | (g6 << 5) | b5;
    }
}

/* Read a pixel respecting the current bpp */
static gfx_color_t gfx_read_pixel(gfx_context_t *ctx, int32_t x, int32_t y) {
    if (ctx->bpp == 32) {
        return ctx->buffer[y * (ctx->pitch / 4) + x];
    } else if (ctx->bpp == 24) {
        uint8_t *fb8 = (uint8_t *)ctx->buffer;
        uint32_t byte_offset = y * ctx->pitch + x * 3;
        return (uint32_t)fb8[byte_offset] |
               ((uint32_t)fb8[byte_offset + 1] << 8) |
               ((uint32_t)fb8[byte_offset + 2] << 16);
    } else if (ctx->bpp == 16 || ctx->bpp == 15) {
        uint16_t *fb16 = (uint16_t *)ctx->buffer;
        /* Convert RGB565 -> RGB888 */
        uint16_t pixel = fb16[y * (ctx->pitch / 2) + x];
        uint32_t r = ((uint32_t)((pixel >> 11) & 0x1F) << 3);
        uint32_t g = ((uint32_t)((pixel >> 5) & 0x3F) << 2);
        uint32_t b = ((uint32_t)(pixel & 0x1F) << 3);
        return (r << 16) | (g << 8) | b;
    }
    return 0;
}

void gfx_set_pixel(gfx_context_t *ctx, int32_t x, int32_t y, gfx_color_t color) {
    if (x < 0 || x >= (int32_t)ctx->width || y < 0 || y >= (int32_t)ctx->height) return;
    if (!in_clip(ctx, x, y)) return;
    gfx_write_pixel(ctx, x, y, color);
}

gfx_color_t gfx_get_pixel(gfx_context_t *ctx, int32_t x, int32_t y) {
    if (x < 0 || x >= (int32_t)ctx->width || y < 0 || y >= (int32_t)ctx->height) return 0;
    if (!in_clip(ctx, x, y)) return 0;
    return gfx_read_pixel(ctx, x, y);
}

void gfx_draw_line(gfx_context_t *ctx, int32_t x0, int32_t y0, int32_t x1, int32_t y1, gfx_color_t color) {
    int32_t dx = x1 - x0;
    int32_t dy = y1 - y0;
    int32_t sx, sy, err, e2;

    if (dx < 0) { dx = -dx; sx = -1; } else { sx = 1; }
    if (dy < 0) { dy = -dy; sy = -1; } else { sy = 1; }

    if (dx > dy) {
        err = dx / 2;
        while (x0 != x1) {
            gfx_set_pixel(ctx, x0, y0, color);
            err -= dy;
            if (err < 0) { y0 += sy; err += dx; }
            x0 += sx;
        }
    } else {
        err = dy / 2;
        while (y0 != y1) {
            gfx_set_pixel(ctx, x0, y0, color);
            err -= dx;
            if (err < 0) { x0 += sx; err += dy; }
            y0 += sy;
        }
    }
    gfx_set_pixel(ctx, x0, y0, color);
}

void gfx_draw_rect(gfx_context_t *ctx, gfx_rect_t rect, gfx_color_t color) {
    gfx_draw_line(ctx, rect.x, rect.y, rect.x + rect.w - 1, rect.y, color);
    gfx_draw_line(ctx, rect.x, rect.y + rect.h - 1, rect.x + rect.w - 1, rect.y + rect.h - 1, color);
    gfx_draw_line(ctx, rect.x, rect.y, rect.x, rect.y + rect.h - 1, color);
    gfx_draw_line(ctx, rect.x + rect.w - 1, rect.y, rect.x + rect.w - 1, rect.y + rect.h - 1, color);
}

void gfx_fill_rect(gfx_context_t *ctx, gfx_rect_t rect, gfx_color_t color) {
    int32_t x0 = clamp_int(rect.x, 0, (int32_t)ctx->width);
    int32_t y0 = clamp_int(rect.y, 0, (int32_t)ctx->height);
    int32_t x1 = clamp_int(rect.x + rect.w, 0, (int32_t)ctx->width);
    int32_t y1 = clamp_int(rect.y + rect.h, 0, (int32_t)ctx->height);

    if (x0 < ctx->clip.x) x0 = ctx->clip.x;
    if (y0 < ctx->clip.y) y0 = ctx->clip.y;
    if (x1 > ctx->clip.x + ctx->clip.w) x1 = ctx->clip.x + ctx->clip.w;
    if (y1 > ctx->clip.y + ctx->clip.h) y1 = ctx->clip.y + ctx->clip.h;

    if (ctx->bpp == 32) {
        for (int32_t y = y0; y < y1; y++) {
            uint32_t *row = &ctx->buffer[y * (ctx->pitch / 4)];
            for (int32_t x = x0; x < x1; x++) {
                row[x] = color;
            }
        }
    } else if (ctx->bpp == 24) {
        uint8_t b0 = color & 0xFF;
        uint8_t b1 = (color >> 8) & 0xFF;
        uint8_t b2 = (color >> 16) & 0xFF;
        uint8_t *fb8 = (uint8_t *)ctx->buffer;
        for (int32_t y = y0; y < y1; y++) {
            uint32_t row_off = y * ctx->pitch;
            for (int32_t x = x0; x < x1; x++) {
                uint32_t off = row_off + x * 3;
                fb8[off]     = b0;
                fb8[off + 1] = b1;
                fb8[off + 2] = b2;
            }
        }
    } else if (ctx->bpp == 16 || ctx->bpp == 15) {
        /* Convert RGB888 -> RGB565 once */
        uint16_t r5 = (uint16_t)((color >> 19) & 0x1F);
        uint16_t g6 = (uint16_t)((color >> 10) & 0x3F);
        uint16_t b5 = (uint16_t)((color >> 3) & 0x1F);
        uint16_t pixel = (r5 << 11) | (g6 << 5) | b5;
        uint16_t *fb16 = (uint16_t *)ctx->buffer;
        for (int32_t y = y0; y < y1; y++) {
            uint16_t *row = &fb16[y * (ctx->pitch / 2)];
            for (int32_t x = x0; x < x1; x++) {
                row[x] = pixel;
            }
        }
    }
}

void gfx_draw_circle(gfx_context_t *ctx, int32_t cx, int32_t cy, int32_t r, gfx_color_t color) {
    int32_t x = r;
    int32_t y = 0;
    int32_t d = 1 - r;

    while (x >= y) {
        gfx_set_pixel(ctx, cx + x, cy + y, color);
        gfx_set_pixel(ctx, cx - x, cy + y, color);
        gfx_set_pixel(ctx, cx + x, cy - y, color);
        gfx_set_pixel(ctx, cx - x, cy - y, color);
        gfx_set_pixel(ctx, cx + y, cy + x, color);
        gfx_set_pixel(ctx, cx - y, cy + x, color);
        gfx_set_pixel(ctx, cx + y, cy - x, color);
        gfx_set_pixel(ctx, cx - y, cy - x, color);
        y++;
        if (d <= 0) {
            d += 2 * y + 1;
        } else {
            x--;
            d += 2 * (y - x) + 1;
        }
    }
}

void gfx_fill_circle(gfx_context_t *ctx, int32_t cx, int32_t cy, int32_t r, gfx_color_t color) {
    int32_t x = r;
    int32_t y = 0;
    int32_t d = 1 - r;

    while (x >= y) {
        gfx_draw_line(ctx, cx - x, cy + y, cx + x, cy + y, color);
        gfx_draw_line(ctx, cx - x, cy - y, cx + x, cy - y, color);
        gfx_draw_line(ctx, cx - y, cy + x, cx + y, cy + x, color);
        gfx_draw_line(ctx, cx - y, cy - x, cx + y, cy - x, color);
        y++;
        if (d <= 0) {
            d += 2 * y + 1;
        } else {
            x--;
            d += 2 * (y - x) + 1;
        }
    }
}

static void draw_quarter_circle(gfx_context_t *ctx, int32_t cx, int32_t cy, int32_t r, int quadrant, gfx_color_t color) {
    int32_t x = r;
    int32_t y = 0;
    int32_t d = 1 - r;

    while (x >= y) {
        switch (quadrant) {
            case 0:
                gfx_set_pixel(ctx, cx + x, cy - y, color);
                gfx_set_pixel(ctx, cx + y, cy - x, color);
                break;
            case 1:
                gfx_set_pixel(ctx, cx - x, cy - y, color);
                gfx_set_pixel(ctx, cx - y, cy - x, color);
                break;
            case 2:
                gfx_set_pixel(ctx, cx - x, cy + y, color);
                gfx_set_pixel(ctx, cx - y, cy + x, color);
                break;
            case 3:
                gfx_set_pixel(ctx, cx + x, cy + y, color);
                gfx_set_pixel(ctx, cx + y, cy + x, color);
                break;
        }
        y++;
        if (d <= 0) {
            d += 2 * y + 1;
        } else {
            x--;
            d += 2 * (y - x) + 1;
        }
    }
}

void gfx_draw_rounded_rect(gfx_context_t *ctx, gfx_rect_t rect, int32_t radius, gfx_color_t color) {
    int32_t x0 = rect.x;
    int32_t y0 = rect.y;
    int32_t x1 = rect.x + rect.w - 1;
    int32_t y1 = rect.y + rect.h - 1;

    if (radius > rect.w / 2) radius = rect.w / 2;
    if (radius > rect.h / 2) radius = rect.h / 2;

    gfx_draw_line(ctx, x0 + radius, y0, x1 - radius, y0, color);
    gfx_draw_line(ctx, x0 + radius, y1, x1 - radius, y1, color);
    gfx_draw_line(ctx, x0, y0 + radius, x0, y1 - radius, color);
    gfx_draw_line(ctx, x1, y0 + radius, x1, y1 - radius, color);

    draw_quarter_circle(ctx, x1 - radius, y0 + radius, radius, 0, color);
    draw_quarter_circle(ctx, x0 + radius, y0 + radius, radius, 1, color);
    draw_quarter_circle(ctx, x0 + radius, y1 - radius, radius, 2, color);
    draw_quarter_circle(ctx, x1 - radius, y1 - radius, radius, 3, color);
}

void gfx_fill_rounded_rect(gfx_context_t *ctx, gfx_rect_t rect, int32_t radius, gfx_color_t color) {
    int32_t x0 = rect.x;
    int32_t y0 = rect.y;
    int32_t x1 = rect.x + rect.w - 1;
    int32_t y1 = rect.y + rect.h - 1;

    if (radius > rect.w / 2) radius = rect.w / 2;
    if (radius > rect.h / 2) radius = rect.h / 2;

    gfx_rect_t center = { x0, y0 + radius, rect.w, rect.h - 2 * radius };
    gfx_fill_rect(ctx, center, color);

    gfx_rect_t top = { x0 + radius, y0, rect.w - 2 * radius, radius };
    gfx_fill_rect(ctx, top, color);

    gfx_rect_t bottom = { x0 + radius, y1 - radius + 1, rect.w - 2 * radius, radius };
    gfx_fill_rect(ctx, bottom, color);

    int32_t cx_l = x0 + radius;
    int32_t cx_r = x1 - radius;
    int32_t cy_t = y0 + radius;
    int32_t cy_b = y1 - radius;

    for (int32_t dy = 0; dy < radius; dy++) {
        int32_t dx = 0;
        int32_t r2 = radius * radius;
        while (dx * dx + dy * dy < r2) dx++;
        if (dx > 0) dx--;

        gfx_draw_line(ctx, cx_l - dx, cy_t - dy, cx_l + dx, cy_t - dy, color);
        gfx_draw_line(ctx, cx_r - dx, cy_t - dy, cx_r + dx, cy_t - dy, color);
        gfx_draw_line(ctx, cx_l - dx, cy_b + dy, cx_l + dx, cy_b + dy, color);
        gfx_draw_line(ctx, cx_r - dx, cy_b + dy, cx_r + dx, cy_b + dy, color);
    }
}

void gfx_blend_pixel(gfx_context_t *ctx, int32_t x, int32_t y, gfx_color_t color, uint8_t alpha) {
    if (x < 0 || x >= (int32_t)ctx->width || y < 0 || y >= (int32_t)ctx->height) return;
    if (!in_clip(ctx, x, y)) return;
    if (alpha == 0) return;
    if (alpha == 255) {
        gfx_write_pixel(ctx, x, y, color);
        return;
    }

    gfx_color_t dst = gfx_read_pixel(ctx, x, y);
    uint32_t sr = (color >> 16) & 0xFF;
    uint32_t sg = (color >> 8) & 0xFF;
    uint32_t sb = color & 0xFF;
    uint32_t dr = (dst >> 16) & 0xFF;
    uint32_t dg = (dst >> 8) & 0xFF;
    uint32_t db = dst & 0xFF;

    uint32_t inv = 255 - alpha;
    uint32_t r = (sr * alpha + dr * inv) / 255;
    uint32_t g = (sg * alpha + dg * inv) / 255;
    uint32_t b = (sb * alpha + db * inv) / 255;

    gfx_write_pixel(ctx, x, y, (r << 16) | (g << 8) | b);
}

void gfx_set_clip(gfx_context_t *ctx, gfx_rect_t clip) {
    ctx->clip = clip;
}

void gfx_reset_clip(gfx_context_t *ctx) {
    ctx->clip.x = 0;
    ctx->clip.y = 0;
    ctx->clip.w = ctx->width;
    ctx->clip.h = ctx->height;
}

void gfx_blit(gfx_context_t *dst, int32_t dx, int32_t dy, gfx_context_t *src, gfx_rect_t src_rect) {
    int32_t sx, sy, ddx, ddy;

    for (sy = src_rect.y, ddy = dy; sy < src_rect.y + src_rect.h; sy++, ddy++) {
        if (ddy < 0 || ddy >= (int32_t)dst->height) continue;
        if (sy < 0 || sy >= (int32_t)src->height) continue;
        for (sx = src_rect.x, ddx = dx; sx < src_rect.x + src_rect.w; sx++, ddx++) {
            if (ddx < 0 || ddx >= (int32_t)dst->width) continue;
            if (sx < 0 || sx >= (int32_t)src->width) continue;
            gfx_color_t pixel = gfx_read_pixel(src, sx, sy);
            gfx_write_pixel(dst, ddx, ddy, pixel);
        }
    }
}

/* ---- Renderer backend management ---- */

void gfx_set_backend(uint32_t backend) {
    if (backend <= GFX_BACKEND_AUTO) {
        gfx_current_backend = backend;
    }
}

uint32_t gfx_get_backend(void) {
    return gfx_current_backend;
}

void gfx_set_backend_context(gfx_backend_ctx_t fr_ctx) {
    gfx_fr_context = fr_ctx;
}

gfx_backend_ctx_t gfx_get_backend_context(void) {
    return gfx_fr_context;
}

/* ---- FunRender compatibility wrappers ---- */

void gfx_fill_rect_fr(gfx_context_t *ctx, gfx_rect_t rect, gfx_color_t color) {
    if (gfx_current_backend == GFX_BACKEND_FUNRENDER && gfx_fr_context) {
        /* Delegate to FunRender: fr_context_fill_rect(gfx_fr_context, rect, color) */
        /* For now, fall back to software rendering */
    }
    gfx_fill_rect(ctx, rect, color);
}

void gfx_draw_rect_fr(gfx_context_t *ctx, gfx_rect_t rect, gfx_color_t color) {
    if (gfx_current_backend == GFX_BACKEND_FUNRENDER && gfx_fr_context) {
        /* Delegate to FunRender */
    }
    gfx_draw_rect(ctx, rect, color);
}

void gfx_draw_line_fr(gfx_context_t *ctx, int32_t x0, int32_t y0, int32_t x1, int32_t y1, gfx_color_t color) {
    if (gfx_current_backend == GFX_BACKEND_FUNRENDER && gfx_fr_context) {
        /* Delegate to FunRender */
    }
    gfx_draw_line(ctx, x0, y0, x1, y1, color);
}

void gfx_blit_fr(gfx_context_t *dst, int32_t dx, int32_t dy, gfx_context_t *src, gfx_rect_t src_rect) {
    if (gfx_current_backend == GFX_BACKEND_FUNRENDER && gfx_fr_context) {
        /* Delegate to FunRender */
    }
    gfx_blit(dst, dx, dy, src, src_rect);
}

/* ---- FunRender-backed surface creation ---- */

gfx_context_t *gfx_create_fr_surface(uint32_t w, uint32_t h) {
    gfx_context_t *ctx = (gfx_context_t *)kmalloc(sizeof(gfx_context_t));
    if (!ctx) return 0;

    uint32_t *buf = (uint32_t *)kmalloc(w * h * 4);
    if (!buf) {
        kfree(ctx);
        return 0;
    }
    memset(buf, 0, w * h * 4);

    gfx_init(ctx, buf, w, h, w * 4, 32);
    return ctx;
}

void gfx_destroy_fr_surface(gfx_context_t *ctx) {
    if (!ctx) return;
    if (ctx->buffer) kfree(ctx->buffer);
    kfree(ctx);
}
