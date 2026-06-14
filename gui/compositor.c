#include "compositor.h"
#include "wm.h"
#include "cursor.h"
#include "kheap.h"
#include "string.h"

static gfx_context_t front_buffer;
static gfx_context_t back_buffer;
static uint8_t *dirty_regions = 0;
static uint32_t tiles_x = 0;
static uint32_t tiles_y = 0;
static fr_render_handle_t fr_ctx = 0;

#define TILE_SIZE 32

void compositor_init(uint32_t *fb, uint32_t w, uint32_t h) {
    gfx_init(&front_buffer, fb, w, h, w * 4, 32);

    uint32_t *bb = (uint32_t *)kmalloc(w * h * 4);
    if (bb) {
        memset(bb, 0, w * h * 4);
    }
    gfx_init(&back_buffer, bb, w, h, w * 4, 32);

    tiles_x = (w + TILE_SIZE - 1) / TILE_SIZE;
    tiles_y = (h + TILE_SIZE - 1) / TILE_SIZE;
    dirty_regions = (uint8_t *)kmalloc(tiles_x * tiles_y);
    if (dirty_regions) {
        memset(dirty_regions, 0, tiles_x * tiles_y);
    }

    wm_init(w, h);
    cursor_init();
}

void compositor_render(void) {
    if (!back_buffer.buffer) return;

    gfx_fill_rect(&back_buffer, (gfx_rect_t){0, 0, (int32_t)back_buffer.width, (int32_t)back_buffer.height}, COLOR_BLACK);

    wm_render(&back_buffer);

    compositor_mark_dirty((gfx_rect_t){0, 0, (int32_t)back_buffer.width, (int32_t)back_buffer.height});
}

void compositor_swap_buffers(void) {
    if (!dirty_regions || !front_buffer.buffer || !back_buffer.buffer) return;

    for (uint32_t ty = 0; ty < tiles_y; ty++) {
        for (uint32_t tx = 0; tx < tiles_x; tx++) {
            if (dirty_regions[ty * tiles_x + tx]) {
                int32_t x0 = tx * TILE_SIZE;
                int32_t y0 = ty * TILE_SIZE;
                int32_t x1 = x0 + TILE_SIZE;
                int32_t y1 = y0 + TILE_SIZE;
                if (x1 > (int32_t)front_buffer.width) x1 = front_buffer.width;
                if (y1 > (int32_t)front_buffer.height) y1 = front_buffer.height;

                for (int32_t y = y0; y < y1; y++) {
                    uint32_t src_off = y * (back_buffer.pitch / 4) + x0;
                    uint32_t dst_off = y * (front_buffer.pitch / 4) + x0;
                    uint32_t count = (x1 - x0) * 4;
                    memcpy(&front_buffer.buffer[dst_off], &back_buffer.buffer[src_off], count);
                }
            }
        }
    }

    memset(dirty_regions, 0, tiles_x * tiles_y);
}

void compositor_mark_dirty(gfx_rect_t rect) {
    if (!dirty_regions) return;

    int32_t tx0 = rect.x / TILE_SIZE;
    int32_t ty0 = rect.y / TILE_SIZE;
    int32_t tx1 = (rect.x + rect.w + TILE_SIZE - 1) / TILE_SIZE;
    int32_t ty1 = (rect.y + rect.h + TILE_SIZE - 1) / TILE_SIZE;

    if (tx0 < 0) tx0 = 0;
    if (ty0 < 0) ty0 = 0;
    if (tx1 > (int32_t)tiles_x) tx1 = tiles_x;
    if (ty1 > (int32_t)tiles_y) ty1 = tiles_y;

    for (int32_t ty = ty0; ty < ty1; ty++) {
        for (int32_t tx = tx0; tx < tx1; tx++) {
            dirty_regions[ty * tiles_x + tx] = 1;
        }
    }
}

gfx_context_t *compositor_get_back_buffer(void) {
    return &back_buffer;
}

/* ---- Bridge to FunRender rendering pipeline ---- */

void compositor_set_fr_context(fr_render_handle_t ctx) {
    fr_ctx = ctx;
}

fr_render_handle_t compositor_get_fr_context(void) {
    return fr_ctx;
}

int compositor_render_to_funrender(fr_render_handle_t ctx) {
    if (!ctx || !back_buffer.buffer) return -1;

    /* Render old compositor output to back buffer first */
    gfx_fill_rect(&back_buffer, (gfx_rect_t){0, 0, (int32_t)back_buffer.width, (int32_t)back_buffer.height}, COLOR_BLACK);
    wm_render(&back_buffer);

    /* Copy back buffer content to FunRender context */
    if (fr_ctx) {
        /* In production: fr_context_upload_texture(fr_ctx, back_buffer.buffer, ...) */
    }

    return 0;
}

void compositor_sync_with_funrender(void) {
    if (!fr_ctx) return;

    /* Sync compositor state with FunRender */
    /* Re-render using FunRender pipeline */
    compositor_render_to_funrender(fr_ctx);
}
