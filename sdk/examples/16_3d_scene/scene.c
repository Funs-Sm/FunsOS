/* scene.c - Complex 3D scene example
 * Demonstrates a more complex 3D scene with multiple objects and lighting.
 */

#include "funsos.h"

int main(void)
{
    funsos_window_t win = funsos_create_window(50, 30, 800, 600, "3D Scene Demo");
    funsos_fill_window(win, 0x000000);

    funsos_gfx_context_t *ctx = (funsos_gfx_context_t *)funsos_get_window_context(win);
    funsos_3d_init(ctx);

    /* Camera setup */
    funsos_vec3_t eye    = {0.0f, 5.0f, 10.0f};
    funsos_vec3_t center = {0.0f, 0.0f, 0.0f};
    funsos_vec3_t up     = {0.0f, 1.0f, 0.0f};

    funsos_mat4_t view = funsos_3d_lookat(eye, center, up);
    funsos_mat4_t proj = funsos_3d_perspective(60.0f, 800.0f / 600.0f, 0.1f, 100.0f);

    /* Light setup */
    funsos_color_t white_c = {0xFF, 0xFF, 0xFF, 0xFF};
    funsos_color_t blue_c  = {0x00, 0x78, 0xD4, 0xFF};

    float angle = 0.0f;
    int running = 1;
    funsos_event_t event;

    while (running) {
        while (funsos_poll_event(&event) == 0) {
            if (event.type == FUNSOS_EVENT_KEY_PRESS && event.key == 0x1B)
                running = 0;
        }

        angle += 0.01f;

        /* Clear */
        funsos_3d_clear_depth();
        funsos_fill_window(win, 0x1A1A2E);

        /* Draw ground plane */
        funsos_mat4_t ground_model = funsos_3d_translate(0.0f, -1.0f, 0.0f);
        funsos_mat4_t ground_mvp = funsos_3d_mul_matrix(proj, funsos_3d_mul_matrix(view, ground_model));

        funsos_vertex3d_t ground_verts[] = {
            {{-10, -1, -10}, {0, 1, 0}, 0x404040},
            {{ 10, -1, -10}, {0, 1, 0}, 0x404040},
            {{ 10, -1,  10}, {0, 1, 0}, 0x404040},
            {{-10, -1,  10}, {0, 1, 0}, 0x404040},
        };
        funsos_3d_render(ground_verts, 4, ground_mvp, 0);

        /* Rotating cube */
        funsos_mat4_t cube_model = funsos_3d_mul_matrix(
            funsos_3d_translate(0.0f, 1.0f, 0.0f),
            funsos_3d_rotate_y(angle)
        );
        funsos_mat4_t cube_mvp = funsos_3d_mul_matrix(proj, funsos_3d_mul_matrix(view, cube_model));

        funsos_vertex3d_t cube_verts[] = {
            {{-1, 0, -1}, {0, 0, -1}, 0xFF0000},
            {{ 1, 0, -1}, {0, 0, -1}, 0x00FF00},
            {{ 1, 2, -1}, {0, 0, -1}, 0x0000FF},
            {{-1, 2, -1}, {0, 0, -1}, 0xFFFF00},
            {{-1, 0,  1}, {0, 0,  1}, 0xFF00FF},
            {{ 1, 0,  1}, {0, 0,  1}, 0x00FFFF},
            {{ 1, 2,  1}, {0, 0,  1}, 0xFFFFFF},
            {{-1, 2,  1}, {0, 0,  1}, 0x808080},
        };
        funsos_3d_render(cube_verts, 8, cube_mvp, 0);

        /* Second object - offset */
        funsos_mat4_t obj2_model = funsos_3d_translate(4.0f, 0.5f, -2.0f);
        funsos_mat4_t obj2_mvp = funsos_3d_mul_matrix(proj, funsos_3d_mul_matrix(view, obj2_model));

        funsos_vertex3d_t obj2_verts[] = {
            {{-0.5f, 0, -0.5f}, {0, 1, 0}, 0x0078D4},
            {{ 0.5f, 0, -0.5f}, {0, 1, 0}, 0x0078D4},
            {{ 0.5f, 0,  0.5f}, {0, 1, 0}, 0x0078D4},
            {{-0.5f, 0,  0.5f}, {0, 1, 0}, 0x0078D4},
        };
        funsos_3d_render(obj2_verts, 4, obj2_mvp, 0);

        /* HUD */
        funsos_draw_text(win, 10, 10, "3D Scene - Press ESC to exit", white_c);

        funsos_sleep(16);
    }

    funsos_destroy_window(win);
    return 0;
}
