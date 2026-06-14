#include "user_syscall.h"
#include "string.h"

#define WIN_W 560
#define WIN_H 420
#define TITLE_H 20
#define CHAR_W 8
#define CHAR_H 16
#define MARGIN 4
#define TEXT_AREA_X (MARGIN + 2)
#define TEXT_AREA_Y (TITLE_H + MARGIN + 2)

#define COLOR_WIN_BG   0xFFFFFF
#define COLOR_BORDER   0x808080
#define COLOR_TITLEBAR 0x000080
#define COLOR_TEXT     0x000000
#define COLOR_CURSOR   0x0000FF

#define FB_IOCTL_GET_PTR 0x01
#define KBD_IOCTL_READ   0x20

#define MAX_LINES 128
#define LINE_LEN  80

static unsigned int *fb;
static int fb_fd;
static int kbd_fd;

static int win_x = 140;
static int win_y = 100;

static char text_buffer[4096];
static int buf_len = 0;
static int cursor_pos = 0;
static int scroll_y = 0;

static int ctrl_pressed = 0;

static const char *save_path = "/home/newfile.txt";

static void fb_draw_rect(int x, int y, int w, int h, unsigned int color)
{
    int i, j;
    for (j = y; j < y + h; j++) {
        for (i = x; i < x + w; i++) {
            if (i >= 0 && i < 1024 && j >= 0 && j < 768)
                fb[j * 1024 + i] = color;
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
            if (px >= 0 && px < 1024 && py >= 0 && py < 768)
                fb[py * 1024 + px] = (row & (0x80 >> i)) ? fg : bg;
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

static int get_line_count(void)
{
    int count = 1;
    int i;
    for (i = 0; i < buf_len; i++) {
        if (text_buffer[i] == '\n') count++;
    }
    return count;
}

static int get_line_start(int line)
{
    int current_line = 0;
    int i;
    for (i = 0; i < buf_len; i++) {
        if (current_line == line) return i;
        if (text_buffer[i] == '\n') current_line++;
    }
    return buf_len;
}

static void get_cursor_line_col(int *line, int *col)
{
    *line = 0;
    *col = 0;
    int i;
    for (i = 0; i < cursor_pos && i < buf_len; i++) {
        if (text_buffer[i] == '\n') {
            (*line)++;
            *col = 0;
        } else {
            (*col)++;
        }
    }
}

static void draw_window(void)
{
    int line, col, i;
    fb_draw_rect(win_x, win_y, WIN_W, WIN_H, COLOR_BORDER);
    fb_draw_rect(win_x + 2, win_y + 2, WIN_W - 4, TITLE_H, COLOR_TITLEBAR);
    fb_draw_string(win_x + 8, win_y + 4, "Notepad", 0xFFFFFF, COLOR_TITLEBAR);
    fb_draw_rect(win_x + 2, win_y + TITLE_H + 2, WIN_W - 4, WIN_H - TITLE_H - 4, COLOR_WIN_BG);

    int visible_lines = (WIN_H - TITLE_H - MARGIN * 2 - 4) / CHAR_H;
    int text_x = win_x + TEXT_AREA_X;
    int text_y = win_y + TEXT_AREA_Y;

    for (line = scroll_y; line < scroll_y + visible_lines; line++) {
        int start = get_line_start(line);
        int end = get_line_start(line + 1);
        if (start >= buf_len && line > 0) break;

        int draw_y = text_y + (line - scroll_y) * CHAR_H;
        if (draw_y + CHAR_H > win_y + WIN_H) break;

        col = 0;
        for (i = start; i < end && i < buf_len; i++) {
            if (text_buffer[i] == '\n') break;
            if (col >= (WIN_W - TEXT_AREA_X * 2) / CHAR_W) break;
            fb_draw_char(text_x + col * CHAR_W, draw_y, text_buffer[i], COLOR_TEXT, COLOR_WIN_BG);
            col++;
        }
        for (i = col; i < (WIN_W - TEXT_AREA_X * 2) / CHAR_W; i++) {
            fb_draw_rect(text_x + i * CHAR_W, draw_y, CHAR_W, CHAR_H, COLOR_WIN_BG);
        }
    }

    {
        int cur_line, cur_col;
        get_cursor_line_col(&cur_line, &cur_col);
        if (cur_line >= scroll_y && cur_line < scroll_y + visible_lines) {
            int cx = text_x + cur_col * CHAR_W;
            int cy = text_y + (cur_line - scroll_y) * CHAR_H;
            fb_draw_rect(cx, cy, 2, CHAR_H, COLOR_CURSOR);
        }
    }
}

static void save_file(void)
{
    int fd = sys_open(save_path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return;
    sys_write(fd, text_buffer, buf_len);
    sys_close(fd);
}

static void insert_char(char c)
{
    int i;
    if (buf_len >= 4095) return;
    for (i = buf_len; i > cursor_pos; i--) {
        text_buffer[i] = text_buffer[i - 1];
    }
    text_buffer[cursor_pos] = c;
    cursor_pos++;
    buf_len++;
    text_buffer[buf_len] = '\0';
}

static void delete_char(void)
{
    int i;
    if (cursor_pos <= 0) return;
    for (i = cursor_pos; i < buf_len; i++) {
        text_buffer[i - 1] = text_buffer[i];
    }
    cursor_pos--;
    buf_len--;
    text_buffer[buf_len] = '\0';
}

static void handle_keyboard(void)
{
    unsigned char key;
    int n;
    if (kbd_fd < 0) return;
    n = sys_read(kbd_fd, &key, 1);
    if (n <= 0) return;

    if (key == 0x1D || key == 0x9D) {
        ctrl_pressed = (key == 0x1D) ? 1 : 0;
        return;
    }

    if (ctrl_pressed && key == 0x1F) {
        save_file();
        return;
    }

    if (key == 0x0E) {
        delete_char();
        return;
    }

    if (key == 0x1C) {
        insert_char('\n');
        int cur_line, cur_col;
        get_cursor_line_col(&cur_line, &cur_col);
        int visible_lines = (WIN_H - TITLE_H - MARGIN * 2 - 4) / CHAR_H;
        if (cur_line >= scroll_y + visible_lines) {
            scroll_y = cur_line - visible_lines + 1;
        }
        return;
    }

    if (key == 0x48) {
        int cur_line, cur_col;
        get_cursor_line_col(&cur_line, &cur_col);
        if (cur_line > 0) {
            int target_line = cur_line - 1;
            int target_start = get_line_start(target_line);
            int target_end = get_line_start(target_line + 1);
            int line_len = target_end - target_start;
            if (text_buffer[target_end - 1] == '\n') line_len--;
            if (cur_col > line_len) cur_col = line_len;
            cursor_pos = target_start + cur_col;
            if (cur_line - 1 < scroll_y) scroll_y = cur_line - 1;
        }
        return;
    }

    if (key == 0x50) {
        int cur_line, cur_col;
        get_cursor_line_col(&cur_line, &cur_col);
        int total_lines = get_line_count();
        if (cur_line < total_lines - 1) {
            int target_line = cur_line + 1;
            int target_start = get_line_start(target_line);
            int target_end = get_line_start(target_line + 1);
            int line_len = target_end - target_start;
            if (target_end > 0 && text_buffer[target_end - 1] == '\n') line_len--;
            if (cur_col > line_len) cur_col = line_len;
            cursor_pos = target_start + cur_col;
            int visible_lines = (WIN_H - TITLE_H - MARGIN * 2 - 4) / CHAR_H;
            if (cur_line + 1 >= scroll_y + visible_lines) {
                scroll_y = cur_line + 1 - visible_lines + 1;
            }
        }
        return;
    }

    if (key == 0x4B) {
        if (cursor_pos > 0) cursor_pos--;
        return;
    }

    if (key == 0x4D) {
        if (cursor_pos < buf_len) cursor_pos++;
        return;
    }

    char c = 0;
    if (key >= 0x02 && key <= 0x0B) c = '1' + (key - 0x02);
    else if (key == 0x0C) c = '-';
    else if (key == 0x0D) c = '=';
    else if (key >= 0x10 && key <= 0x19) c = 'q' + (key - 0x10);
    else if (key >= 0x1E && key <= 0x26) c = 'a' + (key - 0x1E);
    else if (key >= 0x2C && key <= 0x32) c = 'z' + (key - 0x2C);
    else if (key == 0x39) c = ' ';
    else if (key == 0x0B) c = '0';
    else if (key == 0x28) c = '\n';

    if (c) {
        insert_char(c);
        int cur_line, cur_col;
        get_cursor_line_col(&cur_line, &cur_col);
        int visible_lines = (WIN_H - TITLE_H - MARGIN * 2 - 4) / CHAR_H;
        if (cur_line >= scroll_y + visible_lines) {
            scroll_y = cur_line - visible_lines + 1;
        }
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

    text_buffer[0] = '\0';
    buf_len = 0;
    cursor_pos = 0;
    scroll_y = 0;

    while (1) {
        handle_keyboard();
        draw_window();
        sys_yield();
    }

    return 0;
}
