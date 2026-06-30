#include "splash.h"
#include "vga_text.h"
#include "timer.h"
#include "string.h"
#include "stdio.h"
#include "version.h"
#include "serial.h"

#define SPLASH_DURATION_MS 1000

static const char *funs_art[] = {
    "    ______                       ",
    "   |  ____|                      ",
    "   | |__ _   _ _ __  ___  ___    ",
    "   |  __| | | | '_ \\/ __|/ __|   ",
    "   | |  | |_| | | | \\__ \\ (__    ",
    "   |_|   \\__,_|_| |_|___/\\___|   ",
    "                                "
};

#define ART_HEIGHT 7

static void splash_print_centered(const char *line, int row, uint8_t fg, uint8_t bg) {
    int len = (int)strlen(line);
    int col = (VGA_WIDTH - len) / 2;
    if (col < 0) col = 0;
    if (row < 0 || row >= VGA_HEIGHT) return;

    volatile uint16_t *vga = (volatile uint16_t *)VGA_BUFFER;
    uint16_t color = (uint16_t)((bg << 4) | fg);
    for (int i = 0; i < len && (col + i) < VGA_WIDTH; i++) {
        vga[row * VGA_WIDTH + col + i] = (uint16_t)(line[i] | (color << 8));
    }
}

static void splash_draw_border(int top, int bottom, uint8_t fg, uint8_t bg) {
    if (top < 0) top = 0;
    if (bottom >= VGA_HEIGHT) bottom = VGA_HEIGHT - 1;
    if (top >= bottom) return;

    volatile uint16_t *vga = (volatile uint16_t *)VGA_BUFFER;
    uint16_t color = (uint16_t)((bg << 4) | fg);

    for (int col = 1; col < VGA_WIDTH - 1; col++) {
        vga[top * VGA_WIDTH + col] = (uint16_t)('-' | (color << 8));
        vga[bottom * VGA_WIDTH + col] = (uint16_t)('-' | (color << 8));
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

static void splash_simple_delay(uint32_t ticks) {
    uint32_t start = timer_get_ticks();
    uint32_t max_wait = 0xFFFFFFFF;
    (void)max_wait;

    if (ticks == 0) ticks = 1;

    for (uint32_t i = 0; i < 0xFFFFFFFF; i++) {
        uint32_t current = timer_get_ticks();
        uint32_t elapsed = current - start;
        if (elapsed >= ticks) {
            return;
        }
        if (i > 100000000) {
            return;
        }
    }
}

void splash_show(void) {
    serial_print(COM1, "[SPLASH] Starting splash screen...\n");

    vga_text_clear();

    int total_height = ART_HEIGHT + 6;
    int start_row = (VGA_HEIGHT - total_height) / 2;
    if (start_row < 0) start_row = 0;

    int border_top = start_row - 1;
    int border_bottom = start_row + total_height;
    if (border_bottom >= VGA_HEIGHT) border_bottom = VGA_HEIGHT - 1;

    splash_draw_border(border_top, border_bottom, VGA_COLOR_CYAN, VGA_COLOR_BLACK);

    int funs_row = start_row + 1;
    for (int i = 0; i < ART_HEIGHT; i++) {
        splash_print_centered(funs_art[i], funs_row + i, VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    }

    int subtitle_row = funs_row + ART_HEIGHT + 1;
    char subtitle[64];
    snprintf(subtitle, sizeof(subtitle), "Version %s - Welcome to FUNSOS", KERNEL_VERSION);
    splash_print_centered(subtitle, subtitle_row, VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    const char *loading_chars = "|/-\\";
    int loading_idx = 0;
    int total_steps = 10;
    uint32_t step_ticks = 10;

    serial_print(COM1, "[SPLASH] Animation loop starting...\n");

    for (int step = 0; step < total_steps; step++) {
        char loading_msg[80];
        int percent = (step * 100) / total_steps;
        snprintf(loading_msg, sizeof(loading_msg),
                 " Booting... %c  [%d%%] ",
                 loading_chars[loading_idx],
                 percent);

        int msg_len = (int)strlen(loading_msg);
        int msg_col = (VGA_WIDTH - msg_len) / 2;
        if (msg_col < 0) msg_col = 0;

        volatile uint16_t *vga = (volatile uint16_t *)VGA_BUFFER;
        uint16_t color = (uint16_t)((VGA_COLOR_BLACK << 4) | VGA_COLOR_LIGHT_CYAN);
        for (int i = 0; i < msg_len && (msg_col + i) < VGA_WIDTH; i++) {
            vga[(subtitle_row + 2) * VGA_WIDTH + msg_col + i] =
                (uint16_t)(loading_msg[i] | (color << 8));
        }

        loading_idx = (loading_idx + 1) % 4;
        splash_simple_delay(step_ticks);
    }

    serial_print(COM1, "[SPLASH] Animation done, clearing screen...\n");

    vga_text_clear();
    vga_text_set_cursor(0, 0);

    serial_print(COM1, "[SPLASH] Splash screen finished, entering shell...\n");
}
