/* hello.c - Hello World example
 * The simplest FUNSOS application.
 */

#include "funsos.h"

int main(void)
{
    /* Create a window */
    funsos_window_t win = funsos_create_window(200, 150, 400, 300, "Hello World");
    if (win == NULL)
        return 1;

    /* Fill window with white background */
    funsos_fill_window(win, 0xFFFFFF);

    /* Draw greeting text */
    funsos_color_t black = {0x00, 0x00, 0x00, 0xFF};
    funsos_draw_text(win, 120, 120, "Hello, FUNSOS!", black);
    funsos_draw_text(win, 100, 160, "Welcome to the SDK!", black);

    /* Event loop */
    funsos_event_t event;
    while (1) {
        if (funsos_wait_event(&event) != 0)
            continue;

        if (event.type == FUNSOS_EVENT_WINDOW_CLOSE)
            break;

        if (event.type == FUNSOS_EVENT_KEY_PRESS) {
            if (event.key == 0x1B)  /* Escape */
                break;
        }
    }

    funsos_destroy_window(win);
    return 0;
}
