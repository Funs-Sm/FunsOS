/* particle.c - 粒子系统引擎实现
 * 实现粒子发射器、力场和多种粒子类型
 */

#include "funrender.h"
#include "fr_context.h"
#include "fr_particle.h"
#include "string.h"
#include "../kernel/kheap.h"
#include "../lib/stdio.h"
#include "../lib/math.h"

/* ---- 内部辅助函数 ---- */

static float fr_random_float(float min, float max)
{
    static uint32_t seed = 12345;
    seed = seed * 1103515245 + 12345;
    float r = (float)(seed & 0x7FFFFFFF) / (float)0x7FFFFFFF;
    return min + r * (max - min);
}

static float fr_deg_to_rad(float deg)
{
    return deg * 3.1415926535f / 180.0f;
}

static float fr_clamp(float v, float min, float max)
{
    if (v < min) return min;
    if (v > max) return max;
    return v;
}

static uint8_t fr_lerp_u8(uint8_t a, uint8_t b, float t)
{
    float v = (float)a + ((float)b - (float)a) * t;
    if (v < 0.0f) return 0;
    if (v > 255.0f) return 255;
    return (uint8_t)v;
}

/* ---- 系统管理 ---- */

fr_particle_system_t *fr_particle_system_create(void)
{
    fr_particle_system_t *ps = (fr_particle_system_t *)fr_alloc(sizeof(fr_particle_system_t));
    if (!ps) return NULL;
    memset(ps, 0, sizeof(fr_particle_system_t));
    ps->time_scale = 1.0f;
    return ps;
}

void fr_particle_system_destroy(fr_particle_system_t *ps)
{
    if (ps) fr_free(ps);
}

void fr_particle_system_reset(fr_particle_system_t *ps)
{
    if (!ps) return;
    ps->particle_count = 0;
    ps->emitter_count = 0;
    ps->force_count = 0;
    ps->total_particles_emitted = 0;
}

/* ---- 发射器管理 ---- */

fr_emitter_t *fr_emitter_create(fr_particle_system_t *ps, float x, float y, uint32_t type)
{
    if (!ps || ps->emitter_count >= FR_MAX_EMITTERS) return NULL;

    fr_emitter_t *em = &ps->emitters[ps->emitter_count++];
    memset(em, 0, sizeof(fr_emitter_t));
    em->x = x;
    em->y = y;
    em->particle_type = type;
    em->enabled = 1;
    em->emission_rate = 20.0f;
    em->max_particles = 200;
    em->speed = 100.0f;
    em->speed_variance = 30.0f;
    em->life = 2.0f;
    em->life_variance = 0.5f;
    em->size = 4.0f;
    em->end_size = 2.0f;
    em->angle_variance = 0.5f;
    em->start_r = 255; em->start_g = 255; em->start_b = 255; em->start_a = 255;
    em->end_r = 255; em->end_g = 255; em->end_b = 255; em->end_a = 0;

    /* 根据类型设置默认值 */
    switch (type) {
    case FR_PARTICLE_SPARK:
        em->speed = 200.0f;
        em->life = 0.5f;
        em->angle_variance = 6.28f;
        em->start_r = 255; em->start_g = 200; em->start_b = 50;
        em->end_r = 255; em->end_g = 100; em->end_b = 0;
        em->particle_flags = FR_PARTICLE_FLAG_GRAVITY | FR_PARTICLE_FLAG_FADE_ALPHA;
        break;
    case FR_PARTICLE_SMOKE:
        em->speed = 30.0f;
        em->life = 3.0f;
        em->size = 8.0f;
        em->end_size = 32.0f;
        em->angle = fr_deg_to_rad(-90.0f);
        em->angle_variance = 0.5f;
        em->start_r = 128; em->start_g = 128; em->start_b = 128; em->start_a = 128;
        em->end_r = 64; em->end_g = 64; em->end_b = 64; em->end_a = 0;
        em->particle_flags = FR_PARTICLE_FLAG_FADE_SIZE | FR_PARTICLE_FLAG_FADE_ALPHA;
        break;
    case FR_PARTICLE_FIRE:
        em->speed = 50.0f;
        em->life = 1.0f;
        em->angle = fr_deg_to_rad(-90.0f);
        em->angle_variance = 0.3f;
        em->start_r = 255; em->start_g = 200; em->start_b = 0; em->start_a = 255;
        em->end_r = 255; em->end_g = 0; em->end_b = 0; em->end_a = 0;
        em->particle_flags = FR_PARTICLE_FLAG_FADE_ALPHA | FR_PARTICLE_FLAG_FADE_SIZE;
        em->size = 6.0f;
        em->end_size = 3.0f;
        break;
    case FR_PARTICLE_RAIN:
        em->speed = 600.0f;
        em->life = 1.0f;
        em->angle = fr_deg_to_rad(80.0f);
        em->angle_variance = 0.1f;
        em->size = 2.0f;
        em->end_size = 2.0f;
        em->width = 400.0f;
        em->start_r = 150; em->start_g = 180; em->start_b = 255; em->start_a = 180;
        em->end_r = 150; em->end_g = 180; em->end_b = 255; em->end_a = 0;
        em->particle_flags = FR_PARTICLE_FLAG_GRAVITY;
        break;
    case FR_PARTICLE_SNOW:
        em->speed = 20.0f;
        em->life = 8.0f;
        em->angle = fr_deg_to_rad(80.0f);
        em->angle_variance = 0.5f;
        em->size = 6.0f;
        em->end_size = 4.0f;
        em->width = 400.0f;
        em->start_r = 255; em->start_g = 255; em->start_b = 255; em->start_a = 200;
        em->end_r = 255; em->end_g = 255; em->end_b = 255; em->end_a = 0;
        em->particle_flags = FR_PARTICLE_FLAG_ROTATE;
        em->speed_variance = 10.0f;
        break;
    default:
        break;
    }

    return em;
}

void fr_emitter_destroy(fr_particle_system_t *ps, fr_emitter_t *emitter)
{
    if (!ps || !emitter) return;
    memset(emitter, 0, sizeof(fr_emitter_t));
}

void fr_emitter_set_position(fr_emitter_t *emitter, float x, float y)
{
    if (!emitter) return;
    emitter->x = x;
    emitter->y = y;
}

void fr_emitter_set_angle(fr_emitter_t *emitter, float angle, float variance)
{
    if (!emitter) return;
    emitter->angle = angle;
    emitter->angle_variance = variance;
}

void fr_emitter_set_speed(fr_emitter_t *emitter, float speed, float variance)
{
    if (!emitter) return;
    emitter->speed = speed;
    emitter->speed_variance = variance;
}

void fr_emitter_set_life(fr_emitter_t *emitter, float life, float variance)
{
    if (!emitter) return;
    emitter->life = life;
    emitter->life_variance = variance;
}

void fr_emitter_set_size(fr_emitter_t *emitter, float start, float end)
{
    if (!emitter) return;
    emitter->size = start;
    emitter->end_size = end;
}

void fr_emitter_set_rate(fr_emitter_t *emitter, float rate)
{
    if (!emitter) return;
    emitter->emission_rate = rate;
}

void fr_emitter_set_max_particles(fr_emitter_t *emitter, uint32_t max)
{
    if (!emitter) return;
    emitter->max_particles = max;
}

void fr_emitter_set_color(fr_emitter_t *emitter,
                           uint8_t sr, uint8_t sg, uint8_t sb, uint8_t sa,
                           uint8_t er, uint8_t eg, uint8_t eb, uint8_t ea)
{
    if (!emitter) return;
    emitter->start_r = sr; emitter->start_g = sg; emitter->start_b = sb; emitter->start_a = sa;
    emitter->end_r = er; emitter->end_g = eg; emitter->end_b = eb; emitter->end_a = ea;
}

void fr_emitter_burst(fr_emitter_t *emitter, int count)
{
    if (!emitter) return;
    emitter->burst = 1;
    emitter->emission_accum = (float)count;
}

void fr_emitter_enable(fr_emitter_t *emitter, int enabled)
{
    if (!emitter) return;
    emitter->enabled = enabled;
}

/* ---- 粒子发射 ---- */

fr_particle_t *fr_emit_particle(fr_particle_system_t *ps, fr_emitter_t *emitter)
{
    if (!ps || !emitter) return NULL;
    if (ps->particle_count >= FR_MAX_PARTICLES) return NULL;

    fr_particle_t *p = &ps->particles[ps->particle_count++];
    memset(p, 0, sizeof(fr_particle_t));

    /* 发射位置 */
    float px = emitter->x + fr_random_float(-emitter->width * 0.5f, emitter->width * 0.5f);
    float py = emitter->y + fr_random_float(-emitter->height * 0.5f, emitter->height * 0.5f);

    p->x = px;
    p->y = py;

    /* 发射角度 */
    float angle = emitter->angle + fr_random_float(-emitter->angle_variance, emitter->angle_variance);
    float speed = emitter->speed + fr_random_float(-emitter->speed_variance, emitter->speed_variance);

    p->vx = (float)cos(angle) * speed;
    p->vy = (float)sin(angle) * speed;

    /* 生命 */
    p->max_life = emitter->life + fr_random_float(-emitter->life_variance, emitter->life_variance);
    if (p->max_life < 0.01f) p->max_life = 0.01f;
    p->life = p->max_life;

    /* 大小 */
    p->start_size = emitter->size + fr_random_float(-emitter->size_variance, emitter->size_variance);
    if (p->start_size < 0.5f) p->start_size = 0.5f;
    p->size = p->start_size;
    p->end_size = emitter->end_size;

    /* 颜色 */
    p->start_r = emitter->start_r; p->start_g = emitter->start_g;
    p->start_b = emitter->start_b; p->start_a = emitter->start_a;
    p->end_r = emitter->end_r; p->end_g = emitter->end_g;
    p->end_b = emitter->end_b; p->end_a = emitter->end_a;
    p->r = p->start_r; p->g = p->start_g; p->b = p->start_b; p->a = p->start_a;

    p->type = emitter->particle_type;
    p->flags = emitter->particle_flags;
    p->alive = 1;
    p->rotation_speed = fr_random_float(-3.0f, 3.0f);

    ps->total_particles_emitted++;
    return p;
}

void fr_particle_kill(fr_particle_t *particle)
{
    if (!particle) return;
    particle->alive = 0;
    particle->life = 0.0f;
}

void fr_particle_set_velocity(fr_particle_t *particle, float vx, float vy)
{
    if (!particle) return;
    particle->vx = vx;
    particle->vy = vy;
}

/* ---- 力场管理 ---- */

fr_force_t *fr_force_create(fr_particle_system_t *ps, uint32_t type)
{
    if (!ps || ps->force_count >= FR_MAX_FORCES) return NULL;

    fr_force_t *force = &ps->forces[ps->force_count++];
    memset(force, 0, sizeof(fr_force_t));
    force->type = type;
    force->enabled = 1;
    force->strength = 1.0f;
    force->radius = 100.0f;

    switch (type) {
    case FR_FORCE_GRAVITY:
        force->direction_x = 0.0f;
        force->direction_y = 300.0f;
        break;
    case FR_FORCE_WIND:
        force->direction_x = 50.0f;
        force->direction_y = 0.0f;
        break;
    case FR_FORCE_TURBULENCE:
        force->frequency = 2.0f;
        force->amplitude = 50.0f;
        break;
    case FR_FORCE_VORTEX:
        force->strength = 200.0f;
        break;
    case FR_FORCE_DRAG:
        force->damping = 0.95f;
        break;
    default:
        break;
    }

    return force;
}

void fr_force_destroy(fr_particle_system_t *ps, fr_force_t *force)
{
    if (force) memset(force, 0, sizeof(fr_force_t));
}

void fr_force_set_gravity(fr_force_t *force, float gx, float gy, float strength)
{
    if (!force) return;
    force->direction_x = gx;
    force->direction_y = gy;
    force->strength = strength;
}

void fr_force_set_wind(fr_force_t *force, float wx, float wy, float strength)
{
    if (!force) return;
    force->direction_x = wx;
    force->direction_y = wy;
    force->strength = strength;
}

void fr_force_set_turbulence(fr_force_t *force, float freq, float amp)
{
    if (!force) return;
    force->frequency = freq;
    force->amplitude = amp;
}

void fr_force_set_vortex(fr_force_t *force, float cx, float cy, float radius, float strength)
{
    if (!force) return;
    force->center_x = cx;
    force->center_y = cy;
    force->radius = radius;
    force->strength = strength;
}

void fr_force_set_attractor(fr_force_t *force, float cx, float cy, float radius, float strength)
{
    if (!force) return;
    force->center_x = cx;
    force->center_y = cy;
    force->radius = radius;
    force->strength = strength;
}

/* ---- 力场应用 ---- */

static void fr_apply_force_to_particle(fr_particle_t *p, const fr_force_t *force, float dt)
{
    if (!p->alive) return;

    switch (force->type) {
    case FR_FORCE_GRAVITY:
        if (p->flags & FR_PARTICLE_FLAG_GRAVITY) {
            p->vy += force->direction_y * force->strength * dt;
            p->vx += force->direction_x * force->strength * dt;
        }
        break;
    case FR_FORCE_WIND:
        p->vx += force->direction_x * force->strength * dt;
        p->vy += force->direction_y * force->strength * dt;
        break;
    case FR_FORCE_TURBULENCE: {
        float noise = (float)sin(p->x * force->frequency * 0.01f + p->y * 0.01f) *
                      (float)cos(p->y * force->frequency * 0.013f) * force->amplitude;
        p->vx += noise * dt;
        p->vy += (float)cos(p->x * 0.01f) * force->amplitude * 0.5f * dt;
        break;
    }
    case FR_FORCE_VORTEX: {
        float dx = p->x - force->center_x;
        float dy = p->y - force->center_y;
        float dist = (float)sqrt(dx * dx + dy * dy);
        if (dist < force->radius && dist > 0.1f) {
            float factor = (1.0f - dist / force->radius) * force->strength;
            p->vx += -dy / dist * factor * dt;
            p->vy += dx / dist * factor * dt;
        }
        break;
    }
    case FR_FORCE_POINT_ATTRACT: {
        float dx = force->center_x - p->x;
        float dy = force->center_y - p->y;
        float dist = (float)sqrt(dx * dx + dy * dy);
        if (dist < force->radius && dist > 0.1f) {
            float factor = force->strength / (dist * dist + 1.0f);
            p->vx += dx / dist * factor * dt;
            p->vy += dy / dist * factor * dt;
        }
        break;
    }
    case FR_FORCE_POINT_REPEL: {
        float dx = p->x - force->center_x;
        float dy = p->y - force->center_y;
        float dist = (float)sqrt(dx * dx + dy * dy);
        if (dist < force->radius && dist > 0.1f) {
            float factor = force->strength / (dist * dist + 1.0f);
            p->vx += dx / dist * factor * dt;
            p->vy += dy / dist * factor * dt;
        }
        break;
    }
    case FR_FORCE_DRAG:
        p->vx *= force->damping;
        p->vy *= force->damping;
        break;
    default:
        break;
    }
}

/* ---- 系统更新 ---- */

void fr_particle_system_update(fr_particle_system_t *ps, float dt)
{
    if (!ps || ps->paused) return;

    float scaled_dt = dt * ps->time_scale;

    /* 发射粒子 */
    for (int i = 0; i < ps->emitter_count; i++) {
        fr_emitter_t *em = &ps->emitters[i];
        if (!em->enabled) continue;

        em->emission_accum += em->emission_rate * scaled_dt;
        while (em->emission_accum >= 1.0f && ps->particle_count < FR_MAX_PARTICLES) {
            fr_emit_particle(ps, em);
            em->emission_accum -= 1.0f;
        }
    }

    /* 更新粒子 */
    int alive_count = 0;
    for (int i = 0; i < ps->particle_count; i++) {
        fr_particle_t *p = &ps->particles[i];
        if (!p->alive) continue;

        /* 生命衰减 */
        p->life -= scaled_dt;
        if (p->life <= 0.0f) {
            p->alive = 0;
            continue;
        }

        float t = 1.0f - p->life / p->max_life;

        /* 应用力场 */
        for (int f = 0; f < ps->force_count; f++) {
            if (ps->forces[f].enabled) {
                fr_apply_force_to_particle(p, &ps->forces[f], scaled_dt);
            }
        }

        /* 更新位置 */
        p->x += p->vx * scaled_dt;
        p->y += p->vy * scaled_dt;

        /* 更新大小 */
        if (p->flags & FR_PARTICLE_FLAG_FADE_SIZE) {
            p->size = p->start_size + (p->end_size - p->start_size) * t;
        }

        /* 更新颜色 (淡入淡出) */
        if (p->flags & FR_PARTICLE_FLAG_FADE_ALPHA) {
            p->a = fr_lerp_u8(p->start_a, p->end_a, t);
        }
        p->r = fr_lerp_u8(p->start_r, p->end_r, t);
        p->g = fr_lerp_u8(p->start_g, p->end_g, t);
        p->b = fr_lerp_u8(p->start_b, p->end_b, t);

        /* 旋转 */
        if (p->flags & FR_PARTICLE_FLAG_ROTATE) {
            p->rotation += p->rotation_speed * scaled_dt;
        }

        /* 将存活粒子移动到前面 */
        if (alive_count != i) {
            ps->particles[alive_count] = ps->particles[i];
        }
        alive_count++;
    }

    ps->particle_count = alive_count;
}

/* ---- 系统渲染 ---- */

void fr_particle_system_render(fr_particle_system_t *ps, fr_context_t *ctx)
{
    if (!ps || !ctx || !ctx->framebuffer) return;

    for (int i = 0; i < ps->particle_count; i++) {
        fr_particle_t *p = &ps->particles[i];
        if (!p->alive) continue;

        int sx = (int)p->x;
        int sy = (int)p->y;
        int half_size = (int)(p->size * 0.5f);
        if (half_size < 1) half_size = 1;

        uint32_t color = ((uint32_t)p->a << 24) | ((uint32_t)p->r << 16) |
                         ((uint32_t)p->g << 8) | (uint32_t)p->b;

        /* 根据类型渲染 */
        switch (p->type) {
        case FR_PARTICLE_RAIN:
            /* 雨滴: 垂直线 */
            for (int y = -half_size; y <= half_size; y++) {
                int py = sy + y;
                if (py >= 0 && py < ctx->height && sx >= 0 && sx < ctx->width) {
                    ctx->framebuffer[py * ctx->width + sx] = color;
                }
            }
            break;
        case FR_PARTICLE_SNOW:
        case FR_PARTICLE_SPARK:
            /* 圆形粒子 */
            for (int y = -half_size; y <= half_size; y++) {
                for (int x = -half_size; x <= half_size; x++) {
                    if (x * x + y * y <= half_size * half_size) {
                        int px = sx + x;
                        int py = sy + y;
                        if (px >= 0 && px < ctx->width && py >= 0 && py < ctx->height) {
                            ctx->framebuffer[py * ctx->width + px] = color;
                        }
                    }
                }
            }
            break;
        default:
            /* 方形粒子 */
            for (int y = -half_size; y <= half_size; y++) {
                for (int x = -half_size; x <= half_size; x++) {
                    int px = sx + x;
                    int py = sy + y;
                    if (px >= 0 && px < ctx->width && py >= 0 && py < ctx->height) {
                        ctx->framebuffer[py * ctx->width + px] = color;
                    }
                }
            }
            break;
        }
    }
}

void fr_particle_system_pause(fr_particle_system_t *ps, int paused)
{
    if (ps) ps->paused = paused;
}

void fr_particle_system_set_time_scale(fr_particle_system_t *ps, float scale)
{
    if (ps) ps->time_scale = scale;
}

int fr_particle_system_get_alive_count(fr_particle_system_t *ps)
{
    if (!ps) return 0;
    int count = 0;
    for (int i = 0; i < ps->particle_count; i++) {
        if (ps->particles[i].alive) count++;
    }
    return count;
}

uint32_t fr_particle_system_get_total_emitted(fr_particle_system_t *ps)
{
    if (!ps) return 0;
    return ps->total_particles_emitted;
}