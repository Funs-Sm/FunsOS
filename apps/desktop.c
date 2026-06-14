#include "user_syscall.h"
#include "string.h"
#include "gui_common.h"
#include "gfx_adapter.h"

#define TASKBAR_H 40
#define ICON_W 64
#define ICON_H 64
#define ICON_MARGIN 20
#define ICON_LABEL_H 16

#define COLOR_BG_TOP    0x001050
#define COLOR_BG_BOT    0x002090
#define COLOR_TASKBAR   0x303030
#define COLOR_STARTBTN  0x008000
#define COLOR_WHITE     0xFFFFFF
#define COLOR_BLACK     0x000000
#define COLOR_CLOCK     0xCCCCCC
#define COLOR_ICON_TERM 0x222222
#define COLOR_ICON_FILE 0xCCAA00
#define COLOR_ICON_NOTE 0x4444FF
#define COLOR_ICON_PAINT 0xFF4444
#define COLOR_ICON_CALC 0x44CC44

typedef struct {
    int x, y;
    int w, h;
    unsigned short color;
    const char *label;
    const char *path;
} desktop_icon_t;

static int fb_fd;
static int mouse_fd;
static int kbd_fd;

static int mouse_x = 512;
static int mouse_y = 384;
static int mouse_btn = 0;

static desktop_icon_t icons[5];

static void draw_background(void)
{
    int y;
    for (y = 0; y < SCREEN_HEIGHT - TASKBAR_H; y++) {
        unsigned int r = ((COLOR_BG_TOP & 0xFF0000) >> 16) * (SCREEN_HEIGHT - TASKBAR_H - y) / (SCREEN_HEIGHT - TASKBAR_H)
                       + ((COLOR_BG_BOT & 0xFF0000) >> 16) * y / (SCREEN_HEIGHT - TASKBAR_H);
        unsigned int g = ((COLOR_BG_TOP & 0x00FF00) >> 8) * (SCREEN_HEIGHT - TASKBAR_H - y) / (SCREEN_HEIGHT - TASKBAR_H)
                       + ((COLOR_BG_BOT & 0x00FF00) >> 8) * y / (SCREEN_HEIGHT - TASKBAR_H);
        unsigned int b = (COLOR_BG_TOP & 0x0000FF) * (SCREEN_HEIGHT - TASKBAR_H - y) / (SCREEN_HEIGHT - TASKBAR_H)
                       + (COLOR_BG_BOT & 0x0000FF) * y / (SCREEN_HEIGHT - TASKBAR_H);
        unsigned int color = (r << 16) | (g << 8) | b;
        fb_draw_rect(0, y, SCREEN_WIDTH, 1, color);
    }
}

static void draw_taskbar(void)
{
    fb_draw_rect(0, SCREEN_HEIGHT - TASKBAR_H, SCREEN_WIDTH, TASKBAR_H, COLOR_TASKBAR);
    fb_draw_rect(4, SCREEN_HEIGHT - TASKBAR_H + 4, 80, 32, COLOR_STARTBTN);
    fb_draw_string_transparent(12, SCREEN_HEIGHT - TASKBAR_H + 12, "Start", COLOR_WHITE);
}

static void draw_clock(void)
{
    int ticks;
    int sec, min, hr;
    char buf[16];
    asm volatile("int $0x80" : "=a"(ticks) : "a"(0x30) : "memory");
    sec = (ticks / 100) % 60;
    min = (ticks / 6000) % 60;
    hr = (ticks / 360000) % 24;
    buf[0] = '0' + hr / 10;
    buf[1] = '0' + hr % 10;
    buf[2] = ':';
    buf[3] = '0' + min / 10;
    buf[4] = '0' + min % 10;
    buf[5] = ':';
    buf[6] = '0' + sec / 10;
    buf[7] = '0' + sec % 10;
    buf[8] = '\0';
    fb_draw_string_transparent(SCREEN_WIDTH - 80, SCREEN_HEIGHT - TASKBAR_H + 12, buf, COLOR_CLOCK);
}

static void init_icons(void)
{
    int col = 0;
    int start_x = ICON_MARGIN;
    int start_y = ICON_MARGIN;

    icons[0].x = start_x;
    icons[0].y = start_y;
    icons[0].w = ICON_W;
    icons[0].h = ICON_H;
    icons[0].color = COLOR_ICON_TERM;
    icons[0].label = "Terminal";
    icons[0].path = "/apps/terminal.elf";

    icons[1].x = start_x;
    icons[1].y = start_y + ICON_H + ICON_LABEL_H + ICON_MARGIN;
    icons[1].w = ICON_W;
    icons[1].h = ICON_H;
    icons[1].color = COLOR_ICON_FILE;
    icons[1].label = "Files";
    icons[1].path = "/apps/filemgr.elf";

    icons[2].x = start_x;
    icons[2].y = start_y + (ICON_H + ICON_LABEL_H + ICON_MARGIN) * 2;
    icons[2].w = ICON_W;
    icons[2].h = ICON_H;
    icons[2].color = COLOR_ICON_NOTE;
    icons[2].label = "Notepad";
    icons[2].path = "/apps/notepad.elf";

    icons[3].x = start_x;
    icons[3].y = start_y + (ICON_H + ICON_LABEL_H + ICON_MARGIN) * 3;
    icons[3].w = ICON_W;
    icons[3].h = ICON_H;
    icons[3].color = COLOR_ICON_PAINT;
    icons[3].label = "Paint";
    icons[3].path = "/apps/paint.elf";

    icons[4].x = start_x;
    icons[4].y = start_y + (ICON_H + ICON_LABEL_H + ICON_MARGIN) * 4;
    icons[4].w = ICON_W;
    icons[4].h = ICON_H;
    icons[4].color = COLOR_ICON_CALC;
    icons[4].label = "Calculator";
    icons[4].path = "/apps/calc.elf";
}

static void draw_icons(void)
{
    int i;
    for (i = 0; i < 5; i++) {
        fb_draw_rect(icons[i].x, icons[i].y, icons[i].w, icons[i].h, icons[i].color);
        fb_draw_rect(icons[i].x, icons[i].y, icons[i].w, icons[i].h, COLOR_WHITE);
        fb_draw_string_transparent(icons[i].x, icons[i].y + icons[i].h + 2, icons[i].label, COLOR_WHITE);
    }
}

static void draw_cursor(void)
{
    fb_draw_rect(mouse_x, mouse_y, 8, 8, COLOR_WHITE);
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
    if (mouse_x >= SCREEN_WIDTH) mouse_x = SCREEN_WIDTH - 1;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_y >= SCREEN_HEIGHT) mouse_y = SCREEN_HEIGHT - 1;
    mouse_btn = data[0] & 0x03;
}

static void handle_click(void)
{
    int i;
    if (!(mouse_btn & 0x01)) return;
    for (i = 0; i < 5; i++) {
        if (mouse_x >= icons[i].x && mouse_x < icons[i].x + icons[i].w &&
            mouse_y >= icons[i].y && mouse_y < icons[i].y + icons[i].h) {
            int pid = sys_fork();
            if (pid == 0) {
                char *argv[] = { (char *)icons[i].path, (void *)0 };
                sys_exec(icons[i].path, argv);
                sys_exit(1);
            }
            return;
        }
    }
    if (mouse_x >= 4 && mouse_x < 84 &&
        mouse_y >= SCREEN_HEIGHT - TASKBAR_H + 4 && mouse_y < SCREEN_HEIGHT - 4) {
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

    init_icons();
    draw_background();
    draw_taskbar();
    draw_icons();
    draw_clock();
    draw_cursor();

    while (1) {
        read_mouse();
        handle_click();
        draw_background();
        draw_taskbar();
        draw_icons();
        draw_clock();
        draw_cursor();
        sys_yield();
    }

    return 0;
}
