/* window.c - Window creation example
 * Demonstrates creating and managing multiple windows.
 */

#include "funsos.h"

int main(void)
{
    /* Create main window */
    funsos_window_t main_win = funsos_create_window(50, 50, 500, 400, "Main Window");
    funsos_fill_window(main_win, 0xFFFFFF);

    /* Create secondary window */
    funsos_window_t sec_win = funsos_create_window(300, 100, 350, 250, "Secondary Window");
    funsos_fill_window(sec_win, 0xF0F0F0);

    /* Draw labels */
    funsos_color_t black = {0x00, 0x00, 0x00, 0xFF};
    funsos_draw_text(main_win, 20, 20, "This is the main window", black);
    funsos_draw_text(sec_win, 20, 20, "This is a secondary window", black);

    /* Move and resize windows */
    funsos_move_window(main_win, 100, 80);
    funsos_resize_window(sec_win, 400, 300);

    /* Event loop */
    funsos_event_t event;
    while (1) {
        if (funsos_wait_event(&event) != 0)
            continue;

        if (event.type == FUNSOS_EVENT_KEY_PRESS && event.key == 0x1B)
            break;
    }

    funsos_destroy_window(main_win);
    funsos_destroy_window(sec_win);
    return 0;
}
