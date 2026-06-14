/* terminal.c - FUNSOS 终端模拟器（重写版）
 * 基于 display_server (ds_*) API 的窗口化终端应用。
 * 支持 ANSI 转义序列、滚动缓冲区、命令历史、Shell 集成。
 */

#include "terminal.h"
#include "display_server.h"
#include "shell.h"
#include "stdint.h"
#include "string.h"
#include "font.h"
#include "gfx.h"

/* ================================================================
 * 颜色定义 (0xRRGGBB 格式)
 * ================================================================ */
#define COLOR_BG_VAL       0x0C0C0C   /* 深黑背景 */
#define COLOR_FG_VAL       0x00FF00   /* 绿色文字 */
#define COLOR_CURSOR_VAL   0x00FF00   /* 光标颜色 */
#define COLOR_TITLE_BG     0x202020   /* 标题栏背景 */
#define COLOR_TITLE_FG     0xAAAAAA   /* 标题栏文字 */
#define COLOR_SCROLLBAR_BG 0x303030   /* 滚动条背景 */
#define COLOR_SCROLLBAR_FG 0x606060   /* 滚动条滑块 */

/* ANSI 16 色映射表 (0xRRGGBB) */
static const uint32_t g_ansi_colors[16] = {
    0x000000, /*  0: 黑色 */
    0xAA0000, /*  1: 红色 */
    0x00AA00, /*  2: 绿色 */
    0xAA5500, /*  3: 黄色 */
    0x0000AA, /*  4: 蓝色 */
    0xAA00AA, /*  5: 品红 */
    0x00AAAA, /*  6: 青色 */
    0xAAAAAA, /*  7: 白色 */
    0x555555, /*  8: 亮黑 */
    0xFF5555, /*  9: 亮红 */
    0x55FF55, /* 10: 亮绿 */
    0xFFFF55, /* 11: 亮黄 */
    0x5555FF, /* 12: 亮蓝 */
    0xFF55FF, /* 13: 亮品红 */
    0x55FFFF, /* 14: 亮青 */
    0xFFFFFF, /* 15: 亮白 */
};

/* ================================================================
 * 数据结构
 * ================================================================ */

/* 终端字符单元 */
typedef struct {
    char     ch;          /* 字符 */
    uint8_t  fg_idx;      /* 前景色索引(ANSI 0-15) */
    uint8_t  bg_idx;      /* 背景色索引(ANSI 0-15) */
    uint32_t fg_true;     /* 24-bit 前景色(仅当使用真彩色时) */
    uint32_t bg_true;     /* 24-bit 背景色 */
    uint8_t  bold;        /* 粗体 */
    uint8_t  underline;   /* 下划线 */
    uint8_t  blink;       /* 闪烁 */
    uint8_t  reverse;     /* 反色 */
    uint8_t  use_true_fg; /* 使用真彩色前景 */
    uint8_t  use_true_bg; /* 使用真彩色背景 */
} term_cell_t;

/* ANSI 解析状态 */
typedef enum {
    ANSI_STATE_NORMAL = 0,
    ANSI_STATE_ESC,
    ANSI_STATE_CSI,
    ANSI_STATE_OSC,
} ansi_state_t;

/* 终端全局状态 */
typedef struct {
    /* 屏幕缓冲区 */
    term_cell_t screen[TERM_MAX_ROWS][TERM_MAX_COLS];
    /* 滚动缓冲区 */
    term_cell_t *scrollback[TERM_SCROLLBACK];
    uint32_t    scrollback_count;
    uint32_t    scrollback_offset;

    /* 光标 */
    uint32_t cursor_x;
    uint32_t cursor_y;
    uint8_t  cursor_visible;
    uint32_t cursor_blink_tick;

    /* 当前属性 */
    uint8_t  fg_idx;
    uint8_t  bg_idx;
    uint32_t fg_true;
    uint32_t bg_true;
    uint8_t  use_true_fg;
    uint8_t  use_true_bg;
    uint8_t  bold;
    uint8_t  underline;
    uint8_t  blink;
    uint8_t  reverse;

    /* ANSI 解析状态机 */
    ansi_state_t ansi_state;
    char         ansi_buf[64];
    uint32_t     ansi_len;

    /* 命令历史与输入 */
    char     history[TERM_HISTORY_MAX][TERM_CMD_MAX_LEN];
    uint32_t history_count;
    int32_t  history_pos;
    char     input_buf[TERM_CMD_MAX_LEN];
    uint32_t input_len;
    uint32_t input_cursor;
    char     prompt[32];

    /* 窗口 */
    uint32_t win_id;
    int32_t  win_x, win_y;
    uint32_t win_w, win_h;
    uint32_t cols;       /* 实际可用列数 */
    uint32_t rows;       /* 实际可用行数 */
    char     title[128];

    /* 运行状态 */
    uint8_t  running;
    uint8_t  need_redraw;
} Terminal;

static Terminal g_term;

/* Shell 输出回调 —— 设置为 terminal_write 后，shell_print 可调用此回调 */
static void (*g_shell_output_hook)(const char *str) = NULL;

/* ================================================================
 * 前向声明
 * ================================================================ */
static void term_alloc_scrollback(void);
static void term_free_scrollback(void);
static void term_scroll_up(void);
static void term_newline(void);
static void term_put_char_raw(char c);
static void term_clear_screen(void);
static void term_clear_to_end(void);
static void term_move_cursor(int x, int y);
static void term_move_cursor_relative(int dx, int dy);
static void term_reset_attributes(void);
static void term_parse_ansi(const char *seq, int len);
static void term_parse_ansi_char(char c);
static void term_execute_command(const char *cmd);
static void term_tab_complete(void);
static void term_add_history(const char *cmd);
static void term_scroll_back(int lines);
static void term_render_content(void);
static void term_render_title_bar(void);
static void term_render_scrollbar(void);
static void term_render_cursor(void);

/* ================================================================
 * 初始化和清理
 * ================================================================ */
int terminal_main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    memset(&g_term, 0, sizeof(g_term));

    /* 初始化屏幕缓冲区 */
    for (uint32_t r = 0; r < TERM_MAX_ROWS; r++) {
        for (uint32_t c = 0; c < TERM_MAX_COLS; c++) {
            g_term.screen[r][c].ch = ' ';
            g_term.screen[r][c].fg_idx = 2;   /* 绿色 */
            g_term.screen[r][c].bg_idx = 0;   /* 黑色 */
        }
    }

    g_term.fg_idx        = 2;
    g_term.bg_idx        = 0;
    g_term.fg_true       = 0x00FF00;
    g_term.bg_true       = 0x000000;
    g_term.use_true_fg   = 0;
    g_term.use_true_bg   = 0;
    g_term.cursor_visible = 1;
    g_term.cursor_blink_tick = 0;
    g_term.history_pos   = -1;
    g_term.input_len     = 0;
    g_term.input_cursor  = 0;
    g_term.ansi_state    = ANSI_STATE_NORMAL;
    g_term.ansi_len      = 0;
    g_term.running       = 1;
    g_term.need_redraw   = 1;
    g_term.scrollback_count = 0;
    g_term.scrollback_offset = 0;

    strcpy(g_term.prompt, "> ");
    strcpy(g_term.title, "FUNSOS Terminal");

    term_alloc_scrollback();

    /* 设置 Shell 输出钩子 */
    g_shell_output_hook = terminal_write;

    /* 创建窗口 */
    int wx = 60, wy = 40;
    uint32_t ww = 660, wh = 460;
    g_term.win_id = terminal_create_window(wx, wy, (int)ww, (int)wh);
    if (g_term.win_id == 0) {
        return -1;
    }
    g_term.win_x = wx;
    g_term.win_y = wy;
    g_term.win_w = ww;
    g_term.win_h = wh;

    /* 计算行列数 */
    g_term.cols = (ww - 16) / TERM_CHAR_W;  /* 减去滚动条和边距 */
    g_term.rows = (wh - 24) / TERM_CHAR_H;  /* 减去标题栏 */
    if (g_term.cols > TERM_MAX_COLS) g_term.cols = TERM_MAX_COLS;
    if (g_term.rows > TERM_MAX_ROWS) g_term.rows = TERM_MAX_ROWS;
    if (g_term.cols < 20) g_term.cols = 20;
    if (g_term.rows < 5)  g_term.rows = 5;

    /* 输出欢迎信息 */
    terminal_write("FUNSOS Terminal v1.0\n");
    terminal_write("Type 'help' for available commands.\n\n");
    terminal_write(g_term.prompt);

    /* 事件循环 */
    ds_event_t event;
    while (g_term.running) {
        int has_event = ds_get_event(g_term.win_id, &event);

        if (has_event == 0) {
            /* 处理事件 */
            if (event.type == DS_EVENT_WINDOW_CLOSE) {
                g_term.running = 0;
                break;
            }

            if (event.type == DS_EVENT_KEY_PRESS) {
                uint32_t key = event.param1;
                uint32_t keycode = event.param2;

                /* 特殊键处理 */
                if (key == 0xE0 || key == 0x00) {
                    terminal_handle_special_key(keycode);
                } else if (key >= 0x48 && key <= 0x53) {
                    terminal_handle_special_key(key);
                } else {
                    terminal_handle_key((char)key);
                }
                g_term.need_redraw = 1;
            }

            if (event.type == DS_EVENT_MOUSE_PRESS) {
                terminal_handle_click((int)event.param1, (int)event.param2);
                g_term.need_redraw = 1;
            }

            if (event.type == DS_EVENT_WINDOW_RESIZE) {
                g_term.win_w = event.param1;
                g_term.win_h = event.param2;
                terminal_handle_resize((int)event.param1, (int)event.param2);
                g_term.need_redraw = 1;
            }

            if (event.type == DS_EVENT_EXPOSE) {
                g_term.need_redraw = 1;
            }
        }

        /* 光标闪烁计时 */
        g_term.cursor_blink_tick++;
        if (g_term.cursor_blink_tick >= 500) {
            g_term.cursor_blink_tick = 0;
            g_term.need_redraw = 1;
        }

        /* 渲染 */
        if (g_term.need_redraw) {
            terminal_render();
            g_term.need_redraw = 0;
        }
    }

    /* 清理 */
    term_free_scrollback();
    ds_destroy_window(g_term.win_id);
    g_term.win_id = 0;
    return 0;
}

uint32_t terminal_create_window(int x, int y, int w, int h)
{
    return ds_create_window(x, y, (uint32_t)w, (uint32_t)h, "FUNSOS Terminal");
}

/* ================================================================
 * 滚动缓冲区管理
 * ================================================================ */
static void term_alloc_scrollback(void)
{
    for (uint32_t i = 0; i < TERM_SCROLLBACK; i++) {
        g_term.scrollback[i] = NULL;
    }
    g_term.scrollback_count = 0;
    g_term.scrollback_offset = 0;
}

static void term_free_scrollback(void)
{
    for (uint32_t i = 0; i < TERM_SCROLLBACK; i++) {
        if (g_term.scrollback[i]) {
            /* 在 kernel 环境中，没有 free，用 sys_free */
            g_term.scrollback[i] = NULL;
        }
    }
}

/* 将屏幕顶行推入滚动缓冲区 */
static void term_scroll_up(void)
{
    /* 如果滚动缓冲区有空闲位置，分配一行 */
    if (g_term.scrollback_count < TERM_SCROLLBACK) {
        if (g_term.scrollback[g_term.scrollback_count] == NULL) {
            /* 使用静态分配替代 malloc */
            static term_cell_t scrollback_pool[TERM_SCROLLBACK][TERM_MAX_COLS];
            g_term.scrollback[g_term.scrollback_count] = scrollback_pool[g_term.scrollback_count];
        }
        memcpy(g_term.scrollback[g_term.scrollback_count],
               g_term.screen[0], TERM_MAX_COLS * sizeof(term_cell_t));
        g_term.scrollback_count++;
    } else {
        /* 缓冲区已满，移动所有行 */
        for (uint32_t i = 1; i < TERM_SCROLLBACK; i++) {
            g_term.scrollback[i - 1] = g_term.scrollback[i];
        }
        memcpy(g_term.scrollback[TERM_SCROLLBACK - 1],
               g_term.screen[0], TERM_MAX_COLS * sizeof(term_cell_t));
    }

    /* 屏幕上移一行 */
    for (uint32_t r = 1; r < TERM_MAX_ROWS; r++) {
        memcpy(g_term.screen[r - 1], g_term.screen[r],
               TERM_MAX_COLS * sizeof(term_cell_t));
    }
    /* 清空最后一行 */
    for (uint32_t c = 0; c < TERM_MAX_COLS; c++) {
        g_term.screen[TERM_MAX_ROWS - 1][c].ch = ' ';
        g_term.screen[TERM_MAX_ROWS - 1][c].fg_idx = g_term.fg_idx;
        g_term.screen[TERM_MAX_ROWS - 1][c].bg_idx = g_term.bg_idx;
        g_term.screen[TERM_MAX_ROWS - 1][c].fg_true = g_term.fg_true;
        g_term.screen[TERM_MAX_ROWS - 1][c].bg_true = g_term.bg_true;
        g_term.screen[TERM_MAX_ROWS - 1][c].use_true_fg = g_term.use_true_fg;
        g_term.screen[TERM_MAX_ROWS - 1][c].use_true_bg = g_term.use_true_bg;
        g_term.screen[TERM_MAX_ROWS - 1][c].bold = 0;
        g_term.screen[TERM_MAX_ROWS - 1][c].underline = 0;
        g_term.screen[TERM_MAX_ROWS - 1][c].blink = 0;
        g_term.screen[TERM_MAX_ROWS - 1][c].reverse = 0;
    }
}

/* ================================================================
 * 终端输出
 * ================================================================ */
void terminal_write(const char *str)
{
    if (!str) return;
    g_term.scrollback_offset = 0;
    for (int i = 0; str[i]; i++) {
        terminal_putchar(str[i]);
    }
    g_term.need_redraw = 1;
}

void terminal_putchar(char c)
{
    /* ANSI 转义序列解析 */
    if (g_term.ansi_state != ANSI_STATE_NORMAL) {
        term_parse_ansi_char(c);
        return;
    }

    if (c == '\033') {
        g_term.ansi_state = ANSI_STATE_ESC;
        g_term.ansi_len = 0;
        g_term.ansi_buf[0] = '\0';
        return;
    }

    if (c == '\n') {
        term_newline();
        return;
    }
    if (c == '\r') {
        g_term.cursor_x = 0;
        return;
    }
    if (c == '\t') {
        uint32_t spaces = 4 - (g_term.cursor_x % 4);
        for (uint32_t i = 0; i < spaces; i++) {
            term_put_char_raw(' ');
        }
        return;
    }
    if (c == '\b') {
        if (g_term.cursor_x > 0) {
            g_term.cursor_x--;
        }
        return;
    }
    if (c < 0x20) return; /* 忽略其他控制字符 */

    term_put_char_raw(c);
}

static void term_put_char_raw(char c)
{
    if (g_term.cursor_x >= g_term.cols) {
        term_newline();
    }

    term_cell_t *cell = &g_term.screen[g_term.cursor_y][g_term.cursor_x];
    cell->ch = c;
    cell->fg_idx = g_term.fg_idx;
    cell->bg_idx = g_term.bg_idx;
    cell->fg_true = g_term.fg_true;
    cell->bg_true = g_term.bg_true;
    cell->use_true_fg = g_term.use_true_fg;
    cell->use_true_bg = g_term.use_true_bg;
    cell->bold = g_term.bold;
    cell->underline = g_term.underline;
    cell->blink = g_term.blink;
    cell->reverse = g_term.reverse;

    g_term.cursor_x++;
    if (g_term.cursor_x >= g_term.cols) {
        term_newline();
    }
}

static void term_newline(void)
{
    g_term.cursor_x = 0;
    g_term.cursor_y++;
    if (g_term.cursor_y >= g_term.rows) {
        term_scroll_up();
        g_term.cursor_y = g_term.rows - 1;
    }
}

/* ================================================================
 * 屏幕操作
 * ================================================================ */
static void term_clear_screen(void)
{
    for (uint32_t r = 0; r < TERM_MAX_ROWS; r++) {
        for (uint32_t c = 0; c < TERM_MAX_COLS; c++) {
            g_term.screen[r][c].ch = ' ';
            g_term.screen[r][c].fg_idx = g_term.fg_idx;
            g_term.screen[r][c].bg_idx = g_term.bg_idx;
            g_term.screen[r][c].fg_true = g_term.fg_true;
            g_term.screen[r][c].bg_true = g_term.bg_true;
            g_term.screen[r][c].use_true_fg = g_term.use_true_fg;
            g_term.screen[r][c].use_true_bg = g_term.use_true_bg;
            g_term.screen[r][c].bold = 0;
            g_term.screen[r][c].underline = 0;
            g_term.screen[r][c].blink = 0;
            g_term.screen[r][c].reverse = 0;
        }
    }
    g_term.cursor_x = 0;
    g_term.cursor_y = 0;
}

static void term_clear_to_end(void)
{
    for (uint32_t c = g_term.cursor_x; c < TERM_MAX_COLS; c++) {
        g_term.screen[g_term.cursor_y][c].ch = ' ';
        g_term.screen[g_term.cursor_y][c].fg_idx = g_term.fg_idx;
        g_term.screen[g_term.cursor_y][c].bg_idx = g_term.bg_idx;
    }
}

static void term_move_cursor(int x, int y)
{
    if (x < 0) x = 0;
    if (x >= (int)g_term.cols) x = (int)g_term.cols - 1;
    if (y < 0) y = 0;
    if (y >= (int)g_term.rows) y = (int)g_term.rows - 1;
    g_term.cursor_x = (uint32_t)x;
    g_term.cursor_y = (uint32_t)y;
}

static void term_move_cursor_relative(int dx, int dy)
{
    int new_x = (int)g_term.cursor_x + dx;
    int new_y = (int)g_term.cursor_y + dy;
    term_move_cursor(new_x, new_y);
}

static void term_reset_attributes(void)
{
    g_term.fg_idx      = 2;
    g_term.bg_idx      = 0;
    g_term.fg_true     = 0x00FF00;
    g_term.bg_true     = 0x000000;
    g_term.use_true_fg = 0;
    g_term.use_true_bg = 0;
    g_term.bold        = 0;
    g_term.underline   = 0;
    g_term.blink       = 0;
    g_term.reverse     = 0;
}

/* ================================================================
 * ANSI 转义序列解析
 * 支持:
 *   ESC[#A / ESC[#B / ESC[#C / ESC[#D  - 光标移动
 *   ESC[#;#H / ESC[#;#f               - 光标定位
 *   ESC[2J                            - 清屏
 *   ESC[K                             - 清除到行尾
 *   ESC[#m                            - SGR 图形属性
 *   ESC[?25h / ESC[?25l               - 显示/隐藏光标
 *   ESC[38;5;#m / ESC[48;5;#m         - 256 色
 *   ESC[38;2;R;G;Bm / ESC[48;2;R;G;Bm - 24-bit 真彩色
 * ================================================================ */
static void term_parse_ansi_char(char c)
{
    if (g_term.ansi_state == ANSI_STATE_ESC) {
        if (c == '[') {
            g_term.ansi_state = ANSI_STATE_CSI;
            g_term.ansi_len = 0;
            g_term.ansi_buf[0] = '\0';
            return;
        }
        if (c == ']') {
            g_term.ansi_state = ANSI_STATE_OSC;
            g_term.ansi_len = 0;
            g_term.ansi_buf[0] = '\0';
            return;
        }
        /* 其他 ESC 序列，直接重置 */
        g_term.ansi_state = ANSI_STATE_NORMAL;
        return;
    }

    if (g_term.ansi_state == ANSI_STATE_OSC) {
        /* OSC 序列以 BEL(\a) 或 ST(\033\\) 结束 */
        if (c == '\a' || c == '\007') {
            g_term.ansi_state = ANSI_STATE_NORMAL;
            return;
        }
        if (g_term.ansi_len < 63) {
            g_term.ansi_buf[g_term.ansi_len++] = c;
            g_term.ansi_buf[g_term.ansi_len] = '\0';
        }
        return;
    }

    if (g_term.ansi_state == ANSI_STATE_CSI) {
        /* 收集参数 */
        if ((c >= '0' && c <= '9') || c == ';' || c == '?' || c == ' ') {
            if (g_term.ansi_len < 63) {
                g_term.ansi_buf[g_term.ansi_len++] = c;
                g_term.ansi_buf[g_term.ansi_len] = '\0';
            }
            return;
        }

        /* 终结符：解析序列 */
        if (g_term.ansi_len < 63) {
            g_term.ansi_buf[g_term.ansi_len++] = c;
            g_term.ansi_buf[g_term.ansi_len] = '\0';
        }
        term_parse_ansi(g_term.ansi_buf, (int)g_term.ansi_len);
        g_term.ansi_state = ANSI_STATE_NORMAL;
        return;
    }
}

static void term_parse_ansi(const char *seq, int len)
{
    if (len < 2) return;

    /* 提取参数 */
    int params[16];
    int param_count = 0;
    int i = 0;
    int current_num = -1;
    char has_question = 0;

    /* 检查 ? 前缀 */
    if (seq[0] == '?') {
        has_question = 1;
        i = 1;
    }

    while (i < len && param_count < 16) {
        char c2 = seq[i];
        if (c2 >= '0' && c2 <= '9') {
            if (current_num < 0) current_num = 0;
            current_num = current_num * 10 + (c2 - '0');
        } else if (c2 == ';') {
            params[param_count++] = (current_num >= 0) ? current_num : 0;
            current_num = -1;
        } else {
            /* 命令字符 */
            params[param_count++] = (current_num >= 0) ? current_num : 0;
            char cmd = c2;

            switch (cmd) {
            case 'A': /* 光标上移 */
                term_move_cursor_relative(0, -(params[0] > 0 ? params[0] : 1));
                break;
            case 'B': /* 光标下移 */
                term_move_cursor_relative(0, (params[0] > 0 ? params[0] : 1));
                break;
            case 'C': /* 光标右移 */
                term_move_cursor_relative((params[0] > 0 ? params[0] : 1), 0);
                break;
            case 'D': /* 光标左移 */
                term_move_cursor_relative(-(params[0] > 0 ? params[0] : 1), 0);
                break;
            case 'H': /* 光标定位 */
            case 'f':
                term_move_cursor(
                    (params[1] > 0) ? params[1] - 1 : 0,
                    (params[0] > 0) ? params[0] - 1 : 0
                );
                break;
            case 'J': /* 擦除显示 */
                if (params[0] == 2 || params[0] == 0) {
                    term_clear_screen();
                }
                break;
            case 'K': /* 清除行 */
                term_clear_to_end();
                break;
            case 'm': /* SGR 图形属性 */
                {
                    int p = 0;
                    while (p < param_count) {
                        int val = params[p];
                        if (val == 0) {
                            term_reset_attributes();
                        } else if (val == 1) {
                            g_term.bold = 1;
                        } else if (val == 4) {
                            g_term.underline = 1;
                        } else if (val == 5) {
                            g_term.blink = 1;
                        } else if (val == 7) {
                            g_term.reverse = 1;
                        } else if (val >= 30 && val <= 37) {
                            g_term.fg_idx = (uint8_t)(val - 30);
                            g_term.use_true_fg = 0;
                        } else if (val >= 40 && val <= 47) {
                            g_term.bg_idx = (uint8_t)(val - 40);
                            g_term.use_true_bg = 0;
                        } else if (val >= 90 && val <= 97) {
                            g_term.fg_idx = (uint8_t)(val - 90 + 8);
                            g_term.use_true_fg = 0;
                        } else if (val >= 100 && val <= 107) {
                            g_term.bg_idx = (uint8_t)(val - 100 + 8);
                            g_term.use_true_bg = 0;
                        } else if (val == 38 && p + 1 < param_count) {
                            /* 扩展前景色 */
                            p++;
                            if (params[p] == 5 && p + 1 < param_count) {
                                /* 256 色 */
                                p++;
                                int idx = params[p];
                                if (idx < 16) {
                                    g_term.fg_idx = (uint8_t)idx;
                                    g_term.use_true_fg = 0;
                                } else if (idx < 232) {
                                    /* 6x6x6 颜色立方体 */
                                    idx -= 16;
                                    uint8_t r = (uint8_t)((idx / 36) * 51);
                                    uint8_t g = (uint8_t)(((idx / 6) % 6) * 51);
                                    uint8_t b = (uint8_t)((idx % 6) * 51);
                                    g_term.fg_true = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
                                    g_term.use_true_fg = 1;
                                } else {
                                    /* 灰度 */
                                    uint8_t gray = (uint8_t)((idx - 232) * 10 + 8);
                                    g_term.fg_true = ((uint32_t)gray << 16) | ((uint32_t)gray << 8) | (uint32_t)gray;
                                    g_term.use_true_fg = 1;
                                }
                            } else if (params[p] == 2 && p + 3 < param_count) {
                                /* 24-bit 真彩色 */
                                p++;
                                uint32_t r = (uint32_t)(params[p] & 0xFF);
                                p++;
                                uint32_t g = (uint32_t)(params[p] & 0xFF);
                                p++;
                                uint32_t b = (uint32_t)(params[p] & 0xFF);
                                g_term.fg_true = (r << 16) | (g << 8) | b;
                                g_term.use_true_fg = 1;
                            }
                        } else if (val == 48 && p + 1 < param_count) {
                            /* 扩展背景色 */
                            p++;
                            if (params[p] == 5 && p + 1 < param_count) {
                                p++;
                                int idx = params[p];
                                if (idx < 16) {
                                    g_term.bg_idx = (uint8_t)idx;
                                    g_term.use_true_bg = 0;
                                } else if (idx < 232) {
                                    idx -= 16;
                                    uint8_t r = (uint8_t)((idx / 36) * 51);
                                    uint8_t g = (uint8_t)(((idx / 6) % 6) * 51);
                                    uint8_t b = (uint8_t)((idx % 6) * 51);
                                    g_term.bg_true = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
                                    g_term.use_true_bg = 1;
                                } else {
                                    uint8_t gray = (uint8_t)((idx - 232) * 10 + 8);
                                    g_term.bg_true = ((uint32_t)gray << 16) | ((uint32_t)gray << 8) | (uint32_t)gray;
                                    g_term.use_true_bg = 1;
                                }
                            } else if (params[p] == 2 && p + 3 < param_count) {
                                p++;
                                uint32_t r = (uint32_t)(params[p] & 0xFF);
                                p++;
                                uint32_t g = (uint32_t)(params[p] & 0xFF);
                                p++;
                                uint32_t b = (uint32_t)(params[p] & 0xFF);
                                g_term.bg_true = (r << 16) | (g << 8) | b;
                                g_term.use_true_bg = 1;
                            }
                        }
                        p++;
                    }
                }
                break;
            case 'h': /* 设置模式 */
            case 'l': /* 重置模式 */
                if (has_question && params[0] == 25) {
                    g_term.cursor_visible = (cmd == 'h') ? 1 : 0;
                }
                break;
            default:
                break;
            }
            return;
        }
        i++;
    }
}

/* ================================================================
 * 命令执行
 * ================================================================ */
static const char *g_builtin_cmds[] = {
    "help", "clear", "echo", "ls", "cat", "cd", "pwd", "date",
    "time", "uname", "ps", "kill", "mem", "reboot", "calc",
    "paint", "edit", "files", "settings", "about", "exit", "ver", NULL
};

static void term_execute_command(const char *cmd)
{
    /* 跳过前导空格 */
    while (*cmd == ' ') cmd++;
    if (cmd[0] == '\0') return;

    /* 提取命令名 */
    char cmd_name[64];
    int ci = 0;
    while (*cmd && *cmd != ' ' && ci < 63) {
        cmd_name[ci++] = *cmd++;
    }
    cmd_name[ci] = '\0';
    while (*cmd == ' ') cmd++;

    /* help */
    if (strcmp(cmd_name, "help") == 0) {
        terminal_write("FUNSOS Terminal - Built-in Commands:\n");
        terminal_write("  help        Show this help\n");
        terminal_write("  clear       Clear the screen\n");
        terminal_write("  echo [msg]  Display a message\n");
        terminal_write("  ls          List files\n");
        terminal_write("  cat <file>  Show file contents\n");
        terminal_write("  cd  <dir>   Change directory\n");
        terminal_write("  pwd         Print working directory\n");
        terminal_write("  date        Show date\n");
        terminal_write("  time        Show system uptime\n");
        terminal_write("  uname       Show system info\n");
        terminal_write("  ps          List processes\n");
        terminal_write("  kill <pid>  Terminate process\n");
        terminal_write("  mem         Show memory info\n");
        terminal_write("  reboot      Reboot system\n");
        terminal_write("  calc        Launch Calculator\n");
        terminal_write("  paint       Launch Paint\n");
        terminal_write("  edit        Launch Text Editor\n");
        terminal_write("  files       Launch File Manager\n");
        terminal_write("  settings    Launch Settings\n");
        terminal_write("  about       About FUNSOS\n");
        terminal_write("  exit        Exit terminal\n");
        terminal_write("  ver         Show version\n");
        return;
    }

    /* clear */
    if (strcmp(cmd_name, "clear") == 0) {
        term_clear_screen();
        return;
    }

    /* echo */
    if (strcmp(cmd_name, "echo") == 0) {
        terminal_write(cmd);
        terminal_write("\n");
        return;
    }

    /* exit */
    if (strcmp(cmd_name, "exit") == 0) {
        terminal_write("Goodbye!\n");
        g_term.running = 0;
        return;
    }

    /* 调用内核 Shell 执行命令 */
    shell_execute(cmd_name);
}

/* ================================================================
 * Tab 补全
 * ================================================================ */
static void term_tab_complete(void)
{
    if (g_term.input_len == 0) return;

    int matches[64];
    int match_count = 0;

    for (int i = 0; g_builtin_cmds[i]; i++) {
        if (strncmp(g_builtin_cmds[i], g_term.input_buf, g_term.input_len) == 0) {
            if (match_count < 64) {
                matches[match_count++] = i;
            }
        }
    }

    if (match_count == 0) return;

    if (match_count == 1) {
        const char *full = g_builtin_cmds[matches[0]];
        strcpy(g_term.input_buf, full);
        g_term.input_len = (uint32_t)strlen(full);
        g_term.input_cursor = g_term.input_len;
    } else {
        terminal_write("\n");
        for (int i = 0; i < match_count; i++) {
            terminal_write(g_builtin_cmds[matches[i]]);
            terminal_write("  ");
        }
        terminal_write("\n");
        terminal_write(g_term.prompt);
        terminal_write(g_term.input_buf);
    }
}

/* ================================================================
 * 命令历史
 * ================================================================ */
static void term_add_history(const char *cmd)
{
    if (cmd[0] == '\0') return;
    if (g_term.history_count > 0 &&
        strcmp(g_term.history[g_term.history_count - 1], cmd) == 0) {
        return;
    }
    if (g_term.history_count >= TERM_HISTORY_MAX) {
        for (uint32_t i = 1; i < TERM_HISTORY_MAX; i++) {
            strcpy(g_term.history[i - 1], g_term.history[i]);
        }
        g_term.history_count = TERM_HISTORY_MAX - 1;
    }
    strncpy(g_term.history[g_term.history_count], cmd, TERM_CMD_MAX_LEN - 1);
    g_term.history[g_term.history_count][TERM_CMD_MAX_LEN - 1] = '\0';
    g_term.history_count++;
}

static void term_scroll_back(int lines)
{
    if (lines > 0) {
        g_term.scrollback_offset += (uint32_t)lines;
        if (g_term.scrollback_offset > g_term.scrollback_count) {
            g_term.scrollback_offset = g_term.scrollback_count;
        }
    } else {
        int dec = -lines;
        if ((uint32_t)dec > g_term.scrollback_offset) {
            g_term.scrollback_offset = 0;
        } else {
            g_term.scrollback_offset -= (uint32_t)dec;
        }
    }
}

/* ================================================================
 * 输入处理
 * ================================================================ */
void terminal_handle_key(char c)
{
    /* 回车 */
    if (c == '\r' || c == '\n') {
        g_term.input_buf[g_term.input_len] = '\0';
        terminal_write("\n");
        if (g_term.input_len > 0) {
            term_add_history(g_term.input_buf);
            term_execute_command(g_term.input_buf);
        }
        terminal_write(g_term.prompt);
        g_term.input_len = 0;
        g_term.input_cursor = 0;
        g_term.history_pos = -1;
        return;
    }

    /* 退格 */
    if (c == 0x08 || c == 127) {
        if (g_term.input_len > 0 && g_term.input_cursor > 0) {
            g_term.input_cursor--;
            g_term.input_len--;
            for (uint32_t i = g_term.input_cursor; i < g_term.input_len; i++) {
                g_term.input_buf[i] = g_term.input_buf[i + 1];
            }
            if (g_term.cursor_x > 0) {
                g_term.cursor_x--;
                g_term.screen[g_term.cursor_y][g_term.cursor_x].ch = ' ';
            }
        }
        g_term.history_pos = -1;
        return;
    }

    /* Tab */
    if (c == 0x09) {
        term_tab_complete();
        return;
    }

    /* 可打印字符 */
    if ((unsigned char)c >= 0x20 && (unsigned char)c < 0x7F) {
        if (g_term.input_len < TERM_CMD_MAX_LEN - 1) {
            for (int32_t i = (int32_t)g_term.input_len; i > (int32_t)g_term.input_cursor; i--) {
                g_term.input_buf[i] = g_term.input_buf[i - 1];
            }
            g_term.input_buf[g_term.input_cursor] = c;
            g_term.input_cursor++;
            g_term.input_len++;
            term_put_char_raw(c);
        }
        g_term.history_pos = -1;
        return;
    }
}

void terminal_handle_special_key(uint32_t keycode)
{
    /* 上箭头 */
    if (keycode == 0x48 || keycode == 0xE048) {
        if (g_term.history_count > 0) {
            if (g_term.history_pos < 0) {
                g_term.history_pos = (int32_t)(g_term.history_count - 1);
            } else if (g_term.history_pos > 0) {
                g_term.history_pos--;
            }
            /* 清除当前输入行 */
            for (uint32_t i = 0; i < g_term.input_len; i++) {
                if (g_term.cursor_x > 0) {
                    g_term.cursor_x--;
                    g_term.screen[g_term.cursor_y][g_term.cursor_x].ch = ' ';
                }
            }
            strcpy(g_term.input_buf, g_term.history[g_term.history_pos]);
            g_term.input_len = (uint32_t)strlen(g_term.input_buf);
            g_term.input_cursor = g_term.input_len;
            terminal_write(g_term.input_buf);
        }
        return;
    }

    /* 下箭头 */
    if (keycode == 0x50 || keycode == 0xE050) {
        if (g_term.history_pos >= 0) {
            if (g_term.history_pos < (int32_t)(g_term.history_count - 1)) {
                g_term.history_pos++;
            } else {
                g_term.history_pos = -1;
            }
            for (uint32_t i = 0; i < g_term.input_len; i++) {
                if (g_term.cursor_x > 0) {
                    g_term.cursor_x--;
                    g_term.screen[g_term.cursor_y][g_term.cursor_x].ch = ' ';
                }
            }
            if (g_term.history_pos >= 0) {
                strcpy(g_term.input_buf, g_term.history[g_term.history_pos]);
                g_term.input_len = (uint32_t)strlen(g_term.input_buf);
                g_term.input_cursor = g_term.input_len;
                terminal_write(g_term.input_buf);
            } else {
                g_term.input_buf[0] = '\0';
                g_term.input_len = 0;
                g_term.input_cursor = 0;
            }
        }
        return;
    }

    /* 左箭头 */
    if (keycode == 0x4B || keycode == 0xE04B) {
        if (g_term.input_cursor > 0) {
            g_term.input_cursor--;
            if (g_term.cursor_x > 0) {
                g_term.cursor_x--;
            }
        }
        return;
    }

    /* 右箭头 */
    if (keycode == 0x4D || keycode == 0xE04D) {
        if (g_term.input_cursor < g_term.input_len) {
            g_term.input_cursor++;
            if (g_term.cursor_x < g_term.cols - 1) {
                g_term.cursor_x++;
            }
        }
        return;
    }

    /* Home */
    if (keycode == 0x47 || keycode == 0xE047) {
        g_term.input_cursor = 0;
        int32_t plen = (int32_t)strlen(g_term.prompt);
        g_term.cursor_x = (uint32_t)plen;
        return;
    }

    /* End */
    if (keycode == 0x4F || keycode == 0xE04F) {
        g_term.input_cursor = g_term.input_len;
        int32_t plen = (int32_t)strlen(g_term.prompt);
        g_term.cursor_x = (uint32_t)(plen + (int32_t)g_term.input_len);
        return;
    }

    /* PageUp */
    if (keycode == 0x49 || keycode == 0xE049) {
        term_scroll_back((int)g_term.rows);
        g_term.need_redraw = 1;
        return;
    }

    /* PageDown */
    if (keycode == 0x51 || keycode == 0xE051) {
        term_scroll_back(-((int)g_term.rows));
        g_term.need_redraw = 1;
        return;
    }

    /* Delete */
    if (keycode == 0x53 || keycode == 0xE053) {
        if (g_term.input_cursor < g_term.input_len) {
            for (uint32_t i = g_term.input_cursor; i < g_term.input_len - 1; i++) {
                g_term.input_buf[i] = g_term.input_buf[i + 1];
            }
            g_term.input_len--;
        }
        return;
    }
}

/* ================================================================
 * 鼠标/点击处理
 * ================================================================ */
void terminal_handle_click(int x, int y)
{
    (void)x;
    (void)y;

    /* 检查是否点击了滚动条 */
    int scrollbar_x = (int)g_term.win_w - 12;
    if (x >= scrollbar_x && x <= (int)g_term.win_w - 2) {
        /* 点击滚动条，计算滚动位置 */
        if (g_term.scrollback_count > 0) {
            int content_h = (int)g_term.win_h - 24;
            int sb_h = content_h - 4;
            if (sb_h > 0) {
                int rel_y = y - 24 - 2;
                if (rel_y < 0) rel_y = 0;
                if (rel_y > sb_h) rel_y = sb_h;
                g_term.scrollback_offset = (uint32_t)((uint64_t)rel_y * g_term.scrollback_count / (uint64_t)sb_h);
            }
        }
        g_term.need_redraw = 1;
        return;
    }

    /* 点击标题栏（关闭按钮区域） */
    if (y >= 0 && y < 24) {
        int close_x = (int)g_term.win_w - 24;
        if (x >= close_x) {
            g_term.running = 0;
        }
    }
}

void terminal_handle_resize(int w, int h)
{
    g_term.win_w = (uint32_t)w;
    g_term.win_h = (uint32_t)h;

    uint32_t new_cols = ((uint32_t)w - 16) / TERM_CHAR_W;
    uint32_t new_rows = ((uint32_t)h - 24) / TERM_CHAR_H;
    if (new_cols > TERM_MAX_COLS) new_cols = TERM_MAX_COLS;
    if (new_rows > TERM_MAX_ROWS) new_rows = TERM_MAX_ROWS;
    if (new_cols < 20) new_cols = 20;
    if (new_rows < 5)  new_rows = 5;

    g_term.cols = new_cols;
    g_term.rows = new_rows;

    if (g_term.cursor_x >= g_term.cols) g_term.cursor_x = g_term.cols - 1;
    if (g_term.cursor_y >= g_term.rows) g_term.cursor_y = g_term.rows - 1;
}

/* ================================================================
 * 渲染
 * ================================================================ */
void terminal_render(void)
{
    uint32_t wid = g_term.win_id;

    /* 清空窗口背景 */
    ds_draw_rect(wid, 0, 0, g_term.win_w, g_term.win_h, COLOR_BG_VAL);

    /* 渲染标题栏 */
    term_render_title_bar();

    /* 渲染内容 */
    term_render_content();

    /* 渲染滚动条 */
    term_render_scrollbar();

    /* 渲染光标 */
    term_render_cursor();
}

static void term_render_title_bar(void)
{
    uint32_t wid = g_term.win_id;

    /* 标题栏背景 */
    ds_draw_rect(wid, 0, 0, g_term.win_w, 24, COLOR_TITLE_BG);

    /* 标题文字 */
    ds_draw_text(wid, 8, 4, g_term.title, COLOR_TITLE_FG, COLOR_TITLE_BG);

    /* 关闭按钮 */
    int close_x = (int)g_term.win_w - 22;
    ds_draw_rect(wid, (uint32_t)close_x, 2, 18, 18, 0xCC4444);
    ds_draw_text(wid, (uint32_t)(close_x + 5), 3, "X", 0xFFFFFF, 0xCC4444);
}

static void term_render_content(void)
{
    uint32_t wid = g_term.win_id;
    int start_y = 24; /* 标题栏下方 */
    int render_y = start_y;

    /* 渲染滚动缓冲区内容 */
    if (g_term.scrollback_offset > 0) {
        uint32_t sb_show = g_term.scrollback_offset;
        if (sb_show > g_term.scrollback_count) sb_show = g_term.scrollback_count;
        int sb_start = (int)(g_term.scrollback_count - sb_show);

        for (int i = sb_start;
             i < (int)g_term.scrollback_count && render_y < (int)g_term.win_h;
             i++) {
            if (g_term.scrollback[i]) {
                /* 使用 ds_draw_text 绘制整行以提高性能 */
                char line[TERM_MAX_COLS + 1];
                for (uint32_t c = 0; c < g_term.cols; c++) {
                    line[c] = g_term.scrollback[i][c].ch;
                }
                line[g_term.cols] = '\0';
                ds_draw_text(wid, 4, (uint32_t)render_y, line, COLOR_FG_VAL, COLOR_BG_VAL);
                render_y += TERM_CHAR_H;
            }
        }
    }

    /* 渲染当前屏幕缓冲区 */
    for (uint32_t r = 0; r < g_term.rows && render_y < (int)g_term.win_h; r++) {
        char line[TERM_MAX_COLS + 1];
        for (uint32_t c = 0; c < g_term.cols; c++) {
            line[c] = g_term.screen[r][c].ch;
        }
        line[g_term.cols] = '\0';
        ds_draw_text(wid, 4, (uint32_t)render_y, line, COLOR_FG_VAL, COLOR_BG_VAL);
        render_y += TERM_CHAR_H;
    }
}

static void term_render_scrollbar(void)
{
    uint32_t wid = g_term.win_id;
    int sb_x = (int)g_term.win_w - 12;
    int content_h = (int)g_term.win_h - 24;

    if (content_h <= 0) return;

    /* 滚动条背景 */
    ds_draw_rect(wid, (uint32_t)sb_x, 24, 10, (uint32_t)content_h, COLOR_SCROLLBAR_BG);

    /* 滚动条滑块 */
    if (g_term.scrollback_count > 0) {
        uint32_t total = g_term.scrollback_count + g_term.rows;
        int sb_h = (int)((uint64_t)g_term.rows * (uint32_t)content_h / total);
        if (sb_h < 8) sb_h = 8;

        int sb_y = 24 + (int)((uint64_t)g_term.scrollback_offset * (uint32_t)(content_h - sb_h) / g_term.scrollback_count);
        if (sb_y < 24) sb_y = 24;
        if (sb_y + sb_h > 24 + content_h) sb_y = 24 + content_h - sb_h;

        ds_draw_rect(wid, (uint32_t)sb_x, (uint32_t)sb_y, 10, (uint32_t)sb_h, COLOR_SCROLLBAR_FG);
    }
}

static void term_render_cursor(void)
{
    /* 光标闪烁: 每 500ms 切换可见/不可见 */
    if (!g_term.cursor_visible) return;
    if (g_term.scrollback_offset > 0) return; /* 查看历史时不显示光标 */

    uint32_t blink_phase = (g_term.cursor_blink_tick / 250) % 2;
    if (blink_phase == 0) {
        uint32_t wid = g_term.win_id;
        int cx = 4 + (int)g_term.cursor_x * TERM_CHAR_W;
        int cy = 24 + (int)g_term.cursor_y * TERM_CHAR_H;
        ds_draw_rect(wid, (uint32_t)cx, (uint32_t)(cy + TERM_CHAR_H - 2),
                     TERM_CHAR_W, 2, COLOR_CURSOR_VAL);
    }
}