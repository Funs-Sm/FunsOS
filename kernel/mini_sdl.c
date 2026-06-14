#include "mini_sdl.h"
#include "kheap.h"
#include "string.h"
#include "display_server.h"
#include "font.h"
#include "timer.h"

/* Default pixel format: XRGB8888 */
static sdl_pixel_format_t sdl_default_format = {
    16, 8, 0, 24,   /* rshift, gshift, bshift, ashift */
    0, 0, 0, 8      /* rloss, gloss, bloss, aloss */
};

static sdl_surface_t *sdl_screen;
static uint32_t sdl_initialized;

void sdl_init(void) {
    sdl_initialized = 1;
    sdl_screen = 0;
}

void sdl_quit(void) {
    if (sdl_screen) {
        sdl_free_surface(sdl_screen);
        sdl_screen = 0;
    }
    sdl_initialized = 0;
}

sdl_surface_t *sdl_create_surface(uint32_t w, uint32_t h) {
    sdl_surface_t *s = (sdl_surface_t *)kmalloc(sizeof(sdl_surface_t));
    if (!s) return 0;

    s->flags = SDL_SWSURFACE;
    s->width = w;
    s->height = h;
    s->pitch = w * 4;
    s->bpp = 32;
    s->format = sdl_default_format;
    s->userdata = 0;
    s->locked = 0;

    s->pixels = (uint32_t *)kmalloc(w * h * 4);
    if (!s->pixels) {
        kfree(s);
        return 0;
    }
    memset(s->pixels, 0, w * h * 4);

    return s;
}

void sdl_free_surface(sdl_surface_t *s) {
    if (!s) return;
    if (s->pixels) {
        kfree(s->pixels);
    }
    kfree(s);
}

sdl_surface_t *sdl_get_screen(void) {
    return sdl_screen;
}

void sdl_fill_rect(sdl_surface_t *s, const sdl_rect_t *rect, uint32_t color) {
    if (!s || !s->pixels) return;

    int32_t x0, y0, x1, y1;

    if (rect) {
        x0 = rect->x;
        y0 = rect->y;
        x1 = (int32_t)(rect->x + rect->w);
        y1 = (int32_t)(rect->y + rect->h);
    } else {
        x0 = 0;
        y0 = 0;
        x1 = (int32_t)s->width;
        y1 = (int32_t)s->height;
    }

    /* Clip */
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int32_t)s->width) x1 = (int32_t)s->width;
    if (y1 > (int32_t)s->height) y1 = (int32_t)s->height;

    for (int32_t y = y0; y < y1; y++) {
        for (int32_t x = x0; x < x1; x++) {
            s->pixels[y * s->width + x] = color;
        }
    }
}

void sdl_blit_surface(sdl_surface_t *src, const sdl_rect_t *srcrect,
                      sdl_surface_t *dst, const sdl_rect_t *dstrect) {
    if (!src || !dst || !src->pixels || !dst->pixels) return;

    int32_t sx0, sy0, sw, sh;
    int32_t dx0, dy0;

    if (srcrect) {
        sx0 = srcrect->x;
        sy0 = srcrect->y;
        sw = (int32_t)srcrect->w;
        sh = (int32_t)srcrect->h;
    } else {
        sx0 = 0;
        sy0 = 0;
        sw = (int32_t)src->width;
        sh = (int32_t)src->height;
    }

    if (dstrect) {
        dx0 = dstrect->x;
        dy0 = dstrect->y;
    } else {
        dx0 = 0;
        dy0 = 0;
    }

    for (int32_t y = 0; y < sh; y++) {
        int32_t sy = sy0 + y;
        int32_t dy = dy0 + y;
        if (sy < 0 || sy >= (int32_t)src->height) continue;
        if (dy < 0 || dy >= (int32_t)dst->height) continue;

        for (int32_t x = 0; x < sw; x++) {
            int32_t sxx = sx0 + x;
            int32_t dxx = dx0 + x;
            if (sxx < 0 || sxx >= (int32_t)src->width) continue;
            if (dxx < 0 || dxx >= (int32_t)dst->width) continue;

            uint32_t pixel = src->pixels[sy * src->width + sxx];
            /* Skip fully transparent pixels (alpha = 0) */
            if ((pixel & 0xFF000000) != 0) {
                dst->pixels[dy * dst->width + dxx] = pixel;
            }
        }
    }
}

void sdl_update_rect(sdl_surface_t *s, const sdl_rect_t *rect) {
    (void)s;
    (void)rect;
    /* In our implementation, the display server handles rendering */
}

uint32_t sdl_map_rgb(sdl_pixel_format_t *fmt, uint8_t r, uint8_t g, uint8_t b) {
    (void)fmt;
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

uint32_t sdl_map_rgba(sdl_pixel_format_t *fmt, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    (void)fmt;
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

void sdl_get_rgb(uint32_t pixel, sdl_pixel_format_t *fmt, uint8_t *r, uint8_t *g, uint8_t *b) {
    (void)fmt;
    *r = (uint8_t)((pixel >> 16) & 0xFF);
    *g = (uint8_t)((pixel >> 8) & 0xFF);
    *b = (uint8_t)(pixel & 0xFF);
}

void sdl_draw_line(sdl_surface_t *s, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color) {
    if (!s || !s->pixels) return;

    /* Bresenham's line algorithm */
    int32_t dx = x2 - x1;
    int32_t dy = y2 - y1;
    int32_t sx, sy, err;

    if (dx < 0) { dx = -dx; sx = -1; } else { sx = 1; }
    if (dy < 0) { dy = -dy; sy = -1; } else { sy = 1; }

    if (dx > dy) {
        err = dx / 2;
        while (x1 != x2) {
            if (x1 >= 0 && x1 < (int32_t)s->width && y1 >= 0 && y1 < (int32_t)s->height) {
                s->pixels[y1 * s->width + x1] = color;
            }
            err -= dy;
            if (err < 0) { y1 += sy; err += dx; }
            x1 += sx;
        }
    } else {
        err = dy / 2;
        while (y1 != y2) {
            if (x1 >= 0 && x1 < (int32_t)s->width && y1 >= 0 && y1 < (int32_t)s->height) {
                s->pixels[y1 * s->width + x1] = color;
            }
            err -= dx;
            if (err < 0) { x1 += sx; err += dy; }
            y1 += sy;
        }
    }
    if (x1 >= 0 && x1 < (int32_t)s->width && y1 >= 0 && y1 < (int32_t)s->height) {
        s->pixels[y1 * s->width + x1] = color;
    }
}

void sdl_draw_circle(sdl_surface_t *s, int32_t cx, int32_t cy, int32_t r, uint32_t color) {
    if (!s || !s->pixels) return;

    /* Midpoint circle algorithm */
    int32_t x = r;
    int32_t y = 0;
    int32_t d = 1 - r;

    while (x >= y) {
        /* 8 symmetry points */
        int32_t px[8] = { cx+x, cx-x, cx+x, cx-x, cx+y, cx-y, cx+y, cx-y };
        int32_t py[8] = { cy+y, cy+y, cy-y, cy-y, cy+x, cy+x, cy-x, cy-x };

        for (int i = 0; i < 8; i++) {
            if (px[i] >= 0 && px[i] < (int32_t)s->width &&
                py[i] >= 0 && py[i] < (int32_t)s->height) {
                s->pixels[py[i] * s->width + px[i]] = color;
            }
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

void sdl_fill_circle(sdl_surface_t *s, int32_t cx, int32_t cy, int32_t r, uint32_t color) {
    if (!s || !s->pixels) return;

    int32_t x = r;
    int32_t y = 0;
    int32_t d = 1 - r;

    while (x >= y) {
        /* Draw horizontal lines for each pair of symmetry points */
        for (int32_t i = cx - x; i <= cx + x; i++) {
            if (i >= 0 && i < (int32_t)s->width) {
                if (cy + y >= 0 && cy + y < (int32_t)s->height)
                    s->pixels[(cy + y) * s->width + i] = color;
                if (cy - y >= 0 && cy - y < (int32_t)s->height)
                    s->pixels[(cy - y) * s->width + i] = color;
            }
        }
        for (int32_t i = cx - y; i <= cx + y; i++) {
            if (i >= 0 && i < (int32_t)s->width) {
                if (cy + x >= 0 && cy + x < (int32_t)s->height)
                    s->pixels[(cy + x) * s->width + i] = color;
                if (cy - x >= 0 && cy - x < (int32_t)s->height)
                    s->pixels[(cy - x) * s->width + i] = color;
            }
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

void sdl_draw_triangle(sdl_surface_t *s, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                       int32_t x3, int32_t y3, uint32_t color) {
    sdl_draw_line(s, x1, y1, x2, y2, color);
    sdl_draw_line(s, x2, y2, x3, y3, color);
    sdl_draw_line(s, x3, y3, x1, y1, color);
}

void sdl_draw_text(sdl_surface_t *s, int32_t x, int32_t y, const char *text, uint32_t fg, uint32_t bg) {
    if (!s || !s->pixels || !text) return;

    int32_t cx = x;
    while (*text) {
        uint8_t ch = (uint8_t)*text;
        if (ch >= 32 && ch <= 127) {
            const uint8_t *glyph = font_data[ch - 32];
            for (int row = 0; row < FONT_GLYPH_HEIGHT; row++) {
                uint8_t bits = glyph[row];
                for (int col = 0; col < FONT_GLYPH_WIDTH; col++) {
                    int32_t px = cx + col;
                    int32_t py = y + row;
                    if (px >= 0 && px < (int32_t)s->width &&
                        py >= 0 && py < (int32_t)s->height) {
                        if (bits & (0x80 >> col)) {
                            s->pixels[py * s->width + px] = fg;
                        } else if (bg != 0xFFFFFFFF) {
                            s->pixels[py * s->width + px] = bg;
                        }
                    }
                }
            }
        }
        cx += FONT_GLYPH_WIDTH;
        text++;
    }
}

int sdl_poll_event(sdl_event_t *event) {
    if (!event) return 0;

    /* Try to get events from display server */
    ds_event_t ds_ev;
    if (ds_get_event(0, &ds_ev)) {
        memset(event, 0, sizeof(sdl_event_t));
        event->timestamp = timer_get_ticks();

        switch (ds_ev.type) {
            case DS_EVENT_KEY_PRESS:
            case DS_EVENT_KEY_RELEASE:
                event->type = (ds_ev.type == DS_EVENT_KEY_PRESS) ? 1 : 2;
                event->data.key.key = (uint8_t)ds_ev.param2;
                event->data.key.mod = (uint16_t)ds_ev.param3;
                break;
            case DS_EVENT_MOUSE_MOVE:
                event->type = 3;
                event->data.motion.x = (int32_t)ds_ev.param1;
                event->data.motion.y = (int32_t)ds_ev.param2;
                event->data.motion.buttons = (uint8_t)ds_ev.param3;
                break;
            case DS_EVENT_MOUSE_PRESS:
            case DS_EVENT_MOUSE_RELEASE:
                event->type = (ds_ev.type == DS_EVENT_MOUSE_PRESS) ? 4 : 5;
                event->data.button.x = (int32_t)ds_ev.param1;
                event->data.button.y = (int32_t)ds_ev.param2;
                event->data.button.button = (uint8_t)ds_ev.param3;
                break;
            default:
                return 0;
        }
        return 1;
    }
    return 0;
}

int sdl_wait_event(sdl_event_t *event) {
    while (1) {
        if (sdl_poll_event(event)) return 1;
        asm volatile("hlt");
    }
}
