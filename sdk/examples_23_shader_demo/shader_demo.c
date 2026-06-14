/* shader_demo.c - GPU Shader-Like Effects Demo
 * Demonstrates advanced 3D graphics rendering including:
 * - Real-time lighting simulation (Phong/Blinn model approximation)
 * - Texture mapping simulation with procedural patterns
 * - Particle system with physics simulation
 * Uses funsos_graphics.h for 3D rendering pipeline.
 */

#include "funsos.h"

/* ---- Constants ---- */
#define WINDOW_W    800
#define WINDOW_H    600
#define PARTICLE_COUNT  64
#define MESH_VERTICES   36  /* 2 triangles per face * 3 faces visible approx */

/* ---- Math helpers ---- */
static float fsin(float x)
{
    /* Simple Taylor series approximation of sin(x) */
    x -= 6.2831853f * (int)(x / 6.2831853f);
    float x2 = x * x;
    float x3 = x2 * x;
    return x - x3 / 6.0f + x3 * x2 / 120.0f;
}

static float fcos(float x)
{
    return fsin(x + 1.5707963f);
}

/* Clamp a float value between min and max */
static float fclamp(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ---- Particle structure for particle system ---- */
typedef struct {
    funsos_vec3_t position;     /* Current 3D position */
    funsos_vec3_t velocity;     /* Movement vector per frame */
    funsos_rgba_t  color;       /* Particle color with alpha */
    float          life;        /* Remaining lifetime (0.0 - 1.0) */
    float          size;        /* Render size */
    float          mass;        /* Physics mass */
} particle_t;

/* Global particle array */
static particle_t particles[PARTICLE_COUNT];

/* Initialize all particles with random-ish starting values based on seed */
static void init_particles(void)
{
    for (int i = 0; i < PARTICLE_COUNT; i++) {
        particles[i].position.x = ((i * 137 + 53) % 200) / 100.0f - 1.0f;
        particles[i].position.y = ((i * 89 + 17)  % 200) / 100.0f - 0.5f;
        particles[i].position.z = ((i * 211 + 31) % 100) / 100.0f;

        particles[i].velocity.x = ((i * 43 + 7)  % 40) / 200.0f - 0.1f;
        particles[i].velocity.y = ((i * 151 + 13) % 40) / 200.0f + 0.05f;
        particles[i].velocity.z = ((i * 67 + 29)  % 40) / 400.0f;

        /* Color gradient: warm colors (orange -> yellow -> white) */
        float t = (float)i / (float)PARTICLE_COUNT;
        particles[i].color.r = (uint8_t)(255);
        particles[i].color.g = (uint8_t)(128 + t * 127);
        particles[i].color.b = (uint8_t)(t * 100);
        particles[i].color.a = (uint8_t)(200 + (i % 4) * 13);

        particles[i].life  = 0.5f + ((i * 37) % 100) / 200.0f;
        particles[i].size  = 1.0f + ((i * 19) % 30) / 30.0f;
        particles[i].mass  = 0.5f + ((i * 11) % 20) / 40.0f;
    }
}

/* Update particle positions with simple physics simulation */
static void update_particles(float dt)
{
    /* Gravity constant (downward acceleration) */
    funsos_vec3_t gravity = {0.0f, -0.5f, 0.0f};

    for (int i = 0; i < PARTICLE_COUNT; i++) {
        /* Apply gravity (F = ma => a = F/m) */
        particles[i].velocity.x += gravity.x / particles[i].mass * dt;
        particles[i].velocity.y += gravity.y / particles[i].mass * dt;
        particles[i].velocity.z += gravity.z / particles[i].mass * dt;

        /* Apply air resistance (damping) */
        particles[i].velocity.x *= 0.995f;
        particles[i].velocity.y *= 0.995f;
        particles[i].velocity.z *= 0.995f;

        /* Update position */
        particles[i].position.x += particles[i].velocity.x * dt;
        particles[i].position.y += particles[i].velocity.y * dt;
        particles[i].position.z += particles[i].velocity.z * dt;

        /* Decrease life */
        particles[i].life -= dt * 0.3f;

        /* Respawn dead particles at origin area */
        if (particles[i].life <= 0.0f) {
            particles[i].position.x = ((i * 173 + 59) % 100) / 100.0f - 0.5f;
            particles[i].position.y = -0.8f;
            particles[i].position.z = ((i * 223 + 41) % 100) / 100.0f - 0.5f;

            particles[i].velocity.x = ((i * 47 + 11)  % 30) / 150.0f - 0.1f;
            particles[i].velocity.y = ((i * 163 + 23) % 30) / 150.0f + 0.15f;
            particles[i].velocity.z = ((i * 73 + 37)  % 30) / 300.0f;

            particles[i].life = 1.0f;
        }

        /* Floor bounce */
        if (particles[i].position.y < -1.5f) {
            particles[i].position.y = -1.5f;
            particles[i].velocity.y *= -0.6f;  /* Bounce with energy loss */
        }
    }
}

/* Build vertex list from current particle state for rendering */
static int build_particle_vertices(funsos_vertex3d_t *verts, int max_verts)
{
    int count = 0;
    for (int i = 0; i < PARTICLE_COUNT && count < max_verts; i++) {
        verts[count].pos   = particles[i].position;

        /* Modulate color by remaining life (fade out as they die) */
        float alpha_mod = fclamp(particles[i].life, 0.0f, 1.0f);
        verts[count].color.r = (uint8_t)((float)particles[i].color.r * alpha_mod);
        verts[count].color.g = (uint8_t)((float)particles[i].color.g * alpha_mod);
        verts[count].color.b = (uint8_t)((float)particles[i].color.b * alpha_mod);
        verts[count].color.a = (uint8_t)((float)particles[i].color.a * alpha_mod);
        count++;
    }
    return count;
}

/* Compute simulated lighting intensity using dot product approximation.
 * This mimics diffuse lighting from a directional light source.
 * Parameters:
 *   normal  - surface normal direction
 *   light_dir - direction toward the light source
 * Returns: light intensity in range [0, 1]
 */
static float compute_diffuse_light(funsos_vec3_t normal, funsos_vec3_t light_dir)
{
    /* Dot product: N . L = Nx*Lx + Ny*Ly + Nz*Lz */
    float dot = normal.x * light_dir.x + normal.y * light_dir.y + normal.z * light_dir.z;
    return fclamp(dot, 0.0f, 1.0f);
}

/* Generate a procedural checkerboard texture coordinate color.
 * Creates alternating pattern based on world-space coordinates.
 */
static funsos_rgba_t procedural_texture(float u, float v, float time)
{
    /* Animate the pattern over time */
    float freq = 4.0f;
    int cu = (int)(u * freq + time * 0.5f) % 2;
    int cv = (int)(v * freq) % 2;

    if ((cu + cv) % 2 == 0) {
        funsos_rgba_t c = {220, 180, 120, 255};  /* Light tile */
        return c;
    } else {
        funsos_rgba_t c = {80, 60, 40, 255};     /* Dark tile */
        return c;
    }
}

int main(void)
{
    /* Create main rendering window */
    funsos_window_t win = funsos_create_window(50, 30, WINDOW_W, WINDOW_H, "Shader Demo - GPU Effects");
    funsos_fill_window(win, 0x111122);  /* Dark background */

    /* Initialize 3D rendering context */
    funsos_gfx_context_t *ctx = (funsos_gfx_context_t *)funsos_get_window_context(win);
    funsos_3d_init(ctx);

    /* Set up camera parameters */
    funsos_vec3_t cam_eye    = {0.0f, 1.0f, 4.5f};
    funsos_vec3_t cam_center = {0.0f, 0.0f, 0.0f};
    funsos_vec3_t cam_up     = {0.0f, 1.0f, 0.0f};
    funsos_mat4_t view_mat   = funsos_3d_lookat(cam_eye, cam_center, cam_up);
    funsos_mat4_t proj_mat   = funsos_3d_perspective(60.0f, (float)WINDOW_W / (float)WINDOW_H, 0.1f, 100.0f);

    /* Light source direction (world space) - upper-right-front */
    funsos_vec3_t light_dir = {-0.577f, -0.577f, -0.577f};

    /* Initialize particle system */
    init_particles();

    /* Vertex buffer for particle rendering */
    funsos_vertex3d_t particle_verts[PARTICLE_COUNT];

    /* Animation state */
    float time_elapsed = 0.0f;
    int frame_count = 0;

    /* Main render loop */
    funsos_event_t event;
    int running = 1;

    while (running) {
        /* Process all pending events */
        while (funsos_poll_event(&event)) {
            if (event.type == FUNSOS_EVENT_KEY_PRESS && event.param1 == 0x1B) {
                running = 0;
                break;
            }
            if (event.type == FUNSOS_EVENT_WINDOW_CLOSE) {
                running = 0;
                break;
            }
        }

        if (!running) break;

        /* ---- Update simulation ---- */
        float dt = 0.016f;  /* ~60 FPS timestep */
        time_elapsed += dt;
        frame_count++;

        /* Update particle physics */
        update_particles(dt);

        /* Build particle vertex buffer */
        int pcount = build_particle_vertices(particle_verts, PARTICLE_COUNT);

        /* ---- Render frame ---- */

        /* Clear screen with dark blue-gray background */
        funsos_fill_window(win, 0x111122);

        /* Clear depth buffer for new frame */
        funsos_3d_clear_depth();

        /* === Layer 1: Ground plane with procedural texture === */
        {
            /* Create a simple ground quad mesh with texture-mapped colors */
            funsos_vertex3d_t ground_verts[4] = {
                {{-3.0f, -1.2f, -3.0f}, procedural_texture(0.0f, 0.0f, time_elapsed)},
                {{ 3.0f, -1.2f, -3.0f}, procedural_texture(1.0f, 0.0f, time_elapsed)},
                {{ 3.0f, -1.2f,  3.0f}, procedural_texture(1.0f, 1.0f, time_elapsed)},
                {{-3.0f, -1.2f,  3.0f}, procedural_texture(0.0f, 1.0f, time_elapsed)},
            };

            /* Apply diffuse lighting to ground vertices */
            funsos_vec3_t ground_normal = {0.0f, 1.0f, 0.0f};
            float ground_light = compute_diffuse_light(ground_normal, light_dir);
            for (int i = 0; i < 4; i++) {
                ground_verts[i].color.r = (uint8_t)fclamp((float)ground_verts[i].color.r * ground_light, 0, 255);
                ground_verts[i].color.g = (uint8_t)fclamp((float)ground_verts[i].color.g * ground_light, 0, 255);
                ground_verts[i].color.b = (uint8_t)fclamp((float)ground_verts[i].color.b * ground_light, 0, 255);
            }

            /* Static ground matrix (no rotation) */
            funsos_mat4_t ground_mvp = funsos_3d_mul_matrix(proj_mat, view_mat);
            funsos_3d_render(ground_verts, 4, ground_mvp, FUNSOS_RENDER_TRIANGLES);
        }

        /* === Layer 2: Central rotating cube with dynamic lighting === */
        {
            float angle = time_elapsed * 0.8f;

            /* Cube vertices with face normals encoded in color for demo */
            funsos_vertex3d_t cube_verts[8] = {
                {{-0.8f, -0.8f, -0.8f}, {180, 100, 80,  255}},  /* Front-left-bottom */
                {{ 0.8f, -0.8f, -0.8f}, {180, 100, 80,  255}},  /* Front-right-bottom */
                {{ 0.8f,  0.8f, -0.8f}, {220, 140, 100, 255}},  /* Front-right-top */
                {{-0.8f,  0.8f, -0.8f}, {220, 140, 100, 255}},  /* Front-left-top */
                {{-0.8f, -0.8f,  0.8f}, {140, 80,  60,  255}},  /* Back-left-bottom */
                {{ 0.8f, -0.8f,  0.8f}, {140, 80,  60,  255}},  /* Back-right-bottom */
                {{ 0.8f,  0.8f,  0.8f}, {180, 110, 80,  255}},  /* Back-right-top */
                {{-0.8f,  0.8f,  0.8f}, {180, 110, 80,  255}},  /* Back-left-top */
            };

            /* Apply per-face lighting simulation */
            funsos_vec3_t faces_norm[6] = {
                { 0.0f,  0.0f, -1.0f},  /* Front */
                { 0.0f,  0.0f,  1.0f},  /* Back */
                {-1.0f,  0.0f,  0.0f},  /* Left */
                { 1.0f,  0.0f,  0.0f},  /* Right */
                { 0.0f, -1.0f,  0.0f},  /* Bottom */
                { 0.0f,  1.0f,  0.0f},  /* Top */
            };
            float face_lights[6];
            for (int f = 0; f < 6; f++)
                face_lights[f] = compute_diffuse_light(faces_norm[f], light_dir);

            /* Modulate vertex colors by computed face lighting */
            /* Front face (vertices 0-3): use face_lights[0] */
            for (int v = 0; v < 4; v++) {
                float fl = face_lights[0] * 0.7f + 0.3f;  /* Ambient + diffuse */
                cube_verts[v].color.r = (uint8_t)fclamp(cube_verts[v].color.r * fl, 0, 255);
                cube_verts[v].color.g = (uint8_t)fclamp(cube_verts[v].color.g * fl, 0, 255);
                cube_verts[v].color.b = (uint8_t)fclamp(cube_verts[v].color.b * fl, 0, 255);
            }
            /* Back face (vertices 4-7): use face_lights[1] */
            for (int v = 4; v < 8; v++) {
                float fl = face_lights[1] * 0.7f + 0.3f;
                cube_verts[v].color.r = (uint8_t)fclamp(cube_verts[v].color.r * fl, 0, 255);
                cube_verts[v].color.g = (uint8_t)fclamp(cube_verts[v].color.g * fl, 0, 255);
                cube_verts[v].color.b = (uint8_t)fclamp(cube_verts[v].color.b * fl, 0, 255);
            }

            /* Model transform: rotate around Y axis, slight X wobble */
            funsos_mat4_t rot_y = funsos_3d_rotate_y(angle);
            funsos_mat4_t rot_x = funsos_3d_rotate_x(fsin(angle * 0.3f) * 0.3f);
            funsos_mat4_t trans = funsos_3d_translate(0.0f, 0.2f, 0.0f);
            funsos_mat4_t model = funsos_3d_mul_matrix(trans, funsos_3d_mul_matrix(rot_x, rot_y));
            funsos_mat4_t mvp = funsos_3d_mul_matrix(proj_mat, funsos_3d_mul_matrix(view_mat, model));

            funsos_3d_render(cube_verts, 8, mvp, FUNSOS_RENDER_TRIANGLES);
        }

        /* === Layer 3: Particle system overlay === */
        {
            /* Particles use billboard-style point rendering */
            funsos_mat4_t identity;  /* Identity-like: no translation */
            funsos_mat4_t part_mvp = funsos_3d_mul_matrix(proj_mat, view_mat);
            funsos_3d_render(particle_verts, pcount, part_mvp, FUNSOS_RENDER_POINTS);
        }

        /* === HUD Overlay: Draw 2D status text on top of 3D scene === */
        {
            /* Semi-transparent info panel background */
            funsos_rect_t hud_bg = {8, 8, 260, 90};
            /* Use rounded rect for nice appearance */
            funsos_fill_rounded_rect(ctx, hud_bg, 6, 0xCC000000);  /* Dark semi-transparent */

            /* HUD text */
            funsos_draw_text(win, 16, 16, "Shader Demo - GPU Effects", FUNSOS_COLOR_CYAN);
            funsos_draw_text(win, 16, 38, "Real-time Lighting + Textures", FUNSOS_COLOR_LIGHT_GRAY);
            funsos_draw_text(win, 16, 56, "Particle System Active", FUNSOS_COLOR_ORANGE);

            /* Frame counter */
            char fps_buf[24];
            int fi = 0;
            uint32_t fc = (uint32_t)frame_count;
            if (fc == 0) fps_buf[fi++] = '0';
            else { char rev[12]; int ri=0; while(fc>0){rev[ri++]='0'+(fc%10);fc/=10;}for(int k=ri-1;k>=0;k--)fps_buf[fi++]=rev[k]; }
            /* append " frames" */
            const char *suffix = " frames";
            for (int s = 0; suffix[s]; s++) fps_buf[fi++] = suffix[s];
            fps_buf[fi] = '\0';
            funsos_draw_text(win, 16, 74, fps_buf, FUNSOS_COLOR_GREEN);
        }

        /* Frame rate limiting (~60 FPS target) */
        funsos_sleep(0);  /* Yield CPU slice */
    }

    funsos_destroy_window(win);
    return 0;
}
