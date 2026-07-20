#include "krng.h"
#include "timer.h"
#include "rtc.h"

static struct {
    uint64_t state[2];
    uint8_t  initialized;
} krng;

static inline uint64_t rotl(const uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

void krng_seed(uint64_t seed) {
    krng.state[0] = seed;
    krng.state[1] = seed ^ 0x9E3779B97F4A7C15ULL;
    for (int i = 0; i < 20; i++) {
        (void)krng_next();
    }
}

void krng_init(void) {
    if (krng.initialized) return;

    uint64_t seed = 0;
    rtc_time_t t;
    rtc_read_time(&t);

    seed |= (uint64_t)t.second;
    seed |= (uint64_t)t.minute << 6;
    seed |= (uint64_t)t.hour << 12;
    seed |= (uint64_t)t.day << 17;
    seed |= (uint64_t)t.month << 22;
    seed |= (uint64_t)t.year << 26;
    seed ^= (uint64_t)timer_get_ticks() << 32;
    seed ^= (uint64_t)(uintptr_t)&krng;

    if (seed == 0) seed = 0x123456789ABCDEF0ULL;
    krng_seed(seed);
    krng.initialized = 1;
}

uint64_t krng_next(void) {
    uint64_t s0 = krng.state[0];
    uint64_t s1 = krng.state[1];
    uint64_t result = s0 + s1;

    s1 ^= s0;
    krng.state[0] = rotl(s0, 55) ^ s1 ^ (s1 << 14);
    krng.state[1] = rotl(s1, 36);

    return result;
}

uint32_t krng_next32(void) {
    return (uint32_t)(krng_next() >> 32);
}

uint32_t krng_range(uint32_t min, uint32_t max) {
    if (min >= max) return min;
    uint32_t range = max - min;
    return min + (krng_next32() % range);
}
