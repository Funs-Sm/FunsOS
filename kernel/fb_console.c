#include "fb_console.h"
#include "gfx.h"
#include "font.h"
#include "unicode.h"
#include "string.h"
#include "timer.h"
#include "kheap.h"

static gfx_context_t fb_ctx;
static uint32_t cursor_x = 0;
static uint32_t cursor_y = 0;
static uint32_t fg_color = 0xFFFFFF;
static uint32_t bg_color = 0x000000;
static uint32_t cols = 0;
static uint32_t rows = 0;

/*
 * ================================================================
 *  TEXT-LINE BASED SCROLLBACK SYSTEM
 * ================================================================
 *
 * Instead of saving raw framebuffer pixels (which causes BPP/pitch
 * alignment issues), we store each rendered TEXT LINE as a string.
 * On PGUP/PGDN, we clear the screen and RE-RENDER from the buffer.
 * This guarantees correct font rendering regardless of pixel format.
 */

#define SCROLLBACK_MAX_LINES   500    /* Max lines of history */
#define SCROLLBACK_LINE_LEN     512    /* Max chars per stored line */

typedef struct {
    char text[SCROLLBACK_LINE_LEN];  /* Text content (may include ANSI escapes) */
    uint32_t len;                     /* Actual text length */
} sb_line_t;

static sb_line_t *sb_lines = 0;       /* Circular buffer of scrolled-off lines */
static uint32_t sb_head = 0;          /* Next write position */
static uint32_t sb_count = 0;         /* Total lines stored */
static int32_t  sb_view_offset = 0;   /* 0 = normal/bottom, >0 = viewing history */

/*
 * Screen row snapshot: stores what's currently visible on each screen row.
 * Used to restore the display when returning from scroll-view mode.
 */
#define MAX_SCREEN_ROWS  60
typedef struct {
    char text[SCROLLBACK_LINE_LEN];
    uint32_t len;
    uint32_t fg;
    uint32_t bg;
} screen_row_t;

static screen_row_t screen_rows[MAX_SCREEN_ROWS];
static int32_t saved_cursor_x = 0;
static int32_t saved_cursor_y = 0;
static int screen_snapshot_valid = 0;  /* Whether screen_rows has valid data */

/* Current screen text tracking - mirrors what's visible on each row */
static char screen_text[MAX_SCREEN_ROWS][SCROLLBACK_LINE_LEN];
static uint32_t screen_text_len[MAX_SCREEN_ROWS];

/* Blinking cursor state */
static int cursor_blink_visible = 1;
static uint32_t cursor_blink_last_tick = 0;
#define CURSOR_BLINK_RATE 5  /* toggle every N timer ticks (~250ms) */

/* Standard 8-color ANSI palette */
static const uint32_t ansi_palette[8] = {
    0x000000, /* 0: Black   */
    0xAA0000, /* 1: Red     */
    0x00AA00, /* 2: Green   */
    0xAA5500, /* 3: Yellow  */
    0x0000AA, /* 4: Blue    */
    0xAA00AA, /* 5: Magenta */
    0x00AAAA, /* 6: Cyan    */
    0xAAAAAA  /* 7: White   */
};

/* Bright (bold) 8-color ANSI palette - add 0x55 per component */
static const uint32_t ansi_palette_bright[8] = {
    0x555555, /* 0: Bright Black   */
    0xFF5555, /* 1: Bright Red     */
    0x55FF55, /* 2: Bright Green   */
    0xFFFF55, /* 3: Bright Yellow  */
    0x5555FF, /* 4: Bright Blue    */
    0xFF55FF, /* 5: Bright Magenta */
    0x55FFFF, /* 6: Bright Cyan    */
    0xFFFFFF  /* 7: Bright White   */
};

/* ANSI escape sequence parser state */
enum {
    ANSI_STATE_NORMAL = 0,
    ANSI_STATE_ESCAPE,   /* Received ESC */
    ANSI_STATE_CSI       /* Received ESC [ */
};

static int ansi_state = ANSI_STATE_NORMAL;
static int ansi_params[16];
static int ansi_param_count = 0;
static int ansi_param_accum = -1; /* -1 means no digit seen yet for current param */
static int ansi_bold = 0;

/* UTF-8 multibyte accumulation state */
static uint8_t utf8_buf[4];
static int utf8_len = 0;
static int utf8_expected = 0;

/* Determine expected UTF-8 sequence length from lead byte */
static int utf8_sequence_length(uint8_t lead) {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1; /* invalid, treat as single byte */
}

/* Draw a Unicode codepoint. For non-ASCII, draw a placeholder box. */
static void fb_console_draw_codepoint(uint32_t cp, int width_cols) {
    if (cols == 0 || rows == 0) return;

    /* If this character would exceed the line width, wrap to next line first */
    if (cursor_x + width_cols > cols) {
        cursor_x = 0;
        cursor_y++;
        while (cursor_y >= rows) {
            fb_console_scroll(1);
        }
    }

    int32_t px = (int32_t)(cursor_x * FONT_GLYPH_WIDTH);
    int32_t py = (int32_t)(cursor_y * FONT_GLYPH_HEIGHT);

    if (cp < 128) {
        /* ASCII - use the built-in font */
        font_draw_char(&fb_ctx, (char)cp, px, py, fg_color, bg_color);
    } else {
        /* Non-ASCII Unicode - draw a placeholder box */
        int glyph_w = FONT_GLYPH_WIDTH * width_cols;
        /* Draw a rectangle outline as placeholder */
        gfx_rect_t rect = { px, py, glyph_w, FONT_GLYPH_HEIGHT };
        gfx_fill_rect(&fb_ctx, rect, bg_color);
        /* Draw border */
        for (int i = 0; i < glyph_w; i++) {
            gfx_set_pixel(&fb_ctx, px + i, py, fg_color);
            gfx_set_pixel(&fb_ctx, px + i, py + FONT_GLYPH_HEIGHT - 1, fg_color);
        }
        for (int i = 0; i < (int)FONT_GLYPH_HEIGHT; i++) {
            gfx_set_pixel(&fb_ctx, px, py + i, fg_color);
            gfx_set_pixel(&fb_ctx, px + glyph_w - 1, py + i, fg_color);
        }
        /* Draw '?' in the center */
        if (width_cols >= 2) {
            font_draw_char(&fb_ctx, '?', px + FONT_GLYPH_WIDTH / 2, py, fg_color, bg_color);
        } else {
            font_draw_char(&fb_ctx, '?', px, py, fg_color, bg_color);
        }
    }

    cursor_x += width_cols;
}

/* Finalize the current accumulating parameter */
static void ansi_finish_param(void) {
    if (ansi_param_count < 16) {
        ansi_params[ansi_param_count] = (ansi_param_accum < 0) ? 0 : ansi_param_accum;
        ansi_param_count++;
    }
    ansi_param_accum = -1;
}

/* Get parameter with default value if missing */
static int ansi_get_param(int idx, int defval) {
    if (idx >= ansi_param_count) return defval;
    return ansi_params[idx];
}

/* Handle a complete CSI sequence (final byte received) */
static void ansi_handle_csi(char final_byte) {
    /* Finalize last parameter */
    if (ansi_param_accum >= 0 || ansi_param_count == 0) {
        ansi_finish_param();
    }

    switch (final_byte) {
    case 'm': /* SGR - Select Graphic Rendition */ {
        if (ansi_param_count == 0) {
            /* \033[m means reset */
            ansi_bold = 0;
            fg_color = 0xAAAAAA;
            bg_color = 0x000000;
            break;
        }
        for (int i = 0; i < ansi_param_count; i++) {
            int p = ansi_params[i];
            if (p == 0) {
                ansi_bold = 0;
                fg_color = 0xAAAAAA;
                bg_color = 0x000000;
            } else if (p == 1) {
                ansi_bold = 1;
                /* Re-apply current fg from palette index if it was a standard color */
            } else if (p == 22) {
                ansi_bold = 0;
            } else if (p >= 30 && p <= 37) {
                int idx = p - 30;
                fg_color = ansi_bold ? ansi_palette_bright[idx] : ansi_palette[idx];
            } else if (p >= 40 && p <= 47) {
                int idx = p - 40;
                bg_color = ansi_palette[idx];
            } else if (p == 38 || p == 48) {
                /* Extended color: 38;2;r;g;b or 38;5;n (same for 48) */
                int is_fg = (p == 38);
                if (i + 1 < ansi_param_count && ansi_params[i + 1] == 2) {
                    /* 24-bit RGB: 38;2;r;g;b */
                    if (i + 4 < ansi_param_count) {
                        uint32_t r = (uint32_t)ansi_params[i + 2];
                        uint32_t g = (uint32_t)ansi_params[i + 3];
                        uint32_t b = (uint32_t)ansi_params[i + 4];
                        uint32_t color = (r << 16) | (g << 8) | b;
                        if (is_fg) fg_color = color;
                        else bg_color = color;
                        i += 4; /* skip consumed params */
                    }
                } else if (i + 1 < ansi_param_count && ansi_params[i + 1] == 5) {
                    /* 256-color: 38;5;n */
                    if (i + 2 < ansi_param_count) {
                        int n = ansi_params[i + 2];
                        uint32_t color;
                        if (n < 8) {
                            color = ansi_palette[n];
                        } else if (n < 16) {
                            color = ansi_palette_bright[n - 8];
                        } else {
                            /* 216-color cube or grayscale - approximate */
                            color = 0xAAAAAA;
                        }
                        if (is_fg) fg_color = color;
                        else bg_color = color;
                        i += 2; /* skip consumed params */
                    }
                }
            }
            /* Ignore unknown SGR codes */
        }
        break;
    }

    case 'J': /* Erase in display */ {
        int mode = ansi_get_param(0, 0);
        if (mode == 2) {
            /* Clear entire screen */
            fb_console_clear();
        }
        break;
    }

    case 'K': /* Erase in line */ {
        int mode = ansi_get_param(0, 0);
        if (mode == 0) {
            /* Clear from cursor to end of line */
            int32_t px = (int32_t)(cursor_x * FONT_GLYPH_WIDTH);
            int32_t py = (int32_t)(cursor_y * FONT_GLYPH_HEIGHT);
            int32_t w = (int32_t)fb_ctx.width - px;
            if (w > 0) {
                gfx_rect_t rect = { px, py, w, FONT_GLYPH_HEIGHT };
                gfx_fill_rect(&fb_ctx, rect, bg_color);
            }
        }
        break;
    }

    case 'H': /* Cursor position */ {
        int row = ansi_get_param(0, 1);
        int col = ansi_get_param(1, 1);
        /* ANSI is 1-based */
        cursor_y = (uint32_t)(row - 1);
        cursor_x = (uint32_t)(col - 1);
        if (cursor_x >= cols) cursor_x = cols > 0 ? cols - 1 : 0;
        if (cursor_y >= rows) cursor_y = rows > 0 ? rows - 1 : 0;
        break;
    }

    case 'A': /* Cursor up */ {
        int n = ansi_get_param(0, 1);
        if ((uint32_t)n > cursor_y) cursor_y = 0;
        else cursor_y -= (uint32_t)n;
        break;
    }

    case 'B': /* Cursor down */ {
        int n = ansi_get_param(0, 1);
        cursor_y += (uint32_t)n;
        if (cursor_y >= rows) cursor_y = rows > 0 ? rows - 1 : 0;
        break;
    }

    case 'C': /* Cursor forward */ {
        int n = ansi_get_param(0, 1);
        cursor_x += (uint32_t)n;
        if (cursor_x >= cols) cursor_x = cols > 0 ? cols - 1 : 0;
        break;
    }

    case 'D': /* Cursor back */ {
        int n = ansi_get_param(0, 1);
        if ((uint32_t)n > cursor_x) cursor_x = 0;
        else cursor_x -= (uint32_t)n;
        break;
    }

    default:
        /* Unknown CSI sequence - ignore */
        break;
    }
}

/* Erase the blinking cursor underscore at the current position.
 * Must be called before any output that moves the cursor,
 * otherwise the underscore remains as a ghost white line. */
static void fb_console_erase_cursor(void) {
    if (cols == 0 || rows == 0 || fb_ctx.width == 0) return;
    int32_t px = (int32_t)(cursor_x * FONT_GLYPH_WIDTH);
    int32_t py = (int32_t)(cursor_y * FONT_GLYPH_HEIGHT + FONT_GLYPH_HEIGHT - 2);
    gfx_rect_t erase_rect = {px, py, FONT_GLYPH_WIDTH, 2};
    gfx_fill_rect(&fb_ctx, erase_rect, bg_color);
    cursor_blink_visible = 0;
}

/* Process a complete Unicode codepoint */
static void fb_console_process_codepoint(uint32_t cp) {
    if (cols == 0 || rows == 0) return;

    /* Erase cursor before any output to prevent ghost white line */
    fb_console_erase_cursor();

    int w = unicode_char_width(cp);

    if (cp == '\n') {
        cursor_x = 0;
        cursor_y++;
        /* Clear the new row's text buffer */
        if (cursor_y < MAX_SCREEN_ROWS) {
            screen_text_len[cursor_y] = 0;
            screen_text[cursor_y][0] = '\0';
        }
    } else if (cp == '\r') {
        cursor_x = 0;
        /* Reset current line buffer on carriage return */
        if (cursor_y < MAX_SCREEN_ROWS) {
            screen_text_len[cursor_y] = 0;
            screen_text[cursor_y][0] = '\0';
        }
    } else if (cp == '\t') {
        /* Record tab character in line buffer */
        if (cursor_y < MAX_SCREEN_ROWS && screen_text_len[cursor_y] < SCROLLBACK_LINE_LEN - 1) {
            screen_text[cursor_y][screen_text_len[cursor_y]++] = '\t';
            screen_text[cursor_y][screen_text_len[cursor_y]] = '\0';
        }
        cursor_x = (cursor_x + 8) & ~7u;
        if (cursor_x >= cols) {
            cursor_x = 0;
            cursor_y++;
            if (cursor_y < MAX_SCREEN_ROWS) {
                screen_text_len[cursor_y] = 0;
                screen_text[cursor_y][0] = '\0';
            }
        }
    } else if (cp == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            /* Remove last char from line buffer */
            if (cursor_y < MAX_SCREEN_ROWS && screen_text_len[cursor_y] > 0) {
                screen_text_len[cursor_y]--;
                screen_text[cursor_y][screen_text_len[cursor_y]] = '\0';
            }
            gfx_rect_t rect = {
                (int32_t)(cursor_x * FONT_GLYPH_WIDTH),
                (int32_t)(cursor_y * FONT_GLYPH_HEIGHT),
                FONT_GLYPH_WIDTH,
                FONT_GLYPH_HEIGHT
            };
            gfx_fill_rect(&fb_ctx, rect, bg_color);
        }
    } else {
        uint32_t pre_draw_cy = cursor_y;
        fb_console_draw_codepoint(cp, w);
        /* If draw_codepoint wrapped to a new row, clear that row's buffer */
        if (cursor_y != pre_draw_cy && cursor_y < MAX_SCREEN_ROWS) {
            screen_text_len[cursor_y] = 0;
            screen_text[cursor_y][0] = '\0';
        }
        /* Record printable character at the row where it was drawn */
        if (cursor_y < MAX_SCREEN_ROWS && screen_text_len[cursor_y] < SCROLLBACK_LINE_LEN - 1) {
            screen_text[cursor_y][screen_text_len[cursor_y]++] = (char)cp;
            screen_text[cursor_y][screen_text_len[cursor_y]] = '\0';
        }
    }

    if (cursor_x >= cols) {
        cursor_x = 0;
        cursor_y++;
        /* Clear the new row's text buffer for deferred wrap */
        if (cursor_y < MAX_SCREEN_ROWS) {
            screen_text_len[cursor_y] = 0;
            screen_text[cursor_y][0] = '\0';
        }
    }

    while (cursor_y >= rows) {
        fb_console_scroll(1);
    }
}

void fb_console_init(uint32_t *fb, uint32_t width, uint32_t height, uint32_t pitch, uint32_t bpp) {
    /*
     * Use the VBE-reported pitch value directly.
     * Modern VBE implementations (including QEMU's) report accurate pitch.
     * Only fall back to computed pitch if reported value is clearly invalid.
     */
    uint32_t use_pitch = pitch;
    uint32_t min_pitch = width * (bpp / 8);
    if (bpp > 0 && (use_pitch == 0 || use_pitch < min_pitch)) {
        use_pitch = min_pitch;
    }

    gfx_init(&fb_ctx, fb, width, height, use_pitch, bpp);
    cols = width / FONT_GLYPH_WIDTH;
    rows = height / FONT_GLYPH_HEIGHT;
    cursor_x = 0;
    cursor_y = 0;
    fg_color = 0xFFFFFF;
    bg_color = 0x000000;
    utf8_len = 0;
    utf8_expected = 0;
    ansi_state = ANSI_STATE_NORMAL;
    ansi_param_count = 0;
    ansi_param_accum = -1;
    ansi_bold = 0;

    /* Allocate TEXT-based scrollback buffer */
    if (!sb_lines) {
        sb_lines = (sb_line_t *)kmalloc(sizeof(sb_line_t) * SCROLLBACK_MAX_LINES);
        if (sb_lines) {
            /* Zero-initialize all line slots */
            for (uint32_t i = 0; i < SCROLLBACK_MAX_LINES; i++) {
                sb_lines[i].text[0] = '\0';
                sb_lines[i].len = 0;
            }
        }
    }
    sb_head = 0;
    sb_count = 0;
    sb_view_offset = 0;

    /* Initialize screen row tracking */
    for (uint32_t i = 0; i < MAX_SCREEN_ROWS; i++) {
        screen_rows[i].text[0] = '\0';
        screen_rows[i].len = 0;
        screen_rows[i].fg = 0xFFFFFF;
        screen_rows[i].bg = 0x000000;
    }
    /* Initialize screen text tracking */
    for (uint32_t i = 0; i < MAX_SCREEN_ROWS; i++) {
        screen_text[i][0] = '\0';
        screen_text_len[i] = 0;
    }
    saved_cursor_x = 0;
    saved_cursor_y = 0;
    screen_snapshot_valid = 0;

    fb_console_clear();
}

void fb_console_putchar(char c) {
    uint8_t byte = (uint8_t)c;

    /* ANSI escape sequence parser - intercept before UTF-8 processing */
    if (ansi_state == ANSI_STATE_CSI) {
        if (byte >= '0' && byte <= '9') {
            /* Accumulate numeric parameter */
            if (ansi_param_accum < 0) ansi_param_accum = 0;
            ansi_param_accum = ansi_param_accum * 10 + (byte - '0');
            return;
        } else if (byte == ';') {
            /* Parameter separator */
            ansi_finish_param();
            return;
        } else if (byte >= 0x40 && byte <= 0x7E) {
            /* Final byte - dispatch CSI sequence */
            ansi_handle_csi((char)byte);
            ansi_state = ANSI_STATE_NORMAL;
            return;
        } else {
            /* Intermediate byte (0x20-0x2F) or invalid - abort sequence */
            ansi_state = ANSI_STATE_NORMAL;
            return;
        }
    } else if (ansi_state == ANSI_STATE_ESCAPE) {
        if (byte == '[') {
            /* CSI introducer */
            ansi_state = ANSI_STATE_CSI;
            ansi_param_count = 0;
            ansi_param_accum = -1;
            return;
        } else {
            /* Not a CSI sequence - abort (could handle other ESC sequences here) */
            ansi_state = ANSI_STATE_NORMAL;
            /* Fall through to process the byte normally */
        }
    }

    /* Check for ESC starting a new escape sequence */
    if (byte == 0x1B) {
        ansi_state = ANSI_STATE_ESCAPE;
        return;
    }

    /* If we are accumulating a UTF-8 sequence */
    if (utf8_len > 0) {
        /* Check if this is a valid continuation byte */
        if ((byte & 0xC0) == 0x80) {
            utf8_buf[utf8_len++] = byte;
            if (utf8_len >= utf8_expected) {
                /* Complete sequence - decode and process */
                utf8_buf[utf8_len] = '\0';
                int bytes_read = 0;
                uint32_t cp = utf8_decode((const char *)utf8_buf, &bytes_read);
                utf8_len = 0;
                utf8_expected = 0;
                fb_console_process_codepoint(cp);
            }
        } else {
            /* Invalid continuation - flush what we have as individual bytes,
             * then process the new byte */
            utf8_len = 0;
            utf8_expected = 0;
            /* Process the new byte normally (fall through below) */
            /* Don't return - process this byte */
            goto process_single;
        }
        return;
    }

process_single:
    /* Check if this is the start of a multibyte sequence */
    if (byte >= 0xC0) {
        utf8_expected = utf8_sequence_length(byte);
        utf8_buf[0] = byte;
        utf8_len = 1;
        if (utf8_len >= utf8_expected) {
            /* Single byte (shouldn't happen for >= 0xC0, but safety) */
            int bytes_read = 0;
            uint32_t cp = utf8_decode((const char *)utf8_buf, &bytes_read);
            utf8_len = 0;
            utf8_expected = 0;
            fb_console_process_codepoint(cp);
        }
        return;
    }

    /* ASCII or control character - process directly */
    utf8_len = 0;
    utf8_expected = 0;
    fb_console_process_codepoint((uint32_t)byte);
}

void fb_console_write(const char *str) {
    while (*str) {
        fb_console_putchar(*str);
        str++;
    }
}

void fb_console_clear(void) {
    if (fb_ctx.width == 0 || fb_ctx.height == 0) return;
    gfx_rect_t rect = { 0, 0, (int32_t)fb_ctx.width, (int32_t)fb_ctx.height };
    gfx_fill_rect(&fb_ctx, rect, bg_color);
    cursor_x = 0;
    cursor_y = 0;
    utf8_len = 0;
    utf8_expected = 0;
    sb_view_offset = 0;
    saved_cursor_x = 0;
    saved_cursor_y = 0;
    screen_snapshot_valid = 0;
    for (uint32_t i = 0; i < MAX_SCREEN_ROWS; i++) {
        screen_text[i][0] = '\0';
        screen_text_len[i] = 0;
    }
}

/* Capture current screen rows into snapshot for scroll-view restore */
static void screen_snapshot_save(void) {
    if ((uint32_t)rows > MAX_SCREEN_ROWS) return;
    for (uint32_t r = 0; r < (uint32_t)rows; r++) {
        /* Copy screen_text to screen_rows snapshot */
        for (uint32_t j = 0; j < SCROLLBACK_LINE_LEN; j++) {
            screen_rows[r].text[j] = screen_text[r][j];
        }
        screen_rows[r].len = screen_text_len[r];
        screen_rows[r].fg = fg_color;
        screen_rows[r].bg = bg_color;
    }
    saved_cursor_x = (int32_t)cursor_x;
    saved_cursor_y = (int32_t)cursor_y;
    screen_snapshot_valid = 1;
}

/*
 * Record a line of text into the text-based scrollback buffer.
 * Called when a line scrolls off the top of the screen.
 */
static void sb_push_line(const char *text, uint32_t len) {
    if (!sb_lines) return;
    uint32_t idx = sb_head % SCROLLBACK_MAX_LINES;
    if (len >= SCROLLBACK_LINE_LEN) len = SCROLLBACK_LINE_LEN - 1;
    for (uint32_t i = 0; i < len; i++) {
        sb_lines[idx].text[i] = text[i];
    }
    sb_lines[idx].text[len] = '\0';
    sb_lines[idx].len = len;
    sb_head++;
    if (sb_count < SCROLLBACK_MAX_LINES) sb_count++;
}

/* Get a line from scrollback by reverse index (0 = most recent) */
static const char *sb_get_line(uint32_t rev_index, uint32_t *out_len) {
    if (!sb_lines || rev_index >= sb_count) { *out_len = 0; return ""; }
    int idx = (int)(sb_head - 1 - (int)rev_index);
    if (idx < 0) idx += (int)SCROLLBACK_MAX_LINES;
    *out_len = sb_lines[idx].len;
    return sb_lines[idx].text;
}

void fb_console_scroll(uint32_t lines_to_scroll) {
    if (lines_to_scroll == 0) return;
    if (lines_to_scroll > rows) lines_to_scroll = rows;
    if (fb_ctx.width == 0 || fb_ctx.height == 0) return;

    /* Push scrolled-off lines to scrollback with actual text content */
    for (uint32_t l = 0; l < lines_to_scroll && l < MAX_SCREEN_ROWS; l++) {
        sb_push_line(screen_text[l], screen_text_len[l]);
    }

    /* Shift screen_text up to mirror the framebuffer scroll */
    for (uint32_t i = 0; i < (uint32_t)rows - lines_to_scroll && i + lines_to_scroll < MAX_SCREEN_ROWS; i++) {
        for (uint32_t j = 0; j < SCROLLBACK_LINE_LEN; j++) {
            screen_text[i][j] = screen_text[i + lines_to_scroll][j];
        }
        screen_text_len[i] = screen_text_len[i + lines_to_scroll];
    }
    /* Clear bottom rows in screen_text */
    for (uint32_t i = (uint32_t)rows - lines_to_scroll; i < (uint32_t)rows && i < MAX_SCREEN_ROWS; i++) {
        screen_text[i][0] = '\0';
        screen_text_len[i] = 0;
    }

    uint32_t scroll_pixels = lines_to_scroll * FONT_GLYPH_HEIGHT;

    /* Shift framebuffer up (standard scroll operation) */
    if (fb_ctx.bpp == 32) {
        uint32_t pitch_words = fb_ctx.pitch / 4;
        for (uint32_t y = 0; y < fb_ctx.height - scroll_pixels; y++) {
            for (uint32_t x = 0; x < fb_ctx.width; x++) {
                fb_ctx.buffer[y * pitch_words + x] =
                    fb_ctx.buffer[(y + scroll_pixels) * pitch_words + x];
            }
        }
    } else if (fb_ctx.bpp == 24) {
        uint8_t *fb8 = (uint8_t *)fb_ctx.buffer;
        uint32_t copy_bytes = (fb_ctx.height - scroll_pixels) * fb_ctx.pitch;
        for (uint32_t i = 0; i < copy_bytes; i++) {
            fb8[i] = fb8[i + scroll_pixels * fb_ctx.pitch];
        }
    }

    /* Clear bottom lines */
    gfx_rect_t rect = {
        0,
        (int32_t)(fb_ctx.height - scroll_pixels),
        (int32_t)fb_ctx.width,
        (int32_t)scroll_pixels
    };
    gfx_fill_rect(&fb_ctx, rect, bg_color);

    /* Only reset view offset if user is at bottom */
    if (sb_view_offset == 0) {
        /* already at bottom, no-op */
    }

    if (cursor_y >= lines_to_scroll) {
        cursor_y -= lines_to_scroll;
    } else {
        cursor_y = 0;
    }
}

/* Re-render the screen from scrollback history + current content */
static void fb_console_repaint(void) {
    if (fb_ctx.width == 0 || fb_ctx.height == 0) return;

    /* Clear entire screen to background color */
    gfx_rect_t full_rect = { 0, 0, (int32_t)fb_ctx.width, (int32_t)fb_ctx.height };
    gfx_fill_rect(&fb_ctx, full_rect, bg_color);

    uint32_t visible_rows = rows;
    uint32_t sb_show = 0;

    /* How many scrollback lines to show at top? */
    if (sb_view_offset > 0 && sb_count > 0) {
        sb_show = (uint32_t)sb_view_offset;
        if (sb_show > visible_rows) sb_show = visible_rows;
    }

    /* Save position, render scrollback lines at top */
    uint32_t saved_cx = cursor_x, saved_cy = cursor_y;
    uint32_t saved_fg = fg_color, saved_bg = bg_color;

    cursor_x = 0;
    cursor_y = 0;
    fg_color = 0xFFFFFF;
    bg_color = 0x000000;

    /* Render historical lines from scrollback buffer */
    for (uint32_t l = 0; l < sb_show; l++) {
        uint32_t len = 0;
        const char *txt = sb_get_line((sb_view_offset - (int32_t)(sb_show - l)), &len);
        if (len > 0) {
            for (uint32_t i = 0; i < len; i++) {
                fb_console_putchar(txt[i]);
            }
        }
        /* Ensure we advance to next line */
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= rows) break;
    }

    /* Restore state */
    cursor_x = saved_cx;
    cursor_y = saved_cy;
    fg_color = saved_fg;
    bg_color = saved_bg;
}

/* Scroll up: view older content */
void fb_console_scroll_up(int lines) {
    if (!sb_lines || sb_count == 0 || lines <= 0) return;

    /* On first scroll-up, save current screen state */
    if (sb_view_offset == 0) {
        screen_snapshot_save();
    }

    if (sb_view_offset >= (int32_t)sb_count) return;

    if (lines > (int32_t)sb_count - sb_view_offset) {
        lines = (int32_t)sb_count - sb_view_offset;
    }

    sb_view_offset += lines;

    /* Full repaint: clear screen and re-render from scrollback */
    fb_console_repaint();
}

/* Scroll down: return toward normal view */
void fb_console_scroll_down(int lines) {
    if (lines <= 0 || sb_view_offset <= 0) return;

    if (lines > sb_view_offset) {
        lines = sb_view_offset;
    }

    sb_view_offset -= lines;

    if (sb_view_offset == 0) {
        /* Fully returned to normal view: restore screen from snapshot */
        gfx_rect_t full_rect = { 0, 0, (int32_t)fb_ctx.width, (int32_t)fb_ctx.height };
        gfx_fill_rect(&fb_ctx, full_rect, bg_color);

        /* Restore screen_text from snapshot and re-render each row */
        uint32_t saved_fg_local = fg_color;
        uint32_t saved_bg_local = bg_color;
        fg_color = 0xFFFFFF;
        bg_color = 0x000000;

        for (uint32_t r = 0; r < (uint32_t)rows && r < MAX_SCREEN_ROWS; r++) {
            /* Restore screen_text from snapshot */
            for (uint32_t j = 0; j < SCROLLBACK_LINE_LEN; j++) {
                screen_text[r][j] = screen_rows[r].text[j];
            }
            screen_text_len[r] = screen_rows[r].len;

            /* Re-render the row character by character */
            uint32_t cx = 0;
            for (uint32_t i = 0; i < screen_rows[r].len; i++) {
                char ch = screen_rows[r].text[i];
                if (ch == '\t') {
                    cx = (cx + 8) & ~7u;
                    if (cx >= cols) cx = cols - 1;
                } else if (ch >= 32) {
                    int32_t px = (int32_t)(cx * FONT_GLYPH_WIDTH);
                    int32_t py = (int32_t)(r * FONT_GLYPH_HEIGHT);
                    font_draw_char(&fb_ctx, ch, px, py, fg_color, bg_color);
                    cx++;
                }
            }
        }

        /* Restore cursor position and colors */
        cursor_x = (uint32_t)saved_cursor_x;
        cursor_y = (uint32_t)saved_cursor_y;
        fg_color = saved_fg_local;
        bg_color = saved_bg_local;
        screen_snapshot_valid = 0;
    } else {
        fb_console_repaint();
    }
}

void fb_console_set_color(uint32_t fg, uint32_t bg) {
    fg_color = fg;
    bg_color = bg;
}

void fb_console_set_fg(uint32_t color) {
    fg_color = color;
}

void fb_console_set_bg(uint32_t color) {
    bg_color = color;
}

uint32_t fb_console_get_fg(void) {
    return fg_color;
}

uint32_t fb_console_get_bg(void) {
    return bg_color;
}

/* Blinking cursor: draw/erase underscore at current cursor position.
 * Call this from idle loops (e.g. shell_read_line's key-wait loop).
 * Always erases the previous cursor mark before redrawing, so that
 * cursor movement (e.g. after newline) never leaves a ghost underscore. */
void fb_console_blink_cursor(void) {
    if (cols == 0 || rows == 0 || fb_ctx.width == 0) return;

    uint32_t now = timer_get_ticks();
    if (now - cursor_blink_last_tick >= CURSOR_BLINK_RATE) {
        cursor_blink_last_tick = now;
        cursor_blink_visible = !cursor_blink_visible;
    }

    /* Always erase the cursor-area rectangle first to clean up any
     * leftover underscore from a prior position or state */
    int32_t px = (int32_t)(cursor_x * FONT_GLYPH_WIDTH);
    int32_t py = (int32_t)(cursor_y * FONT_GLYPH_HEIGHT + FONT_GLYPH_HEIGHT - 2);

    /* Unconditional erase of the cursor cell area */
    gfx_rect_t erase_rect = {px, py, FONT_GLYPH_WIDTH, 2};
    gfx_fill_rect(&fb_ctx, erase_rect, bg_color);

    /* Then redraw the underscore only if currently visible */
    if (cursor_blink_visible) {
        gfx_rect_t draw_rect = {px, py, FONT_GLYPH_WIDTH, 2};
        gfx_fill_rect(&fb_ctx, draw_rect, fg_color);
    }
}
