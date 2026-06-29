#ifndef FBCONSOLE_H
#define FBCONSOLE_H

#include "stdint.h"

#define FBCON_THEME_DEFAULT 0
#define FBCON_THEME_DARK    1
#define FBCON_THEME_LIGHT   2
#define FBCON_THEME_RETRO   3
#define FBCON_THEME_CYBER   4
#define FBCON_THEME_FOREST  5
#define FBCON_THEME_OCEAN   6
#define FBCON_THEME_SUNSET  7
#define FBCON_THEME_COUNT   8

#define FBCON_SCROLLBACK_MAX 1000
#define FBCON_MAX_FONT_SCALE 4

typedef struct {
    uint32_t fg;
    uint32_t bg;
    uint32_t selection_fg;
    uint32_t selection_bg;
    uint32_t cursor_fg;
    uint32_t cursor_bg;
    uint32_t border_color;
    char     name[32];
} fbcon_theme_t;

int  fbconsole_init(uint32_t *fb, uint32_t width, uint32_t height, uint32_t pitch, uint32_t bpp);
void fbconsole_write(const char *str);
void fbconsole_putchar(char c);
void fbconsole_clear(void);
void fbconsole_scroll_up(int lines);
void fbconsole_scroll_down(int lines);
void fbconsole_set_theme(int theme_id);
int  fbconsole_get_theme(void);
int  fbconsole_set_font_scale(int scale);
int  fbconsole_get_font_scale(void);
void fbconsole_cursor_home(void);
void fbconsole_cursor_end(void);
const fbcon_theme_t *fbconsole_get_theme_info(int theme_id);
int  fbconsole_get_theme_count(void);

#endif
