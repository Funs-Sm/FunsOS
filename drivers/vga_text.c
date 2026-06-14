#include "vga_text.h"
#include "io.h"

static int cursor_row = 0;
static int cursor_col = 0;
static uint8_t current_color = 0;

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
 * Register values are the standard VGA mode 3 initialization sequence
 * from the IBM VGA BIOS / FreeVGA project.
 */
void vga_text_mode3_switch(void) {
    /* ---- Step 1: Disable display and sequencer ---- */
    /* Turn off screen via sequencer clocking mode register */
    outb(0x3C4, 0x01);  /* Sequencer: Clocking Mode */
    outb(0x3C5, 0x21);  /* bit 5 = screen off, bit 0 = 8-dot mode */

    /* ---- Step 2: Miscellaneous Output Register ---- */
    outb(0x3C2, 0x67);  /* 28.322 MHz clock, 80-col mode, color I/O */

    /* ---- Step 3: Sequencer registers ---- */
    static const uint8_t seq_regs[] = {
        0x00, 0x03,  /* Reset (async reset off) */
        0x01, 0x01,  /* Clocking Mode (8-dot chars) */
        0x02, 0x0F,  /* Map Mask (all 4 planes enabled) */
        0x03, 0x00,  /* Character Map Select */
        0x04, 0x0E,  /* Memory Mode (extended memory, odd/even) */
    };
    for (int i = 0; i < (int)(sizeof(seq_regs) / 2); i++) {
        outb(0x3C4, seq_regs[i * 2]);
        outb(0x3C5, seq_regs[i * 2 + 1]);
    }

    /* ---- Step 4: Unlock CRTC protection ---- */
    outb(0x3D4, 0x11);
    uint8_t crtc11 = inb(0x3D5);
    outb(0x3D5, crtc11 & 0x7F);  /* Unlock CRTC regs 0-7 */

    /* ---- Step 5: CRTC registers ---- */
    static const uint8_t crtc_regs[] = {
        0x00, 0x5F,  /* Horizontal Total */
        0x01, 0x4F,  /* Horizontal Display End */
        0x02, 0x50,  /* Horizontal Blank Start */
        0x03, 0x82,  /* Horizontal Blank End */
        0x04, 0x55,  /* Horizontal Retrace Start */
        0x05, 0x81,  /* Horizontal Retrace End */
        0x06, 0xBF,  /* Vertical Total */
        0x07, 0x1F,  /* Overflow */
        0x08, 0x00,  /* Preset Row Scan */
        0x09, 0x4F,  /* Maximum Scan Line */
        0x0A, 0x0D,  /* Cursor Start */
        0x0B, 0x0E,  /* Cursor End */
        0x0C, 0x00,  /* Start Address High */
        0x0D, 0x00,  /* Start Address Low */
        0x0E, 0x00,  /* Cursor Location High */
        0x0F, 0x00,  /* Cursor Location Low */
        0x10, 0x9C,  /* Vertical Retrace Start */
        0x11, 0x8E,  /* Vertical Retrace End */
        0x12, 0x8F,  /* Vertical Display End */
        0x13, 0x28,  /* Offset (80 columns = 40 words) */
        0x14, 0x1F,  /* Underline Location */
        0x15, 0x96,  /* Vertical Blank Start */
        0x16, 0xB9,  /* Vertical Blank End */
        0x17, 0xA3,  /* Mode Control */
    };
    for (int i = 0; i < (int)(sizeof(crtc_regs) / 2); i++) {
        outb(0x3D4, crtc_regs[i * 2]);
        outb(0x3D5, crtc_regs[i * 2 + 1]);
    }

    /* ---- Step 6: Graphics Controller registers ---- */
    static const uint8_t gfx_regs[] = {
        0x00, 0x00,  /* Set/Reset */
        0x01, 0x00,  /* Enable Set/Reset */
        0x02, 0x00,  /* Color Compare */
        0x03, 0x00,  /* Data Rotate */
        0x04, 0x00,  /* Read Map Select */
        0x05, 0x10,  /* Graphics Mode (text mode) */
        0x06, 0x0E,  /* Miscellaneous (A0000-BFFFF, odd/even) */
        0x07, 0x0F,  /* Color Don't Care */
        0x08, 0xFF,  /* Bit Mask */
    };
    for (int i = 0; i < (int)(sizeof(gfx_regs) / 2); i++) {
        outb(0x3CE, gfx_regs[i * 2]);
        outb(0x3CF, gfx_regs[i * 2 + 1]);
    }

    /* ---- Step 7: Attribute Controller registers ---- */
    /* Reset attribute flip-flop by reading 0x3DA */
    inb(0x3DA);
    static const uint8_t attr_regs[] = {
        0x00, 0x00,  /* Palette 0: Black */
        0x01, 0x01,  /* Palette 1: Blue */
        0x02, 0x02,  /* Palette 2: Green */
        0x03, 0x03,  /* Palette 3: Cyan */
        0x04, 0x04,  /* Palette 4: Red */
        0x05, 0x05,  /* Palette 5: Magenta */
        0x06, 0x06,  /* Palette 6: Brown */
        0x07, 0x07,  /* Palette 7: Light Gray */
        0x08, 0x08,  /* Palette 8: Dark Gray */
        0x09, 0x09,  /* Palette 9: Light Blue */
        0x0A, 0x0A,  /* Palette 10: Light Green */
        0x0B, 0x0B,  /* Palette 11: Light Cyan */
        0x0C, 0x0C,  /* Palette 12: Light Red */
        0x0D, 0x0D,  /* Palette 13: Light Magenta */
        0x0E, 0x0E,  /* Palette 14: Yellow */
        0x0F, 0x0F,  /* Palette 15: White */
        0x10, 0x0C,  /* Mode Control (text mode, 8-bit) */
        0x11, 0x00,  /* Overscan Color */
        0x12, 0x0F,  /* Color Plane Enable */
        0x13, 0x08,  /* Horizontal Pixel Panning */
        0x14, 0x00,  /* Color Select */
    };
    for (int i = 0; i < (int)(sizeof(attr_regs) / 2); i++) {
        inb(0x3DA);  /* Reset flip-flop */
        outb(0x3C0, attr_regs[i * 2]);
        outb(0x3C0, attr_regs[i * 2 + 1]);
    }

    /* ---- Step 8: Re-enable display ---- */
    outb(0x3C4, 0x01);
    outb(0x3C5, 0x01);  /* Screen on, 8-dot mode */

    /* ---- Step 9: Enable attribute controller display ---- */
    inb(0x3DA);
    outb(0x3C0, 0x20);  /* Enable display (bit 5 of attr index) */

    /* Clear the text buffer so we don't see garbage */
    volatile uint16_t *buf = (volatile uint16_t *)VGA_BUFFER;
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        buf[i] = (uint16_t)(' ' | 0x0700);
    }
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
