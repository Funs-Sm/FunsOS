/* custom_ui.c - Custom UI with renderer example
 * Demonstrates building custom UI using the FunRender engine.
 */

#include "funsos.h"

int main(void)
{
    funsos_window_t win = funsos_create_window(80, 50, 640, 480, "Custom UI Demo");
    funsos_fill_window(win, 0xFFFFFF);

    funsos_color_t black = {0x00, 0x00, 0x00, 0xFF};
    funsos_color_t white = {0xFF, 0xFF, 0xFF, 0xFF};
    funsos_color_t blue  = {0x00, 0x78, 0xD4, 0xFF};
    funsos_color_t gray  = {0xE0, 0xE0, 0xE0, 0xFF};

    /* Title */
    funsos_draw_text(win, 20, 15, "Custom UI Demo - FunRender Engine", blue);

    /* Simulated button */
    funsos_draw_rect(win, 20, 50, 120, 35, blue);
    funsos_draw_text(win, 45, 58, "Click Me", white);

    /* Simulated text input */
    funsos_draw_rect(win, 160, 50, 200, 35, gray);
    funsos_draw_rect(win, 160, 50, 200, 35, black);
    funsos_draw_text(win, 168, 58, "Type here...", black);

    /* Simulated checkbox */
    funsos_draw_rect(win, 20, 100, 18, 18, black);
    funsos_draw_text(win, 44, 100, "Enable feature A", black);

    /* Simulated checked checkbox */
    funsos_draw_rect(win, 20, 130, 18, 18, black);
    funsos_draw_line(win, 23, 109, 27, 113, blue);  /* Checkmark */
    funsos_draw_text(win, 44, 130, "Enable feature B", black);

    /* Simulated slider */
    funsos_draw_rect(win, 20, 170, 300, 6, gray);
    funsos_draw_rect(win, 20, 170, 180, 6, blue);
    funsos_draw_rect(win, 196, 162, 16, 22, blue);
    funsos_draw_text(win, 20, 185, "Volume: 60%", black);

    /* Simulated progress bar */
    funsos_draw_rect(win, 20, 220, 300, 20, gray);
    funsos_draw_rect(win, 20, 220, 210, 20, blue);
    funsos_draw_text(win, 130, 223, "70%", white);

    /* Simulated table */
    funsos_draw_rect(win, 20, 260, 400, 24, gray);
    funsos_draw_text(win, 30, 264, "Name", black);
    funsos_draw_text(win, 170, 264, "Value", black);
    funsos_draw_rect(win, 20, 284, 400, 24, white);
    funsos_draw_text(win, 30, 288, "Item 1", black);
    funsos_draw_text(win, 170, 288, "100", black);
    funsos_draw_rect(win, 20, 308, 400, 24, white);
    funsos_draw_text(win, 30, 312, "Item 2", black);
    funsos_draw_text(win, 170, 312, "200", black);

    /* Status bar */
    funsos_draw_rect(win, 0, 450, 640, 30, gray);
    funsos_draw_text(win, 10, 456, "Ready", black);
    funsos_draw_text(win, 550, 456, "FunRender v1.0", black);

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
