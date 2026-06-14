/* fr_particle.h - 粒子系统引擎
 * 提供发射器、力场和多种粒子类型(火花、烟雾、火焰、雨、雪)
 */

#ifndef FR_PARTICLE_H
#define FR_PARTICLE_H

#include "stdint.h"

/* 前向声明 */
struct fr_context;

/* ---- 粒子类型 ---- */
#define FR_PARTICLE_SPARK       0
#define FR_PARTICLE_SMOKE       1
#define FR_PARTICLE_FIRE        2
#define FR_PARTICLE_RAIN        3
#define FR_PARTICLE_SNOW        4
#define FR_PARTICLE_BUBBLE      5
#define FR_PARTICLE_CONFETTI    6
#define FR_PARTICLE_ELECTRIC    7
#define FR_PARTICLE_CUSTOM      8

/* ---- 力场类型 ---- */
#define FR_FORCE_GRAVITY        0
#define FR_FORCE_WIND           1
#define FR_FORCE_TURBULENCE     2
#define FR_FORCE_VORTEX         3
#define FR_FORCE_POINT_ATTRACT  4
#define FR_FORCE_POINT_REPEL    5
#define FR_FORCE_DRAG           6
#define FR_FORCE_BOUNCE         7

/* ---- 粒子结构 ---- */
typedef struct {
    float x, y;                 /* 位置 */
    float vx, vy;               /* 速度 */
    float ax, ay;               /* 加速度 */
    float life;                 /* 剩余生命 */
    float max_life;             /* 最大生命 */
    float size;                 /* 当前大小 */
    float start_size;           /* 初始大小 */
    float end_size;             /* 结束大小 */
    float rotation;             /* 旋转角度 */
    float rotation_speed;       /* 旋转速度 */
    uint8_t r, g, b, a;         /* 颜色 */
    uint8_t start_r, start_g, start_b, start_a;
    uint8_t end_r, end_g, end_b, end_a;
    uint32_t type;              /* 粒子类型 */
    uint32_t flags;             /* 标志 */
    int alive;                  /* 是否存活 */
    void *user_data;            /* 用户数据 */
} fr_particle_t;

/* 粒子标志 */
#define FR_PARTICLE_FLAG_BOUNCE     0x0001
#define FR_PARTICLE_FLAG_GRAVITY    0x0002
#define FR_PARTICLE_FLAG_FADE_SIZE  0x0004
#define FR_PARTICLE_FLAG_FADE_ALPHA 0x0008
#define FR_PARTICLE_FLAG_ROTATE     0x0010
#define FR_PARTICLE_FLAG_TRAIL      0x0020

/* ---- 力场结构 ---- */
typedef struct {
    uint32_t type;              /* 力场类型 */
    float strength;             /* 强度 */
    float direction_x;          /* 方向X (重力/风) */
    float direction_y;          /* 方向Y */
    float center_x;             /* 中心X (涡旋/吸引) */
    float center_y;             /* 中心Y */
    float radius;               /* 影响半径 */
    float frequency;            /* 频率 (湍流) */
    float amplitude;            /* 振幅 (湍流) */
    float damping;              /* 阻尼 */
    int enabled;                /* 是否启用 */
} fr_force_t;

/* ---- 发射器 ---- */
typedef struct {
    float x, y;                 /* 发射位置 */
    float width, height;        /* 发射区域 (0=点发射) */
    float angle;                /* 发射角度 */
    float angle_variance;       /* 角度变化 */
    float speed;                /* 初始速度 */
    float speed_variance;       /* 速度变化 */
    float life;                 /* 粒子生命 */
    float life_variance;        /* 生命变化 */
    float size;                 /* 粒子大小 */
    float size_variance;        /* 大小变化 */
    float end_size;             /* 结束大小 */
    float emission_rate;        /* 发射率 (粒子/秒) */
    float emission_accum;       /* 发射累积 */
    uint32_t max_particles;     /* 最大粒子数 */
    uint32_t particle_type;     /* 粒子类型 */
    uint32_t particle_flags;    /* 粒子标志 */
    uint8_t start_r, start_g, start_b, start_a;
    uint8_t end_r, end_g, end_b, end_a;
    int enabled;                /* 是否启用 */
    int burst;                  /* 爆发模式 */
    void *user_data;            /* 用户数据 */
} fr_emitter_t;

/* ---- 粒子系统 ---- */
#define FR_MAX_PARTICLES    4096
#define FR_MAX_EMITTERS     64
#define FR_MAX_FORCES       16

typedef struct {
    fr_particle_t particles[FR_MAX_PARTICLES];
    int particle_count;
    fr_emitter_t emitters[FR_MAX_EMITTERS];
    int emitter_count;
    fr_force_t forces[FR_MAX_FORCES];
    int force_count;
    int paused;
    float time_scale;
    uint32_t total_particles_emitted;
} fr_particle_system_t;

/* ================================================================
 *  API 函数声明
 * ================================================================ */

/* 系统管理 */
fr_particle_system_t *fr_particle_system_create(void);
void fr_particle_system_destroy(fr_particle_system_t *ps);
void fr_particle_system_reset(fr_particle_system_t *ps);

/* 发射器管理 */
fr_emitter_t *fr_emitter_create(fr_particle_system_t *ps,
                                 float x, float y, uint32_t type);
void fr_emitter_destroy(fr_particle_system_t *ps, fr_emitter_t *emitter);
void fr_emitter_set_position(fr_emitter_t *emitter, float x, float y);
void fr_emitter_set_angle(fr_emitter_t *emitter, float angle, float variance);
void fr_emitter_set_speed(fr_emitter_t *emitter, float speed, float variance);
void fr_emitter_set_life(fr_emitter_t *emitter, float life, float variance);
void fr_emitter_set_size(fr_emitter_t *emitter, float start, float end);
void fr_emitter_set_rate(fr_emitter_t *emitter, float rate);
void fr_emitter_set_max_particles(fr_emitter_t *emitter, uint32_t max);
void fr_emitter_set_color(fr_emitter_t *emitter,
                           uint8_t sr, uint8_t sg, uint8_t sb, uint8_t sa,
                           uint8_t er, uint8_t eg, uint8_t eb, uint8_t ea);
void fr_emitter_burst(fr_emitter_t *emitter, int count);
void fr_emitter_enable(fr_emitter_t *emitter, int enabled);

/* 粒子发射 */
fr_particle_t *fr_emit_particle(fr_particle_system_t *ps, fr_emitter_t *emitter);
void fr_particle_kill(fr_particle_t *particle);
void fr_particle_set_velocity(fr_particle_t *particle, float vx, float vy);

/* 力场管理 */
fr_force_t *fr_force_create(fr_particle_system_t *ps, uint32_t type);
void fr_force_destroy(fr_particle_system_t *ps, fr_force_t *force);
void fr_force_set_gravity(fr_force_t *force, float gx, float gy, float strength);
void fr_force_set_wind(fr_force_t *force, float wx, float wy, float strength);
void fr_force_set_turbulence(fr_force_t *force, float freq, float amp);
void fr_force_set_vortex(fr_force_t *force, float cx, float cy, float radius, float strength);
void fr_force_set_attractor(fr_force_t *force, float cx, float cy, float radius, float strength);

/* 系统更新与渲染 */
void fr_particle_system_update(fr_particle_system_t *ps, float dt);
void fr_particle_system_render(fr_particle_system_t *ps, struct fr_context *ctx);
void fr_particle_system_pause(fr_particle_system_t *ps, int paused);
void fr_particle_system_set_time_scale(fr_particle_system_t *ps, float scale);

/* 获取信息 */
int fr_particle_system_get_alive_count(fr_particle_system_t *ps);
uint32_t fr_particle_system_get_total_emitted(fr_particle_system_t *ps);

#endif /* FR_PARTICLE_H */