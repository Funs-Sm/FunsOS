/* imgview.c - Image viewer example
 * Demonstrates loading and displaying images on FUNSOS.
 */

#include "funsos.h"

int main(void)
{
    funsos_window_t win = funsos_create_window(80, 40, 640, 480, "Image Viewer");
    funsos_fill_window(win, 0x2D2D2D);

    funsos_color_t white = {0xFF, 0xFF, 0xFF, 0xFF};
    funsos_color_t gray  = {0x80, 0x80, 0x80, 0xFF};
    funsos_color_t blue  = {0x00, 0x78, 0xD4, 0xFF};

    /* Toolbar */
    funsos_draw_rect(win, 0, 0, 640, 32, 0x3C3C3C);
    funsos_draw_text(win, 10, 8, "Open  |  Zoom+  |  Zoom-  |  Fit  |  Rotate", gray);

    /* Image area - placeholder */
    funsos_gfx_context_t *ctx = (funsos_gfx_context_t *)funsos_get_window_context(win);

    /* Draw a test pattern */
    if (ctx) {
        for (int y = 50; y < 450; y++) {
            for (int x = 50; x < 590; x++) {
                uint32_t color = ((x - 50) * 255 / 540) << 16 |
                                 ((y - 50) * 255 / 400) << 8 |
                                 0x80;
                if (x >= 0 && x < (int)ctx->width && y >= 0 && y < (int)ctx->height) {
                    ctx->buffer[y * ctx->width + x] = color;
                }
            }
        }
    }

    /* Status bar */
    funsos_draw_rect(win, 0, 455, 640, 25, 0x3C3C3C);
    funsos_draw_text(win, 10, 460, "Image: test.png | 540x400 | Zoom: 100%", gray);

    /* Instructions */
    funsos_draw_text(win, 200, 30, "Press O to open, ESC to quit", blue);

    /* Event loop */
    funsos_event_t event;
    while (1) {
        if (funsos_wait_event(&event) != 0)
            continue;

        if (event.type == FUNSOS_EVENT_KEY_PRESS) {
            if (event.key == 0x1B) break;
            if (event.key == 'o') {
                /* Open file dialog - simplified */
                int fd = funsos_file_open("/home/user/image.png", 0);
                if (fd >= 0) {
                    funsos_file_close(fd);
                }
            }
        }
    }

    funsos_destroy_window(win);
    return 0;
}
