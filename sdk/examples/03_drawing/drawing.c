/* drawing.c - 2D Drawing example
 * Demonstrates the 2D graphics API: shapes, lines, circles.
 */

#include "funsos.h"

int main(void)
{
    funsos_window_t win = funsos_create_window(100, 50, 600, 450, "2D Drawing Demo");
    funsos_fill_window(win, 0x1A1A2E);

    /* Draw colored rectangles */
    funsos_color_t red   = {0xFF, 0x00, 0x00, 0xFF};
    funsos_color_t green = {0x00, 0xFF, 0x00, 0xFF};
    funsos_color_t blue  = {0x00, 0x00, 0xFF, 0xFF};
    funsos_color_t white = {0xFF, 0xFF, 0xFF, 0xFF};
    funsos_color_t yellow = {0xFF, 0xFF, 0x00, 0xFF};
    funsos_color_t cyan  = {0x00, 0xFF, 0xFF, 0xFF};

    /* Rectangles */
    funsos_draw_rect(win, 50, 50, 100, 80, red);
    funsos_draw_rect(win, 200, 50, 100, 80, green);
    funsos_draw_rect(win, 350, 50, 100, 80, blue);

    /* Lines */
    funsos_draw_line(win, 50, 200, 450, 200, white);
    funsos_draw_line(win, 50, 200, 250, 350, yellow);
    funsos_draw_line(win, 450, 200, 250, 350, cyan);

    /* Circles using gfx context */
    funsos_gfx_context_t *ctx = (funsos_gfx_context_t *)funsos_get_window_context(win);
    if (ctx) {
        funsos_draw_circle(ctx, 150, 300, 40, 0xFF0000);
        funsos_fill_circle(ctx, 300, 300, 40, 0x00FF00);
        funsos_draw_circle(ctx, 450, 300, 40, 0x0000FF);
        funsos_fill_circle(ctx, 150, 300, 20, 0xFF8000);
    }

    /* Title text */
    funsos_draw_text(win, 180, 10, "2D Drawing Demo", white);

    /* Event loop */
    funsos_event_t event;
    while (1) {
        if (funsos_wait_event(&event) != 0)
            continue;
        if (event.type == FUNSOS_EVENT_KEY_PRESS && event.key == 0x1B)
            break;
    }

    funsos_destroy_window(win);
    return 0;
}
