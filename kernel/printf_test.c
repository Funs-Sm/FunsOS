/*
 * kernel/printf_test.c - sanity test for lib/stdio.c.
 * v0.9 work: verify that %llu/%lld/%llx/%llX/%llz render correctly.
 * Boot-time smoke; output goes to klog then to serial.
 */
#include "stdio.h"
#include "klog.h"
#include "version.h"

void printf_selftest(void)
{
    long long s_big    = -1234567890123LL;
    unsigned long long u_big = 0xDEADBEEFCAFE1234ULL;
    int      small     = 42;
    unsigned u_small   = 17;
    char     c         = 'X';
    const char *s      = "hello";
    void    *p         = (void *)0x12345678;

    klog_info("printf_selftest start (FunsCore %s)", KERNEL_VERSION);

    /* previously-broken format specifiers */
    klog_info("%%lld     -> %lld", s_big);
    klog_info("%%llu     -> %llu", u_big);
    klog_info("%%llx     -> %llx", u_big);
    klog_info("%%llX     -> %llX", u_big);
    klog_info("%%llx-pad -> %012llx", u_big);

    /* standard format specifiers */
    klog_info("%%d       -> %d", small);
    klog_info("%%u       -> %u", u_small);
    klog_info("%%x       -> %x", u_small);
    klog_info("%%X       -> %X", u_small);
    klog_info("%%c       -> %c", c);
    klog_info("%%s       -> %s", s);
    klog_info("%%p       -> %p", p);

    /* length modifiers */
    klog_info("%%hhd     -> %hhd", small);
    klog_info("%%hd      -> %hd", small);
    klog_info("%%lu      -> %lu", u_big);
    klog_info("%%lX      -> %lX", u_big);
    klog_info("%%zd      -> %zd", small);

    klog_info("printf_selftest end");
}
