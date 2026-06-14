#include "bitmap.h"
#include "kheap.h"
#include "string.h"

bitmap_t *bitmap_create(int32_t width, int32_t height) {
    bitmap_t *bmp = (bitmap_t *)kmalloc(sizeof(bitmap_t));
    if (!bmp) return 0;
    bmp->width = width;
    bmp->height = height;
    bmp->bpp = 32;
    bmp->data = (uint32_t *)kmalloc(width * height * 4);
    if (!bmp->data) {
        kfree(bmp);
        return 0;
    }
    memset(bmp->data, 0, width * height * 4);
    return bmp;
}

void bitmap_destroy(bitmap_t *bmp) {
    if (!bmp) return;
    if (bmp->data) kfree(bmp->data);
    kfree(bmp);
}

int bitmap_load_bmp(const uint8_t *raw_data, uint32_t size, bitmap_t *bmp) {
    if (size < 14 + 40) return -1;

    BMP_FILE_HEADER *fh = (BMP_FILE_HEADER *)raw_data;
    if (fh->signature != 0x4D42) return -1;

    BMP_INFO_HEADER *ih = (BMP_INFO_HEADER *)(raw_data + 14);
    if (ih->bits_per_pixel != 24 && ih->bits_per_pixel != 32) return -1;
    if (ih->compression != 0) return -1;

    bmp->width = ih->width;
    bmp->height = ih->height < 0 ? -ih->height : ih->height;
    bmp->bpp = ih->bits_per_pixel;

    if (bmp->data) kfree(bmp->data);
    bmp->data = (uint32_t *)kmalloc(bmp->width * bmp->height * 4);
    if (!bmp->data) return -1;

    int32_t bottom_up = ih->height > 0;
    int32_t eff_height = bottom_up ? ih->height : -ih->height;
    uint32_t row_size = ((ih->bits_per_pixel * bmp->width + 31) / 32) * 4;
    const uint8_t *pixel_data = raw_data + fh->data_offset;

    for (int32_t y = 0; y < eff_height; y++) {
        int32_t dst_y = bottom_up ? (eff_height - 1 - y) : y;
        const uint8_t *row = pixel_data + y * row_size;
        for (int32_t x = 0; x < bmp->width; x++) {
            uint8_t b = row[x * (ih->bits_per_pixel / 8) + 0];
            uint8_t g = row[x * (ih->bits_per_pixel / 8) + 1];
            uint8_t r = row[x * (ih->bits_per_pixel / 8) + 2];
            uint8_t a = (ih->bits_per_pixel == 32) ? row[x * 4 + 3] : 0xFF;
            gfx_color_t color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            (void)a;
            bmp->data[dst_y * bmp->width + x] = color;
        }
    }

    return 0;
}

void bitmap_draw(gfx_context_t *ctx, bitmap_t *bmp, int32_t x, int32_t y) {
    if (!bmp || !bmp->data) return;
    for (int32_t by = 0; by < bmp->height; by++) {
        for (int32_t bx = 0; bx < bmp->width; bx++) {
            gfx_color_t pixel = bmp->data[by * bmp->width + bx];
            gfx_set_pixel(ctx, x + bx, y + by, pixel);
        }
    }
}

void bitmap_draw_scaled(gfx_context_t *ctx, bitmap_t *bmp, int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!bmp || !bmp->data) return;
    for (int32_t dy = 0; dy < h; dy++) {
        for (int32_t dx = 0; dx < w; dx++) {
            int32_t sx = (dx * bmp->width) / w;
            int32_t sy = (dy * bmp->height) / h;
            if (sx >= bmp->width) sx = bmp->width - 1;
            if (sy >= bmp->height) sy = bmp->height - 1;
            gfx_color_t pixel = bmp->data[sy * bmp->width + sx];
            gfx_set_pixel(ctx, x + dx, y + dy, pixel);
        }
    }
}

gfx_color_t bitmap_get_pixel(bitmap_t *bmp, int32_t x, int32_t y) {
    if (!bmp || !bmp->data) return 0;
    if (x < 0 || x >= bmp->width || y < 0 || y >= bmp->height) return 0;
    return bmp->data[y * bmp->width + x];
}

void bitmap_set_pixel(bitmap_t *bmp, int32_t x, int32_t y, gfx_color_t color) {
    if (!bmp || !bmp->data) return;
    if (x < 0 || x >= bmp->width || y < 0 || y >= bmp->height) return;
    bmp->data[y * bmp->width + x] = color;
}
