/* text_editor.c - FUNSOS 文本编辑器实现
 * 完整的多行文本编辑器，支持文件操作、查找替换、光标移动、
 * 行号显示、插入/覆盖模式等功能。
 */

#include "text_editor.h"
#include "sys_api.h"
#include "stddef.h"
#include "string.h"
#include "stdlib.h"

/* ================================================================
 * 配置常量
 * ================================================================ */
#define MAX_BUFFER_SIZE   65536       /* 最大文本缓冲区 64KB */
#define MAX_LINE_LEN      1024        /* 单行最大长度 */
#define MAX_LINES         2048        /* 最大行数 */
#define CHAR_W            8           /* 字符宽度 */
#define CHAR_H            16          /* 字符高度 */
#define LINE_NUM_W        40          /* 行号宽度 */
#define SCROLLBAR_W       14          /* 滚动条宽度 */
#define STATUSBAR_H       22          /* 状态栏高度 */
#define WIN_W             700         /* 窗口宽度 */
#define WIN_H             500         /* 窗口高度 */
#define CONTENT_START_Y   0           /* 内容起始 Y */
#define TEXT_START_X      LINE_NUM_W /* 文本起始 X */
#define TEXT_AREA_W       (WIN_W - LINE_NUM_W - SCROLLBAR_W) /* 文本区域宽度 */
#define TEXT_AREA_H       (WIN_H - STATUSBAR_H) /* 文本区域高度 */
#define MAX_VISIBLE_LINES (TEXT_AREA_H / CHAR_H) /* 最大可见行数 */

/* 编辑模式 */
#define MODE_INSERT  0
#define MODE_OVERWRITE 1

/* 对话框类型 */
#define DIALOG_NONE    0
#define DIALOG_FIND    1
#define DIALOG_REPLACE 2
#define DIALOG_GOTO    3
#define DIALOG_SAVE    4
#define DIALOG_OPEN    5
#define DIALOG_CONFIRM 6

/* ================================================================
 * 颜色定义
 * ================================================================ */
static const sys_color_t COLOR_BG         = { 0xFF, 0xFF, 0xFF, 0xFF }; /* 白色背景 */
static const sys_color_t COLOR_TEXT       = { 0x00, 0x00, 0x00, 0xFF }; /* 黑色文字 */
static const sys_color_t COLOR_LINE_NUM   = { 0x99, 0x99, 0x99, 0xFF }; /* 灰色行号 */
static const sys_color_t COLOR_LINE_BG    = { 0xF0, 0xF0, 0xF0, 0xFF }; /* 行号背景 */
static const sys_color_t COLOR_STATUSBAR  = { 0xE8, 0xE8, 0xE8, 0xFF }; /* 状态栏背景 */
static const sys_color_t COLOR_STATUSBAR_FG = { 0x33, 0x33, 0x33, 0xFF }; /* 状态栏文字 */
static const sys_color_t COLOR_CURSOR     = { 0x00, 0x00, 0x00, 0xFF }; /* 光标颜色 */
static const sys_color_t COLOR_SELECTION  = { 0xCC, 0xDD, 0xFF, 0xFF }; /* 选择背景 */
static const sys_color_t COLOR_SCROLLBAR  = { 0xCC, 0xCC, 0xCC, 0xFF }; /* 滚动条 */
static const sys_color_t COLOR_SCROLLBAR_BG = { 0xF0, 0xF0, 0xF0, 0xFF }; /* 滚动条背景 */
static const sys_color_t COLOR_DIALOG_BG  = { 0xEE, 0xEE, 0xEE, 0xFF }; /* 对话框背景 */
static const sys_color_t COLOR_DIALOG_BORDER = { 0x88, 0x88, 0x88, 0xFF }; /* 对话框边框 */
static const sys_color_t COLOR_HIGHLIGHT  = { 0xFF, 0xFF, 0x00, 0xFF }; /* 高亮 */

/* ================================================================
 * 编辑器状态
 * ================================================================ */
typedef struct {
    /* 文本缓冲区 */
    char    *buffer;           /* 文本缓冲区 */
    uint32_t buffer_len;       /* 缓冲区长度 */
    uint32_t buffer_capacity;  /* 缓冲区容量 */
    /* 行索引 */
    uint32_t *line_starts;     /* 每行起始偏移 */
    uint32_t line_count;       /* 行数 */
    /* 光标 */
    uint32_t cursor_pos;       /* 光标在缓冲区中的位置 */
    int32_t  cursor_line;      /* 光标所在行 */
    int32_t  cursor_col;       /* 光标所在列 */
    int32_t  preferred_col;    /* 首选列(上下移动时保持) */
    /* 滚动 */
    int32_t  scroll_y;         /* 垂直滚动偏移(行) */
    int32_t  scroll_x;         /* 水平滚动偏移(列) */
    /* 选择 */
    int32_t  sel_start;        /* 选择起始位置 */
    int32_t  sel_end;          /* 选择结束位置 */
    uint8_t  selecting;        /* 是否正在选择 */
    /* 模式 */
    uint8_t  mode;             /* 插入/覆盖模式 */
    uint8_t  modified;         /* 是否已修改 */
    uint8_t  word_wrap;        /* 自动换行 */
    /* 文件 */
    char     file_path[256];   /* 文件路径 */
    /* 对话框 */
    uint8_t  dialog_type;      /* 对话框类型 */
    char     dialog_buf[256];  /* 对话框输入缓冲区 */
    uint32_t dialog_len;       /* 对话框输入长度 */
    char     dialog_title[64]; /* 对话框标题 */
    /* 查找替换 */
    char     find_text[128];   /* 查找文本 */
    char     replace_text[128];/* 替换文本 */
    int32_t  last_find_pos;    /* 上次查找位置 */
    /* 剪贴板 */
    char     *clipboard;       /* 剪贴板缓冲区 */
    uint32_t clipboard_len;    /* 剪贴板长度 */
    /* 窗口 */
    sys_window_t *win;
} editor_state_t;

static editor_state_t g_ed;

/* ================================================================
 * 内部函数声明
 * ================================================================ */
static void ed_rebuild_lines(void);
static void ed_update_cursor_line_col(void);
static void ed_ensure_cursor_visible(void);
static void ed_insert_char(char c);
static void ed_delete_char(int backspace);
static void ed_insert_newline(void);
static void ed_move_cursor_home(void);
static void ed_move_cursor_end(void);
static void ed_move_cursor_page_up(void);
static void ed_move_cursor_page_down(void);
static void ed_move_cursor_ctrl_home(void);
static void ed_move_cursor_ctrl_end(void);
static void ed_select_all(void);
static void ed_copy_selection(void);
static void ed_cut_selection(void);
static void ed_paste_clipboard(void);
static int  ed_find_next(void);
static int  ed_replace_next(void);
static void ed_render(sys_window_t *win);
static void ed_render_text_area(sys_window_t *win);
static void ed_render_line_numbers(sys_window_t *win);
static void ed_render_scrollbar(sys_window_t *win);
static void ed_render_statusbar(sys_window_t *win);
static void ed_render_dialog(sys_window_t *win);
static void ed_render_cursor(sys_window_t *win);
static void ed_handle_key(sys_window_t *win, uint32_t key, uint32_t mod);
static void ed_handle_dialog_key(sys_window_t *win, uint32_t key);
static void ed_show_dialog(int type, const char *title);
static void ed_close_dialog(void);
static void ed_int_to_str(int n, char *buf);

/* ================================================================
 * 初始化和清理
 * ================================================================ */
int text_editor_init(void)
{
    memset(&g_ed, 0, sizeof(g_ed));
    g_ed.buffer_capacity = MAX_BUFFER_SIZE;
    g_ed.buffer = (char *)malloc(g_ed.buffer_capacity);
    if (g_ed.buffer == NULL) return -1;
    memset(g_ed.buffer, 0, g_ed.buffer_capacity);
    g_ed.buffer_len = 0;
    g_ed.line_starts = (uint32_t *)malloc(MAX_LINES * sizeof(uint32_t));
    if (g_ed.line_starts == NULL) {
        free(g_ed.buffer);
        g_ed.buffer = NULL;
        return -1;
    }
    g_ed.line_count = 0;
    g_ed.mode = MODE_INSERT;
    g_ed.word_wrap = 0;
    g_ed.clipboard = NULL;
    g_ed.clipboard_len = 0;
    g_ed.dialog_type = DIALOG_NONE;
    text_editor_new();
    return 0;
}

void text_editor_new(void)
{
    g_ed.buffer[0] = '\0';
    g_ed.buffer_len = 0;
    g_ed.cursor_pos = 0;
    g_ed.cursor_line = 0;
    g_ed.cursor_col = 0;
    g_ed.preferred_col = 0;
    g_ed.scroll_y = 0;
    g_ed.scroll_x = 0;
    g_ed.sel_start = -1;
    g_ed.sel_end = -1;
    g_ed.selecting = 0;
    g_ed.modified = 0;
    g_ed.file_path[0] = '\0';
    g_ed.dialog_type = DIALOG_NONE;
    g_ed.last_find_pos = -1;
    ed_rebuild_lines();
}

/* ================================================================
 * 文件操作
 * ================================================================ */
int text_editor_open(const char *path)
{
    if (path == NULL) return -1;
    int fd = sys_file_open(path, 0);
    if (fd < 0) return -1;

    g_ed.buffer_len = 0;
    char buf[512];
    int n;
    while ((n = sys_file_read(fd, buf, sizeof(buf))) > 0) {
        if (g_ed.buffer_len + (uint32_t)n >= g_ed.buffer_capacity) break;
        memcpy(g_ed.buffer + g_ed.buffer_len, buf, (uint32_t)n);
        g_ed.buffer_len += (uint32_t)n;
    }
    g_ed.buffer[g_ed.buffer_len] = '\0';
    sys_file_close(fd);

    /* 复制文件路径 */
    strncpy(g_ed.file_path, path, 255);
    g_ed.file_path[255] = '\0';

    g_ed.cursor_pos = 0;
    g_ed.cursor_line = 0;
    g_ed.cursor_col = 0;
    g_ed.scroll_y = 0;
    g_ed.scroll_x = 0;
    g_ed.modified = 0;
    g_ed.sel_start = -1;
    g_ed.sel_end = -1;
    ed_rebuild_lines();
    return 0;
}

int text_editor_save(const char *path)
{
    const char *save_path = path ? path : g_ed.file_path;
    if (save_path[0] == '\0') return -1;

    int fd = sys_file_open(save_path, 1); /* 写入模式 */
    if (fd < 0) return -1;

    sys_file_write(fd, g_ed.buffer, g_ed.buffer_len);
    sys_file_close(fd);

    g_ed.modified = 0;
    if (path) {
        strncpy(g_ed.file_path, path, 255);
        g_ed.file_path[255] = '\0';
    }
    return 0;
}

/* ================================================================
 * 行索引重建
 * ================================================================ */
static void ed_rebuild_lines(void)
{
    g_ed.line_count = 0;
    g_ed.line_starts[0] = 0;
    g_ed.line_count = 1;

    for (uint32_t i = 0; i < g_ed.buffer_len && g_ed.line_count < MAX_LINES; i++) {
        if (g_ed.buffer[i] == '\n') {
            g_ed.line_starts[g_ed.line_count++] = i + 1;
        }
    }
}

static void ed_update_cursor_line_col(void)
{
    /* 从 cursor_pos 计算行和列 */
    g_ed.cursor_line = 0;
    g_ed.cursor_col = 0;
    for (uint32_t i = 0; i < g_ed.cursor_pos; i++) {
        if (g_ed.buffer[i] == '\n') {
            g_ed.cursor_line++;
            g_ed.cursor_col = 0;
        } else {
            g_ed.cursor_col++;
        }
    }
}

static void ed_ensure_cursor_visible(void)
{
    /* 确保光标在可见区域内 */
    if (g_ed.cursor_line < g_ed.scroll_y) {
        g_ed.scroll_y = g_ed.cursor_line;
    }
    if (g_ed.cursor_line >= g_ed.scroll_y + (int32_t)MAX_VISIBLE_LINES) {
        g_ed.scroll_y = g_ed.cursor_line - (int32_t)MAX_VISIBLE_LINES + 1;
    }
    if (g_ed.scroll_y < 0) g_ed.scroll_y = 0;
}

/* ================================================================
 * 文本编辑操作
 * ================================================================ */
static void ed_insert_char(char c)
{
    if (g_ed.buffer_len >= g_ed.buffer_capacity - 1) return;

    if (g_ed.mode == MODE_INSERT) {
        /* 插入模式：移动后续字符 */
        for (uint32_t i = g_ed.buffer_len; i > g_ed.cursor_pos; i--) {
            g_ed.buffer[i] = g_ed.buffer[i - 1];
        }
        g_ed.buffer[g_ed.cursor_pos] = c;
        g_ed.cursor_pos++;
        g_ed.buffer_len++;
    } else {
        /* 覆盖模式：替换当前字符 */
        if (g_ed.cursor_pos >= g_ed.buffer_len) {
            g_ed.buffer[g_ed.cursor_pos] = c;
            g_ed.cursor_pos++;
            g_ed.buffer_len++;
        } else {
            if (g_ed.buffer[g_ed.cursor_pos] == '\n') {
                /* 在换行符位置，插入而不是覆盖 */
                for (uint32_t i = g_ed.buffer_len; i > g_ed.cursor_pos; i--) {
                    g_ed.buffer[i] = g_ed.buffer[i - 1];
                }
                g_ed.buffer[g_ed.cursor_pos] = c;
                g_ed.buffer_len++;
            } else {
                g_ed.buffer[g_ed.cursor_pos] = c;
            }
            g_ed.cursor_pos++;
        }
    }
    g_ed.buffer[g_ed.buffer_len] = '\0';
    g_ed.modified = 1;
    g_ed.preferred_col = g_ed.cursor_col + 1;
    ed_rebuild_lines();
    ed_update_cursor_line_col();
    ed_ensure_cursor_visible();
}

static void ed_delete_char(int backspace)
{
    if (backspace) {
        /* 退格：删除光标前的字符 */
        if (g_ed.cursor_pos == 0) return;
        g_ed.cursor_pos--;
        for (uint32_t i = g_ed.cursor_pos; i < g_ed.buffer_len - 1; i++) {
            g_ed.buffer[i] = g_ed.buffer[i + 1];
        }
        g_ed.buffer_len--;
    } else {
        /* Delete：删除光标处的字符 */
        if (g_ed.cursor_pos >= g_ed.buffer_len) return;
        for (uint32_t i = g_ed.cursor_pos; i < g_ed.buffer_len - 1; i++) {
            g_ed.buffer[i] = g_ed.buffer[i + 1];
        }
        g_ed.buffer_len--;
    }
    g_ed.buffer[g_ed.buffer_len] = '\0';
    g_ed.modified = 1;
    g_ed.sel_start = -1;
    g_ed.sel_end = -1;
    ed_rebuild_lines();
    ed_update_cursor_line_col();
    ed_ensure_cursor_visible();
}

static void ed_insert_newline(void)
{
    if (g_ed.buffer_len >= g_ed.buffer_capacity - 1) return;
    for (uint32_t i = g_ed.buffer_len; i > g_ed.cursor_pos; i--) {
        g_ed.buffer[i] = g_ed.buffer[i - 1];
    }
    g_ed.buffer[g_ed.cursor_pos] = '\n';
    g_ed.cursor_pos++;
    g_ed.buffer_len++;
    g_ed.buffer[g_ed.buffer_len] = '\0';
    g_ed.modified = 1;
    g_ed.cursor_col = 0;
    g_ed.preferred_col = 0;
    ed_rebuild_lines();
    ed_update_cursor_line_col();
    ed_ensure_cursor_visible();
}

/* ================================================================
 * 光标移动
 * ================================================================ */
static void ed_move_cursor_home(void)
{
    /* 移到行首 */
    while (g_ed.cursor_pos > 0 && g_ed.buffer[g_ed.cursor_pos - 1] != '\n') {
        g_ed.cursor_pos--;
    }
    g_ed.preferred_col = 0;
    ed_update_cursor_line_col();
    ed_ensure_cursor_visible();
}

static void ed_move_cursor_end(void)
{
    /* 移到行尾 */
    while (g_ed.cursor_pos < g_ed.buffer_len && g_ed.buffer[g_ed.cursor_pos] != '\n') {
        g_ed.cursor_pos++;
    }
    g_ed.preferred_col = g_ed.cursor_col;
    ed_update_cursor_line_col();
    ed_ensure_cursor_visible();
}

static void ed_move_cursor_page_up(void)
{
    for (int i = 0; i < (int)MAX_VISIBLE_LINES; i++) {
        if (g_ed.cursor_pos == 0) break;
        g_ed.cursor_pos--;
        if (g_ed.buffer[g_ed.cursor_pos] == '\n') {
            /* 找到上一行开头 */
        }
    }
    /* 移到行首 */
    while (g_ed.cursor_pos > 0 && g_ed.buffer[g_ed.cursor_pos - 1] != '\n') {
        g_ed.cursor_pos--;
    }
    ed_update_cursor_line_col();
    ed_ensure_cursor_visible();
}

static void ed_move_cursor_page_down(void)
{
    for (int i = 0; i < (int)MAX_VISIBLE_LINES; i++) {
        if (g_ed.cursor_pos >= g_ed.buffer_len) break;
        if (g_ed.buffer[g_ed.cursor_pos] == '\n') {
            g_ed.cursor_pos++;
        } else {
            g_ed.cursor_pos++;
        }
    }
    if (g_ed.cursor_pos > g_ed.buffer_len) g_ed.cursor_pos = g_ed.buffer_len;
    ed_update_cursor_line_col();
    ed_ensure_cursor_visible();
}

static void ed_move_cursor_ctrl_home(void)
{
    g_ed.cursor_pos = 0;
    g_ed.cursor_line = 0;
    g_ed.cursor_col = 0;
    g_ed.preferred_col = 0;
    g_ed.scroll_y = 0;
}

static void ed_move_cursor_ctrl_end(void)
{
    g_ed.cursor_pos = g_ed.buffer_len;
    ed_update_cursor_line_col();
    ed_ensure_cursor_visible();
}

/* ================================================================
 * 选择、复制、剪切、粘贴
 * ================================================================ */
static void ed_select_all(void)
{
    g_ed.sel_start = 0;
    g_ed.sel_end = (int32_t)g_ed.buffer_len;
}

static void ed_copy_selection(void)
{
    if (g_ed.sel_start < 0 || g_ed.sel_end < 0) return;
    int32_t start = g_ed.sel_start;
    int32_t end = g_ed.sel_end;
    if (start > end) { int32_t t = start; start = end; end = t; }
    if (start == end) return;

    uint32_t len = (uint32_t)(end - start);
    if (g_ed.clipboard) free(g_ed.clipboard);
    g_ed.clipboard = (char *)malloc(len + 1);
    if (g_ed.clipboard) {
        memcpy(g_ed.clipboard, g_ed.buffer + start, len);
        g_ed.clipboard[len] = '\0';
        g_ed.clipboard_len = len;
    }
}

static void ed_cut_selection(void)
{
    if (g_ed.sel_start < 0 || g_ed.sel_end < 0) return;
    int32_t start = g_ed.sel_start;
    int32_t end = g_ed.sel_end;
    if (start > end) { int32_t t = start; start = end; end = t; }
    if (start == end) return;

    ed_copy_selection();

    /* 删除选中文本 */
    uint32_t del_len = (uint32_t)(end - start);
    for (uint32_t i = (uint32_t)start; i < g_ed.buffer_len - del_len; i++) {
        g_ed.buffer[i] = g_ed.buffer[i + del_len];
    }
    g_ed.buffer_len -= del_len;
    g_ed.buffer[g_ed.buffer_len] = '\0';
    g_ed.cursor_pos = (uint32_t)start;
    g_ed.modified = 1;
    g_ed.sel_start = -1;
    g_ed.sel_end = -1;
    ed_rebuild_lines();
    ed_update_cursor_line_col();
    ed_ensure_cursor_visible();
}

static void ed_paste_clipboard(void)
{
    if (g_ed.clipboard == NULL || g_ed.clipboard_len == 0) return;
    if (g_ed.buffer_len + g_ed.clipboard_len >= g_ed.buffer_capacity) return;

    /* 插入剪贴板内容 */
    for (int32_t i = (int32_t)g_ed.buffer_len; i >= (int32_t)g_ed.cursor_pos; i--) {
        g_ed.buffer[i + g_ed.clipboard_len] = g_ed.buffer[i];
    }
    memcpy(g_ed.buffer + g_ed.cursor_pos, g_ed.clipboard, g_ed.clipboard_len);
    g_ed.buffer_len += g_ed.clipboard_len;
    g_ed.cursor_pos += g_ed.clipboard_len;
    g_ed.buffer[g_ed.buffer_len] = '\0';
    g_ed.modified = 1;
    ed_rebuild_lines();
    ed_update_cursor_line_col();
    ed_ensure_cursor_visible();
}

/* ================================================================
 * 查找和替换
 * ================================================================ */
static int ed_find_next(void)
{
    if (g_ed.find_text[0] == '\0') return -1;
    int32_t search_from = g_ed.last_find_pos;
    if (search_from < 0) search_from = 0;

    uint32_t find_len = (uint32_t)strlen(g_ed.find_text);
    for (uint32_t i = (uint32_t)search_from; i < g_ed.buffer_len; i++) {
        if (i + find_len > g_ed.buffer_len) break;
        if (memcmp(g_ed.buffer + i, g_ed.find_text, find_len) == 0) {
            g_ed.sel_start = (int32_t)i;
            g_ed.sel_end = (int32_t)(i + find_len);
            g_ed.cursor_pos = (uint32_t)g_ed.sel_end;
            g_ed.last_find_pos = (int32_t)(i + find_len);
            ed_update_cursor_line_col();
            ed_ensure_cursor_visible();
            return 0;
        }
    }
    /* 回到开头重新搜索 */
    g_ed.last_find_pos = -1;
    return -1;
}

static int ed_replace_next(void)
{
    if (g_ed.find_text[0] == '\0') return -1;
    if (g_ed.sel_start < 0 || g_ed.sel_end < 0) return -1;

    int32_t start = g_ed.sel_start;
    int32_t end = g_ed.sel_end;
    if (start > end) { int32_t t = start; start = end; end = t; }

    uint32_t find_len = (uint32_t)strlen(g_ed.find_text);
    uint32_t replace_len = (uint32_t)strlen(g_ed.replace_text);
    uint32_t old_len = (uint32_t)(end - start);

    if (replace_len > old_len) {
        int32_t diff = (int32_t)(replace_len - old_len);
        if (g_ed.buffer_len + (uint32_t)diff >= g_ed.buffer_capacity) return -1;
        for (int32_t i = (int32_t)g_ed.buffer_len; i >= end; i--) {
            g_ed.buffer[i + diff] = g_ed.buffer[i];
        }
        g_ed.buffer_len += (uint32_t)diff;
    } else if (replace_len < old_len) {
        uint32_t diff = old_len - replace_len;
        for (uint32_t i = (uint32_t)end; i < g_ed.buffer_len; i++) {
            g_ed.buffer[i - diff] = g_ed.buffer[i];
        }
        g_ed.buffer_len -= diff;
    }

    memcpy(g_ed.buffer + start, g_ed.replace_text, replace_len);
    g_ed.buffer[g_ed.buffer_len] = '\0';
    g_ed.cursor_pos = (uint32_t)(start + replace_len);
    g_ed.modified = 1;
    g_ed.sel_start = -1;
    g_ed.sel_end = -1;
    g_ed.last_find_pos = (int32_t)(start + replace_len);
    ed_rebuild_lines();
    ed_update_cursor_line_col();
    ed_ensure_cursor_visible();

    /* 继续查找下一个 */
    return ed_find_next();
}

/* ================================================================
 * 渲染函数
 * ================================================================ */
static void ed_render(sys_window_t *win)
{
    sys_fill_window(win, COLOR_BG);
    ed_render_line_numbers(win);
    ed_render_text_area(win);
    ed_render_scrollbar(win);
    ed_render_statusbar(win);
    ed_render_cursor(win);
    if (g_ed.dialog_type != DIALOG_NONE) {
        ed_render_dialog(win);
    }
}

static void ed_render_line_numbers(sys_window_t *win)
{
    sys_draw_rect(win, 0, 0, LINE_NUM_W - 2, WIN_H, COLOR_LINE_BG);

    for (int32_t i = 0; i < (int32_t)MAX_VISIBLE_LINES; i++) {
        int32_t line_num = g_ed.scroll_y + i + 1;
        if (line_num < 1 || (uint32_t)line_num > g_ed.line_count) break;

        char num_buf[8];
        num_buf[0] = '\0';
        ed_int_to_str(line_num, num_buf);

        int num_y = (int)(i * CHAR_H + 2);
        /* 右对齐 */
        int len = (int)strlen(num_buf);
        int num_x = LINE_NUM_W - 8 - len * CHAR_W;
        if (num_x < 2) num_x = 2;

        sys_color_t num_color = COLOR_LINE_NUM;
        if (line_num - 1 == g_ed.cursor_line) {
            num_color = COLOR_TEXT;
        }
        sys_draw_text(win, num_x, num_y, num_buf, num_color);
    }
}

static void ed_render_text_area(sys_window_t *win)
{
    /* 从缓冲区提取并绘制可见行 */
    uint32_t current_line = 0;
    uint32_t line_start = 0;

    for (uint32_t i = 0; i <= g_ed.buffer_len; i++) {
        if (i == g_ed.buffer_len || g_ed.buffer[i] == '\n') {
            if (current_line >= (uint32_t)g_ed.scroll_y &&
                current_line < (uint32_t)(g_ed.scroll_y + (int32_t)MAX_VISIBLE_LINES)) {

                int32_t render_y = (int32_t)((current_line - (uint32_t)g_ed.scroll_y) * CHAR_H + 2);
                uint32_t line_len = i - line_start;

                /* 水平滚动 */
                uint32_t skip = (uint32_t)(g_ed.scroll_x > 0 ? g_ed.scroll_x : 0);
                if (skip < line_len) {
                    uint32_t draw_len = line_len - skip;
                    uint32_t max_chars = TEXT_AREA_W / CHAR_W;
                    if (draw_len > max_chars) draw_len = max_chars;

                    char line_buf[MAX_LINE_LEN];
                    for (uint32_t c = 0; c < draw_len && c < MAX_LINE_LEN - 1; c++) {
                        char ch = g_ed.buffer[line_start + skip + c];
                        line_buf[c] = (ch >= 0x20 && ch < 0x7F) ? ch : ' ';
                    }
                    line_buf[draw_len] = '\0';

                    /* 绘制选择高亮 */
                    int32_t sel_start = g_ed.sel_start;
                    int32_t sel_end = g_ed.sel_end;
                    if (sel_start > sel_end) { int32_t t = sel_start; sel_start = sel_end; sel_end = t; }

                    sys_draw_text(win, TEXT_START_X, render_y, line_buf, COLOR_TEXT);

                    /* 高亮选择区域 */
                    if (sel_start >= 0 && sel_end > sel_start) {
                        int32_t line_start_abs = (int32_t)line_start + (int32_t)skip;
                        int32_t line_end_abs = (int32_t)(line_start + line_len);
                        if (sel_end > line_start_abs && sel_start < line_end_abs) {
                            int32_t highlight_start = (sel_start > line_start_abs) ? sel_start : line_start_abs;
                            int32_t highlight_end = (sel_end < line_end_abs) ? sel_end : line_end_abs;
                            if (highlight_end > highlight_start) {
                                int32_t hx = TEXT_START_X + (int32_t)((uint32_t)(highlight_start - line_start_abs) * CHAR_W);
                                int32_t hw = (int32_t)((uint32_t)(highlight_end - highlight_start) * CHAR_W);
                                sys_draw_rect(win, hx, render_y, hw, CHAR_H, COLOR_SELECTION);
                                /* 重新绘制选择区域的文字 */
                                char sel_buf[MAX_LINE_LEN];
                                for (int32_t c = 0; c < highlight_end - highlight_start && c < MAX_LINE_LEN - 1; c++) {
                                    sel_buf[c] = g_ed.buffer[highlight_start + c];
                                }
                                sel_buf[highlight_end - highlight_start] = '\0';
                                sys_draw_text(win, hx, render_y, sel_buf, COLOR_TEXT);
                            }
                        }
                    }
                }
            }
            current_line++;
            line_start = i + 1;
        }
        if (current_line >= (uint32_t)(g_ed.scroll_y + (int32_t)MAX_VISIBLE_LINES)) break;
    }
}

static void ed_render_scrollbar(sys_window_t *win)
{
    int sx = WIN_W - SCROLLBAR_W;
    sys_draw_rect(win, sx, 0, SCROLLBAR_W, TEXT_AREA_H, COLOR_SCROLLBAR_BG);

    if (g_ed.line_count > 0) {
        int thumb_h = (int)((int64_t)TEXT_AREA_H * MAX_VISIBLE_LINES / g_ed.line_count);
        if (thumb_h < 8) thumb_h = 8;
        int thumb_y = 0;
        if (g_ed.line_count > MAX_VISIBLE_LINES) {
            thumb_y = (int)((int64_t)g_ed.scroll_y * TEXT_AREA_H / g_ed.line_count);
        }
        sys_draw_rect(win, sx + 2, thumb_y, SCROLLBAR_W - 4, thumb_h, COLOR_SCROLLBAR);
    }
}

static void ed_render_statusbar(sys_window_t *win)
{
    int sy = TEXT_AREA_H;
    sys_draw_rect(win, 0, sy, WIN_W, STATUSBAR_H, COLOR_STATUSBAR);

    char status[256];
    int pos = 0;

    /* 文件名 */
    if (g_ed.file_path[0]) {
        const char *fn = g_ed.file_path;
        /* 获取文件名 */
        const char *last_sep = NULL;
        for (const char *p = fn; *p; p++) {
            if (*p == '/') last_sep = p;
        }
        if (last_sep) fn = last_sep + 1;
        while (*fn && pos < 60) status[pos++] = *fn++;
    } else {
        const char *untitled = "Untitled";
        while (*untitled && pos < 60) status[pos++] = *untitled++;
    }
    status[pos++] = ' ';

    /* 修改指示器 */
    if (g_ed.modified) status[pos++] = '*';
    status[pos++] = ' ';
    status[pos++] = '|';
    status[pos++] = ' ';

    /* 行:列 */
    status[pos++] = 'L'; status[pos++] = 'n'; status[pos++] = ' ';
    char nbuf[16];
    ed_int_to_str(g_ed.cursor_line + 1, nbuf);
    for (int i = 0; nbuf[i]; i++) status[pos++] = nbuf[i];
    status[pos++] = ',';
    status[pos++] = ' ';
    status[pos++] = 'C'; status[pos++] = 'o'; status[pos++] = 'l'; status[pos++] = ' ';
    ed_int_to_str(g_ed.cursor_col + 1, nbuf);
    for (int i = 0; nbuf[i]; i++) status[pos++] = nbuf[i];
    status[pos++] = ' ';
    status[pos++] = '|';
    status[pos++] = ' ';

    /* 模式 */
    if (g_ed.mode == MODE_INSERT) {
        const char *ins = "INSERT";
        while (*ins && pos < 250) status[pos++] = *ins++;
    } else {
        const char *ovr = "OVERWRITE";
        while (*ovr && pos < 250) status[pos++] = *ovr++;
    }

    status[pos++] = ' ';
    status[pos++] = '|';
    status[pos++] = ' ';

    /* 总行数 */
    status[pos++] = 'L'; status[pos++] = 'i'; status[pos++] = 'n'; status[pos++] = 'e'; status[pos++] = 's'; status[pos++] = ':'; status[pos++] = ' ';
    ed_int_to_str((int)g_ed.line_count, nbuf);
    for (int i = 0; nbuf[i]; i++) status[pos++] = nbuf[i];

    status[pos] = '\0';
    sys_draw_text(win, 4, sy + 3, status, COLOR_STATUSBAR_FG);
}

static void ed_render_cursor(sys_window_t *win)
{
    if (g_ed.dialog_type != DIALOG_NONE) return;

    int cx = TEXT_START_X + (g_ed.cursor_col - g_ed.scroll_x) * CHAR_W;
    int cy = (int)((g_ed.cursor_line - g_ed.scroll_y) * CHAR_H + 2);

    if (g_ed.mode == MODE_INSERT) {
        /* 插入模式：竖线光标 */
        sys_draw_rect(win, cx, cy, 2, CHAR_H, COLOR_CURSOR);
    } else {
        /* 覆盖模式：下划线光标 */
        sys_draw_rect(win, cx, cy + CHAR_H - 2, CHAR_W, 2, COLOR_CURSOR);
    }
}

static void ed_render_dialog(sys_window_t *win)
{
    if (g_ed.dialog_type == DIALOG_NONE) return;

    /* 对话框背景 */
    int dx = 60;
    int dy = 200;
    int dw = WIN_W - 120;
    int dh = 80;
    sys_draw_rect(win, dx, dy, dw, dh, COLOR_DIALOG_BG);
    sys_draw_rect(win, dx, dy, dw, 1, COLOR_DIALOG_BORDER);
    sys_draw_rect(win, dx, dy + dh - 1, dw, 1, COLOR_DIALOG_BORDER);
    sys_draw_rect(win, dx, dy, 1, dh, COLOR_DIALOG_BORDER);
    sys_draw_rect(win, dx + dw - 1, dy, 1, dh, COLOR_DIALOG_BORDER);

    /* 标题 */
    sys_draw_text(win, dx + 8, dy + 4, g_ed.dialog_title, COLOR_TEXT);

    /* 输入框 */
    sys_draw_rect(win, dx + 8, dy + 28, dw - 16, 20, COLOR_BG);

    /* 输入文本 */
    char input_text[256];
    memcpy(input_text, g_ed.dialog_buf, g_ed.dialog_len);
    input_text[g_ed.dialog_len] = '\0';
    sys_draw_text(win, dx + 12, dy + 30, input_text, COLOR_TEXT);

    /* 提示 */
    const char *hint = "";
    if (g_ed.dialog_type == DIALOG_FIND) hint = "Enter: Find  Esc: Cancel";
    else if (g_ed.dialog_type == DIALOG_REPLACE) hint = "Enter: Replace  Esc: Cancel";
    else if (g_ed.dialog_type == DIALOG_SAVE) hint = "Enter: Save  Esc: Cancel";
    else if (g_ed.dialog_type == DIALOG_OPEN) hint = "Enter: Open  Esc: Cancel";
    else if (g_ed.dialog_type == DIALOG_GOTO) hint = "Enter: Go  Esc: Cancel";
    sys_draw_text(win, dx + 8, dy + 56, hint, COLOR_LINE_NUM);
}

/* ================================================================
 * 对话框管理
 * ================================================================ */
static void ed_show_dialog(int type, const char *title)
{
    g_ed.dialog_type = (uint8_t)type;
    strncpy(g_ed.dialog_title, title, 63);
    g_ed.dialog_title[63] = '\0';
    g_ed.dialog_len = 0;
    g_ed.dialog_buf[0] = '\0';
}

static void ed_close_dialog(void)
{
    g_ed.dialog_type = DIALOG_NONE;
    g_ed.dialog_len = 0;
    g_ed.dialog_buf[0] = '\0';
}

static void ed_handle_dialog_key(sys_window_t *win, uint32_t key)
{
    (void)win;

    if (key == 27) { /* ESC */
        ed_close_dialog();
        return;
    }

    if (key == 0x0D || key == '\r') { /* Enter */
        g_ed.dialog_buf[g_ed.dialog_len] = '\0';

        switch (g_ed.dialog_type) {
        case DIALOG_FIND:
            strncpy(g_ed.find_text, g_ed.dialog_buf, 127);
            g_ed.find_text[127] = '\0';
            g_ed.last_find_pos = -1;
            ed_find_next();
            break;
        case DIALOG_REPLACE:
            strncpy(g_ed.replace_text, g_ed.dialog_buf, 127);
            g_ed.replace_text[127] = '\0';
            ed_replace_next();
            break;
        case DIALOG_SAVE:
            text_editor_save(g_ed.dialog_buf);
            break;
        case DIALOG_OPEN:
            text_editor_open(g_ed.dialog_buf);
            break;
        case DIALOG_GOTO: {
            int line = atoi(g_ed.dialog_buf) - 1;
            if (line >= 0 && (uint32_t)line < g_ed.line_count) {
                g_ed.cursor_pos = g_ed.line_starts[line];
                ed_update_cursor_line_col();
                ed_ensure_cursor_visible();
            }
            break;
        }
        default:
            break;
        }
        ed_close_dialog();
        return;
    }

    if (key == 0x08) { /* Backspace */
        if (g_ed.dialog_len > 0) g_ed.dialog_len--;
        g_ed.dialog_buf[g_ed.dialog_len] = '\0';
        return;
    }

    if (key >= 0x20 && key < 0x7F) {
        if (g_ed.dialog_len < 255) {
            g_ed.dialog_buf[g_ed.dialog_len++] = (char)key;
            g_ed.dialog_buf[g_ed.dialog_len] = '\0';
        }
        return;
    }
}

/* ================================================================
 * 键盘事件处理
 * ================================================================ */
static void ed_handle_key(sys_window_t *win, uint32_t key, uint32_t mod)
{
    /* 对话框模式下优先处理对话框 */
    if (g_ed.dialog_type != DIALOG_NONE) {
        ed_handle_dialog_key(win, key);
        return;
    }

    /* Ctrl 组合键 */
    if (mod == 1) { /* Ctrl 按下 */
        switch (key) {
        case 's': case 'S': /* Ctrl+S: 保存 */
            if (g_ed.file_path[0]) {
                text_editor_save(NULL);
            } else {
                ed_show_dialog(DIALOG_SAVE, "Save As:");
            }
            return;
        case 'o': case 'O': /* Ctrl+O: 打开 */
            ed_show_dialog(DIALOG_OPEN, "Open File:");
            return;
        case 'n': case 'N': /* Ctrl+N: 新建 */
            if (g_ed.modified) {
                /* 简化：直接新建 */
                text_editor_new();
            } else {
                text_editor_new();
            }
            return;
        case 'q': case 'Q': /* Ctrl+Q: 退出 */
            if (g_ed.win) {
                sys_destroy_window(g_ed.win);
            }
            return;
        case 'f': case 'F': /* Ctrl+F: 查找 */
            ed_show_dialog(DIALOG_FIND, "Find:");
            return;
        case 'h': case 'H': /* Ctrl+H: 替换 */
            ed_show_dialog(DIALOG_REPLACE, "Replace with:");
            return;
        case 'a': case 'A': /* Ctrl+A: 全选 */
            ed_select_all();
            return;
        case 'c': case 'C': /* Ctrl+C: 复制 */
            ed_copy_selection();
            return;
        case 'x': case 'X': /* Ctrl+X: 剪切 */
            ed_cut_selection();
            return;
        case 'v': case 'V': /* Ctrl+V: 粘贴 */
            ed_paste_clipboard();
            return;
        case 'g': case 'G': /* Ctrl+G: 跳转 */
            ed_show_dialog(DIALOG_GOTO, "Go to line:");
            return;
        case 'z': case 'Z': /* Ctrl+Z: 撤销 (简化: 不做) */
            return;
        case 'y': case 'Y': /* Ctrl+Y: 重做 (简化: 不做) */
            return;
        default:
            break;
        }
    }

    /* 特殊键 */
    if (key == 0xE0 || key == 0x00) {
        /* 扩展键前缀，通过 param2 传递 */
        uint32_t ext_key = mod;
        switch (ext_key) {
        case 0x48: /* 上箭头 */
            if (g_ed.cursor_line > 0) {
                g_ed.cursor_pos = g_ed.line_starts[g_ed.cursor_line - 1];
                ed_update_cursor_line_col();
                /* 移动到首选列 */
                for (uint32_t i = 0; i < (uint32_t)g_ed.preferred_col && g_ed.cursor_pos < g_ed.buffer_len; i++) {
                    if (g_ed.buffer[g_ed.cursor_pos] == '\n') break;
                    g_ed.cursor_pos++;
                }
                ed_update_cursor_line_col();
                ed_ensure_cursor_visible();
            }
            return;
        case 0x50: /* 下箭头 */
            if (g_ed.cursor_line < (int32_t)g_ed.line_count - 1) {
                g_ed.cursor_pos = g_ed.line_starts[g_ed.cursor_line + 1];
                ed_update_cursor_line_col();
                for (uint32_t i = 0; i < (uint32_t)g_ed.preferred_col && g_ed.cursor_pos < g_ed.buffer_len; i++) {
                    if (g_ed.buffer[g_ed.cursor_pos] == '\n') break;
                    g_ed.cursor_pos++;
                }
                ed_update_cursor_line_col();
                ed_ensure_cursor_visible();
            }
            return;
        case 0x4B: /* 左箭头 */
            if (g_ed.cursor_pos > 0) {
                g_ed.cursor_pos--;
                g_ed.preferred_col = g_ed.cursor_col - 1;
                if (g_ed.preferred_col < 0) g_ed.preferred_col = 0;
                ed_update_cursor_line_col();
                ed_ensure_cursor_visible();
            }
            return;
        case 0x4D: /* 右箭头 */
            if (g_ed.cursor_pos < g_ed.buffer_len) {
                g_ed.cursor_pos++;
                g_ed.preferred_col = g_ed.cursor_col + 1;
                ed_update_cursor_line_col();
                ed_ensure_cursor_visible();
            }
            return;
        case 0x47: /* Home */
            ed_move_cursor_home();
            return;
        case 0x4F: /* End */
            ed_move_cursor_end();
            return;
        case 0x49: /* PageUp */
            ed_move_cursor_page_up();
            return;
        case 0x51: /* PageDown */
            ed_move_cursor_page_down();
            return;
        default:
            break;
        }
        return;
    }

    /* 普通键 */
    if (key == 0x0D || key == '\r') { /* Enter */
        ed_insert_newline();
        return;
    }
    if (key == 0x08) { /* Backspace */
        ed_delete_char(1);
        return;
    }
    if (key == 127) { /* Delete */
        ed_delete_char(0);
        return;
    }
    if (key == 0x09) { /* Tab */
        /* 插入4个空格 */
        for (int i = 0; i < 4; i++) ed_insert_char(' ');
        return;
    }
    if (key == 27) { /* ESC */
        g_ed.sel_start = -1;
        g_ed.sel_end = -1;
        return;
    }
    if (key >= 0x20 && key < 0x7F) { /* 可打印字符 */
        ed_insert_char((char)key);
        return;
    }
}

/* ================================================================
 * 主循环
 * ================================================================ */
void text_editor_run(void)
{
    sys_window_t *win = sys_create_window(60, 40, WIN_W, WIN_H, "FUNSOS Text Editor");
    if (win == NULL) return;

    g_ed.win = win;

    sys_event_t event;
    while (1) {
        if (sys_poll_event(&event) != 0) {
            ed_render(win);
            continue;
        }

        if (event.type == SYS_EVENT_WINDOW_CLOSE) {
            break;
        }

        if (event.type == SYS_EVENT_KEY_PRESS) {
            uint32_t key = event.param1;
            uint32_t mod = event.param2;
            ed_handle_key(win, key, mod);
        }

        if (event.type == SYS_EVENT_KEY_RELEASE) {
            /* 处理修饰键释放 */
        }

        if (event.type == SYS_EVENT_MOUSE_CLICK) {
            int32_t mx = (int32_t)event.param1;
            int32_t my = (int32_t)event.param2;

            /* 点击文本区域 */
            if (mx >= TEXT_START_X && mx < WIN_W - SCROLLBAR_W &&
                my >= 0 && my < TEXT_AREA_H) {
                int32_t clicked_line = g_ed.scroll_y + my / CHAR_H;
                int32_t clicked_col = (mx - TEXT_START_X) / CHAR_W + g_ed.scroll_x;

                if (clicked_line >= 0 && (uint32_t)clicked_line < g_ed.line_count) {
                    g_ed.cursor_pos = g_ed.line_starts[clicked_line];
                    uint32_t line_end = g_ed.buffer_len;
                    if ((uint32_t)(clicked_line + 1) < g_ed.line_count) {
                        line_end = g_ed.line_starts[clicked_line + 1] - 1;
                    }
                    /* 定位到列 */
                    uint32_t col = 0;
                    uint32_t pos = g_ed.cursor_pos;
                    while (pos < line_end && col < (uint32_t)clicked_col) {
                        pos++;
                        col++;
                    }
                    g_ed.cursor_pos = pos;
                    g_ed.preferred_col = (int32_t)col;
                    g_ed.sel_start = -1;
                    g_ed.sel_end = -1;
                    ed_update_cursor_line_col();
                }
            }
        }

        if (event.type == SYS_EVENT_MOUSE_MOVE) {
            /* 可以处理拖拽选择 */
        }

        ed_render(win);
    }

    /* 清理 */
    if (g_ed.buffer) { free(g_ed.buffer); g_ed.buffer = NULL; }
    if (g_ed.line_starts) { free(g_ed.line_starts); g_ed.line_starts = NULL; }
    if (g_ed.clipboard) { free(g_ed.clipboard); g_ed.clipboard = NULL; }
    g_ed.win = NULL;
    sys_destroy_window(win);
}

/* ================================================================
 * 工具函数
 * ================================================================ */
static void ed_int_to_str(int n, char *buf)
{
    if (n == 0) {
        buf[0] = '0'; buf[1] = '\0';
        return;
    }
    int is_neg = 0;
    if (n < 0) { is_neg = 1; n = -n; }
    char tmp[16];
    int pos = 0;
    while (n > 0 && pos < 15) {
        tmp[pos++] = '0' + (n % 10);
        n /= 10;
    }
    int out = 0;
    if (is_neg) buf[out++] = '-';
    for (int i = pos - 1; i >= 0; i--) buf[out++] = tmp[i];
    buf[out] = '\0';
}