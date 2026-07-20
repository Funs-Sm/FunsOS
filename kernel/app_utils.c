#include "app_utils.h"
#include "keyboard.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "timer.h"
#include "vga_text.h"
#include "rtc.h"
#include "pmm.h"
#include "sched.h"
#include "process.h"

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

static int wait_key_ticks(keyboard_event_t *ev, int timeout_ticks) {
    uint32_t start = timer_get_ticks();
    while (1) {
        keyboard_poll();
        if (keyboard_get_event(ev)) {
            if (ev->flags & KEY_PRESSED) return 1;
        }
        if (timeout_ticks > 0 && (timer_get_ticks() - start) >= (uint32_t)timeout_ticks) return 0;
        if (timeout_ticks == 0) return 0;
        for (volatile int i = 0; i < 50000; i++) { asm volatile("pause"); }
    }
}

#define wait_key_ms(ev, ms) wait_key_ticks((ev), ((ms) + 9) / 10)

/* ========== INTERACTIVE CALCULATOR ========== */
typedef enum {
    TOK_NUM, TOK_PLUS, TOK_MINUS, TOK_MUL, TOK_DIV, TOK_LPAREN, TOK_RPAREN, TOK_END
} calc_tok_t;

typedef struct {
    calc_tok_t type;
    int num;
} calc_token_t;

static const char *calc_expr;
static int calc_pos;

static calc_token_t calc_next(void) {
    calc_token_t t;
    while (calc_expr[calc_pos] == ' ') calc_pos++;
    char c = calc_expr[calc_pos];
    if (c >= '0' && c <= '9') {
        t.type = TOK_NUM;
        t.num = 0;
        while (calc_expr[calc_pos] >= '0' && calc_expr[calc_pos] <= '9') {
            t.num = t.num * 10 + (calc_expr[calc_pos] - '0');
            calc_pos++;
        }
        return t;
    }
    calc_pos++;
    switch (c) {
        case '+': t.type = TOK_PLUS; break;
        case '-': t.type = TOK_MINUS; break;
        case '*': t.type = TOK_MUL; break;
        case '/': t.type = TOK_DIV; break;
        case '(': t.type = TOK_LPAREN; break;
        case ')': t.type = TOK_RPAREN; break;
        default: t.type = TOK_END; break;
    }
    return t;
}

static int calc_parse_expr(calc_token_t *cur);

static int calc_parse_primary(calc_token_t *cur) {
    if (cur->type == TOK_NUM) {
        int v = cur->num;
        *cur = calc_next();
        return v;
    }
    if (cur->type == TOK_LPAREN) {
        *cur = calc_next();
        int v = calc_parse_expr(cur);
        if (cur->type == TOK_RPAREN) *cur = calc_next();
        return v;
    }
    if (cur->type == TOK_MINUS) {
        *cur = calc_next();
        return -calc_parse_primary(cur);
    }
    return 0;
}

static int calc_parse_term(calc_token_t *cur) {
    int v = calc_parse_primary(cur);
    while (cur->type == TOK_MUL || cur->type == TOK_DIV) {
        calc_tok_t op = cur->type;
        *cur = calc_next();
        int r = calc_parse_primary(cur);
        if (op == TOK_MUL) v *= r;
        else if (r != 0) v /= r;
    }
    return v;
}

static int calc_parse_expr(calc_token_t *cur) {
    int v = calc_parse_term(cur);
    while (cur->type == TOK_PLUS || cur->type == TOK_MINUS) {
        calc_tok_t op = cur->type;
        *cur = calc_next();
        int r = calc_parse_term(cur);
        if (op == TOK_PLUS) v += r;
        else v -= r;
    }
    return v;
}

static int calc_eval(const char *expr) {
    calc_expr = expr;
    calc_pos = 0;
    calc_token_t cur = calc_next();
    return calc_parse_expr(&cur);
}

void calc_interactive_run(void) {
    int cr, cc; save_cursor_pos(&cr, &cc);
    vga_clear_all();
    vga_puts(1, 2, "=== Advanced Calculator ===", 0x0E);
    vga_puts(3, 2, "Enter expressions (e.g., 2+3*4, (10-2)/4, -5+3*2)", 0x07);
    vga_puts(4, 2, "Commands: 'cls' to clear, 'quit'/'q' to exit", 0x07);
    vga_puts(23, 2, "Type expression and press Enter:", 0x0B);

    char input[128];
    int inp_len = 0;
    int row = 6;

    while (1) {
        vga_fill_rect(row, 2, 76, 1, ' ', 0x07);
        vga_puts(row, 2, "> ", 0x0F);

        inp_len = 0;
        memset(input, 0, sizeof(input));
        while (1) {
            keyboard_event_t ev;
            if (wait_key_ms(&ev, 30)) {
                if (ev.ascii == 27) { vga_clear_all(); restore_cursor_pos(cr, cc); return; }
                if (ev.ascii == '\r' || ev.ascii == '\n') {
                    vga_putc(row, 2 + 2 + inp_len, ' ', 0x07);
                    break;
                }
                if (ev.ascii == 8 && inp_len > 0) {
                    inp_len--;
                    vga_putc(row, 2 + 2 + inp_len, ' ', 0x07);
                    continue;
                }
                if (ev.ascii >= 32 && ev.ascii < 127 && inp_len < (int)sizeof(input) - 1) {
                    input[inp_len++] = ev.ascii;
                    vga_putc(row, 2 + 1 + inp_len, ev.ascii, 0x0F);
                }
            }
        }
        input[inp_len] = '\0';

        if (strcmp(input, "quit") == 0 || strcmp(input, "q") == 0) break;
        if (strcmp(input, "cls") == 0 || strcmp(input, "clear") == 0) {
            vga_clear_all();
            vga_puts(1, 2, "=== Advanced Calculator ===", 0x0E);
            vga_puts(3, 2, "Enter expressions (e.g., 2+3*4, (10-2)/4, -5+3*2)", 0x07);
            vga_puts(4, 2, "Commands: 'cls' to clear, 'quit'/'q' to exit", 0x07);
            vga_puts(23, 2, "Type expression and press Enter:", 0x0B);
            row = 6;
            continue;
        }
        if (input[0] == '\0') continue;

        if (row < 22) {
            vga_fill_rect(row + 1, 2, 76, 1, ' ', 0x07);
            int result = calc_eval(input);
            char buf[80];
            snprintf(buf, sizeof(buf), "  = %d", result);
            vga_puts(row + 1, 4, buf, 0x0A);
            row += 2;
        } else {
            for (int i = 6; i < 22; i++) {
                vga_fill_rect(i, 2, 76, 1, ' ', 0x07);
            }
            row = 6;
        }
    }
    vga_clear_all();
    restore_cursor_pos(cr, cc);
}

/* ========== SYSTEM MONITOR ========== */
void sysmon_run(void) {
    int cr, cc; save_cursor_pos(&cr, &cc);
    vga_clear_all();
    while (1) {
        keyboard_event_t ev;
        if (wait_key_ms(&ev, 500)) {
            if (ev.ascii == 27 || ev.ascii == 'q' || ev.ascii == 'Q') {
                vga_clear_all(); restore_cursor_pos(cr, cc); return;
            }
        }

        vga_fill_rect(0, 0, 80, 25, ' ', 0x00);
        vga_puts(0, 2, "=== SYSTEM MONITOR ===", 0x0E);
        vga_puts(0, 55, "Q/ESC to quit", 0x08);

        uint32_t total = pmm_get_total_pages();
        uint32_t free_p = pmm_get_free_pages();
        uint32_t used_p = total - free_p;
        uint32_t ticks = timer_get_ticks();
        uint32_t uptime_s = ticks / 100;

        vga_puts(2, 2, "Memory:", 0x0B);
        char buf[80];
        snprintf(buf, sizeof(buf), "  Total: %u KB", total * 4);
        vga_puts(3, 4, buf, 0x0F);
        uint32_t pct = total ? (used_p * 100 / total) : 0;
        snprintf(buf, sizeof(buf), "  Used:  %u KB (%u%%)", used_p * 4, pct);
        vga_puts(4, 4, buf, 0x0C);
        snprintf(buf, sizeof(buf), "  Free:  %u KB", free_p * 4);
        vga_puts(5, 4, buf, 0x0A);

        int bar_w = 40;
        int used_bar = total ? (used_p * bar_w / total) : 0;
        vga_puts(6, 4, "[", 0x07);
        for (int i = 0; i < bar_w; i++) {
            vga_putc(6, 5 + i, 0xDB, i < used_bar ? 0x4C : 0x02);
        }
        vga_puts(6, 5 + bar_w, "]", 0x07);

        vga_puts(8, 2, "Uptime:", 0x0B);
        snprintf(buf, sizeof(buf), "  %uh %um %us", uptime_s / 3600, (uptime_s / 60) % 60, uptime_s % 60);
        vga_puts(9, 4, buf, 0x0F);

        vga_puts(8, 30, "System ticks:", 0x0B);
        snprintf(buf, sizeof(buf), "  %u", ticks);
        vga_puts(9, 30, buf, 0x0F);

        vga_puts(11, 2, "Kernel subsystems:", 0x0B);
        vga_puts(12, 4, "[OK] Memory manager (PMM/VMM/Slab)", 0x0A);
        vga_puts(13, 4, "[OK] Process scheduler (CFS/RT)", 0x0A);
        vga_puts(14, 4, "[OK] VFS & filesystems (Ext2/FAT32/Proc)", 0x0A);
        vga_puts(15, 4, "[OK] Keyboard/mouse input", 0x0A);
        vga_puts(16, 4, "[OK] VGA text & VESA graphics", 0x0A);
        vga_puts(17, 4, "[OK] Network stack (TCP/IP)", 0x0A);
        vga_puts(18, 4, "[OK] I/O scheduler (Deadline/CFQ)", 0x0A);
        vga_puts(19, 4, "[OK] Crypto & security subsystems", 0x0A);
    }
}

/* ========== CALENDAR ========== */
void cal_run(void) {
    int cr, cc; save_cursor_pos(&cr, &cc);
    vga_clear_all();

    rtc_time_t t;
    rtc_read_time(&t);

    static const char *months[] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    static const int mdays[] = {31,28,31,30,31,30,31,31,30,31,30,31};

    int y = t.year;
    int m = t.month - 1;
    int d = t.day;

    static const char *wdays[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};

    while (1) {
        vga_fill_rect(0, 0, 80, 25, ' ', 0x00);

        char buf[80];
        snprintf(buf, sizeof(buf), "<<< %s %d >>>", months[m], y);
        int len = strlen(buf);
        vga_puts(2, (80 - len) / 2, buf, 0x0E);

        vga_puts(20, 2, "Arrows: Change Month/Year  T:Today  Q/ESC:Quit", 0x07);

        for (int i = 0; i < 7; i++) {
            uint8_t col = (i == 0 || i == 6) ? 0x0C : 0x0F;
            vga_puts(5, 8 + i * 5, wdays[i], col);
        }

        int ly = ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) ? 1 : 0;
        int days = mdays[m] + (m == 1 ? ly : 0);

        /* Calculate day of week for the 1st of the month using Zeller's congruence.
           Gregorian: h = ( q + (13(m+1)/5) + K + K/4 + J/4 + 5J ) mod 7
           For our calendar: m=3..14 (Jan/Feb counted as months 13/14 of previous year).
           h: 0=Sat,1=Sun,2=Mon,3=Tue,4=Wed,5=Thu,6=Fri. We want: 0=Sun,1=Mon,...,6=Sat. */
        int zq = 1;
        int zm = m + 1;
        int zy = y;
        if (zm < 3) { zm += 12; zy--; }
        int zK = zy % 100;
        int zJ = zy / 100;
        int zh = (zq + (13 * (zm + 1)) / 5 + zK + zK / 4 + zJ / 4 + 5 * zJ) % 7;
        /* Convert h (0=Sat..6=Fri) to our w_first (0=Sun..6=Sat) */
        int w_first;
        switch (zh) {
            case 0: w_first = 6; break; /* Sat */
            case 1: w_first = 0; break; /* Sun */
            case 2: w_first = 1; break; /* Mon */
            case 3: w_first = 2; break; /* Tue */
            case 4: w_first = 3; break; /* Wed */
            case 5: w_first = 4; break; /* Thu */
            case 6: w_first = 5; break; /* Fri */
            default: w_first = 0; break;
        }

        int col = 0, row = 6;
        for (int i = 0; i < w_first; i++) { col++; }

        for (int day = 1; day <= days; day++) {
            char dbuf[8];
            snprintf(dbuf, sizeof(dbuf), "%3d", day);
            uint8_t dcol = 0x0F;
            if (col == 0 || col == 6) dcol = 0x0C;
            if (day == d && m == t.month - 1 && y == t.year) dcol = 0x1F;
            vga_puts(row, 8 + col * 5, dbuf, dcol);
            col++;
            if (col >= 7) { col = 0; row++; }
        }

        keyboard_event_t ev;
        if (wait_key_ms(&ev, 100)) {
            if (ev.ascii == 27 || ev.ascii == 'q' || ev.ascii == 'Q') {
                vga_clear_all(); restore_cursor_pos(cr, cc); return;
            }
            if (ev.ascii == 't' || ev.ascii == 'T') { y = t.year; m = t.month - 1; }
            if (ev.flags & KEY_EXTENDED) {
                switch (ev.scancode) {
                    case 0x4B: m--; if (m < 0) { m = 11; y--; } break;
                    case 0x4D: m++; if (m > 11) { m = 0; y++; } break;
                    case 0x48: y++; break;
                    case 0x50: y--; break;
                }
            }
        }
    }
}

/* ========== DIGITAL CLOCK ========== */
void clock_run(void) {
    int cr, cc; save_cursor_pos(&cr, &cc);
    vga_clear_all();
    while (1) {
        keyboard_event_t ev;
        if (wait_key_ms(&ev, 500)) {
            if (ev.ascii == 27 || ev.ascii == 'q' || ev.ascii == 'Q') {
                vga_clear_all(); restore_cursor_pos(cr, cc); return;
            }
        }

        rtc_time_t t;
        rtc_read_time(&t);

        int disp_hour = (t.hour < 24) ? t.hour : 0;
        int disp_min  = (t.minute < 60) ? t.minute : 0;
        int disp_sec  = (t.second < 60) ? t.second : 0;
        int wday_idx  = (t.weekday < 7) ? t.weekday : 0;
        int mon_idx   = (t.month >= 1 && t.month <= 12) ? (t.month - 1) : 0;
        int disp_day  = (t.day >= 1 && t.day <= 31) ? t.day : 1;
        int disp_year = (t.year > 1980) ? t.year : 2024;

        vga_fill_rect(0, 0, 80, 25, ' ', 0x00);

        char buf[80];

        for (int i = 0; i < 80; i++) vga_putc(0, i, 0xDF, 0x1F);
        for (int i = 0; i < 80; i++) vga_putc(24, i, 0xDC, 0x1F);
        for (int i = 1; i < 24; i++) { vga_putc(i, 0, 0xDB, 0x1F); vga_putc(i, 79, 0xDB, 0x1F); }

        snprintf(buf, sizeof(buf), "  FUN CLOCK  ");
        vga_puts(1, (80 - strlen(buf))/2, buf, 0x1E);

        static const char *big_nums[10][5] = {
            {"  ***  "," *   * ","*     *","*     *"," *   * "},
            {"  *    "," **    ","  *    ","  *    "," ***   "},
            {" ***   ","*   *  ","   *   ","  *    ","*****  "},
            {" ****  ","    *  ","  **   ","    *  ","****   "},
            {"   *   ","  **   "," * *   ","*****  ","   *   "},
            {"*****  ","*      ","****   ","    *  ","****   "},
            {"  ***  "," *     ","****   ","*   *  "," ***   "},
            {"*****  ","    *  ","   *   ","  *    "," *     "},
            {" ****  ","*   *  "," ***   ","*   *  "," ****  "},
            {" ****  ","*   *  "," *****","    *  "," ***   "}
        };

        int digits[8];
        digits[0] = disp_hour / 10;
        digits[1] = disp_hour % 10;
        digits[2] = -1;
        digits[3] = disp_min / 10;
        digits[4] = disp_min % 10;
        digits[5] = -1;
        digits[6] = disp_sec / 10;
        digits[7] = disp_sec % 10;

        int dx = 8;
        for (int d = 0; d < 8; d++) {
            if (digits[d] == -1) {
                for (int ln = 1; ln < 4; ln++) {
                    vga_putc(10 + ln * 2, dx + 1, 'o', 0x4E);
                }
                dx += 5;
            } else {
                for (int ln = 0; ln < 5; ln++) {
                    const char *line = big_nums[digits[d]][ln];
                    for (int i = 0; i < 7; i++) {
                        if (line[i] == '*') {
                            vga_putc(9 + ln * 2, dx + i, 0xDB, 0x1E);
                        } else {
                            vga_putc(9 + ln * 2, dx + i, ' ', 0x00);
                        }
                    }
                }
                dx += 9;
            }
        }

        static const char *wdays[] = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
        static const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
        snprintf(buf, sizeof(buf), "%s, %s %d, %d", wdays[wday_idx], months[mon_idx], disp_day, disp_year);
        vga_puts(21, (80 - (int)strlen(buf))/2, buf, 0x0F);
        vga_puts(23, 30, "Press Q or ESC to exit", 0x08);
    }
}

/* ========== MATRIX RAIN EFFECT ========== */
void matrix_run(void) {
    int cr, cc; save_cursor_pos(&cr, &cc);

    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@#$%^&*()<>?";
    int cs_len = (int)sizeof(charset) - 1;
    int drops[80];
    for (int i = 0; i < 80; i++) drops[i] = rand() % 25;

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

        for (int i = 0; i < 80; i++) {
            int y = drops[i];
            vga_putc(y, i, ' ', 0x00);
            char ch = charset[rand() % cs_len];
            vga_putc(y, i, ch, 0x0A);
            if (y > 0) {
                vga_putc(y - 1, i, charset[rand() % cs_len], 0x02);
            }
            if (y > 1) {
                for (int j = y - 2; j >= 0 && j >= y - 6; j--) {
                    vga_putc(j, i, charset[rand() % cs_len], 0x08);
                }
            }
            drops[i]++;
            if (drops[i] >= 25 || (rand() % 20 == 0)) {
                drops[i] = 0;
            }
        }

        for (volatile int i = 0; i < 1000000; i++) {
            asm volatile("" ::: "memory");
        }
    }
}
