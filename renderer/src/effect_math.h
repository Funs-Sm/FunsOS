/*
 * effect_math.h — 视觉特效模块共享数学/像素工具
 *
 * 设计目的:
 *   effect.c(76 KB)原本把 `clamp_int / clamp_float / lerp_float / lerp_uint8`
 *   写成 static 函数,但它们在多个特效(阴影/模糊/渐变/发光)之间共享。
 *   把它们集中到本头作为 `static inline`,等价于原 .c 文件内 static
 *   副本(零开销,零行为变化),但消除"未来按特效拆文件时 helper 不可见"的障碍。
 *
 * 注意:
 *   - 这是 effect 模块**私有**头;不暴露给 renderer 之外。
 *   - 所有函数保持原 effect.c 中相同的语义,包括边界 clamp 行为。
 */

#ifndef FR_EFFECT_MATH_H
#define FR_EFFECT_MATH_H

#include "stdint.h"

static inline int fr_clamp_int(int val, int min, int max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

static inline float fr_clamp_float(float val, float min, float max) {
    if (val < min) return min;
    if (val > max) return max;
    return val;
}

static inline float fr_lerp_float(float a, float b, float t) {
    return a + (b - a) * t;
}

static inline uint8_t fr_lerp_uint8(uint8_t a, uint8_t b, float t) {
    return (uint8_t)((float)a + (float)(b - a) * t);
}

#endif /* FR_EFFECT_MATH_H */