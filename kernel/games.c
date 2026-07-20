#include "games.h"
#include "keyboard.h"
#include "string.h"
#include "stdio.h"
#include "stdlib.h"
#include "timer.h"
#include "vga_text.h"

static uint16_t * const vga_buf = (uint16_t *)0xB8000;

static inline void vga_putc(int row, int col, char c, uint8_t color) {
    if (row < 0 || row >= 25 || col < 0 || col >= 80) return;
    vga_buf[row * 80 + col] = (uint16_t)((color << 8) | (uint8_t)c);
}

static inline void vga_clear(void) {
    for (int i = 0; i < 80 * 25; i++) vga_buf[i] = 0x0720;
}

static inline void vga_puts(int row, int col, const char *s, uint8_t color) {
    while (*s && col < 80) { vga_putc(row, col++, *s++, color); }
}

static void vga_fill(int row, int col, int w, int h, char c, uint8_t color) {
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            vga_putc(row + y, col + x, c, color);
}

static void save_cursor(int *r, int *c) { vga_text_get_cursor(r, c); }
static void restore_cursor(int r, int c) { vga_text_set_cursor(r, c); vga_text_set_color(15, 0); }

static void delay_ticks(uint32_t t) {
    uint32_t start = timer_get_ticks();
    while ((timer_get_ticks() - start) < t) {
        asm volatile("pause");
    }
}

/* Short busy-wait for responsive keyboard polling - much shorter than 1 tick
 * (10ms) so games feel responsive. ~50000 iterations ≈ <1ms on typical CPU. */
static void delay_short(void) {
    for (volatile int i = 0; i < 50000; i++) { asm volatile("pause"); }
}

static int wait_key(keyboard_event_t *ev, int timeout_ticks) {
    uint32_t start = timer_get_ticks();
    while (1) {
        keyboard_poll();
        if (keyboard_get_event(ev)) {
            if (ev->flags & KEY_PRESSED) return 1;
        }
        if (timeout_ticks > 0 && (timer_get_ticks() - start) >= (uint32_t)timeout_ticks) return 0;
        if (timeout_ticks == 0) return 0;
        delay_short();
    }
}

/* Drain any pending keyboard events to prevent stale input from previous
 * commands interfering with game input. Called at game start. */
static void drain_keyboard(void) {
    keyboard_event_t ev;
    while (1) {
        keyboard_poll();
        if (!keyboard_get_event(&ev)) break;
    }
}

/* ========== SNAKE GAME ========== */
#define SNAKE_MAX_LEN 200
#define SNAKE_W 60
#define SNAKE_H 20
#define SNAKE_X 10
#define SNAKE_Y 2

typedef struct { int x, y; } snake_point_t;
typedef enum { SDIR_UP, SDIR_DOWN, SDIR_LEFT, SDIR_RIGHT } snake_dir_t;

static snake_point_t snake_arr[SNAKE_MAX_LEN];
static int snake_len;
static snake_dir_t snake_dir, snake_next_dir;
static snake_point_t snake_food;
static int snake_score, snake_gameover, snake_paused, snake_speed;
static uint32_t snake_lasttick;

static int snake_is_at(int x, int y) {
    for (int i = 0; i < snake_len; i++)
        if (snake_arr[i].x == x && snake_arr[i].y == y) return 1;
    return 0;
}

static void snake_place_food(void) {
    for (int a = 0; a < 1000; a++) {
        snake_food.x = (timer_get_ticks() * 7 + a * 13) % (SNAKE_W - 2) + 1;
        snake_food.y = (timer_get_ticks() * 11 + a * 17) % (SNAKE_H - 2) + 1;
        if (!snake_is_at(snake_food.x, snake_food.y)) return;
    }
    snake_food.x = 1; snake_food.y = 1;
}

static void snake_init(void) {
    snake_len = 3; snake_dir = SDIR_RIGHT; snake_next_dir = SDIR_RIGHT;
    snake_score = 0; snake_gameover = 0; snake_paused = 0; snake_speed = 10;
    for (int i = 0; i < snake_len; i++) {
        snake_arr[i].x = SNAKE_W / 2 - i;
        snake_arr[i].y = SNAKE_H / 2;
    }
    snake_place_food();
    snake_lasttick = timer_get_ticks();
}

static void snake_draw(void) {
    vga_fill(SNAKE_Y, SNAKE_X, SNAKE_W, SNAKE_H, ' ', 0x00);
    for (int x = 0; x < SNAKE_W; x++) {
        vga_putc(SNAKE_Y, SNAKE_X + x, '#', 0x0F);
        vga_putc(SNAKE_Y + SNAKE_H - 1, SNAKE_X + x, '#', 0x0F);
    }
    for (int y = 1; y < SNAKE_H - 1; y++) {
        vga_putc(SNAKE_Y + y, SNAKE_X, '#', 0x0F);
        vga_putc(SNAKE_Y + y, SNAKE_X + SNAKE_W - 1, '#', 0x0F);
    }
    for (int i = 0; i < snake_len; i++)
        vga_putc(SNAKE_Y + snake_arr[i].y, SNAKE_X + snake_arr[i].x,
                 i == 0 ? '@' : 'O', i == 0 ? 0x02 : 0x0A);
    vga_putc(SNAKE_Y + snake_food.y, SNAKE_X + snake_food.x, '*', 0x0C);
    char buf[40];
    snprintf(buf, sizeof(buf), " Score: %d ", snake_score);
    vga_puts(SNAKE_Y - 1, SNAKE_X, buf, 0x0E);
    vga_puts(SNAKE_Y - 1, SNAKE_X + 20, "Arrows:Move  P:Pause  R:Restart  ESC:Quit", 0x0F);
    if (snake_paused) vga_puts(SNAKE_Y + SNAKE_H/2, SNAKE_X + SNAKE_W/2 - 4, "PAUSED", 0x4F);
    if (snake_gameover) {
        vga_fill(SNAKE_Y + SNAKE_H/2 - 1, SNAKE_X + SNAKE_W/2 - 13, 26, 3, ' ', 0x40);
        vga_puts(SNAKE_Y + SNAKE_H/2 - 1, SNAKE_X + SNAKE_W/2 - 10, "      GAME OVER!      ", 0x4F);
        snprintf(buf, sizeof(buf), " Score: %d  Press R to restart ", snake_score);
        vga_puts(SNAKE_Y + SNAKE_H/2 + 1, SNAKE_X + SNAKE_W/2 - 16, buf, 0x4F);
    }
}

static void snake_update(void) {
    if (snake_gameover || snake_paused) return;
    snake_dir = snake_next_dir;
    snake_point_t nh = snake_arr[0];
    switch (snake_dir) {
        case SDIR_UP: nh.y--; break; case SDIR_DOWN: nh.y++; break;
        case SDIR_LEFT: nh.x--; break; case SDIR_RIGHT: nh.x++; break;
    }
    if (nh.x <= 0 || nh.x >= SNAKE_W - 1 || nh.y <= 0 || nh.y >= SNAKE_H - 1 || snake_is_at(nh.x, nh.y)) {
        snake_gameover = 1; return;
    }
    for (int i = snake_len - 1; i > 0; i--) snake_arr[i] = snake_arr[i - 1];
    snake_arr[0] = nh;
    if (nh.x == snake_food.x && nh.y == snake_food.y) {
        if (snake_len < SNAKE_MAX_LEN) { snake_arr[snake_len] = snake_arr[snake_len - 1]; snake_len++; }
        snake_score += 10;
        if (snake_speed > 3) snake_speed--;
        snake_place_food();
    }
}

void snake_game_run(void) {
    int cr, cc; save_cursor(&cr, &cc);
    drain_keyboard();
    vga_clear(); snake_init(); snake_draw();
    while (1) {
        keyboard_event_t ev;
        keyboard_poll();
        while (keyboard_get_event(&ev)) {
            if (!(ev.flags & KEY_PRESSED)) continue;
            if (ev.ascii == 27) { vga_clear(); restore_cursor(cr, cc); return; }
            if (ev.ascii == 'p' || ev.ascii == 'P') { snake_paused = !snake_paused; snake_draw(); }
            if ((ev.ascii == 'r' || ev.ascii == 'R')) { snake_init(); snake_draw(); }
            if (ev.flags & KEY_EXTENDED) {
                switch (ev.scancode) {
                    case 0x48: if (snake_dir != SDIR_DOWN) snake_next_dir = SDIR_UP; break;
                    case 0x50: if (snake_dir != SDIR_UP) snake_next_dir = SDIR_DOWN; break;
                    case 0x4B: if (snake_dir != SDIR_RIGHT) snake_next_dir = SDIR_LEFT; break;
                    case 0x4D: if (snake_dir != SDIR_LEFT) snake_next_dir = SDIR_RIGHT; break;
                }
            }
        }
        uint32_t now = timer_get_ticks();
        if (!snake_paused && !snake_gameover && (uint32_t)(now - snake_lasttick) >= (uint32_t)snake_speed) {
            snake_update(); snake_lasttick = now; snake_draw();
        }
        delay_short();
    }
}

/* ========== 2048 GAME ========== */
#define G2048_SIZE 4
static int g2048_board[G2048_SIZE][G2048_SIZE];
static int g2048_score, g2048_won, g2048_over;

static void g2048_add_tile(void) {
    int empty[G2048_SIZE * G2048_SIZE], cnt = 0;
    for (int y = 0; y < G2048_SIZE; y++)
        for (int x = 0; x < G2048_SIZE; x++)
            if (g2048_board[y][x] == 0) empty[cnt++] = y * G2048_SIZE + x;
    if (cnt == 0) return;
    int pos = empty[(timer_get_ticks() % cnt)];
    g2048_board[pos / G2048_SIZE][pos % G2048_SIZE] = (timer_get_ticks() % 10 == 0) ? 4 : 2;
}

static void g2048_init(void) {
    memset(g2048_board, 0, sizeof(g2048_board));
    g2048_score = 0; g2048_won = 0; g2048_over = 0;
    g2048_add_tile(); g2048_add_tile();
}

static int g2048_move_left(void) {
    int moved = 0;
    for (int y = 0; y < G2048_SIZE; y++) {
        int pos = 0, last = 0;
        for (int x = 0; x < G2048_SIZE; x++) {
            if (g2048_board[y][x] == 0) continue;
            if (last == g2048_board[y][x] && last != 0) {
                g2048_board[y][pos - 1] *= 2;
                g2048_score += g2048_board[y][pos - 1];
                if (g2048_board[y][pos - 1] == 2048) g2048_won = 1;
                last = 0; moved = 1;
            } else {
                if (last != 0 || pos != x) moved = 1;
                g2048_board[y][pos++] = g2048_board[y][x];
                last = g2048_board[y][x];
            }
        }
        for (int x = pos; x < G2048_SIZE; x++) g2048_board[y][x] = 0;
    }
    return moved;
}

static void g2048_rotate(int dir) {
    int tmp[G2048_SIZE][G2048_SIZE];
    memcpy(tmp, g2048_board, sizeof(tmp));
    for (int y = 0; y < G2048_SIZE; y++)
        for (int x = 0; x < G2048_SIZE; x++) {
            if (dir == 1) g2048_board[x][G2048_SIZE - 1 - y] = tmp[y][x];
            else g2048_board[G2048_SIZE - 1 - x][y] = tmp[y][x];
        }
}

static int g2048_can_move(void) {
    for (int y = 0; y < G2048_SIZE; y++)
        for (int x = 0; x < G2048_SIZE; x++) {
            if (g2048_board[y][x] == 0) return 1;
            if (x < G2048_SIZE - 1 && g2048_board[y][x] == g2048_board[y][x + 1]) return 1;
            if (y < G2048_SIZE - 1 && g2048_board[y][x] == g2048_board[y + 1][x]) return 1;
        }
    return 0;
}

static uint8_t g2048_color(int v) {
    switch (v) {
        case 0: return 0x08; case 2: return 0x07; case 4: return 0x0F;
        case 8: return 0x06; case 16: return 0x0C; case 32: return 0x04;
        case 64: return 0x04; case 128: return 0x0E; case 256: return 0x0E;
        case 512: return 0x0B; case 1024: return 0x0B; case 2048: return 0x0A;
        default: return 0x0D;
    }
}

static void g2048_draw(void) {
    vga_clear();
    vga_puts(1, 2, "===== 2048 =====", 0x0E);
    char buf[40];
    snprintf(buf, sizeof(buf), "Score: %d", g2048_score);
    vga_puts(1, 30, buf, 0x0F);
    vga_puts(3, 2, "Arrows:Move  R:Restart  ESC:Quit", 0x07);
    if (g2048_won) vga_puts(4, 25, "YOU WIN!", 0x0A);
    if (g2048_over) vga_puts(4, 25, "GAME OVER!", 0x0C);
    for (int y = 0; y < G2048_SIZE; y++) {
        int ry = 7 + y * 3;
        for (int x = 0; x < G2048_SIZE; x++) {
            int rx = 5 + x * 12;
            vga_fill(ry, rx, 11, 3, ' ', 0x08);
            if (g2048_board[y][x] != 0) {
                char vb[8];
                snprintf(vb, sizeof(vb), "%d", g2048_board[y][x]);
                int len = strlen(vb);
                for (int i = 0; i < len; i++)
                    vga_putc(ry + 1, rx + 5 - len/2 + i, vb[i], g2048_color(g2048_board[y][x]));
            }
        }
    }
}

void game_2048_run(void) {
    int cr, cc; save_cursor(&cr, &cc);
    drain_keyboard();
    vga_clear(); g2048_init(); g2048_draw();
    while (1) {
        keyboard_event_t ev;
        if (wait_key(&ev, 10)) {
            if (ev.ascii == 27) { vga_clear(); restore_cursor(cr, cc); return; }
            if (ev.ascii == 'r' || ev.ascii == 'R') { g2048_init(); g2048_draw(); continue; }
            int moved = 0;
            if (ev.flags & KEY_EXTENDED) {
                switch (ev.scancode) {
                    case 0x4B: moved = g2048_move_left(); break;
                    case 0x4D: g2048_rotate(1); g2048_rotate(1); moved = g2048_move_left(); g2048_rotate(1); g2048_rotate(1); break;
                    case 0x48: g2048_rotate(0); moved = g2048_move_left(); g2048_rotate(1); break;
                    case 0x50: g2048_rotate(1); moved = g2048_move_left(); g2048_rotate(0); break;
                }
                if (moved) { g2048_add_tile(); if (!g2048_can_move()) g2048_over = 1; g2048_draw(); }
            }
        }
    }
}

/* ========== TETRIS ========== */
#define TET_W 10
#define TET_H 18
#define TET_X 30
#define TET_Y 2

static const int tet_shapes[7][4][4] = {
    {{1,1,1,1},{0,0,0,0},{0,0,0,0},{0,0,0,0}},
    {{1,1,0,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}},
    {{0,1,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
    {{1,0,0,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
    {{0,0,1,0},{1,1,1,0},{0,0,0,0},{0,0,0,0}},
    {{0,1,1,0},{1,1,0,0},{0,0,0,0},{0,0,0,0}},
    {{1,1,0,0},{0,1,1,0},{0,0,0,0},{0,0,0,0}}
};
static const uint8_t tet_colors[7] = { 0x0B, 0x0E, 0x0D, 0x0C, 0x09, 0x0A, 0x06 };

static int tet_board[TET_H][TET_W];
static int tet_cur[4][4], tet_cx, tet_cy, tet_cshape;
static int tet_score, tet_lines, tet_level, tet_tetover, tet_paused;
static uint32_t tet_lastdrop;

static void tet_new_piece(void) {
    tet_cshape = timer_get_ticks() % 7;
    memcpy(tet_cur, tet_shapes[tet_cshape], sizeof(tet_cur));
    tet_cx = TET_W / 2 - 2; tet_cy = 0;
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            if (tet_cur[y][x] && tet_cy + y < TET_H && tet_cx + x >= 0 && tet_cx + x < TET_W)
                if (tet_board[tet_cy + y][tet_cx + x]) tet_tetover = 1;
}

static void tet_rotate_piece(void) {
    int tmp[4][4];
    memcpy(tmp, tet_cur, sizeof(tmp));
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            tet_cur[x][3 - y] = tmp[y][x];
}

static int tet_collides(int nx, int ny, int piece[4][4]) {
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++) {
            if (!piece[y][x]) continue;
            int bx = nx + x, by = ny + y;
            if (bx < 0 || bx >= TET_W || by >= TET_H) return 1;
            if (by >= 0 && tet_board[by][bx]) return 1;
        }
    return 0;
}

static void tet_lock(void) {
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            if (tet_cur[y][x] && tet_cy + y >= 0)
                tet_board[tet_cy + y][tet_cx + x] = tet_cshape + 1;
    int cleared = 0;
    for (int y = TET_H - 1; y >= 0; y--) {
        int full = 1;
        for (int x = 0; x < TET_W; x++) if (!tet_board[y][x]) { full = 0; break; }
        if (full) {
            cleared++;
            for (int ny = y; ny > 0; ny--)
                for (int x = 0; x < TET_W; x++) tet_board[ny][x] = tet_board[ny - 1][x];
            for (int x = 0; x < TET_W; x++) tet_board[0][x] = 0;
            y++;
        }
    }
    if (cleared) {
        int pts[] = {0, 100, 300, 500, 800};
        tet_score += pts[cleared] * tet_level;
        tet_lines += cleared;
        tet_level = 1 + tet_lines / 10;
    }
    tet_new_piece();
}

static void tet_init(void) {
    memset(tet_board, 0, sizeof(tet_board));
    tet_score = 0; tet_lines = 0; tet_level = 1; tet_tetover = 0; tet_paused = 0;
    tet_lastdrop = timer_get_ticks();
    tet_new_piece();
}

static void tet_draw(void) {
    vga_fill(TET_Y, TET_X - 2, TET_W * 2 + 4, TET_H + 2, ' ', 0x00);
    vga_puts(TET_Y - 1, TET_X, "TETRIS", 0x0E);
    char buf[40];
    snprintf(buf, sizeof(buf), "Score: %d", tet_score); vga_puts(TET_Y + 1, 5, buf, 0x0F);
    snprintf(buf, sizeof(buf), "Lines: %d", tet_lines); vga_puts(TET_Y + 3, 5, buf, 0x0F);
    snprintf(buf, sizeof(buf), "Level: %d", tet_level); vga_puts(TET_Y + 5, 5, buf, 0x0F);
    vga_puts(TET_Y + 7, 5, "Arrows:Move", 0x07);
    vga_puts(TET_Y + 8, 5, "Up:Rotate", 0x07);
    vga_puts(TET_Y + 9, 5, "Space:Drop", 0x07);
    vga_puts(TET_Y + 10, 5, "P:Pause R:Restart", 0x07);
    vga_puts(TET_Y + 11, 5, "ESC:Quit", 0x07);
    for (int y = 0; y < TET_H; y++) {
        vga_putc(TET_Y + y, TET_X - 1, '|', 0x08);
        vga_putc(TET_Y + y, TET_X + TET_W * 2, '|', 0x08);
        for (int x = 0; x < TET_W; x++) {
            char c = ' '; uint8_t col = 0x00;
            if (tet_board[y][x]) { c = 0xDB; col = tet_colors[tet_board[y][x] - 1]; }
            vga_putc(TET_Y + y, TET_X + x * 2, c, col);
            vga_putc(TET_Y + y, TET_X + x * 2 + 1, c, col);
        }
    }
    for (int x = 0; x < TET_W * 2 + 2; x++) vga_putc(TET_Y + TET_H, TET_X - 1 + x, '-', 0x08);
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            if (tet_cur[y][x] && tet_cy + y >= 0) {
                char c = 0xDB; uint8_t col = tet_colors[tet_cshape];
                vga_putc(TET_Y + tet_cy + y, TET_X + (tet_cx + x) * 2, c, col);
                vga_putc(TET_Y + tet_cy + y, TET_X + (tet_cx + x) * 2 + 1, c, col);
            }
    if (tet_paused) vga_puts(TET_Y + TET_H/2, TET_X + TET_W - 3, "PAUSED", 0x4F);
    if (tet_tetover) vga_puts(TET_Y + TET_H/2, TET_X + TET_W - 5, "GAME OVER!", 0x4F);
}

void tetris_run(void) {
    int cr, cc; save_cursor(&cr, &cc);
    drain_keyboard();
    vga_clear(); tet_init(); tet_draw();
    while (1) {
        keyboard_event_t ev;
        keyboard_poll();
        while (keyboard_get_event(&ev)) {
            if (!(ev.flags & KEY_PRESSED)) continue;
            if (ev.ascii == 27) { vga_clear(); restore_cursor(cr, cc); return; }
            if (ev.ascii == 'p' || ev.ascii == 'P') { tet_paused = !tet_paused; tet_draw(); }
            if (ev.ascii == 'r' || ev.ascii == 'R') { tet_init(); tet_draw(); }
            if (tet_tetover || tet_paused) continue;
            if (ev.flags & KEY_EXTENDED) {
                switch (ev.scancode) {
                    case 0x4B: if (!tet_collides(tet_cx - 1, tet_cy, tet_cur)) tet_cx--; break;
                    case 0x4D: if (!tet_collides(tet_cx + 1, tet_cy, tet_cur)) tet_cx++; break;
                    case 0x50: if (!tet_collides(tet_cx, tet_cy + 1, tet_cur)) tet_cy++; else tet_lock(); break;
                    case 0x48: {
                        int tmp[4][4]; memcpy(tmp, tet_cur, sizeof(tmp));
                        tet_rotate_piece();
                        if (tet_collides(tet_cx, tet_cy, tet_cur)) memcpy(tet_cur, tmp, sizeof(tet_cur));
                        break;
                    }
                }
                tet_draw();
            }
            if (ev.ascii == ' ') {
                while (!tet_collides(tet_cx, tet_cy + 1, tet_cur)) tet_cy++;
                tet_lock();
                tet_draw();
            }
        }
        if (!tet_tetover && !tet_paused) {
            uint32_t now = timer_get_ticks();
            int drop_speed = 20 - tet_level * 2;
            if (drop_speed < 4) drop_speed = 4;
            if (now - tet_lastdrop >= (uint32_t)drop_speed) {
                if (!tet_collides(tet_cx, tet_cy + 1, tet_cur)) tet_cy++; else tet_lock();
                tet_lastdrop = now; tet_draw();
            }
        }
        delay_short();
    }
}

/* ========== MINESWEEPER ========== */
#define MS_W 20
#define MS_H 12
#define MS_MINES 35

static int ms_board[MS_H][MS_W];
static int ms_revealed[MS_H][MS_W];
static int ms_flagged[MS_H][MS_W];
static int ms_over, ms_win, ms_first;

static void ms_place_mines(int ex, int ey) {
    int placed = 0;
    while (placed < MS_MINES) {
        int x = timer_get_ticks() % MS_W;
        int y = (timer_get_ticks() * 7) % MS_H;
        if (ms_board[y][x] == -1) continue;
        if (abs(x - ex) <= 1 && abs(y - ey) <= 1) continue;
        ms_board[y][x] = -1; placed++;
    }
    for (int y = 0; y < MS_H; y++)
        for (int x = 0; x < MS_W; x++) {
            if (ms_board[y][x] == -1) continue;
            int cnt = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && nx < MS_W && ny >= 0 && ny < MS_H && ms_board[ny][nx] == -1) cnt++;
                }
            ms_board[y][x] = cnt;
        }
}

static void ms_reveal(int x, int y) {
    if (x < 0 || x >= MS_W || y < 0 || y >= MS_H) return;
    if (ms_revealed[y][x] || ms_flagged[y][x]) return;
    ms_revealed[y][x] = 1;
    if (ms_board[y][x] == -1) { ms_over = 1; return; }
    if (ms_board[y][x] == 0) {
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
                if (dx || dy) ms_reveal(x + dx, y + dy);
    }
}

static void ms_init(void) {
    memset(ms_board, 0, sizeof(ms_board));
    memset(ms_revealed, 0, sizeof(ms_revealed));
    memset(ms_flagged, 0, sizeof(ms_flagged));
    ms_over = 0; ms_win = 0; ms_first = 1;
}

static void ms_check_win(void) {
    int unrev = 0;
    for (int y = 0; y < MS_H; y++)
        for (int x = 0; x < MS_W; x++)
            if (!ms_revealed[y][x] && ms_board[y][x] != -1) unrev++;
    if (unrev == 0) ms_win = 1;
}

static const uint8_t ms_num_colors[] = {0x00,0x09,0x0A,0x0C,0x09,0x0D,0x0B,0x0F,0x08};

static void ms_draw(int cx, int cy) {
    vga_clear();
    vga_puts(1, 2, "MINESWEEPER", 0x0E);
    vga_puts(1, 20, "Arrows:Move  Enter:Reveal  F:Flag  R:Restart  ESC:Quit", 0x07);
    for (int y = 0; y < MS_H; y++) {
        for (int x = 0; x < MS_W; x++) {
            char c = '#'; uint8_t col = 0x07;
            if (ms_revealed[y][x]) {
                if (ms_board[y][x] == -1) { c = '*'; col = 0x4C; }
                else if (ms_board[y][x] > 0) { c = '0' + ms_board[y][x]; col = ms_num_colors[ms_board[y][x]]; }
                else { c = ' '; col = 0x08; }
            } else if (ms_flagged[y][x]) { c = 'F'; col = 0x0C; }
            if (x == cx && y == cy) col |= 0x40;
            vga_putc(y + 3, x + 2, c, col);
        }
    }
    if (ms_over) vga_puts(MS_H + 5, 2, "BOOM! Game Over! Press R to restart", 0x4F);
    if (ms_win) vga_puts(MS_H + 5, 2, "YOU WIN! Press R to restart", 0x2F);
}

void minesweeper_run(void) {
    int cr, cc; save_cursor(&cr, &cc);
    drain_keyboard();
    vga_clear(); ms_init();
    int cx = MS_W / 2, cy = MS_H / 2;
    ms_draw(cx, cy);
    while (1) {
        keyboard_event_t ev;
        if (wait_key(&ev, 10)) {
            if (ev.ascii == 27) { vga_clear(); restore_cursor(cr, cc); return; }
            if (ev.ascii == 'r' || ev.ascii == 'R') { ms_init(); cx = MS_W/2; cy = MS_H/2; ms_draw(cx, cy); continue; }
            if (ms_over || ms_win) continue;
            int moved = 0;
            if (ev.flags & KEY_EXTENDED) {
                switch (ev.scancode) {
                    case 0x48: if (cy > 0) { cy--; moved = 1; } break;
                    case 0x50: if (cy < MS_H - 1) { cy++; moved = 1; } break;
                    case 0x4B: if (cx > 0) { cx--; moved = 1; } break;
                    case 0x4D: if (cx < MS_W - 1) { cx++; moved = 1; } break;
                }
            }
            if (ev.ascii == 'f' || ev.ascii == 'F') {
                if (!ms_revealed[cy][cx]) ms_flagged[cy][cx] = !ms_flagged[cy][cx];
                moved = 1;
            }
            if (ev.ascii == '\r' || ev.ascii == '\n' || ev.ascii == ' ') {
                if (!ms_flagged[cy][cx]) {
                    if (ms_first) { ms_place_mines(cx, cy); ms_first = 0; }
                    ms_reveal(cx, cy); ms_check_win();
                }
                moved = 1;
            }
            if (moved) ms_draw(cx, cy);
        }
    }
}

/* ========== PONG ========== */
#define PONG_W 60
#define PONG_H 20
#define PONG_X 10
#define PONG_Y 2

static int pong_py1, pong_py2, pong_bx, pong_by, pong_bdx, pong_bdy;
static int pong_s1, pong_s2, pong_pause, pong_over, pong_mode;
static uint32_t pong_last;

static void pong_init(int mode) {
    pong_py1 = PONG_H / 2; pong_py2 = PONG_H / 2;
    pong_bx = PONG_W / 2; pong_by = PONG_H / 2;
    pong_bdx = (timer_get_ticks() % 2) ? 1 : -1;
    pong_bdy = (timer_get_ticks() % 3) - 1;
    if (pong_bdy == 0) pong_bdy = 1;
    pong_s1 = 0; pong_s2 = 0; pong_pause = 0; pong_over = 0; pong_mode = mode;
    pong_last = timer_get_ticks();
}

static void pong_reset_ball(void) {
    pong_bx = PONG_W / 2; pong_by = PONG_H / 2;
    pong_bdx = -pong_bdx; pong_bdy = (timer_get_ticks() % 3) - 1;
    if (pong_bdy == 0) pong_bdy = 1;
}

static void pong_update(void) {
    if (pong_pause || pong_over) return;
    pong_bx += pong_bdx; pong_by += pong_bdy;
    if (pong_by <= 1 || pong_by >= PONG_H - 2) { pong_bdy = -pong_bdy; pong_by += pong_bdy; }
    if (pong_bx == 2 && pong_by >= pong_py1 - 1 && pong_by <= pong_py1 + 2) {
        pong_bdx = 1; pong_bdy = ((pong_by - pong_py1) - 1);
        if (pong_bdy == 0) pong_bdy = (timer_get_ticks() % 2) ? 1 : -1;
    }
    if (pong_bx == PONG_W - 3 && pong_by >= pong_py2 - 1 && pong_by <= pong_py2 + 2) {
        pong_bdx = -1; pong_bdy = ((pong_by - pong_py2) - 1);
        if (pong_bdy == 0) pong_bdy = (timer_get_ticks() % 2) ? 1 : -1;
    }
    if (pong_bx <= 0) { pong_s2++; pong_reset_ball(); if (pong_s2 >= 10) pong_over = 1; }
    if (pong_bx >= PONG_W - 1) { pong_s1++; pong_reset_ball(); if (pong_s1 >= 10) pong_over = 1; }
    if (pong_mode) {
        if (pong_py2 + 1 < pong_by && pong_py2 < PONG_H - 4) pong_py2++;
        else if (pong_py2 > pong_by && pong_py2 > 1) pong_py2--;
    }
}

static void pong_draw(void) {
    vga_fill(PONG_Y, PONG_X, PONG_W, PONG_H, ' ', 0x00);
    for (int x = 0; x < PONG_W; x++) {
        vga_putc(PONG_Y, PONG_X + x, '#', 0x0F);
        vga_putc(PONG_Y + PONG_H - 1, PONG_X + x, '#', 0x0F);
    }
    for (int y = 0; y < PONG_H; y++)
        vga_putc(PONG_Y + y, PONG_X + PONG_W / 2, '|', 0x08);
    for (int i = 0; i < 4; i++) {
        vga_putc(PONG_Y + pong_py1 + i, PONG_X + 1, 0xDB, 0x0A);
        vga_putc(PONG_Y + pong_py2 + i, PONG_X + PONG_W - 2, 0xDB, 0x0C);
    }
    vga_putc(PONG_Y + pong_by, PONG_X + pong_bx, 'O', 0x0E);
    char buf[40];
    snprintf(buf, sizeof(buf), " %d ", pong_s1); vga_puts(PONG_Y - 1, PONG_X + PONG_W/4, buf, 0x0A);
    snprintf(buf, sizeof(buf), " %d ", pong_s2); vga_puts(PONG_Y - 1, PONG_X + PONG_W*3/4, buf, 0x0C);
    vga_puts(PONG_Y - 1, PONG_X, "PONG", 0x0E);
    if (pong_mode) vga_puts(PONG_Y - 1, PONG_X + 25, "W/S:Left Paddle  P:Pause", 0x07);
    else vga_puts(PONG_Y - 1, PONG_X + 25, "W/S:P1  K/J:P2  P:Pause", 0x07);
    vga_puts(PONG_Y + PONG_H + 1, PONG_X, "ESC:Quit  R:Restart", 0x07);
    if (pong_pause) vga_puts(PONG_Y + PONG_H/2, PONG_X + PONG_W/2 - 4, "PAUSED", 0x4F);
    if (pong_over) {
        vga_puts(PONG_Y + PONG_H/2, PONG_X + PONG_W/2 - 6, pong_s1 >= 10 ? "PLAYER 1 WINS!" : "PLAYER 2 WINS!", 0x4F);
    }
}

void pong_run(void) {
    int cr, cc; save_cursor(&cr, &cc);
    drain_keyboard();
    vga_clear();
    vga_puts(10, 25, "PONG", 0x0E);
    vga_puts(12, 20, "1 - VS CPU", 0x0F);
    vga_puts(13, 20, "2 - 2 Players", 0x0F);
    vga_puts(15, 20, "Press 1 or 2...", 0x07);
    int mode = 1;
    while (1) {
        keyboard_event_t ev;
        if (wait_key(&ev, 5)) {
            if (ev.ascii == '1') { mode = 1; break; }
            if (ev.ascii == '2') { mode = 0; break; }
            if (ev.ascii == 27) { vga_clear(); restore_cursor(cr, cc); return; }
        }
    }
    vga_clear(); pong_init(mode); pong_draw();
    while (1) {
        keyboard_event_t ev;
        keyboard_poll();
        while (keyboard_get_event(&ev)) {
            if (!(ev.flags & KEY_PRESSED)) continue;
            if (ev.ascii == 27) { vga_clear(); restore_cursor(cr, cc); return; }
            if (ev.ascii == 'p' || ev.ascii == 'P') { pong_pause = !pong_pause; pong_draw(); }
            if (ev.ascii == 'r' || ev.ascii == 'R') { pong_init(mode); pong_draw(); }
            if (pong_pause || pong_over) continue;
            switch (ev.ascii) {
                case 'w': case 'W': if (pong_py1 > 1) pong_py1--; break;
                case 's': case 'S': if (pong_py1 < PONG_H - 5) pong_py1++; break;
                case 'j': case 'J': if (pong_mode == 0 && pong_py2 < PONG_H - 5) pong_py2++; break;
                case 'k': case 'K': if (pong_mode == 0 && pong_py2 > 1) pong_py2--; break;
            }
        }
        uint32_t now = timer_get_ticks();
        if (now - pong_last >= 4) { pong_update(); pong_last = now; pong_draw(); }
        delay_short();
    }
}

/* ========== GAME MENU ========== */
void game_menu_run(void) {
    int cr, cc; save_cursor(&cr, &cc);
    drain_keyboard();
    static const char *items[] = { "Snake", "2048", "Tetris", "Minesweeper", "Pong" };
    static void (*runners[])(void) = { snake_game_run, game_2048_run, tetris_run, minesweeper_run, pong_run };
    int sel = 0, count = 5;
    while (1) {
        vga_clear();
        vga_puts(3, 30, "=== GAMES ===", 0x0E);
        vga_puts(5, 20, "Select a game with Up/Down, Enter to start, ESC to exit", 0x07);
        for (int i = 0; i < count; i++) {
            uint8_t col = (i == sel) ? 0x1F : 0x0F;
            char prefix = (i == sel) ? '>' : ' ';
            char buf[40];
            snprintf(buf, sizeof(buf), "%c %d. %s", prefix, i + 1, items[i]);
            vga_puts(7 + i * 2, 32, buf, col);
        }
        keyboard_event_t ev;
        if (wait_key(&ev, 10)) {
            if (ev.ascii == 27) { vga_clear(); restore_cursor(cr, cc); return; }
            if (ev.flags & KEY_EXTENDED) {
                switch (ev.scancode) {
                    case 0x48: if (sel > 0) sel--; break;
                    case 0x50: if (sel < count - 1) sel++; break;
                }
            }
            if (ev.ascii >= '1' && ev.ascii <= '0' + count) {
                int idx = ev.ascii - '1';
                if (idx >= 0 && idx < count) runners[idx]();
            }
            if (ev.ascii == '\r' || ev.ascii == '\n' || ev.ascii == ' ') runners[sel]();
        }
    }
}
