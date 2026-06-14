#include "user_syscall.h"
#include "string.h"

#define GRID_SIZE 20
#define GRID_W 30
#define GRID_H 20
#define SCREEN_W 1024
#define SCREEN_H 768

#define WIN_X 100
#define WIN_Y 60
#define WIN_W (GRID_W * GRID_SIZE + 4)
#define WIN_H (GRID_H * GRID_SIZE + 44)

#define COLOR_BG       0x1A1A2E
#define COLOR_GRID     0x16213E
#define COLOR_SNAKE    0x00CC00
#define COLOR_SNAKE_HEAD 0x00FF00
#define COLOR_FOOD     0xFF0000
#define COLOR_BORDER   0x808080
#define COLOR_TITLEBAR 0x000080
#define COLOR_WHITE    0xFFFFFF
#define COLOR_BLACK    0x000000
#define COLOR_SCORE    0xFFFF00

#define CHAR_W 8
#define CHAR_H 16

#define FB_IOCTL_GET_PTR 0x01
#define KBD_IOCTL_READ   0x20

#define DIR_UP    0
#define DIR_DOWN  1
#define DIR_LEFT  2
#define DIR_RIGHT 3

#define MAX_SNAKE_LEN (GRID_W * GRID_H)

typedef struct {
    int x;
    int y;
} pos_t;

static unsigned int *fb;
static int fb_fd;
static int kbd_fd;

static pos_t snake[MAX_SNAKE_LEN];
static int snake_len = 3;
static int direction = DIR_RIGHT;
static int next_direction = DIR_RIGHT;
static pos_t food;
static int score = 0;
static int game_over = 0;
static int game_speed = 15;

static void fb_draw_rect(int x, int y, int w, int h, unsigned int color)
{
    int i, j;
    for (j = y; j < y + h; j++) {
        for (i = x; i < x + w; i++) {
            if (i >= 0 && i < SCREEN_W && j >= 0 && j < SCREEN_H)
                fb[j * SCREEN_W + i] = color;
        }
    }
}

static void fb_draw_char(int x, int y, char c, unsigned int fg, unsigned int bg)
{
    static const unsigned char font8x16[128][16] = {{0}};
    int i, j;
    if ((unsigned char)c > 127) return;
    for (j = 0; j < 16; j++) {
        unsigned char row = font8x16[(unsigned char)c][j];
        for (i = 0; i < 8; i++) {
            int px = x + i;
            int py = y + j;
            if (px >= 0 && px < SCREEN_W && py >= 0 && py < SCREEN_H)
                fb[py * SCREEN_W + px] = (row & (0x80 >> i)) ? fg : bg;
        }
    }
}

static void fb_draw_string(int x, int y, const char *s, unsigned int fg, unsigned int bg)
{
    while (*s) {
        fb_draw_char(x, y, *s, fg, bg);
        x += CHAR_W;
        s++;
    }
}

static void spawn_food(void)
{
    int valid;
    int attempts = 0;
    do {
        valid = 1;
        food.x = sys_getpid() % GRID_W;
        food.y = (sys_getpid() + score + attempts) % GRID_H;
        {
            int ticks;
            asm volatile("int $0x80" : "=a"(ticks) : "a"(0x30) : "memory");
            food.x = (ticks + attempts * 7) % GRID_W;
            food.y = ((ticks >> 8) + attempts * 13) % GRID_H;
        }
        {
            int i;
            for (i = 0; i < snake_len; i++) {
                if (snake[i].x == food.x && snake[i].y == food.y) {
                    valid = 0;
                    break;
                }
            }
        }
        attempts++;
    } while (!valid && attempts < 1000);
}

static void init_game(void)
{
    int i;
    snake_len = 3;
    direction = DIR_RIGHT;
    next_direction = DIR_RIGHT;
    score = 0;
    game_over = 0;
    game_speed = 15;

    for (i = 0; i < snake_len; i++) {
        snake[i].x = snake_len - 1 - i;
        snake[i].y = GRID_H / 2;
    }

    spawn_food();
}

static void update_game(void)
{
    int i;
    pos_t new_head;

    if (game_over) return;

    direction = next_direction;

    new_head = snake[0];
    if (direction == DIR_UP) new_head.y--;
    else if (direction == DIR_DOWN) new_head.y++;
    else if (direction == DIR_LEFT) new_head.x--;
    else if (direction == DIR_RIGHT) new_head.x++;

    if (new_head.x < 0 || new_head.x >= GRID_W ||
        new_head.y < 0 || new_head.y >= GRID_H) {
        game_over = 1;
        return;
    }

    for (i = 0; i < snake_len; i++) {
        if (snake[i].x == new_head.x && snake[i].y == new_head.y) {
            game_over = 1;
            return;
        }
    }

    if (new_head.x == food.x && new_head.y == food.y) {
        for (i = snake_len; i > 0; i--) {
            snake[i] = snake[i - 1];
        }
        snake[0] = new_head;
        snake_len++;
        score += 10;
        if (game_speed > 5) game_speed--;
        spawn_food();
    } else {
        for (i = snake_len - 1; i > 0; i--) {
            snake[i] = snake[i - 1];
        }
        snake[0] = new_head;
    }
}

static void draw_game(void)
{
    int i, j;
    char score_buf[16];

    fb_draw_rect(WIN_X, WIN_Y, WIN_W, WIN_H, COLOR_BORDER);
    fb_draw_rect(WIN_X + 2, WIN_Y + 2, WIN_W - 4, 20, COLOR_TITLEBAR);
    fb_draw_string(WIN_X + 8, WIN_Y + 4, "Snake", COLOR_WHITE, COLOR_TITLEBAR);

    fb_draw_rect(WIN_X + 2, WIN_Y + 22, GRID_W * GRID_SIZE, GRID_H * GRID_SIZE, COLOR_BG);

    for (j = 0; j < GRID_H; j++) {
        for (i = 0; i < GRID_W; i++) {
            int gx = WIN_X + 2 + i * GRID_SIZE;
            int gy = WIN_Y + 22 + j * GRID_SIZE;
            fb_draw_rect(gx, gy, GRID_SIZE, GRID_SIZE, COLOR_GRID);
        }
    }

    for (i = 0; i < snake_len; i++) {
        int gx = WIN_X + 2 + snake[i].x * GRID_SIZE;
        int gy = WIN_Y + 22 + snake[i].y * GRID_SIZE;
        unsigned int color = (i == 0) ? COLOR_SNAKE_HEAD : COLOR_SNAKE;
        fb_draw_rect(gx + 1, gy + 1, GRID_SIZE - 2, GRID_SIZE - 2, color);
    }

    {
        int gx = WIN_X + 2 + food.x * GRID_SIZE;
        int gy = WIN_Y + 22 + food.y * GRID_SIZE;
        fb_draw_rect(gx + 2, gy + 2, GRID_SIZE - 4, GRID_SIZE - 4, COLOR_FOOD);
    }

    {
        int s = score;
        int idx = 0;
        score_buf[idx++] = 'S';
        score_buf[idx++] = 'c';
        score_buf[idx++] = 'o';
        score_buf[idx++] = 'r';
        score_buf[idx++] = 'e';
        score_buf[idx++] = ':';
        score_buf[idx++] = ' ';
        if (s == 0) {
            score_buf[idx++] = '0';
        } else {
            char tmp[8];
            int tlen = 0;
            while (s > 0) {
                tmp[tlen++] = '0' + (s % 10);
                s /= 10;
            }
            while (tlen > 0) {
                score_buf[idx++] = tmp[--tlen];
            }
        }
        score_buf[idx] = '\0';
        fb_draw_string(WIN_X + WIN_W - idx * CHAR_W - 8, WIN_Y + 4, score_buf, COLOR_SCORE, COLOR_TITLEBAR);
    }

    if (game_over) {
        int cx = WIN_X + WIN_W / 2 - 60;
        int cy = WIN_Y + WIN_H / 2 - 20;
        fb_draw_rect(cx, cy, 120, 40, 0x400000);
        fb_draw_string(cx + 10, cy + 4, "GAME OVER", COLOR_FOOD, 0x400000);
        fb_draw_string(cx + 10, cy + 22, "R:Restart", COLOR_WHITE, 0x400000);
    }
}

static void handle_keyboard(void)
{
    unsigned char key;
    int n;
    if (kbd_fd < 0) return;
    n = sys_read(kbd_fd, &key, 1);
    if (n <= 0) return;

    if (game_over) {
        if (key == 0x13) {
            init_game();
        }
        return;
    }

    if (key == 0x48 && direction != DIR_DOWN) {
        next_direction = DIR_UP;
    } else if (key == 0x50 && direction != DIR_UP) {
        next_direction = DIR_DOWN;
    } else if (key == 0x4B && direction != DIR_RIGHT) {
        next_direction = DIR_LEFT;
    } else if (key == 0x4D && direction != DIR_LEFT) {
        next_direction = DIR_RIGHT;
    }
}

static void init_devices(void)
{
    fb_fd = sys_open("/dev/fb0", O_RDWR);
    if (fb_fd >= 0) {
        sys_ioctl(fb_fd, FB_IOCTL_GET_PTR, &fb);
    }
    kbd_fd = sys_open("/dev/kbd0", O_RDONLY);
}

int main(void)
{
    init_devices();
    if (!fb) {
        sys_exit(1);
    }

    init_game();

    while (1) {
        handle_keyboard();
        update_game();
        draw_game();
        sys_sleep(0);
        {
            int i;
            for (i = 0; i < game_speed * 10000; i++) {
                __asm__ volatile("" ::: "memory");
            }
        }
    }

    return 0;
}
