/* signals.c - Signal handling example
 * Demonstrates POSIX signal handling on FUNSOS.
 */

#include "funsos.h"

/* Signal handler */
static void sig_handler(int sig)
{
    (void)sig;
    /* In a real application, handle the signal here */
}

int main(void)
{
    funsos_window_t win = funsos_create_window(120, 80, 500, 350, "Signal Demo");
    funsos_fill_window(win, 0xFFFFFF);

    funsos_color_t black = {0x00, 0x00, 0x00, 0xFF};
    funsos_color_t blue  = {0x00, 0x00, 0xFF, 0xFF};
    funsos_color_t red   = {0xFF, 0x00, 0x00, 0xFF};

    funsos_draw_text(win, 20, 20, "Signal Handling Demo", blue);

    /* Register signal handlers */
    funsos_signal(FUNSOS_SIGINT, sig_handler);
    funsos_signal(FUNSOS_SIGTERM, sig_handler);
    funsos_signal(FUNSOS_SIGUSR1, sig_handler);

    funsos_draw_text(win, 20, 60, "Registered handlers for:", black);
    funsos_draw_text(win, 40, 85, "SIGINT  (Ctrl+C)", black);
    funsos_draw_text(win, 40, 110, "SIGTERM (terminate)", black);
    funsos_draw_text(win, 40, 135, "SIGUSR1 (user signal)", black);

    funsos_draw_text(win, 20, 180, "Press K to send SIGUSR1 to self", blue);
    funsos_draw_text(win, 20, 210, "Press T to send SIGTERM to self", red);

    /* Event loop */
    funsos_event_t event;
    while (1) {
        if (funsos_wait_event(&event) != 0)
            continue;

        if (event.type == FUNSOS_EVENT_KEY_PRESS) {
            uint32_t pid = funsos_get_pid();

            switch (event.key) {
            case 0x1B:
                funsos_destroy_window(win);
                return 0;
            case 'k':
                funsos_kill(pid, FUNSOS_SIGUSR1);
                funsos_draw_text(win, 20, 250, "SIGUSR1 sent!", black);
                break;
            case 't':
                funsos_kill(pid, FUNSOS_SIGTERM);
                funsos_draw_text(win, 20, 250, "SIGTERM sent!", red);
                break;
            }
        }
    }
}
