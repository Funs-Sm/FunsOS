/* animation.c - 动画系统实现
 * 淡入淡出/滑动/缩放/旋转/弹性动画
 */

#include "funrender.h"
#include "fr_animation.h"
#include "fr_context.h"
#include "math.h"

/* 创建动画 */
fr_animation_t *fr_anim_create(fr_handle_t widget, uint32_t type, uint32_t duration_ms)
{
    fr_animation_t *anim = (fr_animation_t *)fr_alloc(sizeof(fr_animation_t));
    if (anim == NULL) return NULL;

    anim->type = type;
    anim->ease = FR_EASE_LINEAR;
    anim->state = FR_ANIM_STOPPED;
    anim->target = widget;
    anim->duration_ms = duration_ms;
    anim->elapsed_ms = 0;
    anim->delay_ms = 0;
    anim->loop = 1;
    anim->direction = 0;
    anim->from_val = 0.0f;
    anim->to_val = 1.0f;
    anim->current_val = 0.0f;
    anim->on_update = NULL;
    anim->on_finish = NULL;
    anim->user_data = NULL;
    anim->next = NULL;

    return anim;
}

/* 销毁动画 */
void fr_anim_destroy(fr_animation_t *anim)
{
    if (anim) fr_free(anim);
}

/* 启动动画 */
void fr_anim_start(fr_animation_t *anim)
{
    if (anim == NULL) return;
    anim->state = FR_ANIM_RUNNING;
    anim->elapsed_ms = 0;
    anim->current_val = anim->from_val;
}

/* 停止动画 */
void fr_anim_stop(fr_animation_t *anim)
{
    if (anim) anim->state = FR_ANIM_STOPPED;
}

/* 暂停动画 */
void fr_anim_pause(fr_animation_t *anim)
{
    if (anim && anim->state == FR_ANIM_RUNNING)
        anim->state = FR_ANIM_PAUSED;
}

/* 恢复动画 */
void fr_anim_resume(fr_animation_t *anim)
{
    if (anim && anim->state == FR_ANIM_PAUSED)
        anim->state = FR_ANIM_RUNNING;
}

/* 设置参数 */
void fr_anim_set_ease(fr_animation_t *anim, uint32_t ease)       { if (anim) anim->ease = ease; }
void fr_anim_set_delay(fr_animation_t *anim, uint32_t delay_ms)  { if (anim) anim->delay_ms = delay_ms; }
void fr_anim_set_loop(fr_animation_t *anim, int count)            { if (anim) anim->loop = count; }
void fr_anim_set_direction(fr_animation_t *anim, int reverse)     { if (anim) anim->direction = reverse; }
void fr_anim_set_range(fr_animation_t *anim, float from, float to){ if (anim) { anim->from_val = from; anim->to_val = to; } }
void fr_anim_set_callback(fr_animation_t *anim, fr_anim_callback on_update,
                          fr_anim_callback on_finish, void *user_data)
{
    if (anim) { anim->on_update = on_update; anim->on_finish = on_finish; anim->user_data = user_data; }
}

/* 缓动函数计算 */
float fr_ease_calc(uint32_t ease, float t)
{
    if (t <= 0.0f) return 0.0f;
    if (t >= 1.0f) return 1.0f;

    switch (ease) {
    case FR_EASE_LINEAR:
        return t;

    case FR_EASE_IN_QUAD:
        return t * t;

    case FR_EASE_OUT_QUAD:
        return t * (2.0f - t);

    case FR_EASE_IN_OUT_QUAD:
        return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;

    case FR_EASE_IN_CUBIC:
        return t * t * t;

    case FR_EASE_OUT_CUBIC: {
        float t1 = t - 1.0f;
        return t1 * t1 * t1 + 1.0f;
    }

    case FR_EASE_IN_OUT_CUBIC:
        return t < 0.5f ? 4.0f * t * t * t : (t - 1.0f) * (2.0f * t - 2.0f) * (2.0f * t - 2.0f) + 1.0f;

    case FR_EASE_IN_BOUNCE: {
        float t1 = 1.0f - t;
        if (t1 < 1.0f / 2.75f) return 1.0f - 7.5625f * t1 * t1;
        if (t1 < 2.0f / 2.75f) { t1 -= 1.5f / 2.75f; return 1.0f - (7.5625f * t1 * t1 + 0.75f); }
        if (t1 < 2.5f / 2.75f) { t1 -= 2.25f / 2.75f; return 1.0f - (7.5625f * t1 * t1 + 0.9375f); }
        t1 -= 2.625f / 2.75f; return 1.0f - (7.5625f * t1 * t1 + 0.984375f);
    }

    case FR_EASE_OUT_BOUNCE: {
        if (t < 1.0f / 2.75f) return 7.5625f * t * t;
        if (t < 2.0f / 2.75f) { float t1 = t - 1.5f / 2.75f; return 7.5625f * t1 * t1 + 0.75f; }
        if (t < 2.5f / 2.75f) { float t1 = t - 2.25f / 2.75f; return 7.5625f * t1 * t1 + 0.9375f; }
        { float t1 = t - 2.625f / 2.75f; return 7.5625f * t1 * t1 + 0.984375f; }
    }

    case FR_EASE_IN_ELASTIC: {
        if (t == 0.0f || t == 1.0f) return t;
        return -(float)pow(2.0, 10.0 * (t - 1.0)) * (float)sin((t - 1.1) * 5.0f * 3.14159f);
    }

    case FR_EASE_OUT_ELASTIC: {
        if (t == 0.0f || t == 1.0f) return t;
        return (float)pow(2.0, -10.0 * t) * (float)sin((t - 0.1) * 5.0f * 3.14159f) + 1.0f;
    }

    default:
        return t;
    }
}

/* 更新所有动画 */
void fr_anim_update_all(fr_handle_t ctx, uint32_t delta_ms)
{
    fr_context_t *c = (fr_context_t *)ctx;
    if (c == NULL) return;

    fr_animation_t *anim = (fr_animation_t *)c->animations;
    while (anim) {
        if (anim->state == FR_ANIM_RUNNING) {
            anim->elapsed_ms += delta_ms;

            if (anim->elapsed_ms >= anim->duration_ms) {
                anim->current_val = anim->to_val;
                anim->state = FR_ANIM_FINISHED;

                if (anim->on_finish)
                    anim->on_finish(anim->target, 1.0f, anim->user_data);

                /* 循环 */
                if (anim->loop > 0) {
                    anim->loop--;
                    if (anim->loop > 0) {
                        anim->elapsed_ms = 0;
                        anim->state = FR_ANIM_RUNNING;
                    }
                } else if (anim->loop < 0) {
                    /* 无限循环 */
                    anim->elapsed_ms = 0;
                    anim->state = FR_ANIM_RUNNING;
                    if (anim->direction) {
                        float tmp = anim->from_val;
                        anim->from_val = anim->to_val;
                        anim->to_val = tmp;
                    }
                }
            } else {
                float t = (float)anim->elapsed_ms / (float)anim->duration_ms;
                float eased = fr_ease_calc(anim->ease, t);
                anim->current_val = anim->from_val + (anim->to_val - anim->from_val) * eased;

                if (anim->on_update)
                    anim->on_update(anim->target, eased, anim->user_data);
            }
        }
        anim = anim->next;
    }
}
