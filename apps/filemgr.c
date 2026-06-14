#include "user_syscall.h"
#include "string.h"
#include "gui_common.h"
#include "gfx_adapter.h"

#define WIN_W 600
#define WIN_H 450
#define NAV_H 28
#define ICON_SIZE 48
#define ICON_PAD 12
#define LABEL_H 14
#define MARGIN 4

#define COLOR_WIN_BG   0xE0E0E0
#define COLOR_NAV_BG   0xC0C0C0
#define COLOR_BORDER   0x808080
#define COLOR_TITLEBAR 0x000080
#define COLOR_WHITE    0xFFFFFF
#define COLOR_BLACK    0x000000
#define COLOR_FOLDER   0xCCAA00
#define COLOR_FILE     0xFFFFFF
#define COLOR_LABEL    0x000000
#define COLOR_UPBTN    0x008000

#define MAX_ENTRIES 64
#define MAX_PATH 256

typedef struct {
    char name[64];
    int is_dir;
} dir_entry_t;

static int fb_fd;
static int mouse_fd;
static int kbd_fd;

static int win_x = 120;
static int win_y = 60;

static char current_path[MAX_PATH];
static dir_entry_t entries[MAX_ENTRIES];
static int entry_count = 0;

static int mouse_x = 512;
static int mouse_y = 384;
static int mouse_btn = 0;
static int last_click_time = 0;
static int last_click_idx = -1;

static void read_directory(void)
{
    int fd;
    char buf[4096];
    int n;
    int i;
    char *p;

    entry_count = 0;

    fd = sys_open(current_path, O_RDONLY | O_DIRECTORY);
    if (fd < 0) return;

    n = sys_readdir(fd, buf, sizeof(buf));
    sys_close(fd);

    if (n <= 0) return;

    p = buf;
    while (p < buf + n && entry_count < MAX_ENTRIES) {
        int name_len = 0;
        while (p + name_len < buf + n && p[name_len] != '\0' && p[name_len] != '|' && name_len < 63) {
            entries[entry_count].name[name_len] = p[name_len];
            name_len++;
        }
        entries[entry_count].name[name_len] = '\0';
        if (name_len == 0) break;

        entries[entry_count].is_dir = (p[name_len] == '|') ? (p[name_len + 1] == 'd') : 0;
        entry_count++;

        while (p < buf + n && *p != '\n' && *p != '\0') p++;
        if (p < buf + n) p++;
    }
}

static void draw_window(void)
{
    int i;
    fb_draw_rect(win_x, win_y, WIN_W, WIN_H, COLOR_BORDER);
    fb_draw_rect(win_x + 2, win_y + 2, WIN_W - 4, 20, COLOR_TITLEBAR);
    fb_draw_string(win_x + 8, win_y + 4, "File Manager", COLOR_WHITE, COLOR_TITLEBAR);

    fb_draw_rect(win_x + 2, win_y + 22, WIN_W - 4, NAV_H, COLOR_NAV_BG);
    fb_draw_rect(win_x + 6, win_y + 26, 60, 20, COLOR_UPBTN);
    fb_draw_string(win_x + 14, win_y + 28, "Up", COLOR_WHITE, COLOR_UPBTN);
    fb_draw_string(win_x + 76, win_y + 28, current_path, COLOR_BLACK, COLOR_NAV_BG);

    fb_draw_rect(win_x + 2, win_y + 22 + NAV_H, WIN_W - 4, WIN_H - 22 - NAV_H - 2, COLOR_WIN_BG);

    int grid_x_start = win_x + MARGIN + 2;
    int grid_y_start = win_y + 22 + NAV_H + MARGIN;
    int cols_per_row = (WIN_W - MARGIN * 2 - 4) / (ICON_SIZE + ICON_PAD);
    if (cols_per_row < 1) cols_per_row = 1;

    for (i = 0; i < entry_count; i++) {
        int col = i % cols_per_row;
        int row = i / cols_per_row;
        int ix = grid_x_start + col * (ICON_SIZE + ICON_PAD);
        int iy = grid_y_start + row * (ICON_SIZE + LABEL_H + ICON_PAD);

        if (iy + ICON_SIZE + LABEL_H > win_y + WIN_H) break;

        unsigned int icon_color = entries[i].is_dir ? COLOR_FOLDER : COLOR_FILE;
        fb_draw_rect(ix, iy, ICON_SIZE, ICON_SIZE, icon_color);
        fb_draw_rect(ix, iy, ICON_SIZE, ICON_SIZE, COLOR_BORDER);

        if (entries[i].is_dir) {
            fb_draw_rect(ix + 4, iy + 4, 20, 8, COLOR_FOLDER);
            fb_draw_rect(ix + 2, iy + 10, ICON_SIZE - 4, ICON_SIZE - 14, COLOR_FOLDER);
        }

        int name_len = 0;
        while (entries[i].name[name_len] && name_len < 8) name_len++;
        int label_x = ix + (ICON_SIZE - name_len * CHAR_WIDTH) / 2;
        fb_draw_string(label_x, iy + ICON_SIZE + 2, entries[i].name, COLOR_LABEL, COLOR_WIN_BG);
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

static void handle_click(void)
{
    int ticks;
    int i;
    if (!(mouse_btn & 0x01)) return;

    asm volatile("int $0x80" : "=a"(ticks) : "a"(0x30) : "memory");

    if (mouse_x >= win_x + 6 && mouse_x < win_x + 66 &&
        mouse_y >= win_y + 26 && mouse_y < win_y + 46) {
        int len = 0;
        while (current_path[len]) len++;
        while (len > 1 && current_path[len - 1] == '/') len--;
        while (len > 1 && current_path[len - 1] != '/') len--;
        if (len > 1) current_path[len - 1] = '\0';
        else { current_path[0] = '/'; current_path[1] = '\0'; }
        sys_chdir(current_path);
        read_directory();
        return;
    }

    int grid_x_start = win_x + MARGIN + 2;
    int grid_y_start = win_y + 22 + NAV_H + MARGIN;
    int cols_per_row = (WIN_W - MARGIN * 2 - 4) / (ICON_SIZE + ICON_PAD);
    if (cols_per_row < 1) cols_per_row = 1;

    for (i = 0; i < entry_count; i++) {
        int col = i % cols_per_row;
        int row = i / cols_per_row;
        int ix = grid_x_start + col * (ICON_SIZE + ICON_PAD);
        int iy = grid_y_start + row * (ICON_SIZE + LABEL_H + ICON_PAD);

        if (mouse_x >= ix && mouse_x < ix + ICON_SIZE &&
            mouse_y >= iy && mouse_y < iy + ICON_SIZE + LABEL_H) {

            int is_double = (ticks - last_click_time < 30) && (last_click_idx == i);
            last_click_time = ticks;
            last_click_idx = i;

            if (is_double) {
                if (entries[i].is_dir) {
                    int plen = 0;
                    while (current_path[plen]) plen++;
                    if (plen > 1) {
                        current_path[plen] = '/';
                        plen++;
                    }
                    int nlen = 0;
                    while (entries[i].name[nlen] && plen + nlen < MAX_PATH - 1) {
                        current_path[plen + nlen] = entries[i].name[nlen];
                        nlen++;
                    }
                    current_path[plen + nlen] = '\0';
                    sys_chdir(current_path);
                    read_directory();
                } else {
                    int pid = sys_fork();
                    if (pid == 0) {
                        char full_path[MAX_PATH];
                        int plen = 0;
                        while (current_path[plen]) { full_path[plen] = current_path[plen]; plen++; }
                        if (plen > 1) { full_path[plen] = '/'; plen++; }
                        int nlen = 0;
                        while (entries[i].name[nlen] && plen + nlen < MAX_PATH - 1) {
                            full_path[plen + nlen] = entries[i].name[nlen];
                            nlen++;
                        }
                        full_path[plen + nlen] = '\0';
                        char *argv[] = { full_path, (void *)0 };
                        sys_exec("/apps/notepad.elf", argv);
                        sys_exit(1);
                    }
                }
            }
            return;
        }
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

    current_path[0] = '/';
    current_path[1] = '\0';
    sys_getcwd(current_path, MAX_PATH);
    read_directory();

    while (1) {
        read_mouse();
        handle_click();
        draw_window();
        sys_yield();
    }

    return 0;
}
