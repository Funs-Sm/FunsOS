#ifndef PANIC_H
#define PANIC_H

#include "kernel_types.h"

void kernel_panic(const char *msg, const char *file, int line);
void panic_register_dump(regs_t *regs);
void vga_print(const char *str);
void vga_print_hex(uint32_t value);
void vga_set_color(uint8_t fg, uint8_t bg);
void vga_clear(void);

#define KERNEL_PANIC(msg) kernel_panic(msg, __FILE__, __LINE__)

#endif
