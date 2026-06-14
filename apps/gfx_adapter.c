#include "gfx_adapter.h"

static unsigned int *g_fb;
static int g_screen_w;
static int g_screen_h;

static const unsigned char font8x16[128][16] = {{0}};

void gfx_adapter_init(unsigned int *fb, int width, int height)
{
    g_fb = fb;
    g_screen_w = width;
    g_screen_h = height;
}

int gfx_adapter_is_initialized(void)
{
    return g_fb != (void *)0;
}

unsigned int *gfx_adapter_get_fb(void)
{
    return g_fb;
}

void fb_draw_rect(int x, int y, int w, int h, unsigned int color)
{
    int i, j;
    for (j = y; j < y + h; j++) {
        for (i = x; i < x + w; i++) {
            if (i >= 0 && i < g_screen_w && j >= 0 && j < g_screen_h)
                g_fb[j * g_screen_w + i] = color;
        }
    }
}

void fb_draw_char(int x, int y, char c, unsigned int fg, unsigned int bg)
{
    int i, j;
    if ((unsigned char)c > 127) return;
    for (j = 0; j < 16; j++) {
        unsigned char row = font8x16[(unsigned char)c][j];
        for (i = 0; i < 8; i++) {
            int px = x + i;
            int py = y + j;
            if (px >= 0 && px < g_screen_w && py >= 0 && py < g_screen_h)
                g_fb[py * g_screen_w + px] = (row & (0x80 >> i)) ? fg : bg;
        }
    }
}

void fb_draw_char_transparent(int x, int y, char c, unsigned int fg)
{
    int i, j;
    if ((unsigned char)c > 127) return;
    for (j = 0; j < 16; j++) {
        unsigned char row = font8x16[(unsigned char)c][j];
        for (i = 0; i < 8; i++) {
            if (row & (0x80 >> i)) {
                int px = x + i;
                int py = y + j;
                if (px >= 0 && px < g_screen_w && py >= 0 && py < g_screen_h)
                    g_fb[py * g_screen_w + px] = fg;
            }
        }
    }
}

void fb_draw_string(int x, int y, const char *s, unsigned int fg, unsigned int bg)
{
    while (*s) {
        fb_draw_char(x, y, *s, fg, bg);
        x += 8;
        s++;
    }
}

void fb_draw_string_transparent(int x, int y, const char *s, unsigned int fg)
{
    while (*s) {
        fb_draw_char_transparent(x, y, *s, fg);
        x += 8;
        s++;
    }
}
