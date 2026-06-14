#include "user_syscall.h"
#include "string.h"
#include "gui_common.h"
#include "gfx_adapter.h"

#define WIN_W 640
#define WIN_H 500
#define TITLE_H 20
#define PALETTE_H 40
#define CANVAS_Y (TITLE_H + 2)
#define CANVAS_H (WIN_H - TITLE_H - PALETTE_H - 4)

#define COLOR_WIN_BG   0x808080
#define COLOR_BORDER   0x404040
#define COLOR_TITLEBAR 0x000080
#define COLOR_WHITE    0xFFFFFF
#define COLOR_BLACK    0x000000
#define COLOR_CANVAS   0xFFFFFF

#define CANVAS_W 636
#define CANVAS_H_ACTUAL 436

static unsigned int palette_colors[8] = {
    0x000000,
    0xFF0000,
    0x00FF00,
    0x0000FF,
    0xFFFF00,
    0xFF00FF,
    0x00FFFF,
    0xFFFFFF
};

static int fb_fd;
static int mouse_fd;
static int kbd_fd;

static int win_x = 80;
static int win_y = 60;

static unsigned int canvas[CANVAS_W * CANVAS_H_ACTUAL];
static int current_color = 0;
static int brush_size = 2;
static int drawing = 0;
static int last_mx = -1;
static int last_my = -1;

static int mouse_x = 512;
static int mouse_y = 384;
static int mouse_btn = 0;

static void canvas_set_pixel(int cx, int cy, unsigned int color)
{
    if (cx >= 0 && cx < CANVAS_W && cy >= 0 && cy < CANVAS_H_ACTUAL) {
        canvas[cy * CANVAS_W + cx] = color;
    }
}

static void canvas_draw_circle(int cx, int cy, int r, unsigned int color)
{
    int dx, dy;
    for (dy = -r; dy <= r; dy++) {
        for (dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r) {
                canvas_set_pixel(cx + dx, cy + dy, color);
            }
        }
    }
}

static void canvas_draw_line(int x0, int y0, int x1, int y1, unsigned int color)
{
    int dx = x1 - x0;
    int dy = y1 - y0;
    int steps;
    int i;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    steps = dx > dy ? dx : dy;
    if (steps == 0) {
        canvas_draw_circle(x0, y0, brush_size, color);
        return;
    }
    for (i = 0; i <= steps; i++) {
        int x = x0 + (x1 - x0) * i / steps;
        int y = y0 + (y1 - y0) * i / steps;
        canvas_draw_circle(x, y, brush_size, color);
    }
}

static void clear_canvas(void)
{
    int i;
    for (i = 0; i < CANVAS_W * CANVAS_H_ACTUAL; i++) {
        canvas[i] = COLOR_CANVAS;
    }
}

static void draw_window(void)
{
    int i, j;
    fb_draw_rect(win_x, win_y, WIN_W, WIN_H, COLOR_BORDER);
    fb_draw_rect(win_x + 2, win_y + 2, WIN_W - 4, TITLE_H, COLOR_TITLEBAR);
    fb_draw_string(win_x + 8, win_y + 4, "Paint", COLOR_WHITE, COLOR_TITLEBAR);

    int canvas_screen_x = win_x + 2;
    int canvas_screen_y = win_y + CANVAS_Y;

    for (j = 0; j < CANVAS_H_ACTUAL; j++) {
        for (i = 0; i < CANVAS_W; i++) {
            int sx = canvas_screen_x + i;
            int sy = canvas_screen_y + j;
            if (sx >= 0 && sx < SCREEN_WIDTH && sy >= 0 && sy < SCREEN_HEIGHT) {
                gfx_adapter_get_fb()[sy * SCREEN_WIDTH + sx] = canvas[j * CANVAS_W + i];
            }
        }
    }

    int palette_y = win_y + WIN_H - PALETTE_H - 2;
    fb_draw_rect(win_x + 2, palette_y, WIN_W - 4, PALETTE_H, 0x404040);

    for (i = 0; i < 8; i++) {
        int bx = win_x + 8 + i * 36;
        int by = palette_y + 4;
        fb_draw_rect(bx, by, 30, 24, palette_colors[i]);
        if (i == current_color) {
            fb_draw_rect(bx - 1, by - 1, 32, 26, 0xFFFF00);
        } else {
            fb_draw_rect(bx - 1, by - 1, 32, 26, 0x808080);
        }
        fb_draw_rect(bx, by, 30, 24, palette_colors[i]);
    }

    {
        int bx = win_x + 8 + 8 * 36 + 10;
        int by = palette_y + 4;
        fb_draw_rect(bx, by, 50, 24, 0x008000);
        fb_draw_string(bx + 4, by + 4, "Clear", COLOR_WHITE, 0x008000);
    }

    {
        int bx = win_x + 8 + 8 * 36 + 70;
        int by = palette_y + 4;
        char size_label[8];
        size_label[0] = 'S';
        size_label[1] = ':';
        size_label[2] = '0' + brush_size;
        size_label[3] = '\0';
        fb_draw_rect(bx, by, 50, 24, 0x606060);
        fb_draw_string(bx + 4, by + 4, size_label, COLOR_WHITE, 0x606060);
    }
}

static void read_mouse(void)
{
    unsigned char data[4];
    int n;
    if (mouse_fd < 0) return;
    n = sys_read(mouse_fd, data, 4);
    if (n < 3) return;
    mouse_x += (signed char)data[1];
    mouse_y += (signed char)data[2];
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_x >= 1024) mouse_x = 1023;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_y >= 768) mouse_y = 767;
    mouse_btn = data[0] & 0x03;
}

static void handle_mouse(void)
{
    int canvas_screen_x = win_x + 2;
    int canvas_screen_y = win_y + CANVAS_Y;
    int palette_y = win_y + WIN_H - PALETTE_H - 2;

    int cx = mouse_x - canvas_screen_x;
    int cy = mouse_y - canvas_screen_y;

    if (mouse_btn & 0x01) {
        if (cx >= 0 && cx < CANVAS_W && cy >= 0 && cy < CANVAS_H_ACTUAL) {
            if (drawing && last_mx >= 0 && last_my >= 0) {
                canvas_draw_line(last_mx, last_my, cx, cy, palette_colors[current_color]);
            } else {
                canvas_draw_circle(cx, cy, brush_size, palette_colors[current_color]);
            }
            drawing = 1;
            last_mx = cx;
            last_my = cy;
            return;
        }

        {
            int i;
            for (i = 0; i < 8; i++) {
                int bx = win_x + 8 + i * 36;
                int by = palette_y + 4;
                if (mouse_x >= bx && mouse_x < bx + 30 &&
                    mouse_y >= by && mouse_y < by + 24) {
                    current_color = i;
                    return;
                }
            }
        }

        {
            int bx = win_x + 8 + 8 * 36 + 10;
            int by = palette_y + 4;
            if (mouse_x >= bx && mouse_x < bx + 50 &&
                mouse_y >= by && mouse_y < by + 24) {
                clear_canvas();
                return;
            }
        }
    } else {
        drawing = 0;
        last_mx = -1;
        last_my = -1;
    }

    if (mouse_btn & 0x02) {
        if (cx >= 0 && cx < CANVAS_W && cy >= 0 && cy < CANVAS_H_ACTUAL) {
            canvas_draw_circle(cx, cy, brush_size, COLOR_CANVAS);
        }
    }
}

static void handle_keyboard(void)
{
    unsigned char key;
    int n;
    if (kbd_fd < 0) return;
    n = sys_read(kbd_fd, &key, 1);
    if (n <= 0) return;

    if (key == 0x0B) {
        brush_size = 1;
    } else if (key == 0x02) {
        brush_size = 2;
    } else if (key == 0x03) {
        brush_size = 3;
    } else if (key == 0x04) {
        brush_size = 5;
    } else if (key == 0x05) {
        brush_size = 8;
    }
}

static void init_devices(void)
{
    unsigned int *fb;
    fb_fd = sys_open("/dev/fb0", O_RDWR);
    if (fb_fd >= 0) {
        sys_ioctl(fb_fd, FB_IOCTL_GET_PTR, &fb);
    }
    mouse_fd = sys_open("/dev/mouse0", O_RDONLY);
    kbd_fd = sys_open("/dev/kbd0", O_RDONLY);
    gfx_adapter_init(fb, SCREEN_WIDTH, SCREEN_HEIGHT);
}

int main(void)
{
    init_devices();
    if (!gfx_adapter_is_initialized()) {
        sys_exit(1);
    }

    clear_canvas();

    while (1) {
        read_mouse();
        handle_mouse();
        handle_keyboard();
        draw_window();
        sys_yield();
    }

    return 0;
}
