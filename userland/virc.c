#include "user_syscall.h"
#include "string.h"

#define MODE_NORMAL  0
#define MODE_INSERT  1
#define MODE_COMMAND 2

static char lines[256][256];
static int line_count = 0;
static int cursor_x = 0;
static int cursor_y = 0;
static int mode = MODE_NORMAL;
static char cmd_buf[64];
static int cmd_len = 0;
static char filename[128];
static int modified = 0;

static void redraw(void) {
    sys_write(1, "\033[2J\033[H", 7);

    for (int i = 0; i < line_count && i < 24; i++) {
        sys_write(1, lines[i], strlen(lines[i]));
        sys_write(1, "\n", 1);
    }

    char pos[32];
    int plen = 0;
    pos[plen++] = '\033';
    pos[plen++] = '[';
    pos[plen++] = '0' + (cursor_y + 1) / 10;
    pos[plen++] = '0' + (cursor_y + 1) % 10;
    pos[plen++] = ';';
    pos[plen++] = '0' + (cursor_x + 1) / 10;
    pos[plen++] = '0' + (cursor_x + 1) % 10;
    pos[plen++] = 'H';
    sys_write(1, pos, plen);

    const char *mode_str = (mode == MODE_INSERT) ? "-- INSERT --" : "";
    sys_write(1, "\033[25;1H", 6);
    sys_write(1, mode_str, strlen(mode_str));
}

static void load_file(const char *fname) {
    strncpy(filename, fname, 127);
    filename[127] = '\0';

    int fd = sys_open(fname, 0);
    if (fd < 0) {
        line_count = 1;
        lines[0][0] = '\0';
        return;
    }

    char buf[4096];
    int n = sys_read(fd, buf, 4095);
    sys_close(fd);

    if (n <= 0) {
        line_count = 1;
        lines[0][0] = '\0';
        return;
    }

    buf[n] = '\0';
    line_count = 0;
    int pos = 0;
    for (int i = 0; i < n && line_count < 256; i++) {
        if (buf[i] == '\n') {
            lines[line_count][pos] = '\0';
            line_count++;
            pos = 0;
        } else if (buf[i] == '\r') {
            continue;
        } else {
            if (pos < 255) lines[line_count][pos++] = buf[i];
        }
    }
    if (pos > 0 && line_count < 256) {
        lines[line_count][pos] = '\0';
        line_count++;
    }

    if (line_count == 0) {
        line_count = 1;
        lines[0][0] = '\0';
    }
}

static void save_file(void) {
    int fd = sys_open(filename, 0x42);
    if (fd < 0) return;

    for (int i = 0; i < line_count; i++) {
        sys_write(fd, lines[i], strlen(lines[i]));
        sys_write(fd, "\n", 1);
    }

    sys_close(fd);
    modified = 0;
}

int main(int argc, char *argv[]) {
    line_count = 1;
    lines[0][0] = '\0';

    if (argc >= 2) {
        load_file(argv[1]);
    }

    redraw();

    while (1) {
        char c;
        int r = sys_read(0, &c, 1);
        if (r <= 0) continue;

        if (mode == MODE_NORMAL) {
            if (c == 'h') {
                if (cursor_x > 0) cursor_x--;
            } else if (c == 'j') {
                if (cursor_y < line_count - 1) cursor_y++;
            } else if (c == 'k') {
                if (cursor_y > 0) cursor_y--;
            } else if (c == 'l') {
                int len = strlen(lines[cursor_y]);
                if (cursor_x < len) cursor_x++;
            } else if (c == 'i') {
                mode = MODE_INSERT;
            } else if (c == ':') {
                mode = MODE_COMMAND;
                cmd_len = 0;
                cmd_buf[0] = '\0';
            } else if (c == 'x') {
                int len = strlen(lines[cursor_y]);
                if (cursor_x < len) {
                    for (int i = cursor_x; i < len; i++) {
                        lines[cursor_y][i] = lines[cursor_y][i + 1];
                    }
                    modified = 1;
                }
            } else if (c == 'd') {
                char c2;
                sys_read(0, &c2, 1);
                if (c2 == 'd') {
                    if (line_count > 1) {
                        for (int i = cursor_y; i < line_count - 1; i++) {
                            strcpy(lines[i], lines[i + 1]);
                        }
                        line_count--;
                        modified = 1;
                    } else {
                        lines[0][0] = '\0';
                        modified = 1;
                    }
                }
            }
        } else if (mode == MODE_INSERT) {
            if (c == 27) {
                mode = MODE_NORMAL;
            } else if (c == '\n') {
                if (line_count < 256) {
                    for (int i = line_count; i > cursor_y + 1; i--) {
                        strcpy(lines[i], lines[i - 1]);
                    }
                    int len = strlen(lines[cursor_y]);
                    int split_pos = cursor_x;
                    strcpy(lines[cursor_y + 1], lines[cursor_y] + split_pos);
                    lines[cursor_y][split_pos] = '\0';
                    line_count++;
                    cursor_y++;
                    cursor_x = 0;
                    modified = 1;
                }
            } else if (c == 127 || c == 8) {
                if (cursor_x > 0) {
                    int len = strlen(lines[cursor_y]);
                    for (int i = cursor_x - 1; i < len; i++) {
                        lines[cursor_y][i] = lines[cursor_y][i + 1];
                    }
                    cursor_x--;
                    modified = 1;
                }
            } else {
                int len = strlen(lines[cursor_y]);
                if (len < 255) {
                    for (int i = len; i >= cursor_x; i--) {
                        lines[cursor_y][i + 1] = lines[cursor_y][i];
                    }
                    lines[cursor_y][cursor_x] = c;
                    cursor_x++;
                    modified = 1;
                }
            }
        } else if (mode == MODE_COMMAND) {
            if (c == '\n') {
                if (cmd_len >= 1) {
                    if (cmd_buf[0] == 'w') {
                        save_file();
                    } else if (cmd_buf[0] == 'q') {
                        if (cmd_len >= 2 && cmd_buf[1] == '!') {
                            sys_exit(0);
                        } else if (modified) {
                            sys_write(1, "No write since last change\n", 27);
                        } else {
                            sys_exit(0);
                        }
                    } else if (cmd_len >= 2 && cmd_buf[0] == 'w' && cmd_buf[1] == 'q') {
                        save_file();
                        sys_exit(0);
                    }
                }
                mode = MODE_NORMAL;
            } else if (c == 27) {
                mode = MODE_NORMAL;
            } else {
                if (cmd_len < 63) {
                    cmd_buf[cmd_len++] = c;
                    cmd_buf[cmd_len] = '\0';
                }
            }
        }

        redraw();
    }

    return 0;
}
