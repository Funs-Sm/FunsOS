/* game.c - Game development example
 * A simple bouncing ball game demonstrating game loop and input.
 */

#include "funsos.h"

int main(void)
{
    funsos_window_t win = funsos_create_window(100, 50, 640, 480, "Bouncing Ball Game");
    funsos_fill_window(win, 0x000000);

    /* Ball state */
    float ball_x = 300.0f, ball_y = 200.0f;
    float ball_vx = 3.0f, ball_vy = 2.0f;
    int ball_r = 12;

    /* Paddle state */
    int paddle_x = 270, paddle_y = 440;
    int paddle_w = 100, paddle_h = 12;

    /* Score */
    int score = 0;

    funsos_color_t white = {0xFF, 0xFF, 0xFF, 0xFF};
    funsos_color_t blue  = {0x00, 0x78, 0xD4, 0xFF};
    funsos_color_t green = {0x00, 0xFF, 0x00, 0xFF};

    /* Game loop */
    funsos_event_t event;
    int running = 1;

    while (running) {
        /* Process input */
        while (funsos_poll_event(&event) == 0) {
            if (event.type == FUNSOS_EVENT_KEY_PRESS) {
                switch (event.key) {
                case 0x1B: running = 0; break;
                case 0x25: paddle_x -= 20; break;  /* Left */
                case 0x27: paddle_x += 20; break;  /* Right */
                }
            }
            if (event.type == FUNSOS_EVENT_MOUSE_MOVE) {
                paddle_x = (int)event.mouse_x - paddle_w / 2;
            }
        }

        /* Update ball position */
        ball_x += ball_vx;
        ball_y += ball_vy;

        /* Wall collisions */
        if (ball_x <= ball_r || ball_x >= 640 - ball_r) ball_vx = -ball_vx;
        if (ball_y <= ball_r) ball_vy = -ball_vy;

        /* Paddle collision */
        if (ball_y + ball_r >= paddle_y &&
            ball_x >= paddle_x && ball_x <= paddle_x + paddle_w) {
            ball_vy = -ball_vy;
            score++;
        }

        /* Ball out of bounds */
        if (ball_y > 480) {
            ball_x = 300; ball_y = 200;
            ball_vx = 3; ball_vy = 2;
            score = 0;
        }

        /* Clamp paddle */
        if (paddle_x < 0) paddle_x = 0;
        if (paddle_x > 640 - paddle_w) paddle_x = 640 - paddle_w;

        /* Render */
        funsos_fill_window(win, 0x000000);

        /* Ball */
        funsos_gfx_context_t *ctx = (funsos_gfx_context_t *)funsos_get_window_context(win);
        if (ctx) {
            funsos_fill_circle(ctx, (int)ball_x, (int)ball_y, ball_r, 0x0078D4);
        }

        /* Paddle */
        funsos_draw_rect(win, paddle_x, paddle_y, paddle_w, paddle_h, green);

        /* Score */
        funsos_draw_text(win, 10, 10, "Score:", white);

        /* Frame delay */
        funsos_sleep(16);
    }

    funsos_destroy_window(win);
    return 0;
}
