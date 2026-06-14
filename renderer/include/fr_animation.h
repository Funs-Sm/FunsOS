/* fr_animation.h - 动画系统
 * 淡入淡出/滑动/缩放/旋转/弹性动画
 */

#ifndef FR_ANIMATION_H
#define FR_ANIMATION_H

#include "stdint.h"

/* 动画类型 */
#define FR_ANIM_FADE_IN     1
#define FR_ANIM_FADE_OUT    2
#define FR_ANIM_SLIDE_LEFT  3
#define FR_ANIM_SLIDE_RIGHT 4
#define FR_ANIM_SLIDE_UP    5
#define FR_ANIM_SLIDE_DOWN  6
#define FR_ANIM_SCALE_UP    7
#define FR_ANIM_SCALE_DOWN  8
#define FR_ANIM_ROTATE      9
#define FR_ANIM_BOUNCE      10
#define FR_ANIM_ELASTIC     11
#define FR_ANIM_CUSTOM      12

/* 缓动函数 */
#define FR_EASE_LINEAR      0
#define FR_EASE_IN_QUAD     1
#define FR_EASE_OUT_QUAD    2
#define FR_EASE_IN_OUT_QUAD 3
#define FR_EASE_IN_CUBIC    4
#define FR_EASE_OUT_CUBIC   5
#define FR_EASE_IN_OUT_CUBIC 6
#define FR_EASE_IN_BOUNCE   7
#define FR_EASE_OUT_BOUNCE  8
#define FR_EASE_IN_ELASTIC  9
#define FR_EASE_OUT_ELASTIC 10

/* 动画状态 */
#define FR_ANIM_STOPPED     0
#define FR_ANIM_RUNNING     1
#define FR_ANIM_PAUSED      2
#define FR_ANIM_FINISHED    3

/* 动画回调 */
typedef void (*fr_anim_callback)(fr_handle_t widget, float progress, void *user_data);

/* 动画结构 */
typedef struct fr_animation {
    uint32_t type;
    uint32_t ease;
    uint32_t state;

    fr_handle_t target;        /* 目标控件 */
    uint32_t duration_ms;      /* 持续时间 */
    uint32_t elapsed_ms;       /* 已过时间 */
    uint32_t delay_ms;         /* 延迟 */
    int loop;                  /* 循环次数, -1=无限 */
    int direction;             /* 0=正向, 1=反向 */

    /* 动画参数 */
    float from_val;
    float to_val;
    float current_val;

    /* 回调 */
    fr_anim_callback on_update;
    fr_anim_callback on_finish;
    void *user_data;

    struct fr_animation *next;
} fr_animation_t;

/* 动画操作 */
fr_animation_t *fr_anim_create(fr_handle_t widget, uint32_t type,
                               uint32_t duration_ms);
void fr_anim_destroy(fr_animation_t *anim);

/* 启动/停止 */
void fr_anim_start(fr_animation_t *anim);
void fr_anim_stop(fr_animation_t *anim);
void fr_anim_pause(fr_animation_t *anim);
void fr_anim_resume(fr_animation_t *anim);

/* 设置参数 */
void fr_anim_set_ease(fr_animation_t *anim, uint32_t ease);
void fr_anim_set_delay(fr_animation_t *anim, uint32_t delay_ms);
void fr_anim_set_loop(fr_animation_t *anim, int count);
void fr_anim_set_direction(fr_animation_t *anim, int reverse);
void fr_anim_set_range(fr_animation_t *anim, float from, float to);
void fr_anim_set_callback(fr_animation_t *anim, fr_anim_callback on_update,
                          fr_anim_callback on_finish, void *user_data);

/* 更新所有动画 */
void fr_anim_update_all(fr_handle_t ctx, uint32_t delta_ms);

/* 缓动函数计算 */
float fr_ease_calc(uint32_t ease, float t);

#endif /* FR_ANIMATION_H */
