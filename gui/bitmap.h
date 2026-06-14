#ifndef BITMAP_H
#define BITMAP_H

#include "gfx.h"
#include "stdint.h"

typedef struct {
    uint16_t signature;
    uint32_t file_size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t data_offset;
} BMP_FILE_HEADER;

typedef struct {
    uint32_t header_size;
    int32_t width;
    int32_t height;
    uint16_t planes;
    uint16_t bits_per_pixel;
    uint32_t compression;
    uint32_t image_size;
    int32_t x_pixels_per_m;
    int32_t y_pixels_per_m;
    uint32_t colors_used;
    uint32_t colors_important;
} BMP_INFO_HEADER;

typedef struct {
    uint32_t *data;
    int32_t width;
    int32_t height;
    uint8_t bpp;
} bitmap_t;

bitmap_t *bitmap_create(int32_t width, int32_t height);
void bitmap_destroy(bitmap_t *bmp);
int bitmap_load_bmp(const uint8_t *raw_data, uint32_t size, bitmap_t *bmp);
void bitmap_draw(gfx_context_t *ctx, bitmap_t *bmp, int32_t x, int32_t y);
void bitmap_draw_scaled(gfx_context_t *ctx, bitmap_t *bmp, int32_t x, int32_t y, int32_t w, int32_t h);
gfx_color_t bitmap_get_pixel(bitmap_t *bmp, int32_t x, int32_t y);
void bitmap_set_pixel(bitmap_t *bmp, int32_t x, int32_t y, gfx_color_t color);

#endif
