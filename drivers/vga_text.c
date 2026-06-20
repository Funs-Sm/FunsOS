#include "vga_text.h"
#include "io.h"
#include "serial.h"
#include "../gui/font.h"

static int cursor_row = 0;
static int cursor_col = 0;
static uint8_t current_color = 0;

static void update_hardware_cursor(void);

/*
 * vga_text_mode3_switch - Switch VGA hardware from graphics mode to
 * text mode 3 (80x25, 16 colors) using VGA register writes.
 *
 * This is needed because the bootloader sets a VBE graphics mode via
 * INT 10h.  If the kernel cannot find the VBE framebuffer, the legacy
 * VGA text buffer at 0xB8000 is NOT visible because the CRTC is still
 * programmed for graphics mode.  We must reprogram the VGA registers
 * to text mode 3 without calling BIOS (which is unavailable in
 * protected mode).
 *
 * IMPORTANT: The bootloader already called INT 10h AH=00h AL=03
 * (set video mode 3) before entering protected mode, which:
 *   - Programmed all CRTC/sequencer/GFX registers correctly
 *   - Loaded the standard 8x16 ROM font into CGRAM
 *   - Cleared the text buffer at 0xB8000
 *
 * This function only needs to clear the screen and reset cursor,
 * since BIOS did the heavy lifting.
 */
void vga_text_mode3_switch(void) {
    /* Clear text buffer with spaces on light-grey-on-black */
    volatile uint16_t *buf = (volatile uint16_t *)VGA_BUFFER;
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        buf[i] = (uint16_t)(' ' | 0x0700);
    }

    /* Reset cursor to top-left */
    cursor_row = 0;
    cursor_col = 0;
    update_hardware_cursor();
}

static void update_hardware_cursor(void) {
    uint16_t pos = cursor_row * VGA_WIDTH + cursor_col;
    outb(0x3D4, 14);
    outb(0x3D5, (uint8_t)(pos >> 8));
    outb(0x3D4, 15);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
}

void vga_text_init(void) {
    current_color = (VGA_COLOR_BLACK << 4) | VGA_COLOR_LIGHT_GREY;
    vga_text_clear();
    cursor_row = 0;
    cursor_col = 0;
    update_hardware_cursor();
}

void vga_text_set_color(uint8_t fg, uint8_t bg) {
    current_color = (bg << 4) | fg;
}

void vga_text_clear(void) {
    volatile uint16_t *buf = (volatile uint16_t *)VGA_BUFFER;
    uint16_t blank = (uint16_t)(' ' | (current_color << 8));
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        buf[i] = blank;
    }
    cursor_row = 0;
    cursor_col = 0;
    update_hardware_cursor();
}

void vga_text_scroll(void) {
    volatile uint16_t *buf = (volatile uint16_t *)VGA_BUFFER;
    for (int row = 0; row < VGA_HEIGHT - 1; row++) {
        for (int col = 0; col < VGA_WIDTH; col++) {
            int src = (row + 1) * VGA_WIDTH + col;
            int dst = row * VGA_WIDTH + col;
            buf[dst] = buf[src];
        }
    }
    uint16_t blank = (uint16_t)(' ' | (current_color << 8));
    for (int col = 0; col < VGA_WIDTH; col++) {
        buf[(VGA_HEIGHT - 1) * VGA_WIDTH + col] = blank;
    }
    cursor_row = VGA_HEIGHT - 1;
    cursor_col = 0;
    update_hardware_cursor();
}

void vga_text_putchar(char c) {
    volatile uint16_t *buf = (volatile uint16_t *)VGA_BUFFER;

    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
    } else if (c == '\r') {
        cursor_col = 0;
    } else if (c == '\t') {
        cursor_col = (cursor_col + 8) & ~7;
        if (cursor_col >= VGA_WIDTH) {
            cursor_col = 0;
            cursor_row++;
        }
    } else if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
        }
        int offset = cursor_row * VGA_WIDTH + cursor_col;
        buf[offset] = (uint16_t)(' ' | (current_color << 8));
    } else {
        int offset = cursor_row * VGA_WIDTH + cursor_col;
        buf[offset] = (uint16_t)(c | (current_color << 8));
        cursor_col++;
    }

    if (cursor_col >= VGA_WIDTH) {
        cursor_col = 0;
        cursor_row++;
    }

    while (cursor_row >= VGA_HEIGHT) {
        vga_text_scroll();
    }

    update_hardware_cursor();
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
    update_hardware_cursor();
}

void vga_text_get_cursor(int *row, int *col) {
    *row = cursor_row;
    *col = cursor_col;
}

/* Dump the visible VGA text buffer to the serial port for debugging
 * garbled output.  Each cell is a (char, attr) pair at 0xB8000. */
void vga_text_dump_screen(void) {
    volatile uint16_t *buf = (volatile uint16_t *)VGA_BUFFER;
    serial_print(COM1, "--- VGA screen dump ---\n");
    for (int row = 0; row < VGA_HEIGHT && row < 25; row++) {
        char line[VGA_WIDTH + 1];
        for (int col = 0; col < VGA_WIDTH; col++) {
            uint16_t cell = buf[row * VGA_WIDTH + col];
            char c = (char)(cell & 0xFF);
            line[col] = (c >= 32 && c < 127) ? c : ' ';
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

/* Diagnostic: print the full ASCII table via VGA and verify font RAM.
 * Outputs every printable character (32-126) in rows of 16,
 * then reads back the first few bytes of CGRAM for 'A' (char 65)
 * and dumps them to serial so we can confirm the font was loaded. */
void vga_text_font_diagnostic(void) {
    /* Print ASCII grid on screen */
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

    /* Read back font data from plane 2 and dump to serial */
    {
        uint8_t saved_seq2, saved_seq4, saved_gfx5, saved_gfx6;

        outb(0x3C4, 0x02); saved_seq2 = inb(0x3C5);
        outb(0x3C4, 0x04); saved_seq4 = inb(0x3C5);
        outb(0x3CE, 0x05); saved_gfx5 = inb(0x3CF);
        outb(0x3CE, 0x06); saved_gfx6 = inb(0x3CF);

        /* Select plane 2 for reading, read mode 0 */
        outb(0x3C4, 0x02); outb(0x3C5, 0x04);
        outb(0x3C4, 0x04); outb(0x3C5, 0x07);
        outb(0x3CE, 0x05); outb(0x3CF, 0x00);  /* read mode 0 */
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
            hdr[hi++] = '\0';
            serial_print(COM1, hdr);
            for (int line = 0; line < 16; line++) {
                uint8_t val = font_ram[ch * 32 + line];
                /* print as binary */
                for (int b = 7; b >= 0; b--) {
                    char bitc = (val & (1 << b)) ? '#' : '.';
                    serial_putchar(COM1, bitc);
                }
                serial_print(COM1, "  ");
                /* also hex */
                static const char hx[] = "0123456789ABCDEF";
                serial_putchar(COM1, '0');
                serial_putchar(COM1, 'x');
                serial_putchar(COM1, hx[(val >> 4) & 0xF]);
                serial_putchar(COM1, hx[val & 0xF]);
                serial_putchar(COM1, '\n');
            }
        }

        /* Also read back what we expect for 'A' from font_data array */
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

        /* Restore registers */
        outb(0x3CE, 0x05); outb(0x3CF, saved_gfx5);
        outb(0x3CE, 0x06); outb(0x3CF, saved_gfx6);
        outb(0x3C4, 0x02); outb(0x3C5, saved_seq2);
        outb(0x3C4, 0x04); outb(0x3C5, saved_seq4);
    }

    vga_text_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}
