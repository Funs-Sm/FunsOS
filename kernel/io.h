#ifndef IO_H
#define IO_H

#include "stdint.h"

/* I/O port access.  Force inlining with __attribute__((always_inline))
 * because GCC 16 will otherwise refuse to inline `static inline`
 * functions whose port operand is not a compile-time constant in
 * [0, 255].  When that happens, the function is emitted as a global
 * `_outb`/`_inb` symbol that the linker may resolve to some other
 * translation unit's copy, producing silent corruption of port I/O
 * and an early boot hang in the kernel. */
#define ALWAYS_INLINE static inline __attribute__((always_inline))

ALWAYS_INLINE void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "d"(port));
}

ALWAYS_INLINE void outb_var(uint16_t port, uint8_t val) {
    outb(port, val);
}

ALWAYS_INLINE uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "d"(port));
    return ret;
}

ALWAYS_INLINE uint8_t inb_var(uint16_t port) {
    return inb(port);
}

ALWAYS_INLINE void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "d"(port));
}

ALWAYS_INLINE void outw_var(uint16_t port, uint16_t val) {
    outw(port, val);
}

ALWAYS_INLINE uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "d"(port));
    return ret;
}

ALWAYS_INLINE uint16_t inw_var(uint16_t port) {
    return inw(port);
}

ALWAYS_INLINE void outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "d"(port));
}

ALWAYS_INLINE void outl_var(uint16_t port, uint32_t val) {
    outl(port, val);
}

ALWAYS_INLINE uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "d"(port));
    return ret;
}

ALWAYS_INLINE uint32_t inl_var(uint16_t port) {
    return inl(port);
}

ALWAYS_INLINE void io_wait(void) {
    outb(0x80, 0);
}

#endif
