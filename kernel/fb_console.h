#ifndef FB_CONSOLE_H
#define FB_CONSOLE_H

#include "stdint.h"

void fb_console_init(uint32_t *fb, uint32_t width, uint32_t height, uint32_t pitch, uint32_t bpp);
void fb_console_putchar(char c);
void fb_console_write(const char *str);
void fb_console_clear(void);
void fb_console_set_color(uint32_t fg, uint32_t bg);
void fb_console_set_fg(uint32_t color);
void fb_console_set_bg(uint32_t color);
uint32_t fb_console_get_fg(void);
uint32_t fb_console_get_bg(void);
void fb_console_scroll(uint32_t lines);
void fb_console_scroll_up(int lines);
void fb_console_scroll_down(int lines);
void fb_console_blink_cursor(void);

#endif
