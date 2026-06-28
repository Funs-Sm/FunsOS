#include "user_syscall.h"
#include "string.h"
#include "gui_common.h"
#include "gfx_adapter.h"

#define WIN_W 640
#define WIN_H 480
#define COLS 80
#define ROWS 25
#define MARGIN 4
#define TITLE_H 20

#define COLOR_BG       0x000000
#define COLOR_FG       0x00FF00
#define COLOR_WIN_BG   0x1A1A1A
#define COLOR_TITLEBAR 0x000080
#define COLOR_BORDER   0x808080

static int fb_fd;
static int mouse_fd;
static int kbd_fd;

static int win_x = 100;
static int win_y = 80;

static char text_buf[ROWS * COLS];
static int cursor_x = 0;
static int cursor_y = 0;
static int scroll_offset = 0;

static int shell_pid;
static int pipe_read;
static int pipe_write;
static int pipe_stdin[2];

static int sys_dup2(int oldfd, int newfd);

static void scroll_up(void)
{
    int y;
    for (y = 0; y < ROWS - 1; y++) {
        memcpy(&text_buf[y * COLS], &text_buf[(y + 1) * COLS], COLS);
    }
    memset(&text_buf[(ROWS - 1) * COLS], ' ', COLS);
    cursor_y = ROWS - 1;
    cursor_x = 0;
}

static void put_char(char c)
{
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= ROWS) {
            scroll_up();
        }
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            text_buf[cursor_y * COLS + cursor_x] = ' ';
        }
    } else if (c == '\r') {
        cursor_x = 0;
    } else {
        text_buf[cursor_y * COLS + cursor_x] = c;
        cursor_x++;
        if (cursor_x >= COLS) {
            cursor_x = 0;
            cursor_y++;
            if (cursor_y >= ROWS) {
                scroll_up();
            }
        }
    }
}

static void draw_window(void)
{
    int i, j;
    fb_draw_rect(win_x, win_y, WIN_W, WIN_H, COLOR_BORDER);
    fb_draw_rect(win_x + 2, win_y + 2, WIN_W - 4, TITLE_H, COLOR_TITLEBAR);
    fb_draw_string(win_x + 8, win_y + 4, "Terminal", 0xFFFFFF, COLOR_TITLEBAR);
    fb_draw_rect(win_x + 2, win_y + TITLE_H + 2, WIN_W - 4, WIN_H - TITLE_H - 4, COLOR_WIN_BG);

    int text_area_x = win_x + MARGIN + 2;
    int text_area_y = win_y + TITLE_H + MARGIN + 2;
    int visible_rows = (WIN_H - TITLE_H - MARGIN * 2 - 4) / CHAR_HEIGHT;
    if (visible_rows > ROWS) visible_rows = ROWS;

    for (j = 0; j < visible_rows; j++) {
        int row = j + scroll_offset;
        if (row >= ROWS) break;
        for (i = 0; i < COLS; i++) {
            char c = text_buf[row * COLS + i];
            int cx = text_area_x + i * CHAR_WIDTH;
            int cy = text_area_y + j * CHAR_HEIGHT;
            if (c != ' ') {
                fb_draw_char(cx, cy, c, COLOR_FG, COLOR_WIN_BG);
            } else {
                fb_draw_rect(cx, cy, CHAR_WIDTH, CHAR_HEIGHT, COLOR_WIN_BG);
            }
        }
    }

    {
        int cx = text_area_x + cursor_x * CHAR_WIDTH;
        int cy = text_area_y + (cursor_y - scroll_offset) * CHAR_HEIGHT;
        if (cursor_y >= scroll_offset && cursor_y < scroll_offset + visible_rows) {
            fb_draw_rect(cx, cy, CHAR_WIDTH, CHAR_HEIGHT, COLOR_FG);
            if (text_buf[cursor_y * COLS + cursor_x] != ' ') {
                fb_draw_char(cx, cy, text_buf[cursor_y * COLS + cursor_x], COLOR_WIN_BG, COLOR_FG);
            }
        }
    }
}

static void init_text_buf(void)
{
    memset(text_buf, ' ', ROWS * COLS);
    cursor_x = 0;
    cursor_y = 0;
}

static void start_shell(void)
{
    sys_pipe(pipe_stdin);
    int stdout_pipe[2];
    sys_pipe(stdout_pipe);

    pipe_read = stdout_pipe[0];
    pipe_write = stdout_pipe[1];

    shell_pid = sys_fork();
    if (shell_pid == 0) {
        sys_close(pipe_stdin[1]);
        sys_close(stdout_pipe[0]);
        sys_close(0);
        sys_close(1);
        sys_dup2(pipe_stdin[0], 0);
        sys_dup2(stdout_pipe[1], 1);
        sys_close(pipe_stdin[0]);
        sys_close(stdout_pipe[1]);
        char *argv[] = { "/bin/shell", (void *)0 };
        sys_exec("/bin/shell", argv);
        sys_exit(1);
    }

    sys_close(pipe_stdin[0]);
    sys_close(stdout_pipe[1]);
}

static int sys_dup2(int oldfd, int newfd)
{
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(21), "b"(oldfd), "c"(newfd) : "memory");
    return ret;
}

static void read_shell_output(void)
{
    char buf[256];
    int n;
    int i;
    while (1) {
        n = sys_read(pipe_read, buf, 256);
        if (n <= 0) break;
        for (i = 0; i < n; i++) {
            put_char(buf[i]);
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

    if (key == 0x48) {
        if (scroll_offset > 0) scroll_offset--;
        return;
    }
    if (key == 0x50) {
        int visible = (WIN_H - TITLE_H - MARGIN * 2 - 4) / CHAR_HEIGHT;
        if (scroll_offset + visible < ROWS) scroll_offset++;
        return;
    }

    char c = 0;
    if (key >= 0x02 && key <= 0x0B) c = '1' + (key - 0x02);
    else if (key == 0x0C) c = '-';
    else if (key == 0x0D) c = '=';
    else if (key == 0x0F) c = '\t';
    else if (key == 0x1C) c = '\n';
    else if (key == 0x0E) c = '\b';
    else if (key >= 0x10 && key <= 0x19) c = 'q' + (key - 0x10);
    else if (key >= 0x1E && key <= 0x26) c = 'a' + (key - 0x1E);
    else if (key >= 0x2C && key <= 0x32) c = 'z' + (key - 0x2C);
    else if (key == 0x39) c = ' ';

    if (c) {
        sys_write(pipe_stdin[1], &c, 1);
        if (c == '\n' || c == '\b') {
            put_char(c);
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

    init_text_buf();
    start_shell();

    put_char('$');
    put_char(' ');

    while (1) {
        read_shell_output();
        handle_keyboard();
        draw_window();
        sys_yield();
    }

    return 0;
}
