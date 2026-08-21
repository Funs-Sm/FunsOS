/*
 * inttypes.h - freestanding PRIdxx/PRIxxx/SCNxxx 格式宏
 *
 * Project policy in this codebase is to write `printf("%llu", (uint64_t)x)`
 * directly because the codebase is fixed at -m32 and uint64_t is always
 * `unsigned long long`.  However that ties every call site to a single
 * platform width, and breaks if the kernel is ever ported to LLP64 or
 * LP64 environments.  Providing the standard C99 PRI* macros lets call
 * sites switch without churn:
 *
 *     printf("%" PRIu64 "\n", value);
 *
 * The numeric suffix here mirrors the standard <inttypes.h>; on this
 * target (32-bit) uint64_t == unsigned long long so the stringification
 * resolves to "llu" etc.  The reader must `#include "inttypes.h"`
 * (which already pulls in <stdint.h>) and we deliberately do NOT wrap
 * the macros in `#ifndef __PRINTF_MACRO_DEFINED`, because the kernel's
 * own snprintf/vsnprintf is what consumes these strings and it has no
 * concept of hostlibc.
 */

#ifndef INTTYPES_H
#define INTTYPES_H

#include "stdint.h"

/* ---------- decimal ---------- */
#define PRId8    "d"
#define PRIi8    "i"
#define PRId16   "d"
#define PRIi16   "i"
#define PRId32   "d"
#define PRIi32   "i"
#define PRId64   "lld"
#define PRIi64   "lli"

#define PRIu8    "u"
#define PRIu16   "u"
#define PRIu32   "u"
#define PRIu64   "llu"

#define PRIo8    "o"
#define PRIo16   "o"
#define PRIo32   "o"
#define PRIo64   "llo"

#define PRIx8    "x"
#define PRIX8    "X"
#define PRIx16   "x"
#define PRIX16   "X"
#define PRIx32   "x"
#define PRIX32   "X"
#define PRIx64   "llx"
#define PRIX64   "llX"

/* maximum-width decimal/hex for integer types */
#define PRIdMAX  "lld"
#define PRIiMAX  "lli"
#define PRIuMAX  "llu"
#define PRIoMAX  "llo"
#define PRIxMAX  "llx"
#define PRIXMAX  "llX"

/* pointer-sized */
#define PRIdPTR  "d"
#define PRIiPTR  "i"
#define PRIuPTR  "u"
#define PRIxPTR  "x"
#define PRIXPTR  "X"

/* ---------- scanf counterparts (snprintf-side use only) ---------- */
#define SCNd8    "hhd"
#define SCNi8    "hhi"
#define SCNd16   "hd"
#define SCNi16   "hi"
#define SCNd32   "d"
#define SCNi32   "i"
#define SCNd64   "lld"
#define SCNi64   "lli"

#define SCNu8    "hhu"
#define SCNu16   "hu"
#define SCNu32   "u"
#define SCNu64   "llu"

#define SCNo8    "hho"
#define SCNo16   "ho"
#define SCNo32   "o"
#define SCNo64   "llo"

#define SCNx8    "hhx"
#define SCNx16   "hx"
#define SCNx32   "x"
#define SCNx64   "llx"

#define SCNdMAX  "lld"
#define SCNiMAX  "lli"
#define SCNuMAX  "llu"
#define SCNoMAX  "llo"
#define SCNxMAX  "llx"

#define SCNdPTR  "d"
#define SCNiPTR  "i"
#define SCNuPTR  "u"
#define SCNxPTR  "x"

#endif /* INTTYPES_H */
