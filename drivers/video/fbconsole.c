#include "fbconsole.h"
#include "klog.h"
#include "kheap.h"
#include "string.h"
#include "font.h"

#define FBCON_CHAR_W (FONT_GLYPH_WIDTH * fbcon.font_scale)
#define FBCON_CHAR_H (FONT_GLYPH_HEIGHT * fbcon.font_scale)

typedef struct {
    uint32_t *fb;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t cursor_x;
    uint32_t cursor_y;
    uint32_t cols;
    uint32_t rows;
    int theme_id;
    int font_scale;
    int initialized;
} fbcon_t;

static fbcon_t fbcon;

static const fbcon_theme_t themes[FBCON_THEME_COUNT] = {
    [FBCON_THEME_DEFAULT] = {
        .fg = 0xFFFFFF, .bg = 0x000000,
        .selection_fg = 0x000000, .selection_bg = 0xAAAAAA,
        .cursor_fg = 0xFFFFFF, .cursor_bg = 0xFFFFFF,
        .border_color = 0x333333,
        .name = "Default"
    },
    [FBCON_THEME_DARK] = {
        .fg = 0xE0E0E0, .bg = 0x1E1E1E,
        .selection_fg = 0xFFFFFF, .selection_bg = 0x264F78,
        .cursor_fg = 0xE0E0E0, .cursor_bg = 0xE0E0E0,
        .border_color = 0x3C3C3C,
        .name = "Dark"
    },
    [FBCON_THEME_LIGHT] = {
        .fg = 0x000000, .bg = 0xFFFFFF,
        .selection_fg = 0xFFFFFF, .selection_bg = 0x0078D7,
        .cursor_fg = 0x000000, .cursor_bg = 0x000000,
        .border_color = 0xE0E0E0,
        .name = "Light"
    },
    [FBCON_THEME_RETRO] = {
        .fg = 0x00FF00, .bg = 0x000000,
        .selection_fg = 0x000000, .selection_bg = 0x00FF00,
        .cursor_fg = 0x00FF00, .cursor_bg = 0x00FF00,
        .border_color = 0x003300,
        .name = "Retro"
    },
    [FBCON_THEME_CYBER] = {
        .fg = 0x00FFFF, .bg = 0x0A0A1A,
        .selection_fg = 0x000000, .selection_bg = 0xFF00FF,
        .cursor_fg = 0xFFFF00, .cursor_bg = 0xFFFF00,
        .border_color = 0xFF00FF,
        .name = "Cyberpunk"
    },
    [FBCON_THEME_FOREST] = {
        .fg = 0x90EE90, .bg = 0x0D1F0D,
        .selection_fg = 0x000000, .selection_bg = 0x90EE90,
        .cursor_fg = 0x90EE90, .cursor_bg = 0x90EE90,
        .border_color = 0x228B22,
        .name = "Forest"
    },
    [FBCON_THEME_OCEAN] = {
        .fg = 0x87CEEB, .bg = 0x0A1628,
        .selection_fg = 0xFFFFFF, .selection_bg = 0x4169E1,
        .cursor_fg = 0x87CEEB, .cursor_bg = 0x87CEEB,
        .border_color = 0x1E90FF,
        .name = "Ocean"
    },
    [FBCON_THEME_SUNSET] = {
        .fg = 0xFFD700, .bg = 0x1A0A00,
        .selection_fg = 0x000000, .selection_bg = 0xFF6347,
        .cursor_fg = 0xFFD700, .cursor_bg = 0xFFD700,
        .border_color = 0xFF4500,
        .name = "Sunset"
    }
};

static void draw_pixel(int x, int y, uint32_t color) {
    if (!fbcon.fb || x < 0 || (uint32_t)x >= fbcon.width || y < 0 || (uint32_t)y >= fbcon.height) {
        return;
    }
    fbcon.fb[y * fbcon.pitch / 4 + x] = color;
}

static void draw_rect(int x, int y, int w, int h, uint32_t color) {
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            draw_pixel(x + i, y + j, color);
        }
    }
}

static void draw_char_raw(int x, int y, char c, uint32_t fg, uint32_t bg) {
    if (!fbcon.fb) return;

    int glyph_idx = (c >= FONT_FIRST_CHAR && c <= FONT_LAST_CHAR) ? (c - FONT_FIRST_CHAR) : 0;
    const uint8_t *glyph = font_data[glyph_idx];

    for (int j = 0; j < FONT_GLYPH_HEIGHT; j++) {
        uint8_t line = glyph[j];
        for (int i = 0; i < FONT_GLYPH_WIDTH; i++) {
            if (line & (1 << (7 - i))) {
                draw_pixel(x + i, y + j, fg);
            } else {
                draw_pixel(x + i, y + j, bg);
            }
        }
    }
}

static void draw_char(int x, int y, char c, uint32_t fg, uint32_t bg) {
    if (fbcon.font_scale <= 1) {
        draw_char_raw(x, y, c, fg, bg);
        return;
    }

    int scale = fbcon.font_scale;
    int glyph_idx = (c >= FONT_FIRST_CHAR && c <= FONT_LAST_CHAR) ? (c - FONT_FIRST_CHAR) : 0;
    const uint8_t *glyph = font_data[glyph_idx];

    for (int j = 0; j < FONT_GLYPH_HEIGHT; j++) {
        uint8_t line = glyph[j];
        for (int i = 0; i < FONT_GLYPH_WIDTH; i++) {
            uint32_t color = (line & (1 << (7 - i))) ? fg : bg;
            draw_rect(x + i * scale, y + j * scale, scale, scale, color);
        }
    }
}

static void scroll_up(void) {
    if (!fbcon.fb) return;

    uint32_t char_h = FBCON_CHAR_H;
    uint32_t line_size = char_h * fbcon.pitch / 4;
    uint32_t total_lines = fbcon.height / char_h;
    (void)total_lines;

    uint32_t *src = fbcon.fb + line_size;
    uint32_t count = (fbcon.height - char_h) * fbcon.pitch / 4;
    for (uint32_t i = 0; i < count; i++) {
        fbcon.fb[i] = src[i];
    }

    draw_rect(0, (int)(fbcon.height - char_h), (int)fbcon.width, (int)char_h,
              themes[fbcon.theme_id].bg);
}

static void new_line(void) {
    fbcon.cursor_x = 0;
    fbcon.cursor_y++;
    if (fbcon.cursor_y >= fbcon.rows) {
        scroll_up();
        fbcon.cursor_y = fbcon.rows - 1;
    }
}

static void carriage_return(void) {
    fbcon.cursor_x = 0;
}

static void backspace(void) {
    if (fbcon.cursor_x > 0) {
        fbcon.cursor_x--;
    } else if (fbcon.cursor_y > 0) {
        fbcon.cursor_y--;
        fbcon.cursor_x = fbcon.cols - 1;
    }

    int x = (int)(fbcon.cursor_x * FBCON_CHAR_W);
    int y = (int)(fbcon.cursor_y * FBCON_CHAR_H);
    draw_rect(x, y, (int)FBCON_CHAR_W, (int)FBCON_CHAR_H, themes[fbcon.theme_id].bg);
}

void fbconsole_putchar(char c) {
    if (!fbcon.initialized || !fbcon.fb) return;

    switch (c) {
    case '\n':
        new_line();
        break;
    case '\r':
        carriage_return();
        break;
    case '\b':
        backspace();
        break;
    case '\t':
        fbcon.cursor_x = (fbcon.cursor_x + 8) & ~7u;
        if (fbcon.cursor_x >= fbcon.cols) {
            new_line();
        }
        break;
    default:
        {
            int x = (int)(fbcon.cursor_x * FBCON_CHAR_W);
            int y = (int)(fbcon.cursor_y * FBCON_CHAR_H);
            draw_char(x, y, c, themes[fbcon.theme_id].fg, themes[fbcon.theme_id].bg);
            fbcon.cursor_x++;
            if (fbcon.cursor_x >= fbcon.cols) {
                new_line();
            }
        }
        break;
    }
}

void fbconsole_write(const char *str) {
    if (!str) return;
    while (*str) {
        fbconsole_putchar(*str++);
    }
}

void fbconsole_clear(void) {
    if (!fbcon.fb) return;
    draw_rect(0, 0, (int)fbcon.width, (int)fbcon.height, themes[fbcon.theme_id].bg);
    fbcon.cursor_x = 0;
    fbcon.cursor_y = 0;
}

void fbconsole_scroll_up(int lines) {
    if (lines <= 0) return;
    for (int i = 0; i < lines; i++) {
        scroll_up();
    }
}

void fbconsole_scroll_down(int lines) {
    (void)lines;
}

void fbconsole_set_theme(int theme_id) {
    if (theme_id < 0 || theme_id >= FBCON_THEME_COUNT) return;
    fbcon.theme_id = theme_id;
}

int fbconsole_get_theme(void) {
    return fbcon.theme_id;
}

int fbconsole_set_font_scale(int scale) {
    if (scale < 1 || scale > FBCON_MAX_FONT_SCALE) return -1;
    fbcon.font_scale = scale;
    fbcon.cols = fbcon.width / FBCON_CHAR_W;
    fbcon.rows = fbcon.height / FBCON_CHAR_H;
    return 0;
}

int fbconsole_get_font_scale(void) {
    return fbcon.font_scale;
}

void fbconsole_cursor_home(void) {
    fbcon.cursor_x = 0;
    fbcon.cursor_y = 0;
}

void fbconsole_cursor_end(void) {
    fbcon.cursor_x = fbcon.cols - 1;
    fbcon.cursor_y = fbcon.rows - 1;
}

const fbcon_theme_t *fbconsole_get_theme_info(int theme_id) {
    if (theme_id < 0 || theme_id >= FBCON_THEME_COUNT) return NULL;
    return &themes[theme_id];
}

int fbconsole_get_theme_count(void) {
    return FBCON_THEME_COUNT;
}

int fbconsole_init(uint32_t *fb, uint32_t width, uint32_t height, uint32_t pitch, uint32_t bpp) {
    memset(&fbcon, 0, sizeof(fbcon));
    fbcon.fb = fb;
    fbcon.width = width;
    fbcon.height = height;
    fbcon.pitch = pitch;
    fbcon.bpp = bpp;
    fbcon.theme_id = FBCON_THEME_DEFAULT;
    fbcon.font_scale = 1;
    fbcon.cursor_x = 0;
    fbcon.cursor_y = 0;

    fbcon.cols = width / FONT_GLYPH_WIDTH;
    fbcon.rows = height / FONT_GLYPH_HEIGHT;
    fbcon.initialized = 1;

    fbconsole_clear();

    klog_info("fbconsole: initialized %ux%u, %ux%u chars, %u bpp\n",
              width, height, fbcon.cols, fbcon.rows, bpp);

    return 0;
}
