#include "splash.h"
#include "vga_text.h"
#include "timer.h"
#include "string.h"
#include "stdio.h"
#include "version.h"
#include "serial.h"

#define SPLASH_SECONDS 3

static void splash_puts(int row, int col, const char *str, uint8_t fg, uint8_t bg) {
    if (row < 0 || row >= VGA_HEIGHT) return;
    if (col < 0 || col >= VGA_WIDTH) return;

    volatile uint16_t *vga = (volatile uint16_t *)VGA_BUFFER;
    uint16_t color = (uint16_t)((bg << 4) | fg);
    int i = 0;
    while (str[i] && (col + i) < VGA_WIDTH) {
        vga[row * VGA_WIDTH + col + i] = (uint16_t)(str[i] | (color << 8));
        i++;
    }
}

static void splash_draw_border(void) {
    volatile uint16_t *vga = (volatile uint16_t *)VGA_BUFFER;
    uint16_t color = (uint16_t)((VGA_COLOR_BLACK << 4) | VGA_COLOR_CYAN);

    for (int col = 0; col < VGA_WIDTH; col++) {
        vga[0 * VGA_WIDTH + col] = (uint16_t)('-' | (color << 8));
        vga[(VGA_HEIGHT - 1) * VGA_WIDTH + col] = (uint16_t)('-' | (color << 8));
    }

    for (int row = 1; row < VGA_HEIGHT - 1; row++) {
        vga[row * VGA_WIDTH + 0] = (uint16_t)('|' | (color << 8));
        vga[row * VGA_WIDTH + VGA_WIDTH - 1] = (uint16_t)('|' | (color << 8));
    }

    vga[0 * VGA_WIDTH + 0] = (uint16_t)('+' | (color << 8));
    vga[0 * VGA_WIDTH + VGA_WIDTH - 1] = (uint16_t)('+' | (color << 8));
    vga[(VGA_HEIGHT - 1) * VGA_WIDTH + 0] = (uint16_t)('+' | (color << 8));
    vga[(VGA_HEIGHT - 1) * VGA_WIDTH + VGA_WIDTH - 1] = (uint16_t)('+' | (color << 8));
}

static void splash_wait_seconds(uint32_t seconds) {
    uint32_t start = timer_get_ticks();
    uint32_t target = seconds * 100;

    uint32_t max_loop = 200000000;
    volatile uint32_t count = 0;

    while (count < max_loop) {
        uint32_t now = timer_get_ticks();
        if (now - start >= target) {
            return;
        }
        count++;
        for (volatile int i = 0; i < 50; i++) {
            asm volatile("nop");
        }
    }
}

void splash_show(void) {
    serial_print(COM1, "[SPLASH] Showing splash screen...\n");

    vga_text_clear();
    splash_draw_border();

    const char *title = "FUNSOS";
    int title_len = (int)strlen(title);
    int title_col = (VGA_WIDTH - title_len) / 2;
    int title_row = VGA_HEIGHT / 2 - 3;

    {
        volatile uint16_t *vga = (volatile uint16_t *)VGA_BUFFER;
        uint16_t color = (uint16_t)((VGA_COLOR_BLACK << 4) | VGA_COLOR_GREEN);
        for (int i = 0; i < title_len; i++) {
            vga[title_row * VGA_WIDTH + title_col + i] =
                (uint16_t)(title[i] | (color << 8));
        }
    }

    char version_str[64];
    snprintf(version_str, sizeof(version_str), "Version %s - Welcome to FUNSOS", KERNEL_VERSION);
    int ver_len = (int)strlen(version_str);
    int ver_col = (VGA_WIDTH - ver_len) / 2;
    splash_puts(title_row + 2, ver_col, version_str, VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

    const char *boot_msg = "Booting...";
    int msg_len = (int)strlen(boot_msg);
    int msg_col = (VGA_WIDTH - msg_len) / 2;
    splash_puts(title_row + 4, msg_col, boot_msg, VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);

    serial_print(COM1, "[SPLASH] Waiting 3 seconds...\n");
    splash_wait_seconds(SPLASH_SECONDS);
    serial_print(COM1, "[SPLASH] Done, entering shell.\n");

    vga_text_clear();
    vga_text_set_cursor(0, 0);
}
