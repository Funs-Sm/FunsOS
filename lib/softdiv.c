/*
 * lib/softdiv.c - Software 64-bit integer division for 32-bit targets
 * Provides __udivdi3, __umoddi3, __divdi3, __moddi3 for GCC
 */

#include "stdint.h"

/* 64-bit unsigned division */
uint64_t __udivdi3(uint64_t num, uint64_t den) {
    if (den == 0) return 0;

    uint64_t quot = 0;
    uint64_t bit = 1;

    /* Align den with num */
    while (den <= num && !(den & ((uint64_t)1 << 63))) {
        den <<= 1;
        bit <<= 1;
    }

    while (bit) {
        if (num >= den) {
            num -= den;
            quot |= bit;
        }
        den >>= 1;
        bit >>= 1;
    }

    return quot;
}

/* 64-bit unsigned modulo */
uint64_t __umoddi3(uint64_t num, uint64_t den) {
    if (den == 0) return 0;
    return num - __udivdi3(num, den) * den;
}

/* 64-bit signed division */
int64_t __divdi3(int64_t num, int64_t den) {
    int neg = 0;
    if (num < 0) { num = -num; neg = !neg; }
    if (den < 0) { den = -den; neg = !neg; }
    int64_t result = (int64_t)__udivdi3((uint64_t)num, (uint64_t)den);
    return neg ? -result : result;
}

/* 64-bit signed modulo */
int64_t __moddi3(int64_t num, int64_t den) {
    int neg = 0;
    if (num < 0) { num = -num; neg = 1; }
    if (den < 0) { den = -den; }
    int64_t result = (int64_t)__umoddi3((uint64_t)num, (uint64_t)den);
    return neg ? -result : result;
}
