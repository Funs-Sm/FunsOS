#include "more_apps.h"
#include "keyboard.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "timer.h"
#include "vga_text.h"
#include "vfs.h"
#include "klog.h"
#include "version.h"
#include "pmm.h"

static uint16_t * const vga_buf = (uint16_t *)0xB8000;

static inline void vga_putc(int row, int col, char c, uint8_t color) {
    if (row < 0 || row >= 25 || col < 0 || col >= 80) return;
    vga_buf[row * 80 + col] = (uint16_t)((color << 8) | (uint8_t)c);
}

static inline void vga_clear_all(void) {
    for (int i = 0; i < 80 * 25; i++) vga_buf[i] = 0x0720;
}

static inline void vga_puts(int row, int col, const char *s, uint8_t color) {
    while (*s && col < 80) { vga_putc(row, col++, *s++, color); }
}

static void vga_fill_rect(int row, int col, int w, int h, char c, uint8_t color) {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            vga_putc(row + y, col + x, c, color);
}

static void save_cursor_pos(int *r, int *c) { vga_text_get_cursor(r, c); }
static void restore_cursor_pos(int r, int c) { vga_text_set_cursor(r, c); vga_text_set_color(15, 0); }

static int key_hit(void) {
    keyboard_poll();
    keyboard_event_t ev;
    if (keyboard_get_event(&ev)) {
        if (ev.flags & KEY_PRESSED) return 1;
    }
    return 0;
}

static void delay_ms_approx(int ms) {
    for (int i = 0; i < ms * 10000; i++) asm volatile("" ::: "memory");
}

/* ================================================================
   Conway's Game of Life
   ================================================================ */
void life_run(void) {
    static uint8_t grid[40][20];
    static uint8_t next[40][20];
    const int GW = 40, GH = 20;
    const int SX = 2, SY = 2;

    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++)
            grid[x][y] = (timer_get_ticks() ^ (x * 7 + y * 13)) & 1;

    uint32_t last = timer_get_ticks();
    int running = 1;
    int speed = 5;
    int gen = 0;

    int cr, cc; save_cursor_pos(&cr, &cc);

    while (1) {
        keyboard_event_t ev;
        while (keyboard_get_event(&ev)) {
            if (!(ev.flags & KEY_PRESSED)) continue;
            if (ev.ascii == 27) { vga_clear_all(); restore_cursor_pos(cr, cc); return; }
            if (ev.ascii == ' ') running = !running;
            if (ev.ascii == 'r' || ev.ascii == 'R') {
                for (int y = 0; y < GH; y++)
                    for (int x = 0; x < GW; x++)
                        grid[x][y] = (timer_get_ticks() ^ (x * 17 + y * 23)) & 1;
                gen = 0;
            }
            if (ev.ascii == '+' || ev.ascii == '=') { if (speed > 1) speed--; }
            if (ev.ascii == '-' || ev.ascii == '_') { if (speed < 20) speed++; }
        }
        keyboard_poll();

        vga_putc(SY - 1, SX - 1, '+', 0x0B);
        for (int x = 0; x < GW; x++) vga_putc(SY - 1, SX + x, '-', 0x0B);
        vga_putc(SY - 1, SX + GW, '+', 0x0B);
        for (int y = 0; y < GH; y++) {
            vga_putc(SY + y, SX - 1, '|', 0x0B);
            vga_putc(SY + y, SX + GW, '|', 0x0B);
            for (int x = 0; x < GW; x++)
                vga_putc(SY + y, SX + x, grid[x][y] ? (char)0xDB : ' ', grid[x][y] ? 0x0A : 0x00);
        }
        vga_putc(SY + GH, SX - 1, '+', 0x0B);
        for (int x = 0; x < GW; x++) vga_putc(SY + GH, SX + x, '-', 0x0B);
        vga_putc(SY + GH, SX + GW, '+', 0x0B);

        char buf[64];
        snprintf(buf, sizeof(buf), " Gen: %d  Speed: %d ", gen, speed);
        vga_puts(SY + GH + 1, SX, buf, 0x0F);
        vga_puts(SY + GH + 2, SX, " Space:Pause  +/-:Speed  R:Reset  ESC:Quit", 0x08);

        uint32_t now = timer_get_ticks();
        if (running && now - last >= (uint32_t)speed) {
            last = now; gen++;
            for (int y = 0; y < GH; y++) {
                for (int x = 0; x < GW; x++) {
                    int n = 0;
                    for (int dy = -1; dy <= 1; dy++)
                        for (int dx = -1; dx <= 1; dx++) {
                            if (dx == 0 && dy == 0) continue;
                            int nx = (x + dx + GW) % GW;
                            int ny = (y + dy + GH) % GH;
                            n += grid[nx][ny];
                        }
                    next[x][y] = (grid[x][y]) ? (n == 2 || n == 3) : (n == 3);
                }
            }
            memcpy(grid, next, sizeof(grid));
        }
        delay_ms_approx(20);
    }
}

/* ================================================================
   Sokoban - simple level
   ================================================================ */
#define SK_W 12
#define SK_H 10
static const char sk_level[] =
    "  ########  "
    "  #      #  "
    "  # .$@  #  "
    "  #  $ . #  "
    "  # .  $ #  "
    "  #      #  "
    "  ########  ";

void sokoban_run(void) {
    char map[SK_H][SK_W + 1];
    int px = 0, py = 0;

    for (int y = 0; y < SK_H; y++) {
        for (int x = 0; x < SK_W; x++) {
            char c = sk_level[y * SK_W + x];
            if (c == '@') { px = x; py = y; c = ' '; }
            map[y][x] = c;
        }
        map[y][SK_W] = '\0';
    }

    int moves = 0;
    const int SX = 30, SY = 5;
    int cr, cc; save_cursor_pos(&cr, &cc);
    vga_clear_all();

    while (1) {
        vga_fill_rect(SY, SX, SK_W, SK_H, ' ', 0x07);
        for (int y = 0; y < SK_H; y++)
            for (int x = 0; x < SK_W; x++) {
                char c = map[y][x];
                uint8_t col = 0x07; char ch = c;
                if (c == '#') { ch = 0xDB; col = 0x08; }
                else if (c == '$') { ch = 'O'; col = 0x0C; }
                else if (c == '.') { ch = '.'; col = 0x0B; }
                else if (c == '*') { ch = 'X'; col = 0x0A; }
                vga_putc(SY + y, SX + x, ch, col);
            }
        vga_putc(SY + py, SX + px, '@', 0x0E);

        char buf[64];
        snprintf(buf, sizeof(buf), " Moves: %d ", moves);
        vga_puts(SY + SK_H + 1, SX, buf, 0x0F);
        vga_puts(SY + SK_H + 2, SX, " Arrows:Move  R:Reset  ESC:Quit", 0x08);

        keyboard_event_t ev;
        int dx = 0, dy = 0, reset = 0;
        while (1) {
            while (keyboard_get_event(&ev)) {
                if (!(ev.flags & KEY_PRESSED)) continue;
                if (ev.ascii == 27) { vga_clear_all(); restore_cursor_pos(cr, cc); return; }
                if (ev.ascii == 'r' || ev.ascii == 'R') reset = 1;
                if (ev.flags & KEY_EXTENDED) {
                    switch (ev.scancode) {
                        case 0x48: dy = -1; break;
                        case 0x50: dy = 1; break;
                        case 0x4B: dx = -1; break;
                        case 0x4D: dx = 1; break;
                    }
                }
            }
            keyboard_poll();
            if (reset || dx || dy) break;
            delay_ms_approx(20);
        }

        if (reset) {
            for (int y = 0; y < SK_H; y++)
                for (int x = 0; x < SK_W; x++) {
                    char c = sk_level[y * SK_W + x];
                    if (c == '@') { px = x; py = y; c = ' '; }
                    map[y][x] = c;
                }
            moves = 0; continue;
        }

        int nx = px + dx, ny = py + dy;
        if (nx < 0 || nx >= SK_W || ny < 0 || ny >= SK_H) continue;
        if (map[ny][nx] == '#') continue;
        int nbx = nx + dx, nby = ny + dy;
        int pushing = (map[ny][nx] == '$' || map[ny][nx] == '*');
        if (pushing) {
            if (nbx < 0 || nbx >= SK_W || nby < 0 || nby >= SK_H) continue;
            char dest = map[nby][nbx];
            if (dest == '#' || dest == '$' || dest == '*') continue;
            map[ny][nx] = (map[ny][nx] == '*') ? '.' : ' ';
            map[nby][nbx] = (dest == '.') ? '*' : '$';
        }
        px = nx; py = ny; moves++;
    }
}

/* ================================================================
   Typing Tutor
   ================================================================ */
static const char *typing_words[] = {
    "the", "quick", "brown", "fox", "jumps", "over", "lazy", "dog",
    "hello", "world", "kernel", "system", "computer", "program",
    "memory", "file", "network", "driver", "shell", "command",
    "funsos", "operating", "process", "thread", "scheduler",
    "keyboard", "mouse", "display", "console", "terminal",
    NULL
};

void typing_run(void) {
    int score = 0, total = 0, correct = 0, idx = 0;
    const int SX = 5, SY = 8;
    int cr, cc; save_cursor_pos(&cr, &cc);
    uint32_t start = timer_get_ticks();
    vga_clear_all();

    while (1) {
        vga_clear_all();
        vga_puts(SY - 3, SX, "=== Typing Practice ===", 0x0B);
        vga_puts(SY - 1, SX, "Type the words shown. Press ESC to quit.", 0x08);

        const char *target = typing_words[idx % 30];

        char buf[80];
        snprintf(buf, sizeof(buf), "Score: %d   Accuracy: %d%%   Time: %us",
                 score, total > 0 ? (correct * 100 / total) : 100,
                 (timer_get_ticks() - start) / 100);
        vga_puts(SY - 4, SX, buf, 0x0F);

        vga_puts(SY, SX, target, 0x0E);
        vga_puts(SY + 2, SX, "> ", 0x0A);

        int pos = 0; char typed[64] = {0};

        while (1) {
            keyboard_event_t ev;
            while (keyboard_get_event(&ev)) {
                if (!(ev.flags & KEY_PRESSED)) continue;
                char ch = ev.ascii;
                if (ch == 27) { vga_clear_all(); restore_cursor_pos(cr, cc); return; }
                if (ch == 13) goto word_done;
                if (ch == 8 || ch == 127) {
                    if (pos > 0) {
                        pos--; typed[pos] = '\0';
                        vga_putc(SY + 2, SX + 2 + pos, ' ', 0x07);
                    }
                    continue;
                }
                if (ch >= 32 && ch < 127 && pos < 40) {
                    typed[pos] = ch; total++;
                    uint8_t col = (ch == target[pos]) ? 0x0A : 0x0C;
                    if (ch == target[pos]) correct++; else score = score > 2 ? score - 2 : 0;
                    vga_putc(SY + 2, SX + 2 + pos, ch, col);
                    pos++;
                }
            }
            keyboard_poll();
            delay_ms_approx(10);
        }
    word_done:
        typed[pos] = '\0';
        if (strcmp(typed, target) == 0) score += 10;
        idx++;
    }
}

/* ================================================================
   ASCII Table
   ================================================================ */
void ascii_table_run(void) {
    int cr, cc; save_cursor_pos(&cr, &cc);
    vga_clear_all();
    vga_puts(1, 2, "=== ASCII Table ===", 0x0B);
    vga_puts(2, 2, "Dec  Hex  Chr  | Dec  Hex  Chr  | Dec  Hex  Chr  | Dec  Hex  Chr", 0x08);

    for (int i = 32; i < 127; i++) {
        int col = ((i - 32) / 24) * 19;
        int r = 4 + ((i - 32) % 24);
        char c = (char)i;
        if (i == 127) c = ' ';
        char buf[20];
        snprintf(buf, sizeof(buf), "%3d  %02X  '%c'", i, i, c);
        uint8_t ca = 0x0F;
        if (i == ' ') ca = 0x08;
        vga_puts(r, 2 + col, buf, ca);
    }
    vga_puts(24, 2, "Press any key to return...", 0x08);
    while (1) {
        keyboard_event_t ev;
        while (keyboard_get_event(&ev)) {
            if (ev.flags & KEY_PRESSED) { vga_clear_all(); restore_cursor_pos(cr, cc); return; }
        }
        keyboard_poll();
        delay_ms_approx(20);
    }
}

/* ================================================================
   Hexdump viewer
   ================================================================ */
void hexdump_run(const char *filepath) {
    int cr, cc; save_cursor_pos(&cr, &cc);

    if (!filepath || !*filepath) {
        vga_clear_all();
        vga_puts(2, 2, "Usage: hexdump <filename>", 0x0C);
        vga_puts(4, 2, "Press any key...", 0x08);
        while (!key_hit()) { keyboard_poll(); delay_ms_approx(20); }
        { keyboard_event_t ev; while (keyboard_get_event(&ev)) { if (ev.flags & KEY_PRESSED) break; } }
        vga_clear_all(); restore_cursor_pos(cr, cc); return;
    }

    file_t *f = NULL;
    if (vfs_open(filepath, FILE_MODE_READ, &f) != 0 || !f) {
        vga_clear_all();
        vga_puts(2, 2, "Error: Cannot open file ", 0x0C);
        vga_puts(2, 24, filepath, 0x0F);
        vga_puts(4, 2, "Press any key...", 0x08);
        while (!key_hit()) { keyboard_poll(); delay_ms_approx(20); }
        { keyboard_event_t ev; while (keyboard_get_event(&ev)) { if (ev.flags & KEY_PRESSED) break; } }
        vga_clear_all(); restore_cursor_pos(cr, cc); return;
    }

    while (1) {
        vga_clear_all();
        vga_puts(1, 2, "=== Hexdump ===", 0x0B);
        char fn[64]; snprintf(fn, sizeof(fn), "File: %s", filepath);
        vga_puts(2, 2, fn, 0x0F);
        vga_puts(3, 2, "Offset   Hex data                              ASCII", 0x08);

        uint8_t buf[16]; uint32_t offset = 0; int row = 5;
        for (int i = 0; i < 18; i++) {
            int32_t n = vfs_read(f, buf, 16);
            if (n <= 0) break;
            char line[80]; int lp = 0;
            lp += snprintf(line + lp, sizeof(line) - lp, "%08x ", offset);
            for (int j = 0; j < 16; j++) {
                if (j < n) lp += snprintf(line + lp, sizeof(line) - lp, "%02x ", buf[j]);
                else lp += snprintf(line + lp, sizeof(line) - lp, "   ");
                if (j == 7) lp += snprintf(line + lp, sizeof(line) - lp, " ");
            }
            lp += snprintf(line + lp, sizeof(line) - lp, " |");
            for (int j = 0; j < n; j++)
                lp += snprintf(line + lp, sizeof(line) - lp, "%c", (buf[j] >= 32 && buf[j] < 127) ? buf[j] : '.');
            lp += snprintf(line + lp, sizeof(line) - lp, "|");
            vga_puts(row++, 2, line, 0x0F);
            offset += (uint32_t)n;
            if (n < 16) break;
        }
        vga_puts(24, 2, "Press any key (more) or ESC/Q to quit...", 0x08);
        while (1) {
            keyboard_event_t ev;
            while (keyboard_get_event(&ev)) {
                if (ev.flags & KEY_PRESSED) {
                    if (ev.ascii == 27 || ev.ascii == 'q' || ev.ascii == 'Q') {
                        vfs_close(f); vga_clear_all(); restore_cursor_pos(cr, cc); return;
                    }
                    goto next_page;
                }
            }
            keyboard_poll(); delay_ms_approx(20);
        }
    next_page:;
    }
}

/* ================================================================
   dmesg - kernel log viewer
   ================================================================ */
void dmesg_run(void) {
    int cr, cc; save_cursor_pos(&cr, &cc);
    vga_clear_all();
    vga_puts(1, 2, "=== Kernel Log (dmesg) ===", 0x0B);

    char buf[76 * 24];
    uint32_t total = klog_get_line_count();
    uint32_t start = (total > 20) ? total - 20 : 0;
    uint32_t got = klog_read_from(start, buf, sizeof(buf));

    int row = 3; char *p = buf, *ls = p;
    for (uint32_t i = 0; i < got && row < 24; i++) {
        if (buf[i] == '\n') {
            buf[i] = '\0'; vga_puts(row++, 2, ls, 0x0A); buf[i] = '\n'; ls = &buf[i + 1];
        }
    }
    vga_puts(24, 2, "Press any key to return...", 0x08);
    while (!key_hit()) { keyboard_poll(); delay_ms_approx(20); }
    { keyboard_event_t ev; while (keyboard_get_event(&ev)) { if (ev.flags & KEY_PRESSED) break; } }
    vga_clear_all(); restore_cursor_pos(cr, cc);
}

/* ================================================================
   uname - system information
   ================================================================ */
void uname_run(void) {
    int cr, cc; save_cursor_pos(&cr, &cc);
    char buf[128];
    vga_clear_all();
    vga_puts(2, 2, "System information:", 0x0B);
    snprintf(buf, sizeof(buf), "  Kernel:   %s", KERNEL_STRING); vga_puts(4, 4, buf, 0x0F);
    uint32_t total_p = pmm_get_used_pages() + pmm_get_free_pages();
    uint32_t mem_kb = total_p * 4;
    snprintf(buf, sizeof(buf), "  Memory:   %u KB total (%u KB used, %u KB free)",
             mem_kb, pmm_get_used_pages() * 4, pmm_get_free_pages() * 4);
    vga_puts(6, 4, buf, 0x0F);
    snprintf(buf, sizeof(buf), "  VFS:      ramfs, devfs, procfs, sysfs, tmpfs, tarfs"); vga_puts(7, 4, buf, 0x0F);
    snprintf(buf, sizeof(buf), "  Network:  TCP/IP stack with loopback"); vga_puts(8, 4, buf, 0x0F);
    snprintf(buf, sizeof(buf), "  Features: %d games, %d apps, GUI desktop", 8, 15); vga_puts(9, 4, buf, 0x0F);
    snprintf(buf, sizeof(buf), "  Uptime:   %u seconds", timer_get_ticks() / 100); vga_puts(11, 4, buf, 0x0F);
    vga_puts(13, 4, "Press any key to return...", 0x08);
    while (!key_hit()) { keyboard_poll(); delay_ms_approx(20); }
    { keyboard_event_t ev; while (keyboard_get_event(&ev)) { if (ev.flags & KEY_PRESSED) break; } }
    vga_clear_all(); restore_cursor_pos(cr, cc);
}

/* ================================================================
   Simple Text Editor (nano-like)
   ================================================================ */
#define NE_MAX_LINES 500
#define NE_MAX_COLS  76
static char ne_buf[NE_MAX_LINES][NE_MAX_COLS + 1];
static int ne_line_count = 0;

void nano_run(const char *filepath) {
    ne_line_count = 0;
    memset(ne_buf, 0, sizeof(ne_buf));

    if (filepath && *filepath) {
        file_t *f = NULL;
        if (vfs_open(filepath, FILE_MODE_READ, &f) == 0 && f) {
            int cl = 0, cc = 0; char ch;
            while (cl < NE_MAX_LINES) {
                int32_t n = vfs_read(f, &ch, 1);
                if (n <= 0) break;
                if (ch == '\n') { ne_buf[cl][cc] = '\0'; cl++; cc = 0; }
                else if (ch == '\r') continue;
                else if (cc < NE_MAX_COLS) ne_buf[cl][cc++] = ch;
            }
            if (cc > 0 || cl == 0) { ne_buf[cl][cc] = '\0'; cl++; }
            ne_line_count = cl; vfs_close(f);
        }
    }
    if (ne_line_count == 0) { strcpy(ne_buf[0], ""); ne_line_count = 1; }

    int cx = 0, cy = 0, start_line = 0, modified = 0;
    const int VIEW = 22;
    int cr, cc_; save_cursor_pos(&cr, &cc_);
    vga_clear_all();

    while (1) {
        vga_fill_rect(0, 0, 80, 23, ' ', 0x07);
        vga_fill_rect(0, 23, 80, 1, ' ', 0x1F);
        char status[80];
        snprintf(status, sizeof(status), "-- FUNSOS Editor --  File: %s%s  Lines: %d  Ln %d Col %d",
                 filepath ? filepath : "[New]", modified ? " [Modified]" : "", ne_line_count, start_line + cy + 1, cx + 1);
        vga_puts(23, 0, status, 0x1F);
        vga_fill_rect(0, 24, 80, 1, ' ', 0x70);
        vga_puts(24, 0, "^X=Exit  ^S=Save  Arrows:Move  BS/DEL:Delete  Enter:New line", 0x70);

        for (int i = 0; i < VIEW; i++) {
            int ln = start_line + i;
            if (ln >= ne_line_count) break;
            vga_puts(i, 0, ne_buf[ln], 0x07);
        }
        vga_text_set_cursor(cx, cy);

        keyboard_event_t ev;
        while (keyboard_get_event(&ev)) {
            if (!(ev.flags & KEY_PRESSED)) continue;
            char ch = ev.ascii;

            if ((ev.flags & KEY_EXTENDED) == 0 && ch == 0x18) {
                vga_clear_all(); restore_cursor_pos(cr, cc_); return;
            }
            if ((ev.flags & KEY_EXTENDED) == 0 && ch == 0x13) {
                if (filepath && *filepath) {
                    file_t *wf = NULL;
                    if (vfs_open(filepath, FILE_MODE_WRITE | FILE_MODE_CREATE, &wf) == 0 && wf) {
                        for (int i = 0; i < ne_line_count; i++) {
                            int32_t len = (int32_t)strlen(ne_buf[i]);
                            vfs_write(wf, ne_buf[i], len);
                            vfs_write(wf, "\n", 1);
                        }
                        vfs_close(wf); modified = 0;
                        vga_puts(23, 65, "[Saved]", 0x1F);
                        delay_ms_approx(500);
                    }
                }
                continue;
            }

            if (ev.flags & KEY_EXTENDED) {
                switch (ev.scancode) {
                    case 0x48:
                        if (cy > 0) cy--; else if (start_line > 0) start_line--;
                        { int ln = start_line + cy; if (cx > (int)strlen(ne_buf[ln])) cx = (int)strlen(ne_buf[ln]); }
                        break;
                    case 0x50: {
                        int ln = start_line + cy + 1;
                        if (cy < VIEW - 1 && ln < ne_line_count) cy++;
                        else if (start_line + VIEW < ne_line_count) start_line++;
                        ln = start_line + cy;
                        if (ln < ne_line_count && cx > (int)strlen(ne_buf[ln])) cx = (int)strlen(ne_buf[ln]);
                        break;
                    }
                    case 0x4B:
                        if (cx > 0) cx--;
                        else if (cy > 0 || start_line > 0) {
                            if (cy > 0) cy--; else start_line--;
                            cx = (int)strlen(ne_buf[start_line + cy]);
                        }
                        break;
                    case 0x4D: {
                        int ln = start_line + cy;
                        if (cx < (int)strlen(ne_buf[ln])) cx++;
                        else if (ln + 1 < ne_line_count) { cx = 0; if (cy < VIEW - 1) cy++; else start_line++; }
                        break;
                    }
                    case 0x53:
                    case 0x47:
                        break;
                }
                continue;
            }

            if (ch == 13) {
                int ln = start_line + cy;
                if (ne_line_count < NE_MAX_LINES - 1) {
                    for (int i = ne_line_count; i > ln + 1; i--) strcpy(ne_buf[i], ne_buf[i-1]);
                    int len = (int)strlen(ne_buf[ln]);
                    if (cx < len) { strcpy(ne_buf[ln+1], ne_buf[ln]+cx); ne_buf[ln][cx] = '\0'; }
                    else ne_buf[ln+1][0] = '\0';
                    ne_line_count++; modified = 1; cx = 0;
                    if (cy < VIEW - 1) cy++; else start_line++;
                }
                continue;
            }
            if (ch == 8 || ch == 127) {
                int ln = start_line + cy;
                if (cx > 0) {
                    int len = (int)strlen(ne_buf[ln]);
                    for (int i = cx - 1; i < len; i++) ne_buf[ln][i] = ne_buf[ln][i+1];
                    cx--; modified = 1;
                } else if (ln > 0) {
                    int prev = ln - 1;
                    int pl = (int)strlen(ne_buf[prev]);
                    cx = pl; strcat(ne_buf[prev], ne_buf[ln]);
                    for (int i = ln; i < ne_line_count - 1; i++) strcpy(ne_buf[i], ne_buf[i+1]);
                    ne_line_count--; modified = 1;
                    if (cy > 0) cy--; else start_line--;
                }
                continue;
            }
            if (ch >= 32 && ch < 127) {
                int ln = start_line + cy;
                int len = (int)strlen(ne_buf[ln]);
                if (len < NE_MAX_COLS) {
                    for (int i = len; i > cx; i--) ne_buf[ln][i] = ne_buf[ln][i-1];
                    ne_buf[ln][cx] = ch; ne_buf[ln][len+1] = '\0';
                    cx++; modified = 1;
                }
            }
        }
        keyboard_poll();
        delay_ms_approx(10);
    }
}

/* ================================================================
   Top-like Task Monitor (simplified)
   ================================================================ */
void top_run(void) {
    int cr, cc; save_cursor_pos(&cr, &cc);
    vga_clear_all();

    while (1) {
        vga_fill_rect(0, 0, 80, 25, ' ', 0x0F);
        uint32_t up = timer_get_ticks() / 100;
        char line[80];

        vga_fill_rect(0, 0, 80, 1, ' ', 0x1F);
        snprintf(line, sizeof(line), "FUNSOS Task Monitor - Uptime: %us", up);
        vga_puts(0, 0, line, 0x1F);

        uint32_t total_p = pmm_get_used_pages() + pmm_get_free_pages();
        snprintf(line, sizeof(line), "Mem: %uK used, %uK free, %uK total",
                 pmm_get_used_pages() * 4, pmm_get_free_pages() * 4, total_p * 4);
        vga_puts(1, 0, line, 0x0F);

        vga_puts(3, 0, "System processes:", 0x0B);
        vga_puts(5, 2, "[kernel]          - System kernel (idle)", 0x08);
        vga_puts(6, 2, "[shell]           - Command shell", 0x0A);
        vga_puts(7, 2, "[idle]            - Idle task", 0x08);
        vga_puts(8, 2, "[kworker]         - Kernel worker threads", 0x08);

        vga_puts(11, 0, "Kernel subsystems active:", 0x0B);
        vga_puts(13, 2, "- Memory management (pmm/vmm/slab)", 0x0F);
        vga_puts(14, 2, "- Process scheduler (CFS/deadline/rt/fifo)", 0x0F);
        vga_puts(15, 2, "- VFS (ramfs/devfs/procfs/sysfs/tmpfs/fat32)", 0x0F);
        vga_puts(16, 2, "- Network stack (TCP/UDP/IP/ARP/ICMP/DNS/DHCP)", 0x0F);
        vga_puts(17, 2, "- VGA text mode + GUI display server", 0x0F);
        vga_puts(18, 2, "- 50+ device drivers and 30+ kernel subsystems", 0x0F);

        vga_puts(21, 0, "Press any key to exit...", 0x08);

        for (int i = 0; i < 50; i++) {
            keyboard_poll();
            keyboard_event_t ev;
            while (keyboard_get_event(&ev)) {
                if (ev.flags & KEY_PRESSED) {
                    vga_clear_all(); restore_cursor_pos(cr, cc); return;
                }
            }
            delay_ms_approx(20);
        }
    }
}

/* ================================================================
   file - simple file type detection
   ================================================================ */
void file_run(const char *filepath) {
    if (!filepath || !*filepath) { printf("file: missing filename\n"); return; }
    file_t *f = NULL;
    if (vfs_open(filepath, FILE_MODE_READ, &f) != 0 || !f) {
        printf("file: cannot open '%s'\n", filepath); return;
    }
    uint8_t buf[512];
    int32_t n = vfs_read(f, buf, 512);
    vfs_close(f);
    if (n <= 0) { printf("%s: empty\n", filepath); return; }

    const char *type = "data";
    if (n >= 4 && buf[0] == 0x7F && buf[1] == 'E' && buf[2] == 'L' && buf[3] == 'F')
        type = "ELF executable";
    else if (n >= 2 && buf[0] == 'M' && buf[1] == 'Z')
        type = "DOS/MBR executable";
    else if (n >= 8 && buf[0] == 0x89 && buf[1] == 'P' && buf[2] == 'N' && buf[3] == 'G')
        type = "PNG image data";
    else if (n >= 3 && buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF)
        type = "JPEG image data";
    else if (n >= 4 && buf[0] == 'P' && buf[1] == 'K' && buf[2] == 0x03 && buf[3] == 0x04)
        type = "Zip archive data";
    else if (n >= 5 && buf[0] == '<' && buf[1] == 'h' && buf[2] == 't' && buf[3] == 'm')
        type = "HTML document, ASCII text";
    else if (n >= 2 && buf[0] == '#' && buf[1] == '!')
        type = "script text executable";
    else if (n >= 6 && buf[0] == 'u' && buf[1] == 's' && buf[2] == 't' && buf[3] == 'a' && buf[4] == 'r')
        type = "POSIX tar archive";
    else {
        int printable = 0;
        for (int i = 0; i < n; i++) {
            if ((buf[i] >= 0x20 && buf[i] < 0x7F) || buf[i] == '\n' || buf[i] == '\r' || buf[i] == '\t')
                printable++;
        }
        if (printable * 10 >= n * 9) type = "ASCII text";
        else type = "data";
    }
    printf("%s: %s\n", filepath, type);
}

/* ================================================================
   strings - extract printable strings from files
   ================================================================ */
void strings_run(const char *filepath) {
    if (!filepath || !*filepath) { printf("strings: missing filename\n"); return; }
    file_t *f = NULL;
    if (vfs_open(filepath, FILE_MODE_READ, &f) != 0 || !f) {
        printf("strings: cannot open '%s'\n", filepath); return;
    }
    char str_buf[256];
    int sl = 0;
    uint8_t ch;
    while (vfs_read(f, &ch, 1) == 1) {
        if (ch >= 0x20 && ch < 0x7F) {
            if (sl < 255) str_buf[sl++] = ch;
        } else {
            if (sl >= 4) { str_buf[sl] = '\0'; printf("%s\n", str_buf); }
            sl = 0;
        }
    }
    if (sl >= 4) { str_buf[sl] = '\0'; printf("%s\n", str_buf); }
    vfs_close(f);
}

/* ================================================================
   stat - display file status
   ================================================================ */
void stat_run(const char *filepath) {
    if (!filepath || !*filepath) { printf("stat: missing filename\n"); return; }
    inode_t st;
    memset(&st, 0, sizeof(st));
    if (vfs_stat(filepath, &st) != 0) {
        printf("stat: cannot stat '%s'\n", filepath); return;
    }
    printf("  File: %s\n", filepath);
    printf("  Size: %-12u\tBlocks: %u\n", st.size, (st.size / 512 + 1));
    printf("Access: (");
    printf("%c", st.mode & 0400 ? 'r' : '-');
    printf("%c", st.mode & 0200 ? 'w' : '-');
    printf("%c", st.mode & 0100 ? 'x' : '-');
    printf("%c", st.mode & 0040 ? 'r' : '-');
    printf("%c", st.mode & 0020 ? 'w' : '-');
    printf("%c", st.mode & 0010 ? 'x' : '-');
    printf("%c", st.mode & 0004 ? 'r' : '-');
    printf("%c", st.mode & 0002 ? 'w' : '-');
    printf("%c)", st.mode & 0001 ? 'x' : '-');
    const char *ftype = "regular file";
    if (st.mode & FILE_MODE_DIR) ftype = "directory";
    else if (st.mode & FILE_MODE_LNK) ftype = "symbolic link";
    printf("  Uid: %-8u Gid: %-8u\n", st.uid, st.gid);
    printf("  Type: %s\n", ftype);
    printf("Inode: %-12u Links: %u\n", st.ino, st.nlinks);
}

/* ================================================================
   factor - factorize a number
   ================================================================ */
void factor_run(const char *num_str) {
    if (!num_str || !*num_str) { printf("factor: missing number\n"); return; }
    unsigned long n = strtoul(num_str, NULL, 10);
    if (n < 2) { printf("%lu: 1\n", n); return; }
    printf("%lu:", n);
    for (unsigned long p = 2; p * p <= n; p++) {
        while (n % p == 0) { printf(" %lu", p); n /= p; }
    }
    if (n > 1) printf(" %lu", n);
    printf("\n");
}

/* ================================================================
   primes - generate prime numbers
   ================================================================ */
void primes_run(const char *limit_str) {
    uint32_t limit = 100;
    if (limit_str && *limit_str) limit = strtoul(limit_str, NULL, 10);
    if (limit < 2) { printf("primes: limit must be >= 2\n"); return; }
    if (limit > 100000) limit = 100000;

    static uint8_t sieve[100001];
    memset(sieve, 1, sizeof(sieve));
    sieve[0] = sieve[1] = 0;
    for (uint32_t i = 2; i * i <= limit; i++) {
        if (sieve[i]) {
            for (uint32_t j = i * i; j <= limit; j += i) sieve[j] = 0;
        }
    }
    int col = 0;
    for (uint32_t i = 2; i <= limit; i++) {
        if (sieve[i]) {
            printf("%7u", i);
            if (++col >= 10) { printf("\n"); col = 0; }
        }
    }
    if (col > 0) printf("\n");
}

/* ================================================================
   yes - repeatedly output a string
   ================================================================ */
void yes_run(const char *str) {
    const char *s = (str && *str) ? str : "y";
    while (1) {
        printf("%s\n", s);
        keyboard_poll();
        keyboard_event_t ev;
        if (keyboard_get_event(&ev)) {
            if (ev.flags & KEY_PRESSED) {
                if (ev.ascii == 'c' || ev.ascii == 'C' || ev.scancode == 0x01) return;
            }
        }
        for (volatile int i = 0; i < 500000; i++);
    }
}

/* ================================================================
   cowsay - ASCII cow says something
   ================================================================ */
void cowsay_run(const char *msg) {
    if (!msg || !*msg) msg = "Hello from FunsOS!";
    int len = (int)strlen(msg);
    if (len > 40) len = 40;
    printf(" ");
    for (int i = 0; i < len + 2; i++) printf("_");
    printf("\n");
    printf("< %.*s >\n", len, msg);
    printf(" ");
    for (int i = 0; i < len + 2; i++) printf("-");
    printf("\n");
    printf("        \\   ^__^\n");
    printf("         \\  (oo)\\_______\n");
    printf("            (__)\\       )\\/\\\n");
    printf("                ||----w |\n");
    printf("                ||     ||\n");
}

/* ================================================================
   fortune - random quote
   ================================================================ */
void fortune_run(void) {
    static const char *fortunes[] = {
        "The only way to do great work is to love what you do. - Steve Jobs",
        "Code is like humor. When you have to explain it, it's bad. - Cory House",
        "First, solve the problem. Then, write the code. - John Johnson",
        "The best error message is the one that never shows up. - Thomas Fuchs",
        "Talk is cheap. Show me the code. - Linus Torvalds",
        "Programs must be written for people to read, and only incidentally for machines to execute. - H. Abelson",
        "Simplicity is the soul of efficiency. - Austin Freeman",
        "Before software can be reusable it first has to be usable. - Ralph Johnson",
        "A computer would deserve to be called intelligent if it could deceive a human into believing that it was human. - Turing",
        "Any fool can write code that a computer can understand. Good programmers write code that humans can understand. - M. Fowler",
        "The most disastrous thing that you can ever learn is your first programming language. - Alan Kay",
        "FunsOS: because writing an OS is easier than understanding one.",
        "In FunsOS, even the bugs are having fun.",
        "If you think math is hard, try writing an OS.",
        "Every great developer you know got there by solving problems they were unqualified to solve until they actually did it.",
    };
    int idx = (int)(timer_get_ticks() % (sizeof(fortunes)/sizeof(fortunes[0])));
    printf("%s\n", fortunes[idx]);
}

/* ================================================================
   sleep - delay for seconds
   ================================================================ */
void sleep_run(const char *sec_str) {
    if (!sec_str || !*sec_str) { printf("sleep: missing seconds\n"); return; }
    uint32_t sec = strtoul(sec_str, NULL, 10);
    uint32_t end = timer_get_ticks() + sec * 100;
    while (timer_get_ticks() < end) {
        keyboard_poll();
        keyboard_event_t ev;
        if (keyboard_get_event(&ev)) {
            if (ev.flags & KEY_PRESSED && (ev.ascii == 'c' || ev.ascii == 'C')) return;
        }
        delay_ms_approx(10);
    }
}

/* ================================================================
   seq - print sequence of numbers
   ================================================================ */
void seq_run(const char *a, const char *b, const char *c) {
    long start = 1, end = 100, step = 1;
    if (c && *c) { start = strtol(a, NULL, 10); step = strtol(b, NULL, 10); end = strtol(c, NULL, 10); }
    else if (b && *b) { start = strtol(a, NULL, 10); end = strtol(b, NULL, 10); }
    else if (a && *a) { end = strtol(a, NULL, 10); }
    if (step == 0) step = 1;
    if (step > 0 && start > end) return;
    if (step < 0 && start < end) return;
    for (long i = start; ; i += step) {
        printf("%ld\n", i);
        if ((step > 0 && i + step > end) || (step < 0 && i + step < end)) break;
    }
}

/* ================================================================
   banner - large ASCII text
   ================================================================ */
static const char *banner_font[26][5] = {
    {" █████╗","██╔══██╗","███████║","██╔══██║","██║  ██║"},
    {"██████╗ ","██╔══██╗","██████╔╝","██╔══██╗","██████╔╝"},
    {" ██████╗","██╔════╝","██║     ","██║     ","╚██████╔"},
    {"██████╗ ","██╔══██╗","██║  ██║","██║  ██║","██████╔╝"},
    {"███████╗","██╔════╝","█████╗  ","██╔══╝  ","███████╗"},
    {"███████╗","██╔════╝","█████╗  ","██╔══╝  ","██║     "},
    {" ██████╗","██╔════╝","██║ ████","██║  ██║","╚██████╔"},
    {"██╗  ██╗","██║  ██║","███████║","██╔══██║","██║  ██║"},
    {"╚██╗██╔╝"," ╚███╔╝ "," ██╔██╗ ","██╔╝ ██╗","██║  ██║"},
    {"██╗██╗","╚═╝██║","   ██║","   ██║","███████╗"},
    {"██╗██╗","╚═╝██║","  ██╔╝"," ██╔╝ ","██╔═╝  "},
    {"██║     ","██║     ","██║     ","██║     ","███████╗"},
    {"██╗   ██╗","███╗ ███║","████╗ ██║","██╔██╗██║","██║╚████║"},
    {"███╗  ██║","████╗ ██║","██╔██╗██║","██║╚████║","██║ ╚███║"},
    {" █████╗ ","██╔══██╗","██║  ██║","██║  ██║","╚█████╔╝"},
    {"██████╗ ","██╔══██╗","██████╔╝","██╔═══╝ ","██║     "},
    {" █████╗ ","██╔══██╗","██║  ██║","██║  ██║","╚═╝  ╚═╝"},
    {"██████╗ ","██╔══██╗","██████╔╝","██╔══██╗","██║  ██║"},
    {" ██████╗","██╔════╝","╚█████╗ "," ╚═══██╗","██████╔╝"},
    {"███████╗","╚══███╔╝","  ███╔╝ "," ███╔╝  ","███████╗"},
    {"██╗   ██╗","██║   ██║","██║   ██║","██║   ██║","╚██████╔╝"},
    {"██╗   ██╗","██║   ██║","██║   ██║","╚██╗ ██╔╝"," ╚████╔╝ "},
    {"██╗   ██╗","██║   ██║","██║ █╗ ██║","██║███╗██║","╚███╔███╔╝"},
    {"╚██╗ ██╔╝"," ╚████╔╝ ","  ╚██╔╝  ","   ██║   ","   ╚═╝   "},
    {"╚██╗██╔╝"," ╚███╔╝ "," ██╔██╗ ","██╔╝╚██╗ ","██║  ██║"},
    {"███████╗","╚═══███╔╝","   ██╔╝  ","  ██╔╝   ","███████╗"},
};

void banner_run(const char *text) {
    if (!text || !*text) text = "HELLO";
    for (int row = 0; row < 5; row++) {
        const char *p = text;
        while (*p) {
            char ch = *p++;
            if (ch >= 'a' && ch <= 'z') ch -= 32;
            if (ch >= 'A' && ch <= 'Z') {
                printf("%s ", banner_font[ch - 'A'][row]);
            } else {
                printf("       ");
            }
        }
        printf("\n");
    }
}

/* ================================================================
   mktemp - create temporary file
   ================================================================ */
void mktemp_run(void) {
    static uint32_t tmp_counter = 1000;
    char path[64];
    uint32_t ticks = timer_get_ticks();
    snprintf(path, sizeof(path), "/tmp/tmp.%08x.%04x", ticks, tmp_counter++);
    file_t *f = NULL;
    if (vfs_open(path, FILE_MODE_WRITE | FILE_MODE_CREATE, &f) == 0 && f) {
        vfs_close(f);
        printf("%s\n", path);
    } else {
        printf("mktemp: failed to create temp file\n");
    }
}

/* ================================================================
   cp - copy file (simplified, same filesystem)
   ================================================================ */
void cp_run(const char *src, const char *dst) {
    if (!src || !*src || !dst || !*dst) {
        printf("cp: missing source or destination\n"); return;
    }
    file_t *sf = NULL, *df = NULL;
    if (vfs_open(src, FILE_MODE_READ, &sf) != 0 || !sf) {
        printf("cp: cannot open '%s'\n", src); return;
    }
    if (vfs_open(dst, FILE_MODE_WRITE | FILE_MODE_CREATE, &df) != 0 || !df) {
        printf("cp: cannot create '%s'\n", dst); vfs_close(sf); return;
    }
    uint8_t buf[512];
    int32_t total = 0, n;
    while ((n = vfs_read(sf, buf, sizeof(buf))) > 0) {
        vfs_write(df, buf, n);
        total += n;
    }
    vfs_close(sf); vfs_close(df);
    printf("'%s' -> '%s' (%d bytes)\n", src, dst, total);
}

/* ================================================================
   rain - Matrix-style digital rain effect
   ================================================================ */
void rain_run(void) {
    int cr, cc;
    save_cursor_pos(&cr, &cc);
    const int W = 80, H = 25;
    vga_clear_all();
    vga_text_set_cursor(0, H);

    static int rain_cols[80];
    static char rain_chars[80][25];
    for (int i = 0; i < W; i++) {
        rain_cols[i] = -((timer_get_ticks() + i * 7) % H);
    }

    while (1) {
        keyboard_poll();
        keyboard_event_t ev;
        if (keyboard_get_event(&ev) && (ev.flags & KEY_PRESSED)) {
            vga_clear_all(); restore_cursor_pos(cr, cc); return;
        }

        for (int x = 0; x < W; x++) {
            rain_cols[x]++;
            if (rain_cols[x] >= H + 4) {
                rain_cols[x] = -(((timer_get_ticks() >> 2) + x * 3) % (H * 2));
            }
            int y = rain_cols[x];
            if (y >= 0 && y < H) {
                char ch = '!' + ((timer_get_ticks() + x * 31 + y * 17) % 93);
                vga_putc(y, x, ch, 0x0A);
                if (y > 0) {
                    vga_putc(y-1, x, rain_chars[x][y-1], 0x02);
                }
                if (y > 2) {
                    vga_putc(y-3, x, ' ', 0x07);
                }
                rain_chars[x][y] = ch;
            }
        }
        delay_ms_approx(50);
    }
}

/* ================================================================
   snow - falling snow animation
   ================================================================ */
void snow_run(void) {
    int cr, cc;
    save_cursor_pos(&cr, &cc);
    const int W = 80, H = 25;
    vga_clear_all();
    vga_text_set_cursor(0, H);

    static int snow_y[80], snow_x[80], snow_speed[80];
    static char snow_char[80];
    for (int i = 0; i < W; i++) {
        snow_x[i] = (timer_get_ticks() + i * 37) % W;
        snow_y[i] = -((timer_get_ticks() + i * 13) % H);
        snow_speed[i] = 1 + ((timer_get_ticks() + i) % 2);
        snow_char[i] = (i % 3 == 0) ? '*' : ((i % 3 == 1) ? '.' : '+');
    }

    while (1) {
        keyboard_poll();
        keyboard_event_t ev;
        if (keyboard_get_event(&ev) && (ev.flags & KEY_PRESSED)) {
            vga_clear_all(); restore_cursor_pos(cr, cc); return;
        }

        for (int i = 0; i < W; i++) {
            int old_y = snow_y[i];
            if (old_y >= 0 && old_y < H) vga_putc(old_y, snow_x[i], ' ', 0x07);
            snow_y[i] += snow_speed[i];
            if (snow_y[i] >= H) {
                snow_y[i] = 0;
                snow_x[i] = (timer_get_ticks() + i * 41 + W) % W;
            }
            int ny = snow_y[i];
            if (ny >= 0 && ny < H) vga_putc(ny, snow_x[i], snow_char[i], 0x0F);
        }
        delay_ms_approx(100);
    }
}

/* ================================================================
   fire - simple fire effect (text mode)
   ================================================================ */
void fire_run(void) {
    int cr, cc;
    save_cursor_pos(&cr, &cc);
    const int W = 80, H = 25;
    vga_clear_all();
    vga_text_set_cursor(0, H);

    static uint8_t fire_buf[80][25];
    for (int x = 0; x < W; x++) fire_buf[x][H-1] = 70;

    static const char fire_chars[] = " .:=+*#%@";
    static const uint8_t fire_colors[] = {0x00, 0x04, 0x04, 0x06, 0x0C, 0x0C, 0x0E, 0x0E, 0x0F, 0x0F};

    while (1) {
        keyboard_poll();
        keyboard_event_t ev;
        if (keyboard_get_event(&ev) && (ev.flags & KEY_PRESSED)) {
            vga_clear_all(); restore_cursor_pos(cr, cc); return;
        }

        for (int x = 0; x < W; x++) fire_buf[x][H-1] = 50 + (((timer_get_ticks() >> 1) + x * 11) % 40);

        for (int y = 0; y < H-1; y++) {
            for (int x = 0; x < W; x++) {
                int src_x = (x + (((timer_get_ticks() >> 3) % 3) - 1) + W) % W;
                int v = fire_buf[src_x][y+1];
                v = v > 3 ? v - 1 - (v/25) : 0;
                if (v < 0) v = 0;
                if (v > 9) v = 9;
                fire_buf[x][y] = (uint8_t)v;
                vga_putc(y, x, fire_chars[v], fire_colors[v]);
            }
        }
        delay_ms_approx(60);
    }
}

/* ================================================================
   matrix2 - improved Matrix effect
   ================================================================ */
void matrix2_run(void) {
    rain_run();
}

/* ================================================================
   rot13 - ROT13 Caesar cipher
   ================================================================ */
void rot13_run(const char *text) {
    if (!text || !*text) { printf("rot13: missing text\n"); return; }
    const char *p = text;
    while (*p) {
        char c = *p++;
        if ((c >= 'A' && c <= 'M') || (c >= 'a' && c <= 'm')) c += 13;
        else if ((c >= 'N' && c <= 'Z') || (c >= 'n' && c <= 'z')) c -= 13;
        putchar(c);
    }
    printf("\n");
}

/* ================================================================
   rev - reverse characters in each line
   ================================================================ */
void rev_run(const char *text) {
    if (!text || !*text) { printf("rev: missing text\n"); return; }
    int len = (int)strlen(text);
    for (int i = len - 1; i >= 0; i--) putchar(text[i]);
    printf("\n");
}

/* ================================================================
   whoami - print current user name
   ================================================================ */
void whoami_run(void) {
    printf("root\n");
}

/* ================================================================
   tty - print terminal name
   ================================================================ */
void tty_run(void) {
    printf("/dev/console\n");
}

/* ================================================================
   number - convert number to English words (small numbers)
   ================================================================ */
void number_run(const char *num_str) {
    if (!num_str || !*num_str) { printf("number: missing number\n"); return; }
    long n = strtol(num_str, NULL, 10);
    if (n < 0) { printf("negative "); n = -n; }
    static const char *ones[] = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine",
                                 "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen",
                                 "seventeen", "eighteen", "nineteen"};
    static const char *tens[] = {"", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"};
    if (n < 20) { printf("%s\n", ones[n]); return; }
    if (n < 100) {
        printf("%s", tens[n / 10]);
        if (n % 10) printf("-%s", ones[n % 10]);
        printf("\n");
        return;
    }
    if (n < 1000) {
        printf("%s hundred", ones[n / 100]);
        if (n % 100) {
            int rem = n % 100;
            if (rem < 20) printf(" and %s", ones[rem]);
            else {
                printf(" and %s", tens[rem / 10]);
                if (rem % 10) printf("-%s", ones[rem % 10]);
            }
        }
        printf("\n");
        return;
    }
    printf("%ld (too large for words)\n", n);
}

/* ================================================================
   cksum - simple checksum of file
   ================================================================ */
void cksum_run(const char *filepath) {
    if (!filepath || !*filepath) { printf("cksum: missing filename\n"); return; }
    file_t *f = NULL;
    if (vfs_open(filepath, FILE_MODE_READ, &f) != 0 || !f) {
        printf("cksum: cannot open '%s'\n", filepath); return;
    }
    uint32_t crc = 0xFFFFFFFF;
    uint8_t buf[256];
    int32_t n, total = 0;
    while ((n = vfs_read(f, buf, sizeof(buf))) > 0) {
        for (int32_t i = 0; i < n; i++) {
            crc ^= buf[i];
            for (int bit = 0; bit < 8; bit++) {
                if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
                else crc >>= 1;
            }
        }
        total += n;
    }
    vfs_close(f);
    crc ^= 0xFFFFFFFF;
    printf("%u %u %s\n", crc, total, filepath);
}

/* ================================================================
   reset - reset terminal
   ================================================================ */
void reset_run(void) {
    vga_clear_all();
    vga_text_set_cursor(0, 0);
    for (int i = 0; i < 2000; i++) vga_text_putchar(' ');
    vga_clear_all();
    vga_text_set_cursor(0, 0);
}

/* ========== WC - Word/Line/Char count ========== */
void wc_run(const char *filepath) {
    if (!filepath || !*filepath) {
        printf("wc: missing filename\n");
        return;
    }
    file_t *f = NULL;
    if (vfs_open(filepath, FILE_MODE_READ, &f) != 0 || !f) {
        printf("wc: cannot open '%s'\n", filepath);
        return;
    }
    uint32_t lines = 0, words = 0, chars = 0;
    int in_word = 0;
    char buf[512];
    int32_t n;
    while ((n = vfs_read(f, buf, sizeof(buf))) > 0) {
        for (int32_t i = 0; i < n; i++) {
            chars++;
            if (buf[i] == '\n') lines++;
            if (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\n' || buf[i] == '\r') {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                words++;
            }
        }
    }
    vfs_close(f);
    printf(" %5u %5u %5u %s\n", lines, words, chars, filepath);
}

/* ========== HEAD - Show first N lines ========== */
void head_run(const char *filepath, const char *lines_str) {
    if (!filepath || !*filepath) {
        printf("head: missing filename\n");
        return;
    }
    int max_lines = 10;
    if (lines_str && *lines_str) max_lines = atoi(lines_str);
    if (max_lines <= 0) max_lines = 10;
    file_t *f = NULL;
    if (vfs_open(filepath, FILE_MODE_READ, &f) != 0 || !f) {
        printf("head: cannot open '%s'\n", filepath);
        return;
    }
    int cur_line = 0;
    char buf[512];
    int32_t n;
    while (cur_line < max_lines && (n = vfs_read(f, buf, sizeof(buf))) > 0) {
        for (int32_t i = 0; i < n && cur_line < max_lines; i++) {
            putchar(buf[i]);
            if (buf[i] == '\n') cur_line++;
        }
    }
    vfs_close(f);
}

/* ========== TAIL - Show last N lines (simplified) ========== */
void tail_run(const char *filepath, const char *lines_str) {
    if (!filepath || !*filepath) {
        printf("tail: missing filename\n");
        return;
    }
    int max_lines = 10;
    if (lines_str && *lines_str) max_lines = atoi(lines_str);
    if (max_lines <= 0) max_lines = 10;
    file_t *f = NULL;
    if (vfs_open(filepath, FILE_MODE_READ, &f) != 0 || !f) {
        printf("tail: cannot open '%s'\n", filepath);
        return;
    }
    int total_lines = 0;
    char fbuf[65536];
    int32_t fsize = 0;
    int32_t n;
    while ((n = vfs_read(f, fbuf + fsize, sizeof(fbuf) - fsize - 1)) > 0) {
        fsize += n;
        if (fsize >= (int32_t)sizeof(fbuf) - 1) break;
    }
    fbuf[fsize] = 0;
    vfs_close(f);
    for (int32_t i = 0; i < fsize; i++) if (fbuf[i] == '\n') total_lines++;
    int start_line = total_lines - max_lines;
    if (start_line < 0) start_line = 0;
    int cur = 0;
    for (int32_t i = 0; i < fsize; i++) {
        if (cur >= start_line) {
            putchar(fbuf[i]);
        }
        if (fbuf[i] == '\n') cur++;
    }
}

/* ========== PLASMA EFFECT ========== */
static int plasma_sin(int x) {
    static const int sintab[256] = {
        0,3,6,9,12,15,18,21,24,27,30,33,36,39,42,45,48,51,54,57,59,62,65,67,
        70,72,75,77,79,82,84,86,88,90,92,94,96,97,99,101,102,103,105,106,107,108,109,110,
        111,112,113,113,114,114,115,115,115,115,115,115,115,115,114,114,113,113,
        112,111,110,109,108,107,106,105,103,102,101,99,97,96,94,92,90,88,86,84,
        82,79,77,75,72,70,67,65,62,59,57,54,51,48,45,42,39,36,33,30,27,24,21,18,
        15,12,9,6,3,0,-3,-6,-9,-12,-15,-18,-21,-24,-27,-30,-33,-36,-39,-42,-45,-48,
        -51,-54,-57,-59,-62,-65,-67,-70,-72,-75,-77,-79,-82,-84,-86,-88,-90,-92,-94,-96,-97,-99,-101,-102,
        -103,-105,-106,-107,-108,-109,-110,-111,-112,-113,-113,-114,-114,-115,-115,-115,-115,-115,-115,-115,-115,-114,-114,-113,
        -113,-112,-111,-110,-109,-108,-107,-106,-105,-103,-102,-101,-99,-97,-96,-94,-92,-90,-88,-86,-84,-82,-79,-77,
        -75,-72,-70,-67,-65,-62,-59,-57,-54,-51,-48,-45,-42,-39,-36,-33,-30,-27,-24,-21,-18,-15,-12,-9,
        -6,-3
    };
    x = x & 0xFF;
    return sintab[x];
}

void plasma_run(void) {
    int cr, cc; save_cursor_pos(&cr, &cc);
    static const char palette[] = " .:-=+*#%@";
    static const uint8_t colors[] = {0x01,0x09,0x0B,0x03,0x02,0x0A,0x0E,0x0C,0x0D,0x0F};
    int t = 0;
    vga_clear_all();
    while (1) {
        keyboard_event_t ev;
        keyboard_poll();
        if (keyboard_get_event(&ev)) {
            if (ev.flags & KEY_PRESSED) {
                if (ev.ascii == 27 || ev.ascii == 'q' || ev.ascii == 'Q') {
                    vga_clear_all(); restore_cursor_pos(cr, cc); return;
                }
            }
        }
        for (int y = 0; y < 25; y++) {
            for (int x = 0; x < 80; x++) {
                int v1 = plasma_sin((x * 2 + t) & 0xFF);
                int v2 = plasma_sin((y * 3 + t * 2) & 0xFF);
                int v3 = plasma_sin(((x + y) * 2 + t * 3) & 0xFF);
                int v4 = plasma_sin(((x - y + 200) * 2 + t) & 0xFF);
                int val = (v1 + v2 + v3 + v4 + 460) / 10;
                if (val < 0) val = 0;
                if (val > 9) val = 9;
                vga_putc(y, x, palette[val], colors[val]);
            }
        }
        t += 2;
        {
            uint32_t ds = timer_get_ticks();
            while ((timer_get_ticks() - ds) < 1) { asm volatile("pause"); }
        }
    }
}
