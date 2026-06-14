#include "cursor.h"
#include "gfx.h"

static uint32_t cursor_type = CURSOR_ARROW;

static const uint8_t cursor_arrow_mask[CURSOR_HEIGHT] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF
};

static const uint8_t cursor_arrow_data[CURSOR_HEIGHT] = {
    0x00, 0x40, 0x60, 0x70, 0x78, 0x7C, 0x7E, 0x7F,
    0x7C, 0x6C, 0x46, 0x06, 0x06, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};

static const uint8_t cursor_ibeam_mask[CURSOR_HEIGHT] = {
    0xFE, 0xFF, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFE,
    0xFF, 0xFE, 0x00, 0x00
};

static const uint8_t cursor_ibeam_data[CURSOR_HEIGHT] = {
    0x01, 0x00, 0x01, 0x18, 0x18, 0x18, 0x18, 0x18,
    0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x01,
    0x00, 0x01, 0x00, 0x00
};

static const uint8_t cursor_cross_mask[CURSOR_HEIGHT] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF
};

static const uint8_t cursor_cross_data[CURSOR_HEIGHT] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00,
    0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00
};

static const uint8_t cursor_hand_mask[CURSOR_HEIGHT] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF
};

static const uint8_t cursor_hand_data[CURSOR_HEIGHT] = {
    0x00, 0x00, 0x00, 0x00, 0x60, 0x78, 0x78, 0x78,
    0x78, 0x78, 0x78, 0x78, 0x78, 0x7E, 0x7E, 0x00,
    0x00, 0x00, 0x00, 0x00
};

void cursor_init(void) {
    cursor_type = CURSOR_ARROW;
}

void cursor_draw(gfx_context_t *ctx, int32_t x, int32_t y) {
    const uint8_t *mask = 0;
    const uint8_t *data = 0;

    switch (cursor_type) {
        case CURSOR_ARROW:
            mask = cursor_arrow_mask;
            data = cursor_arrow_data;
            break;
        case CURSOR_IBEAM:
            mask = cursor_ibeam_mask;
            data = cursor_ibeam_data;
            break;
        case CURSOR_CROSS:
            mask = cursor_cross_mask;
            data = cursor_cross_data;
            break;
        case CURSOR_HAND:
            mask = cursor_hand_mask;
            data = cursor_hand_data;
            break;
        default:
            mask = cursor_arrow_mask;
            data = cursor_arrow_data;
            break;
    }

    for (int32_t row = 0; row < CURSOR_HEIGHT; row++) {
        for (int32_t col = 0; col < CURSOR_WIDTH; col++) {
            uint8_t m = mask[row];
            uint8_t d = data[row];
            int32_t px = x + col;
            int32_t py = y + row;

            if (px < 0 || px >= (int32_t)ctx->width || py < 0 || py >= (int32_t)ctx->height) continue;

            uint8_t mask_bit = m & (0x80 >> col);
            uint8_t data_bit = d & (0x80 >> col);

            if (!mask_bit && data_bit) {
                gfx_set_pixel(ctx, px, py, COLOR_BLACK);
            } else if (!mask_bit && !data_bit) {
                gfx_set_pixel(ctx, px, py, COLOR_WHITE);
            } else if (mask_bit && data_bit) {
                gfx_color_t orig = gfx_get_pixel(ctx, px, py);
                gfx_color_t inv = (~orig) & 0xFFFFFF;
                gfx_set_pixel(ctx, px, py, inv);
            }
        }
    }
}

void cursor_set_type(uint32_t type) {
    if (type <= CURSOR_HAND) cursor_type = type;
}

uint32_t cursor_get_type(void) {
    return cursor_type;
}
