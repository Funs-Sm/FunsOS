/*
 * lib/kprintf.c - portable-format helper shim.
 *
 * FunsCore's primary v0.9 cleanup is to standardise on the C99 PRI*
 * family where 64-bit values are formatted.  Call sites should look
 * like:
 *
 *     #include "kprintf.h"
 *     printf("ts=%" PRIu64 "us\n", ts);
 *
 * rather than the platform-specific `"%llu"` spellings.  We do NOT
 * redefine printf or vsnprintf; instead this helper does nothing
 * more than exist so callers that want to wrap the format helpers
 * have a single anchor point.
 *
 * Real portability work lives in:
 *   - lib/inttypes.h   : the PRI* macros themselves
 *   - lib/stdio.c      : the format-engine that consumes them
 *   - lib/kprintf.h    : convenience include + trait helpers
 */
#include "kprintf.h"
#include "stdio.h"
#include "klog.h"

int kprintf_version(void)
{
    klog_info("kprintf: PRI* + C99 length modifiers in use (FunsCore 0.9)");
    return 0;
}
