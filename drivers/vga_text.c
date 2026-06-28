#include "vga_text.h"
#include "io.h"
#include "serial.h"
#include "../gui/font.h"

#define VGA_HISTORY_SIZE 400

static int cursor_row = 0;
static int cursor_col = 0;
static uint8_t current_color = 0;

static uint16_t history[VGA_HISTORY_SIZE][VGA_WIDTH];
static int history_count = 0;

static uint16_t current_line[VGA_WIDTH];

static int sb_view = 0;
static int sb_active = 0;

static void update_hw_cursor(void);
static void redraw_screen(void);

static inline uint16_t make_cell(char c) {
    return (uint16_t)((uint16_t)c | ((uint16_t)current_color << 8));
}

static void clear_line_buf(uint16_t *line) {
    for (int i = 0; i < VGA_WIDTH; i++) line[i] = make_cell(' ');
}

void vga_text_mode3_switch(void) {
    volatile uint16_t *buf = (volatile uint16_t *)VGA_BUFFER;
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        buf[i] = make_cell(' ');
    }
    cursor_row = 0;
    cursor_col = 0;
    history_count = 0;
    sb_view = 0;
    sb_active = 0;
    clear_line_buf(current_line);
    update_hw_cursor();
}

static void update_hw_cursor(void) {
    int disp_row = cursor_row;
    int disp_col = cursor_col;
    if (sb_active) {
        disp_row = VGA_HEIGHT - 1;
        disp_col = VGA_WIDTH - 1;
    }
    uint16_t pos = (uint16_t)(disp_row * VGA_WIDTH + disp_col);
    outb(0x3D4, 14);
    outb(0x3D5, (uint8_t)(pos >> 8));
    outb(0x3D4, 15);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
}

static void commit_current_line(void) {
    if (history_count < VGA_HISTORY_SIZE) {
        for (int i = 0; i < VGA_WIDTH; i++) {
            history[history_count][i] = current_line[i];
        }
        history_count++;
    } else {
        for (int s = 0; s < VGA_HISTORY_SIZE - 1; s++) {
            for (int i = 0; i < VGA_WIDTH; i++) {
                history[s][i] = history[s + 1][i];
            }
        }
        for (int i = 0; i < VGA_WIDTH; i++) {
            history[VGA_HISTORY_SIZE - 1][i] = current_line[i];
        }
    }
    clear_line_buf(current_line);
}

static void redraw_screen(void) {
    volatile uint16_t *vga = (volatile uint16_t *)VGA_BUFFER;

    if (sb_active) {
        int start = sb_view;
        for (int row = 0; row < VGA_HEIGHT - 1; row++) {
            int hidx = start + row;
            for (int col = 0; col < VGA_WIDTH; col++) {
                if (hidx >= 0 && hidx < history_count) {
                    vga[row * VGA_WIDTH + col] = history[hidx][col];
                } else {
                    vga[row * VGA_WIDTH + col] = make_cell(' ');
                }
            }
        }
        for (int col = 0; col < VGA_WIDTH; col++) {
            vga[(VGA_HEIGHT - 1) * VGA_WIDTH + col] = make_cell(' ');
        }
        {
            int col = 0;
            const char *msg = "-- SCROLLBACK";
            uint8_t yellow = (VGA_COLOR_BLACK << 4) | VGA_COLOR_YELLOW;
            for (int i = 0; msg[i] && col < VGA_WIDTH; i++, col++) {
                vga[(VGA_HEIGHT - 1) * VGA_WIDTH + col] =
                    (uint16_t)(msg[i] | ((uint16_t)yellow << 8));
            }
        }
    } else {
        int visible_history = VGA_HEIGHT - 1;
        int start = history_count - visible_history;
        if (start < 0) start = 0;

        for (int row = 0; row < VGA_HEIGHT - 1; row++) {
            int hidx = start + row;
            for (int col = 0; col < VGA_WIDTH; col++) {
                if (hidx < history_count) {
                    vga[row * VGA_WIDTH + col] = history[hidx][col];
                } else {
                    vga[row * VGA_WIDTH + col] = make_cell(' ');
                }
            }
        }

        for (int col = 0; col < VGA_WIDTH; col++) {
            vga[(VGA_HEIGHT - 1) * VGA_WIDTH + col] = current_line[col];
        }
    }
    update_hw_cursor();
}

void vga_text_init(void) {
    current_color = (uint8_t)((VGA_COLOR_BLACK << 4) | VGA_COLOR_LIGHT_GREY);
    history_count = 0;
    sb_view = 0;
    sb_active = 0;
    cursor_row = 0;
    cursor_col = 0;
    clear_line_buf(current_line);
    vga_text_clear();
}

void vga_text_set_color(uint8_t fg, uint8_t bg) {
    current_color = (uint8_t)((bg << 4) | fg);
}

void vga_text_clear(void) {
    volatile uint16_t *buf = (volatile uint16_t *)VGA_BUFFER;
    uint16_t blank = make_cell(' ');
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        buf[i] = blank;
    }
    history_count = 0;
    sb_active = 0;
    sb_view = 0;
    cursor_row = 0;
    cursor_col = 0;
    clear_line_buf(current_line);
    update_hw_cursor();
}

void vga_text_scroll(void) {
    cursor_row = VGA_HEIGHT - 1;
    cursor_col = 0;
    sb_active = 0;
    sb_view = history_count - (VGA_HEIGHT - 1);
    if (sb_view < 0) sb_view = 0;
    redraw_screen();
}

void vga_text_putchar(char c) {
    if (sb_active) {
        sb_active = 0;
        sb_view = history_count - (VGA_HEIGHT - 1);
        if (sb_view < 0) sb_view = 0;
        cursor_row = (history_count < VGA_HEIGHT - 1) ? history_count : VGA_HEIGHT - 1;
        cursor_col = 0;
        clear_line_buf(current_line);
    }

    if (c == '\n') {
        commit_current_line();
        cursor_col = 0;
        cursor_row++;
    } else if (c == '\r') {
        cursor_col = 0;
        for (int i = 0; i < VGA_WIDTH; i++) current_line[i] = make_cell(' ');
    } else if (c == '\t') {
        int next_col = (cursor_col + 8) & ~7;
        while (cursor_col < next_col && cursor_col < VGA_WIDTH) {
            current_line[cursor_col++] = make_cell(' ');
        }
        if (cursor_col >= VGA_WIDTH) {
            commit_current_line();
            cursor_col = 0;
            cursor_row++;
        }
    } else if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
            current_line[cursor_col] = make_cell(' ');
        }
    } else {
        if (cursor_col < VGA_WIDTH) {
            current_line[cursor_col] = make_cell(c);
            cursor_col++;
        }
    }

    if (cursor_col >= VGA_WIDTH) {
        commit_current_line();
        cursor_col = 0;
        cursor_row++;
    }

    while (cursor_row >= VGA_HEIGHT) {
        vga_text_scroll();
    }

    redraw_screen();
}

void vga_text_print(const char *str) {
    while (*str) {
        vga_text_putchar(*str);
        str++;
    }
}

void vga_text_set_cursor(int row, int col) {
    cursor_row = row;
    cursor_col = col;
    update_hw_cursor();
}

void vga_text_get_cursor(int *row, int *col) {
    *row = cursor_row;
    *col = cursor_col;
}

void vga_text_scroll_up(int lines) {
    if (history_count == 0) return;
    if (!sb_active) {
        sb_active = 1;
        sb_view = history_count - (VGA_HEIGHT - 1);
        if (sb_view < 0) sb_view = 0;
    }
    sb_view -= lines;
    if (sb_view < 0) sb_view = 0;
    redraw_screen();
}

void vga_text_scroll_down(int lines) {
    if (!sb_active) return;
    sb_view += lines;
    int max_view = history_count - (VGA_HEIGHT - 1);
    if (max_view < 0) max_view = 0;
    if (sb_view >= max_view) {
        sb_active = 0;
        sb_view = max_view;
        cursor_row = (history_count < VGA_HEIGHT - 1) ? history_count : VGA_HEIGHT - 1;
        redraw_screen();
        return;
    }
    redraw_screen();
}

void vga_text_scroll_home(void) {
    if (history_count == 0) return;
    sb_active = 1;
    sb_view = 0;
    redraw_screen();
}

void vga_text_scroll_end(void) {
    sb_active = 0;
    sb_view = history_count - (VGA_HEIGHT - 1);
    if (sb_view < 0) sb_view = 0;
    cursor_row = (history_count < VGA_HEIGHT - 1) ? history_count : VGA_HEIGHT - 1;
    cursor_col = 0;
    clear_line_buf(current_line);
    redraw_screen();
}

int vga_text_in_scrollback(void) {
    return sb_active;
}

void vga_text_dump_screen(void) {
    volatile uint16_t *buf = (volatile uint16_t *)VGA_BUFFER;
    serial_print(COM1, "--- VGA screen dump ---\n");
    for (int row = 0; row < VGA_HEIGHT && row < 25; row++) {
        char line[VGA_WIDTH + 1];
        for (int col = 0; col < VGA_WIDTH; col++) {
            uint16_t cell = buf[row * VGA_WIDTH + col];
            char ch = (char)(cell & 0xFF);
            line[col] = (ch >= 32 && ch < 127) ? ch : ' ';
        }
        line[VGA_WIDTH] = '\0';
        serial_print(COM1, line);
        serial_print(COM1, "\n");
    }
    serial_print(COM1, "--- end VGA screen dump ---\n");
}

void vga_text_print_hex(uint32_t val) {
    static const char hex_chars[] = "0123456789ABCDEF";
    vga_text_putchar('0');
    vga_text_putchar('x');
    int started = 0;
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (val >> i) & 0xF;
        if (nibble != 0 || started || i == 0) {
            vga_text_putchar(hex_chars[nibble]);
            started = 1;
        }
    }
}

void vga_text_print_dec(uint32_t val) {
    char buf[12];
    int i = 0;
    if (val == 0) {
        vga_text_putchar('0');
        return;
    }
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    for (int j = i - 1; j >= 0; j--) {
        vga_text_putchar(buf[j]);
    }
}

void vga_text_font_diagnostic(void) {
    vga_text_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLUE);
    vga_text_print("=== VGA FONT DIAGNOSTIC ===\n");

    int test_chars[] = {'A', 'B', 'a', '0', ' ', '#'};
    int n_test = sizeof(test_chars) / sizeof(test_chars[0]);

    for (int c = 32; c < 127; c++) {
        if ((c - 32) % 16 == 0) {
            char buf[8];
            int n = 0;
            int tmp = c;
            buf[n++] = '[';
            if (tmp == 0) { buf[n++] = '0'; }
            else {
                char rev[4]; int ri = 0;
                while (tmp > 0) { rev[ri++] = '0' + (tmp % 10); tmp /= 10; }
                while (ri > 0) buf[n++] = rev[--ri];
            }
            buf[n++] = ']';
            buf[n++] = ':';
            buf[n++] = ' ';
            buf[n] = '\0';
            vga_text_print(buf);
        }
        vga_text_putchar((char)c);
        vga_text_putchar(' ');
    }
    vga_text_putchar('\n');

    {
        uint8_t saved_seq2, saved_seq4, saved_gfx5, saved_gfx6;

        outb(0x3C4, 0x02); saved_seq2 = inb(0x3C5);
        outb(0x3C4, 0x04); saved_seq4 = inb(0x3C5);
        outb(0x3CE, 0x05); saved_gfx5 = inb(0x3CF);
        outb(0x3CE, 0x06); saved_gfx6 = inb(0x3CF);

        outb(0x3C4, 0x02); outb(0x3C5, 0x04);
        outb(0x3C4, 0x04); outb(0x3C5, 0x07);
        outb(0x3CE, 0x05); outb(0x3CF, 0x00);
        outb(0x3CE, 0x06); outb(0x3CF, 0x04);

        volatile uint8_t *font_ram = (volatile uint8_t *)0xA0000;

        serial_print(COM1, "[FONT-DIAG] Dumping CGRAM for test chars:\n");
        for (int ti = 0; ti < n_test; ti++) {
            int ch = test_chars[ti];
            char hdr[32];
            int hi = 0;
            hdr[hi++] = '[';
            hdr[hi++] = (char)ch;
            hdr[hi++] = ']';
            hdr[hi++] = ' ';
            int tmp = ch;
            if (tmp == 0) { hdr[hi++] = '0'; }
            else {
                char rev[4]; int ri = 0;
                while (tmp > 0) { rev[ri++] = '0' + (tmp % 10); tmp /= 10; }
                while (ri > 0) hdr[hi++] = rev[--ri];
            }
            hdr[hi++] = ':';
            hdr[hi] = '\0';
            serial_print(COM1, hdr);
            for (int line = 0; line < 16; line++) {
                uint8_t val = font_ram[ch * 32 + line];
                for (int b = 7; b >= 0; b--) {
                    char bitc = (val & (1 << b)) ? '#' : '.';
                    serial_putchar(COM1, bitc);
                }
                serial_print(COM1, "  ");
                static const char hx[] = "0123456789ABCDEF";
                serial_putchar(COM1, '0');
                serial_putchar(COM1, 'x');
                serial_putchar(COM1, hx[(val >> 4) & 0xF]);
                serial_putchar(COM1, hx[val & 0xF]);
                serial_putchar(COM1, '\n');
            }
        }

        serial_print(COM1, "[FONT-DIAG] Expected 'A' from font_data array:\n");
        extern const uint8_t font_data[][16];
        for (int line = 0; line < 16; line++) {
            uint8_t val = font_data['A' - 32][line];
            for (int b = 7; b >= 0; b--) {
                char bitc = (val & (1 << b)) ? '#' : '.';
                serial_putchar(COM1, bitc);
            }
            serial_print(COM1, "\n");
        }

        outb(0x3CE, 0x05); outb(0x3CF, saved_gfx5);
        outb(0x3CE, 0x06); outb(0x3CF, saved_gfx6);
        outb(0x3C4, 0x02); outb(0x3C5, saved_seq2);
        outb(0x3C4, 0x04); outb(0x3C5, saved_seq4);
    }

    vga_text_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}
