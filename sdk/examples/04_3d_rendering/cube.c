/* cube.c - 3D Rendering example
 * Demonstrates 3D cube rendering with rotation.
 */

#include "funsos.h"

int main(void)
{
    funsos_window_t win = funsos_create_window(100, 50, 640, 480, "3D Cube Demo");
    funsos_fill_window(win, 0x000000);

    /* Initialize 3D context */
    funsos_gfx_context_t *ctx = (funsos_gfx_context_t *)funsos_get_window_context(win);
    funsos_3d_init(ctx);

    /* Set up camera */
    funsos_vec3_t eye    = {0.0f, 2.0f, 5.0f};
    funsos_vec3_t center = {0.0f, 0.0f, 0.0f};
    funsos_vec3_t up     = {0.0f, 1.0f, 0.0f};

    funsos_mat4_t view = funsos_3d_lookat(eye, center, up);
    funsos_mat4_t proj = funsos_3d_perspective(60.0f, 640.0f / 480.0f, 0.1f, 100.0f);

    /* Create cube mesh */
    funsos_vertex3d_t vertices[8] = {
        {{-1, -1, -1}, {0, 0, -1}, 0xFF0000},
        {{ 1, -1, -1}, {0, 0, -1}, 0x00FF00},
        {{ 1,  1, -1}, {0, 0, -1}, 0x0000FF},
        {{-1,  1, -1}, {0, 0, -1}, 0xFFFF00},
        {{-1, -1,  1}, {0, 0,  1}, 0xFF00FF},
        {{ 1, -1,  1}, {0, 0,  1}, 0x00FFFF},
        {{ 1,  1,  1}, {0, 0,  1}, 0xFFFFFF},
        {{-1,  1,  1}, {0, 0,  1}, 0x808080},
    };

    float angle = 0.0f;

    /* Main loop */
    funsos_event_t event;
    int running = 1;

    while (running) {
        /* Process events */
        while (funsos_poll_event(&event) == 0) {
            if (event.type == FUNSOS_EVENT_KEY_PRESS && event.key == 0x1B)
                running = 0;
        }

        /* Update rotation */
        angle += 0.02f;
        funsos_mat4_t model = funsos_3d_rotate_y(angle);
        funsos_mat4_t mvp = funsos_3d_mul_matrix(proj, funsos_3d_mul_matrix(view, model));

        /* Render */
        funsos_3d_clear_depth();
        funsos_fill_window(win, 0x000000);
        funsos_3d_render(vertices, 8, mvp, 0);

        /* Wait for next frame */
        funsos_sleep(16);
    }

    funsos_destroy_window(win);
    return 0;
}
