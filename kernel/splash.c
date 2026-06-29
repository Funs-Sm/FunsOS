#include "splash.h"
#include "vga_text.h"
#include "timer.h"
#include "string.h"
#include "stdio.h"
#include "version.h"

#define SPLASH_DURATION_MS 1000

static const char *hello_art[] = {
    "  _    _          _ _         ",
    " | |  | |        | | |        ",
    " | |__| | ___  __| | | ___    ",
    " |  __  |/ _ \\/ _` | |/ _ \\   ",
    " | |  | |  __/ (_| | | (_) |  ",
    " |_|  |_|\\___|\\__,_|_|\\___/   ",
    "                              "
};

static const char *funsos_art[] = {
    "  ______                       ",
    " |  ____|                      ",
    " | |__ _   _ _ __  ___  ___    ",
    " |  __| | | | '_ \\/ __|/ __|   ",
    " | |  | |_| | | | \\__ \\ (__    ",
    " |_|   \\__,_|_| |_|___/\\___|   ",
    "                              "
};

#define ART_HEIGHT 7

static void splash_print_centered(const char *line, int row, uint8_t fg, uint8_t bg) {
    int len = strlen(line);
    int col = (VGA_WIDTH - len) / 2;
    if (col < 0) col = 0;

    vga_text_set_color(fg, bg);
    vga_text_set_cursor(row, col);

    volatile uint16_t *vga = (volatile uint16_t *)VGA_BUFFER;
    uint16_t color = (uint16_t)((bg << 4) | fg);
    for (int i = 0; i < len && (col + i) < VGA_WIDTH; i++) {
        vga[row * VGA_WIDTH + col + i] = (uint16_t)(line[i] | (color << 8));
    }
}

static void splash_draw_border(int top, int bottom, uint8_t fg, uint8_t bg) {
    volatile uint16_t *vga = (volatile uint16_t *)VGA_BUFFER;
    uint16_t color = (uint16_t)((bg << 4) | fg);

    for (int col = 1; col < VGA_WIDTH - 1; col++) {
        vga[top * VGA_WIDTH + col] = (uint16_t)('=' | (color << 8));
        vga[bottom * VGA_WIDTH + col] = (uint16_t)('=' | (color << 8));
    }

    for (int row = top + 1; row < bottom; row++) {
        vga[row * VGA_WIDTH + 0] = (uint16_t)('|' | (color << 8));
        vga[row * VGA_WIDTH + VGA_WIDTH - 1] = (uint16_t)('|' | (color << 8));
    }

    vga[top * VGA_WIDTH + 0] = (uint16_t)('+' | (color << 8));
    vga[top * VGA_WIDTH + VGA_WIDTH - 1] = (uint16_t)('+' | (color << 8));
    vga[bottom * VGA_WIDTH + 0] = (uint16_t)('+' | (color << 8));
    vga[bottom * VGA_WIDTH + VGA_WIDTH - 1] = (uint16_t)('+' | (color << 8));
}

void splash_show(void) {
    vga_text_clear();

    int total_height = ART_HEIGHT * 2 + 6;
    int start_row = (VGA_HEIGHT - total_height) / 2;
    if (start_row < 0) start_row = 0;

    int border_top = start_row - 1;
    int border_bottom = start_row + total_height;
    if (border_bottom >= VGA_HEIGHT) border_bottom = VGA_HEIGHT - 1;

    splash_draw_border(border_top, border_bottom, VGA_COLOR_CYAN, VGA_COLOR_BLACK);

    int hello_row = start_row + 1;
    for (int i = 0; i < ART_HEIGHT; i++) {
        splash_print_centered(hello_art[i], hello_row + i, VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    }

    int funsos_row = start_row + ART_HEIGHT + 3;
    for (int i = 0; i < ART_HEIGHT; i++) {
        splash_print_centered(funsos_art[i], funsos_row + i, VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    }

    int subtitle_row = funsos_row + ART_HEIGHT + 1;
    char subtitle[64];
    snprintf(subtitle, sizeof(subtitle), "Version %s - Welcome to FUNSOS", KERNEL_VERSION);
    splash_print_centered(subtitle, subtitle_row, VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    vga_text_set_cursor(VGA_HEIGHT - 1, 0);

    uint32_t start_ticks = timer_get_ticks();
    uint32_t start_ms = start_ticks * 10;

    const char *loading_chars = "|/-\\";
    int loading_idx = 0;
    uint32_t last_update = 0;

    while (1) {
        uint32_t current_ticks = timer_get_ticks();
        uint32_t current_ms = current_ticks * 10;
        uint32_t elapsed = current_ms - start_ms;

        if (elapsed >= SPLASH_DURATION_MS)
            break;

        if (current_ms - last_update > 100) {
            char loading_msg[80];
            snprintf(loading_msg, sizeof(loading_msg),
                     " Loading... %c  [%d%%] ",
                     loading_chars[loading_idx],
                     (int)(elapsed * 100 / SPLASH_DURATION_MS));

            int msg_len = strlen(loading_msg);
            int msg_col = (VGA_WIDTH - msg_len) / 2;
            if (msg_col < 0) msg_col = 0;

            vga_text_set_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
            vga_text_set_cursor(subtitle_row + 2, msg_col);

            volatile uint16_t *vga = (volatile uint16_t *)VGA_BUFFER;
            uint16_t color = (uint16_t)((VGA_COLOR_BLACK << 4) | VGA_COLOR_LIGHT_CYAN);
            for (int i = 0; i < msg_len && (msg_col + i) < VGA_WIDTH; i++) {
                vga[(subtitle_row + 2) * VGA_WIDTH + msg_col + i] =
                    (uint16_t)(loading_msg[i] | (color << 8));
            }

            loading_idx = (loading_idx + 1) % 4;
            last_update = current_ms;
        }
    }

    vga_text_clear();
}
