#ifndef VGA_TEXT_H
#define VGA_TEXT_H

#include "stdint.h"

#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define VGA_BUFFER  0xB8000

#define VGA_COLOR_BLACK       0
#define VGA_COLOR_BLUE        1
#define VGA_COLOR_GREEN       2
#define VGA_COLOR_CYAN        3
#define VGA_COLOR_RED         4
#define VGA_COLOR_MAGENTA     5
#define VGA_COLOR_BROWN       6
#define VGA_COLOR_LIGHT_GREY  7
#define VGA_COLOR_DARK_GREY   8
#define VGA_COLOR_LIGHT_BLUE  9
#define VGA_COLOR_LIGHT_GREEN 10
#define VGA_COLOR_LIGHT_CYAN  11
#define VGA_COLOR_LIGHT_RED   12
#define VGA_COLOR_LIGHT_MAGENTA 13
#define VGA_COLOR_YELLOW      14
#define VGA_COLOR_WHITE       15

void vga_text_mode3_switch(void);
void vga_text_init(void);
void vga_text_putchar(char c);
void vga_text_print(const char *str);
void vga_text_set_color(uint8_t fg, uint8_t bg);
void vga_text_clear(void);
void vga_text_set_cursor(int row, int col);
void vga_text_get_cursor(int *row, int *col);
void vga_text_dump_screen(void);
void vga_text_scroll(void);
void vga_text_print_hex(uint32_t val);
void vga_text_print_dec(uint32_t val);
void vga_text_font_diagnostic(void);

void vga_text_scroll_up(int lines);
void vga_text_scroll_down(int lines);
void vga_text_scroll_home(void);
void vga_text_scroll_end(void);
int vga_text_in_scrollback(void);

#endif
