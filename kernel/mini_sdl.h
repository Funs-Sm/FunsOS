#ifndef MINI_SDL_H
#define MINI_SDL_H

#include "stdint.h"

/* Surface flags */
#define SDL_SWSURFACE    0
#define SDL_HWSURFACE    1

/* Pixel format */
typedef struct {
    uint8_t rshift, gshift, bshift, ashift;
    uint8_t rloss, gloss, bloss, aloss;
} sdl_pixel_format_t;

typedef struct sdl_surface {
    uint32_t flags;
    uint32_t width, height;
    uint32_t pitch;
    uint32_t *pixels;
    uint32_t bpp;
    sdl_pixel_format_t format;
    void *userdata;
    uint32_t locked;
} sdl_surface_t;

typedef struct {
    int32_t x, y;
    uint32_t w, h;
} sdl_rect_t;

typedef struct {
    uint8_t r, g, b, a;
} sdl_color_t;

/* Core */
void sdl_init(void);
void sdl_quit(void);
sdl_surface_t *sdl_create_surface(uint32_t w, uint32_t h);
void sdl_free_surface(sdl_surface_t *s);
sdl_surface_t *sdl_get_screen(void);

/* Drawing */
void sdl_fill_rect(sdl_surface_t *s, const sdl_rect_t *rect, uint32_t color);
void sdl_blit_surface(sdl_surface_t *src, const sdl_rect_t *srcrect, sdl_surface_t *dst, const sdl_rect_t *dstrect);
void sdl_update_rect(sdl_surface_t *s, const sdl_rect_t *rect);
uint32_t sdl_map_rgb(sdl_pixel_format_t *fmt, uint8_t r, uint8_t g, uint8_t b);
uint32_t sdl_map_rgba(sdl_pixel_format_t *fmt, uint8_t r, uint8_t g, uint8_t b, uint8_t a);
void sdl_get_rgb(uint32_t pixel, sdl_pixel_format_t *fmt, uint8_t *r, uint8_t *g, uint8_t *b);

/* Primitives */
void sdl_draw_line(sdl_surface_t *s, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t color);
void sdl_draw_circle(sdl_surface_t *s, int32_t cx, int32_t cy, int32_t r, uint32_t color);
void sdl_fill_circle(sdl_surface_t *s, int32_t cx, int32_t cy, int32_t r, uint32_t color);
void sdl_draw_triangle(sdl_surface_t *s, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3, uint32_t color);

/* Text rendering */
void sdl_draw_text(sdl_surface_t *s, int32_t x, int32_t y, const char *text, uint32_t fg, uint32_t bg);

/* Events */
typedef struct {
    uint32_t type;
    uint32_t timestamp;
    union {
        struct { uint8_t key; uint16_t mod; } key;
        struct { int32_t x, y; int32_t dx, dy; uint8_t buttons; } motion;
        struct { int32_t x, y; uint8_t button; } button;
        struct { uint32_t w, h; } resize;
    } data;
} sdl_event_t;

int sdl_poll_event(sdl_event_t *event);
int sdl_wait_event(sdl_event_t *event);

#endif
