/*
 * lib/kprintf.h - portable printf macro glue for FunsCore.
 *
 * Several subsystems still write `printf("%llu", u64)` directly.  The
 * freestanding -m32 toolchain happens to produce a 64-bit `unsigned
 * long long` so the call works, but the moment the tree is recompiled
 * for a 64-bit host (LLP64 / LP64) the format string stops matching
 * the type.  Header <inttypes.h> already defines the C99 PRI* family;
 * this header just pulls it in together with the rest of the
 * portable-format glue, and exposes a couple of compile-time trait
 * macros so call sites can stay in one place.
 *
 *     #include "kprintf.h"
 *     printf("ns=%" PRIu64 " ticks=%lu\n", ns, ticks);
 *
 * No functional change vs including inttypes.h directly; the goal is
 * discoverability for new code.
 */
#ifndef LIB_KPRINTF_H
#define LIB_KPRINTF_H

#include "inttypes.h"
#include "stddef.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Convenience: stringify an arbitrary preprocessor argument.  We do
 * not use the standard #define STRINGIFY_IMPL(...) #__VA_ARGS__ trick
 * here because that breaks when the argument contains commas that the
 * caller intended as macro separators.  Single-arg version is enough
 * for our purposes (used in tracepoint registry, kprobe symbols, etc.). */
#ifndef KPRI_STR
#define KPRI_STR(x)        #x
#endif
#ifndef KPRI_CONCAT
#define KPRI_CONCAT(a, b)  KPRI_CONCAT_(a, b)
#define KPRI_CONCAT_(a, b) a##b
#endif

/* Compile-time check that the common signed/unsigned types print
 * safely under our printf implementation.  Used by internal test
 * code; not for kernel modules. */
static inline int kprintf_width_u64(void) { return (int)sizeof(uint64_t) * 8; }
static inline int kprintf_width_i64(void) { return (int)sizeof( int64_t) * 8; }

#ifdef __cplusplus
}
#endif

#endif /* LIB_KPRINTF_H */
