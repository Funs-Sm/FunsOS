#ifndef GFX_ADAPTER_H
#define GFX_ADAPTER_H

void gfx_adapter_init(unsigned int *fb, int width, int height);
int  gfx_adapter_is_initialized(void);
unsigned int *gfx_adapter_get_fb(void);

void fb_draw_rect(int x, int y, int w, int h, unsigned int color);
void fb_draw_char(int x, int y, char c, unsigned int fg, unsigned int bg);
void fb_draw_char_transparent(int x, int y, char c, unsigned int fg);
void fb_draw_string(int x, int y, const char *s, unsigned int fg, unsigned int bg);
void fb_draw_string_transparent(int x, int y, const char *s, unsigned int fg);

#endif
