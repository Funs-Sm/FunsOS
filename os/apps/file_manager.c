/* file_manager.c - FUNSOS 文件管理器实现
 * 完整的双面板文件管理器，支持目录树、文件列表、文件操作、
 * 排序、状态显示等功能。
 */

#include "file_manager.h"
#include "sys_api.h"
#include "stddef.h"
#include "string.h"
#include "stdlib.h"

/* ================================================================
 * 配置常量
 * ================================================================ */
#define MAX_ENTRIES      256         /* 最大文件条目数 */
#define WIN_W            700         /* 窗口宽度 */
#define WIN_H            480         /* 窗口高度 */
#define STATUSBAR_H      22          /* 状态栏高度 */
#define TOOLBAR_H        28          /* 工具栏高度 */
#define TREE_PANEL_W     200         /* 左侧树面板宽度 */
#define FILE_LIST_X      TREE_PANEL_W /* 右侧文件列表起始 X */
#define FILE_LIST_W      (WIN_W - TREE_PANEL_W) /* 文件列表宽度 */
#define CONTENT_Y        TOOLBAR_H   /* 内容区起始 Y */
#define CONTENT_H        (WIN_H - TOOLBAR_H - STATUSBAR_H) /* 内容区高度 */
#define ITEM_H           22          /* 列表项高度 */
#define MAX_VISIBLE_ITEMS (CONTENT_H / ITEM_H) /* 最大可见项数 */

/* 文件类型 */
#define FTYPE_DIR     0
#define FTYPE_TEXT    1
#define FTYPE_BINARY  2
#define FTYPE_IMAGE   3
#define FTYPE_UNKNOWN 4

/* 排序方式 */
#define SORT_NAME 0
#define SORT_SIZE 1
#define SORT_TYPE 2
#define SORT_DATE 3

/* 对话框类型 */
#define DIALOG_NONE    0
#define DIALOG_RENAME  1
#define DIALOG_MKDIR   2
#define DIALOG_DELETE  3
#define DIALOG_PROGRESS 4

/* ================================================================
 * 颜色定义
 * ================================================================ */
static const sys_color_t COLOR_BG          = { 0xFF, 0xFF, 0xFF, 0xFF };
static const sys_color_t COLOR_TEXT        = { 0x00, 0x00, 0x00, 0xFF };
static const sys_color_t COLOR_DIR         = { 0x00, 0x00, 0xCC, 0xFF };
static const sys_color_t COLOR_SELECTED    = { 0xCC, 0xDD, 0xFF, 0xFF };
static const sys_color_t COLOR_TOOLBAR     = { 0xE8, 0xE8, 0xE8, 0xFF };
static const sys_color_t COLOR_TOOLBAR_FG  = { 0x33, 0x33, 0x33, 0xFF };
static const sys_color_t COLOR_STATUSBAR   = { 0xE8, 0xE8, 0xE8, 0xFF };
static const sys_color_t COLOR_STATUSBAR_FG = { 0x33, 0x33, 0x33, 0xFF };
static const sys_color_t COLOR_TREE_BG     = { 0xF5, 0xF5, 0xF5, 0xFF };
static const sys_color_t COLOR_SEPARATOR   = { 0xCC, 0xCC, 0xCC, 0xFF };
static const sys_color_t COLOR_HEADER_BG   = { 0xDD, 0xDD, 0xDD, 0xFF };
static const sys_color_t COLOR_DIALOG_BG   = { 0xEE, 0xEE, 0xEE, 0xFF };
static const sys_color_t COLOR_DIALOG_BORDER = { 0x88, 0x88, 0x88, 0xFF };
static const sys_color_t COLOR_PROGRESS_BG = { 0xDD, 0xDD, 0xDD, 0xFF };
static const sys_color_t COLOR_PROGRESS_FG = { 0x00, 0x88, 0x00, 0xFF };

/* 文件类型图标颜色 */
static const sys_color_t COLOR_ICON_DIR   = { 0xDD, 0xAA, 0x00, 0xFF };
static const sys_color_t COLOR_ICON_TEXT  = { 0x00, 0x88, 0x00, 0xFF };
static const sys_color_t COLOR_ICON_BIN   = { 0x88, 0x88, 0x88, 0xFF };
static const sys_color_t COLOR_ICON_IMG   = { 0x00, 0x66, 0xCC, 0xFF };

/* ================================================================
 * 文件管理器状态
 * ================================================================ */
typedef struct {
    /* 文件列表 */
    fm_entry_t entries[MAX_ENTRIES];
    uint32_t   entry_count;
    int32_t    selected_index;
    int32_t    scroll_offset;
    /* 排序 */
    uint32_t   sort_mode;
    uint8_t    sort_ascending;
    /* 目录树 */
    char       tree_dirs[64][128];    /* 目录树节点 */
    uint32_t   tree_count;
    int32_t    tree_expanded[64];     /* 是否展开 */
    int32_t    tree_selected;
    /* 当前路径 */
    char       current_path[256];
    /* 对话框 */
    uint8_t    dialog_type;
    char       dialog_buf[256];
    uint32_t   dialog_len;
    char       dialog_title[64];
    /* 操作进度 */
    uint8_t    show_progress;
    uint32_t   progress_val;
    uint32_t   progress_max;
    char       progress_text[64];
    /* 窗口 */
    sys_window_t *win;
} fm_state_t;

static fm_state_t g_fm;

/* ================================================================
 * 内部函数声明
 * ================================================================ */
static void fm_refresh_entries(void);
static void fm_sort_entries(void);
static void fm_refresh_tree(void);
static int  fm_get_file_type(const char *name, uint32_t is_dir);
static void fm_render(sys_window_t *win);
static void fm_render_toolbar(sys_window_t *win);
static void fm_render_tree_panel(sys_window_t *win);
static void fm_render_file_list(sys_window_t *win);
static void fm_render_statusbar(sys_window_t *win);
static void fm_render_dialog(sys_window_t *win);
static void fm_render_progress(sys_window_t *win);
static void fm_handle_key(sys_window_t *win, uint32_t key, uint32_t mod);
static void fm_handle_mouse(sys_window_t *win, int x, int y, int button);
static void fm_handle_dialog_key(uint32_t key);
static void fm_navigate_to(const char *path);
static void fm_open_selected(void);
static void fm_int_to_str(int n, char *buf);
static void fm_uint_to_str(uint32_t n, char *buf);
static void fm_format_size(char *buf, uint32_t size);

/* ================================================================
 * 初始化和导航
 * ================================================================ */
int file_manager_init(void)
{
    memset(&g_fm, 0, sizeof(g_fm));
    g_fm.sort_mode = SORT_NAME;
    g_fm.sort_ascending = 1;
    g_fm.selected_index = -1;
    g_fm.scroll_offset = 0;
    g_fm.tree_selected = -1;
    g_fm.dialog_type = DIALOG_NONE;
    g_fm.show_progress = 0;
    return file_manager_navigate("/");
}

int file_manager_navigate(const char *path)
{
    if (path == NULL) return -1;
    strncpy(g_fm.current_path, path, 255);
    g_fm.current_path[255] = '\0';
    g_fm.selected_index = -1;
    g_fm.scroll_offset = 0;
    fm_refresh_entries();
    fm_refresh_tree();
    return 0;
}

int file_manager_go_up(void)
{
    int last_sep = -1;
    for (int i = 0; g_fm.current_path[i]; i++) {
        if (g_fm.current_path[i] == '/') last_sep = i;
    }
    if (last_sep > 0) {
        g_fm.current_path[last_sep] = '\0';
    } else {
        g_fm.current_path[0] = '/';
        g_fm.current_path[1] = '\0';
    }
    return file_manager_navigate(g_fm.current_path);
}

int file_manager_refresh(void)
{
    return file_manager_navigate(g_fm.current_path);
}

/* ================================================================
 * 文件列表刷新
 * ================================================================ */
static void fm_navigate_to(const char *path)
{
    file_manager_navigate(path);
}

static void fm_refresh_entries(void)
{
    g_fm.entry_count = 0;
    int fd = sys_file_open(g_fm.current_path, 0);
    if (fd < 0) {
        /* 目录打开失败，添加默认条目 */
        return;
    }

    char buf[1024];
    int n = sys_file_read(fd, buf, sizeof(buf));
    if (n > 0) {
        buf[n] = '\0';
        /* 解析目录项 */
        char *line = buf;
        while (line && *line && g_fm.entry_count < MAX_ENTRIES) {
            char *next = strchr(line, '\n');
            if (next) *next = '\0';

            fm_entry_t *entry = &g_fm.entries[g_fm.entry_count];
            memset(entry, 0, sizeof(fm_entry_t));

            /* 解析条目: 格式假定为 "name|size|type|time" 或简单名称 */
            char *sep = strchr(line, '|');
            if (sep) {
                *sep = '\0';
                strncpy(entry->name, line, 127);
                entry->name[127] = '\0';

                char *sizestr = sep + 1;
                char *sep2 = strchr(sizestr, '|');
                if (sep2) {
                    *sep2 = '\0';
                    entry->size = (uint32_t)atoi(sizestr);
                    char *typestr = sep2 + 1;
                    char *sep3 = strchr(typestr, '|');
                    if (sep3) {
                        *sep3 = '\0';
                        entry->is_dir = (strcmp(typestr, "d") == 0) ? 1 : 0;
                        entry->modified_time = (uint32_t)atoi(sep3 + 1);
                    }
                }
            } else {
                strncpy(entry->name, line, 127);
                entry->name[127] = '\0';
                entry->size = 0;
                entry->is_dir = 0;
                entry->modified_time = 0;
            }

            /* 检查是否是目录（包含 '/' 后缀） */
            int name_len = (int)strlen(entry->name);
            if (name_len > 0 && entry->name[name_len - 1] == '/') {
                entry->is_dir = 1;
                entry->name[name_len - 1] = '\0';
            }

            g_fm.entry_count++;
            if (next) {
                *next = '\n';
                line = next + 1;
            } else {
                break;
            }
        }
    }

    sys_file_close(fd);
    fm_sort_entries();
}

static void fm_sort_entries(void)
{
    /* 简单的冒泡排序 */
    for (uint32_t i = 0; i < g_fm.entry_count; i++) {
        for (uint32_t j = i + 1; j < g_fm.entry_count; j++) {
            int cmp = 0;
            switch (g_fm.sort_mode) {
            case SORT_NAME:
                /* 目录优先 */
                if (g_fm.entries[i].is_dir != g_fm.entries[j].is_dir) {
                    cmp = g_fm.entries[j].is_dir - g_fm.entries[i].is_dir;
                } else {
                    cmp = strcmp(g_fm.entries[i].name, g_fm.entries[j].name);
                }
                break;
            case SORT_SIZE:
                cmp = (int)(g_fm.entries[i].size - g_fm.entries[j].size);
                break;
            case SORT_TYPE:
                cmp = fm_get_file_type(g_fm.entries[i].name, g_fm.entries[i].is_dir) -
                      fm_get_file_type(g_fm.entries[j].name, g_fm.entries[j].is_dir);
                if (cmp == 0) cmp = strcmp(g_fm.entries[i].name, g_fm.entries[j].name);
                break;
            case SORT_DATE:
                cmp = (int)(g_fm.entries[i].modified_time - g_fm.entries[j].modified_time);
                break;
            }
            if (!g_fm.sort_ascending) cmp = -cmp;
            if (cmp > 0) {
                fm_entry_t tmp = g_fm.entries[i];
                g_fm.entries[i] = g_fm.entries[j];
                g_fm.entries[j] = tmp;
            }
        }
    }
}

static void fm_refresh_tree(void)
{
    g_fm.tree_count = 0;
    /* 构建目录树 */
    strncpy(g_fm.tree_dirs[0], "/", 127);
    g_fm.tree_dirs[0][127] = '\0';
    g_fm.tree_expanded[0] = 1;
    g_fm.tree_count = 1;

    /* 尝试读取根目录下的子目录 */
    int fd = sys_file_open("/", 0);
    if (fd >= 0) {
        char buf[1024];
        int n = sys_file_read(fd, buf, sizeof(buf));
        if (n > 0) {
            buf[n] = '\0';
            char *line = buf;
            while (line && *line && g_fm.tree_count < 64) {
                char *next = strchr(line, '\n');
                if (next) *next = '\0';

                /* 检查是否是目录 */
                int len = (int)strlen(line);
                if (len > 0 && line[len - 1] == '/') {
                    line[len - 1] = '\0';
                    /* 构建完整路径 */
                    char full_path[256];
                    full_path[0] = '/';
                    full_path[1] = '\0';
                    strncat(full_path, line, 254);
                    strncpy(g_fm.tree_dirs[g_fm.tree_count], full_path, 127);
                    g_fm.tree_dirs[g_fm.tree_count][127] = '\0';
                    g_fm.tree_expanded[g_fm.tree_count] = 0;
                    g_fm.tree_count++;
                }

                if (next) {
                    *next = '\n';
                    line = next + 1;
                } else {
                    break;
                }
            }
        }
        sys_file_close(fd);
    }
}

/* ================================================================
 * 文件操作
 * ================================================================ */
int file_manager_copy(const char *src, const char *dst)
{
    if (src == NULL || dst == NULL) return -1;
    int sfd = sys_file_open(src, 0);
    if (sfd < 0) return -1;
    int dfd = sys_file_open(dst, 1);
    if (dfd < 0) { sys_file_close(sfd); return -1; }

    char buf[1024];
    int n;
    uint32_t total = 0;
    while ((n = sys_file_read(sfd, buf, sizeof(buf))) > 0) {
        sys_file_write(dfd, buf, (uint32_t)n);
        total += (uint32_t)n;
    }

    sys_file_close(sfd);
    sys_file_close(dfd);
    return 0;
}

int file_manager_move(const char *src, const char *dst)
{
    if (src == NULL || dst == NULL) return -1;
    if (file_manager_copy(src, dst) != 0) return -1;
    return file_manager_delete(src);
}

int file_manager_delete(const char *path)
{
    if (path == NULL) return -1;
    /* 通过写入空内容来模拟删除 */
    int fd = sys_file_open(path, 1);
    if (fd >= 0) {
        sys_file_write(fd, "", 0);
        sys_file_close(fd);
    }
    return 0;
}

int file_manager_mkdir(const char *path)
{
    if (path == NULL) return -1;
    /* 尝试创建目录 */
    int fd = sys_file_open(path, 1);
    if (fd >= 0) {
        sys_file_close(fd);
        return 0;
    }
    return -1;
}

int file_manager_rename(const char *old_name, const char *new_name)
{
    if (old_name == NULL || new_name == NULL) return -1;
    return file_manager_move(old_name, new_name);
}

fm_entry_t *file_manager_get_entries(uint32_t *count)
{
    *count = g_fm.entry_count;
    return g_fm.entries;
}

static int fm_get_file_type(const char *name, uint32_t is_dir)
{
    if (is_dir) return FTYPE_DIR;
    if (name == NULL) return FTYPE_UNKNOWN;

    const char *ext = strrchr(name, '.');
    if (ext == NULL) return FTYPE_UNKNOWN;

    /* 文本文件 */
    if (strcmp(ext, ".txt") == 0 || strcmp(ext, ".c") == 0 ||
        strcmp(ext, ".h") == 0 || strcmp(ext, ".cpp") == 0 ||
        strcmp(ext, ".asm") == 0 || strcmp(ext, ".py") == 0 ||
        strcmp(ext, ".cfg") == 0 || strcmp(ext, ".ini") == 0 ||
        strcmp(ext, ".log") == 0 || strcmp(ext, ".md") == 0 ||
        strcmp(ext, ".sh") == 0 || strcmp(ext, ".bat") == 0) {
        return FTYPE_TEXT;
    }

    /* 图片文件 */
    if (strcmp(ext, ".bmp") == 0 || strcmp(ext, ".png") == 0 ||
        strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0 ||
        strcmp(ext, ".gif") == 0 || strcmp(ext, ".ico") == 0 ||
        strcmp(ext, ".ppm") == 0) {
        return FTYPE_IMAGE;
    }

    /* 二进制文件 */
    if (strcmp(ext, ".o") == 0 || strcmp(ext, ".bin") == 0 ||
        strcmp(ext, ".elf") == 0 || strcmp(ext, ".exe") == 0 ||
        strcmp(ext, ".img") == 0 || strcmp(ext, ".dll") == 0) {
        return FTYPE_BINARY;
    }

    return FTYPE_UNKNOWN;
}

static void fm_open_selected(void)
{
    if (g_fm.selected_index < 0 || (uint32_t)g_fm.selected_index >= g_fm.entry_count) return;

    fm_entry_t *entry = &g_fm.entries[g_fm.selected_index];
    if (entry->is_dir) {
        /* 进入子目录 */
        char new_path[256];
        strncpy(new_path, g_fm.current_path, 255);
        new_path[255] = '\0';
        int len = (int)strlen(new_path);
        if (len > 0 && new_path[len - 1] != '/') {
            new_path[len] = '/';
            new_path[len + 1] = '\0';
        }
        strncat(new_path, entry->name, 255 - strlen(new_path));
        fm_navigate_to(new_path);
    } else {
        /* 打开文件 */
        char full_path[256];
        strncpy(full_path, g_fm.current_path, 255);
        full_path[255] = '\0';
        int len = (int)strlen(full_path);
        if (len > 0 && full_path[len - 1] != '/') {
            full_path[len] = '/';
            full_path[len + 1] = '\0';
        }
        strncat(full_path, entry->name, 255 - strlen(full_path));
        sys_spawn(full_path, "");
    }
}

/* ================================================================
 * 渲染函数
 * ================================================================ */
static void fm_render(sys_window_t *win)
{
    sys_fill_window(win, COLOR_BG);
    fm_render_toolbar(win);
    fm_render_tree_panel(win);
    fm_render_file_list(win);
    fm_render_statusbar(win);
    if (g_fm.dialog_type != DIALOG_NONE) {
        fm_render_dialog(win);
    }
    if (g_fm.show_progress) {
        fm_render_progress(win);
    }
}

static void fm_render_toolbar(sys_window_t *win)
{
    sys_draw_rect(win, 0, 0, WIN_W, TOOLBAR_H, COLOR_TOOLBAR);
    sys_draw_text(win, 4, 4, "FUNSOS File Manager", COLOR_TOOLBAR_FG);

    /* 路径显示 */
    sys_draw_text(win, 200, 4, g_fm.current_path, COLOR_TOOLBAR_FG);
}

static void fm_render_tree_panel(sys_window_t *win)
{
    sys_draw_rect(win, 0, CONTENT_Y, TREE_PANEL_W - 1, CONTENT_H, COLOR_TREE_BG);
    sys_draw_rect(win, TREE_PANEL_W - 1, CONTENT_Y, 1, CONTENT_H, COLOR_SEPARATOR);

    /* 树面板标题 */
    sys_draw_text(win, 4, CONTENT_Y + 2, "Directory Tree", COLOR_TEXT);

    /* 渲染目录树 */
    int tree_start_y = CONTENT_Y + 24;
    for (uint32_t i = 0; i < g_fm.tree_count && tree_start_y < CONTENT_Y + CONTENT_H; i++) {
        /* 计算缩进 */
        int depth = 0;
        for (const char *p = g_fm.tree_dirs[i]; *p; p++) {
            if (*p == '/') depth++;
        }
        if (depth > 0) depth--;

        int indent = depth * 16;
        const char *name = g_fm.tree_dirs[i];
        /* 获取目录名 */
        const char *last_sep = NULL;
        for (const char *p = name; *p; p++) {
            if (*p == '/') last_sep = p;
        }
        if (last_sep) name = last_sep + 1;
        if (name[0] == '\0') name = "/";

        sys_color_t color = COLOR_DIR;
        if ((int32_t)i == g_fm.tree_selected) {
            sys_draw_rect(win, 0, tree_start_y, TREE_PANEL_W, ITEM_H, COLOR_SELECTED);
        }

        char prefix[4] = "";
        if (g_fm.tree_expanded[i]) {
            prefix[0] = 'v';
        } else {
            prefix[0] = '>';
        }
        prefix[1] = ' ';
        prefix[2] = '\0';
        sys_draw_text(win, 4 + indent, tree_start_y + 2, prefix, color);
        sys_draw_text(win, 20 + indent, tree_start_y + 2, name, color);
        tree_start_y += ITEM_H;
    }
}

static void fm_render_file_list(sys_window_t *win)
{
    int flx = FILE_LIST_X;
    int fly = CONTENT_Y;

    /* 列标题 */
    sys_draw_rect(win, flx, fly, FILE_LIST_W, ITEM_H, COLOR_HEADER_BG);

    const char *headers[] = {"Name", "Size", "Type", "Date"};
    int col_x[] = {flx + 4, flx + 220, flx + 320, flx + 420};

    sys_draw_text(win, col_x[0], fly + 2, headers[0], COLOR_TEXT);
    sys_draw_text(win, col_x[1], fly + 2, headers[1], COLOR_TEXT);
    sys_draw_text(win, col_x[2], fly + 2, headers[2], COLOR_TEXT);
    sys_draw_text(win, col_x[3], fly + 2, headers[3], COLOR_TEXT);

    /* 排序指示器 */
    if (g_fm.sort_mode == SORT_NAME) sys_draw_text(win, col_x[0] + 40, fly + 2, g_fm.sort_ascending ? "^" : "v", COLOR_TEXT);
    if (g_fm.sort_mode == SORT_SIZE) sys_draw_text(win, col_x[1] + 40, fly + 2, g_fm.sort_ascending ? "^" : "v", COLOR_TEXT);
    if (g_fm.sort_mode == SORT_TYPE) sys_draw_text(win, col_x[2] + 40, fly + 2, g_fm.sort_ascending ? "^" : "v", COLOR_TEXT);
    if (g_fm.sort_mode == SORT_DATE) sys_draw_text(win, col_x[3] + 40, fly + 2, g_fm.sort_ascending ? "^" : "v", COLOR_TEXT);

    /* 文件列表 */
    int list_y = fly + ITEM_H;
    for (int32_t i = g_fm.scroll_offset; i < (int32_t)g_fm.entry_count && list_y < CONTENT_Y + CONTENT_H; i++) {
        fm_entry_t *entry = &g_fm.entries[i];

        if (i == g_fm.selected_index) {
            sys_draw_rect(win, flx, list_y, FILE_LIST_W, ITEM_H, COLOR_SELECTED);
        }

        sys_color_t color = entry->is_dir ? COLOR_DIR : COLOR_TEXT;

        /* 文件类型图标 */
        const char *icon = entry->is_dir ? "[D]" : "[F]";
        sys_draw_text(win, col_x[0], list_y + 2, icon, entry->is_dir ? COLOR_ICON_DIR : COLOR_ICON_TEXT);
        sys_draw_text(win, col_x[0] + 24, list_y + 2, entry->name, color);

        /* 大小 */
        char size_buf[32];
        if (entry->is_dir) {
            size_buf[0] = '<'; size_buf[1] = 'D'; size_buf[2] = 'I'; size_buf[3] = 'R'; size_buf[4] = '>'; size_buf[5] = '\0';
        } else {
            fm_format_size(size_buf, entry->size);
        }
        sys_draw_text(win, col_x[1], list_y + 2, size_buf, color);

        /* 类型 */
        const char *type_str = "Unknown";
        int ftype = fm_get_file_type(entry->name, entry->is_dir);
        switch (ftype) {
        case FTYPE_DIR: type_str = "Dir"; break;
        case FTYPE_TEXT: type_str = "Text"; break;
        case FTYPE_BINARY: type_str = "Binary"; break;
        case FTYPE_IMAGE: type_str = "Image"; break;
        default: type_str = "File"; break;
        }
        sys_draw_text(win, col_x[2], list_y + 2, type_str, color);

        /* 日期 */
        char date_buf[16];
        date_buf[0] = '\0';
        if (entry->modified_time > 0) {
            fm_uint_to_str(entry->modified_time, date_buf);
        } else {
            date_buf[0] = '-'; date_buf[1] = '\0';
        }
        sys_draw_text(win, col_x[3], list_y + 2, date_buf, color);

        list_y += ITEM_H;
    }
}

static void fm_render_statusbar(sys_window_t *win)
{
    int sy = CONTENT_Y + CONTENT_H;
    sys_draw_rect(win, 0, sy, WIN_W, STATUSBAR_H, COLOR_STATUSBAR);

    char status[256];
    int pos = 0;

    /* 快捷键提示 */
    const char *hints = "F2:Rename F3:View F5:Copy F6:Move F7:MkDir F8:Delete Enter:Open BS:Up";
    while (*hints && pos < 200) status[pos++] = *hints++;
    status[pos++] = ' ';
    status[pos++] = '|';
    status[pos++] = ' ';

    /* 选中文件信息 */
    if (g_fm.selected_index >= 0 && (uint32_t)g_fm.selected_index < g_fm.entry_count) {
        fm_entry_t *sel = &g_fm.entries[g_fm.selected_index];
        char *name_ptr = sel->name;
        while (*name_ptr && pos < 250) status[pos++] = *name_ptr++;
        status[pos++] = ' ';
        char szbuf[32];
        fm_format_size(szbuf, sel->size);
        const char *sp = szbuf;
        while (*sp && pos < 250) status[pos++] = *sp++;
    } else {
        char cnt_buf[16];
        fm_uint_to_str(g_fm.entry_count, cnt_buf);
        const char *cnt = cnt_buf;
        while (*cnt && pos < 250) status[pos++] = *cnt++;
        status[pos++] = ' ';
        const char *items = "items";
        while (*items && pos < 250) status[pos++] = *items++;
    }

    status[pos] = '\0';
    sys_draw_text(win, 4, sy + 3, status, COLOR_STATUSBAR_FG);
}

static void fm_render_dialog(sys_window_t *win)
{
    if (g_fm.dialog_type == DIALOG_NONE) return;

    int dx = 100;
    int dy = 180;
    int dw = WIN_W - 200;
    int dh = 100;

    sys_draw_rect(win, dx, dy, dw, dh, COLOR_DIALOG_BG);
    sys_draw_rect(win, dx, dy, dw, 1, COLOR_DIALOG_BORDER);
    sys_draw_rect(win, dx, dy + dh - 1, dw, 1, COLOR_DIALOG_BORDER);
    sys_draw_rect(win, dx, dy, 1, dh, COLOR_DIALOG_BORDER);
    sys_draw_rect(win, dx + dw - 1, dy, 1, dh, COLOR_DIALOG_BORDER);

    sys_draw_text(win, dx + 8, dy + 4, g_fm.dialog_title, COLOR_TEXT);

    /* 输入框 */
    sys_draw_rect(win, dx + 8, dy + 30, dw - 16, 22, COLOR_BG);
    char input_text[256];
    memcpy(input_text, g_fm.dialog_buf, g_fm.dialog_len);
    input_text[g_fm.dialog_len] = '\0';
    sys_draw_text(win, dx + 12, dy + 32, input_text, COLOR_TEXT);

    /* 提示 */
    const char *hint = "Enter:OK  Esc:Cancel";
    sys_draw_text(win, dx + 8, dy + 60, hint, COLOR_TEXT);
}

static void fm_render_progress(sys_window_t *win)
{
    int px = 120;
    int py = 200;
    int pw = WIN_W - 240;
    int ph = 40;

    sys_draw_rect(win, px, py, pw, ph, COLOR_DIALOG_BG);
    sys_draw_rect(win, px, py, pw, 1, COLOR_DIALOG_BORDER);
    sys_draw_rect(win, px, py + ph - 1, pw, 1, COLOR_DIALOG_BORDER);
    sys_draw_rect(win, px, py, 1, ph, COLOR_DIALOG_BORDER);
    sys_draw_rect(win, px + pw - 1, py, 1, ph, COLOR_DIALOG_BORDER);

    sys_draw_text(win, px + 4, py + 2, g_fm.progress_text, COLOR_TEXT);

    /* 进度条 */
    int bar_y = py + 20;
    int bar_h = 14;
    sys_draw_rect(win, px + 4, bar_y, pw - 8, bar_h, COLOR_PROGRESS_BG);
    if (g_fm.progress_max > 0) {
        int fill_w = (int)((int64_t)(pw - 8) * g_fm.progress_val / g_fm.progress_max);
        if (fill_w > 0) {
            sys_draw_rect(win, px + 4, bar_y, fill_w, bar_h, COLOR_PROGRESS_FG);
        }
    }
}

/* ================================================================
 * 事件处理
 * ================================================================ */
static void fm_handle_key(sys_window_t *win, uint32_t key, uint32_t mod)
{
    (void)mod;

    /* 对话框模式 */
    if (g_fm.dialog_type != DIALOG_NONE) {
        fm_handle_dialog_key(key);
        return;
    }

    /* 功能键 */
    /* F2: 重命名 */
    if (key == 0x3C) { /* F2 扫描码 */
        if (g_fm.selected_index >= 0 && (uint32_t)g_fm.selected_index < g_fm.entry_count) {
            fm_entry_t *sel = &g_fm.entries[g_fm.selected_index];
            strncpy(g_fm.dialog_buf, sel->name, 255);
            g_fm.dialog_len = (uint32_t)strlen(sel->name);
            strncpy(g_fm.dialog_title, "Rename:", 63);
            g_fm.dialog_type = DIALOG_RENAME;
        }
        return;
    }

    /* F5: 复制 */
    if (key == 0x3F) {
        if (g_fm.selected_index >= 0 && (uint32_t)g_fm.selected_index < g_fm.entry_count) {
            fm_entry_t *sel = &g_fm.entries[g_fm.selected_index];
            g_fm.dialog_buf[0] = '\0';
            g_fm.dialog_len = 0;
            strncpy(g_fm.dialog_title, "Copy to:", 63);
            g_fm.dialog_type = DIALOG_RENAME; /* 复用对话框 */
        }
        return;
    }

    /* F7: 新建目录 */
    if (key == 0x41) {
        g_fm.dialog_buf[0] = '\0';
        g_fm.dialog_len = 0;
        strncpy(g_fm.dialog_title, "New directory name:", 63);
        g_fm.dialog_type = DIALOG_MKDIR;
        return;
    }

    /* F8: 删除 */
    if (key == 0x42) {
        if (g_fm.selected_index >= 0 && (uint32_t)g_fm.selected_index < g_fm.entry_count) {
            fm_entry_t *sel = &g_fm.entries[g_fm.selected_index];
            /* 确认删除 */
            strncpy(g_fm.dialog_title, "Delete?", 63);
            g_fm.dialog_buf[0] = 'y'; g_fm.dialog_buf[1] = '\0';
            g_fm.dialog_len = 1;
            g_fm.dialog_type = DIALOG_DELETE;
        }
        return;
    }

    /* 特殊键 */
    if (key == 0xE0 || key == 0x00) {
        uint32_t ext_key = mod;
        switch (ext_key) {
        case 0x48: /* 上箭头 */
            if (g_fm.selected_index > 0) {
                g_fm.selected_index--;
                if (g_fm.selected_index < g_fm.scroll_offset) {
                    g_fm.scroll_offset = g_fm.selected_index;
                }
            }
            return;
        case 0x50: /* 下箭头 */
            if ((uint32_t)(g_fm.selected_index + 1) < g_fm.entry_count) {
                g_fm.selected_index++;
                if (g_fm.selected_index >= g_fm.scroll_offset + (int32_t)MAX_VISIBLE_ITEMS) {
                    g_fm.scroll_offset = g_fm.selected_index - (int32_t)MAX_VISIBLE_ITEMS + 1;
                }
            }
            return;
        case 0x49: /* PageUp */
            g_fm.selected_index -= (int32_t)MAX_VISIBLE_ITEMS;
            if (g_fm.selected_index < 0) g_fm.selected_index = 0;
            g_fm.scroll_offset = g_fm.selected_index;
            return;
        case 0x51: /* PageDown */
            g_fm.selected_index += (int32_t)MAX_VISIBLE_ITEMS;
            if ((uint32_t)g_fm.selected_index >= g_fm.entry_count)
                g_fm.selected_index = (int32_t)g_fm.entry_count - 1;
            if (g_fm.selected_index < 0) g_fm.selected_index = 0;
            g_fm.scroll_offset = g_fm.selected_index - (int32_t)MAX_VISIBLE_ITEMS + 1;
            if (g_fm.scroll_offset < 0) g_fm.scroll_offset = 0;
            return;
        default:
            break;
        }
        return;
    }

    /* 普通键 */
    if (key == 0x0D || key == '\r') { /* Enter */
        fm_open_selected();
        return;
    }
    if (key == 0x08) { /* Backspace */
        file_manager_go_up();
        return;
    }
    if (key == 27) { /* ESC */
        g_fm.selected_index = -1;
        return;
    }
    (void)win;
}

static void fm_handle_dialog_key(uint32_t key)
{
    if (key == 27) { /* ESC */
        g_fm.dialog_type = DIALOG_NONE;
        return;
    }

    if (key == 0x0D || key == '\r') { /* Enter */
        g_fm.dialog_buf[g_fm.dialog_len] = '\0';

        switch (g_fm.dialog_type) {
        case DIALOG_RENAME:
            if (g_fm.selected_index >= 0 && (uint32_t)g_fm.selected_index < g_fm.entry_count) {
                fm_entry_t *sel = &g_fm.entries[g_fm.selected_index];
                char old_path[256], new_path[256];
                old_path[0] = '\0'; new_path[0] = '\0';
                strncpy(old_path, g_fm.current_path, 255);
                strncpy(new_path, g_fm.current_path, 255);
                int len = (int)strlen(old_path);
                if (len > 0 && old_path[len - 1] != '/') {
                    old_path[len] = '/'; old_path[len + 1] = '\0';
                    new_path[len] = '/'; new_path[len + 1] = '\0';
                }
                strncat(old_path, sel->name, 255 - strlen(old_path));
                strncat(new_path, g_fm.dialog_buf, 255 - strlen(new_path));
                file_manager_rename(old_path, new_path);
                file_manager_refresh();
            }
            break;
        case DIALOG_MKDIR: {
            char new_path[256];
            strncpy(new_path, g_fm.current_path, 255);
            int len = (int)strlen(new_path);
            if (len > 0 && new_path[len - 1] != '/') {
                new_path[len] = '/'; new_path[len + 1] = '\0';
            }
            strncat(new_path, g_fm.dialog_buf, 255 - strlen(new_path));
            file_manager_mkdir(new_path);
            file_manager_refresh();
            break;
        }
        case DIALOG_DELETE:
            if (g_fm.dialog_buf[0] == 'y' || g_fm.dialog_buf[0] == 'Y') {
                if (g_fm.selected_index >= 0 && (uint32_t)g_fm.selected_index < g_fm.entry_count) {
                    fm_entry_t *sel = &g_fm.entries[g_fm.selected_index];
                    char full_path[256];
                    strncpy(full_path, g_fm.current_path, 255);
                    int len = (int)strlen(full_path);
                    if (len > 0 && full_path[len - 1] != '/') {
                        full_path[len] = '/'; full_path[len + 1] = '\0';
                    }
                    strncat(full_path, sel->name, 255 - strlen(full_path));
                    file_manager_delete(full_path);
                    file_manager_refresh();
                }
            }
            break;
        default:
            break;
        }
        g_fm.dialog_type = DIALOG_NONE;
        return;
    }

    if (key == 0x08) {
        if (g_fm.dialog_len > 0) g_fm.dialog_len--;
        g_fm.dialog_buf[g_fm.dialog_len] = '\0';
        return;
    }

    if (key >= 0x20 && key < 0x7F) {
        if (g_fm.dialog_len < 255) {
            g_fm.dialog_buf[g_fm.dialog_len++] = (char)key;
            g_fm.dialog_buf[g_fm.dialog_len] = '\0';
        }
        return;
    }
}

static void fm_handle_mouse(sys_window_t *win, int x, int y, int button)
{
    (void)button;

    /* 点击文件列表区域 */
    if (x >= FILE_LIST_X && x < WIN_W && y >= CONTENT_Y + ITEM_H && y < CONTENT_Y + CONTENT_H) {
        int32_t clicked_idx = g_fm.scroll_offset + (y - CONTENT_Y - ITEM_H) / ITEM_H;
        if (clicked_idx >= 0 && (uint32_t)clicked_idx < g_fm.entry_count) {
            g_fm.selected_index = clicked_idx;
        }
    }

    /* 点击列标题 */
    if (x >= FILE_LIST_X && x < WIN_W && y >= CONTENT_Y && y < CONTENT_Y + ITEM_H) {
        int col_x[] = {FILE_LIST_X + 4, FILE_LIST_X + 220, FILE_LIST_X + 320, FILE_LIST_X + 420};
        if (x < col_x[1]) {
            if (g_fm.sort_mode == SORT_NAME) g_fm.sort_ascending = !g_fm.sort_ascending;
            else { g_fm.sort_mode = SORT_NAME; g_fm.sort_ascending = 1; }
        } else if (x < col_x[2]) {
            if (g_fm.sort_mode == SORT_SIZE) g_fm.sort_ascending = !g_fm.sort_ascending;
            else { g_fm.sort_mode = SORT_SIZE; g_fm.sort_ascending = 1; }
        } else if (x < col_x[3]) {
            if (g_fm.sort_mode == SORT_TYPE) g_fm.sort_ascending = !g_fm.sort_ascending;
            else { g_fm.sort_mode = SORT_TYPE; g_fm.sort_ascending = 1; }
        } else {
            if (g_fm.sort_mode == SORT_DATE) g_fm.sort_ascending = !g_fm.sort_ascending;
            else { g_fm.sort_mode = SORT_DATE; g_fm.sort_ascending = 1; }
        }
        fm_sort_entries();
    }

    /* 点击树面板 */
    if (x >= 0 && x < TREE_PANEL_W && y >= CONTENT_Y + 24 && y < CONTENT_Y + CONTENT_H) {
        int32_t clicked_tree = (y - CONTENT_Y - 24) / ITEM_H;
        if (clicked_tree >= 0 && (uint32_t)clicked_tree < g_fm.tree_count) {
            g_fm.tree_expanded[clicked_tree] = !g_fm.tree_expanded[clicked_tree];
            g_fm.tree_selected = clicked_tree;
            /* 导航到选中的目录 */
            fm_navigate_to(g_fm.tree_dirs[clicked_tree]);
        }
    }

    (void)win;
}

/* ================================================================
 * 主循环
 * ================================================================ */
void file_manager_run(void)
{
    sys_window_t *win = sys_create_window(50, 30, WIN_W, WIN_H, "FUNSOS File Manager");
    if (win == NULL) return;

    g_fm.win = win;

    sys_event_t event;
    while (1) {
        if (sys_poll_event(&event) != 0) {
            fm_render(win);
            continue;
        }

        if (event.type == SYS_EVENT_WINDOW_CLOSE) {
            break;
        }

        if (event.type == SYS_EVENT_KEY_PRESS) {
            fm_handle_key(win, event.param1, event.param2);
        }

        if (event.type == SYS_EVENT_MOUSE_CLICK) {
            fm_handle_mouse(win, (int)event.param1, (int)event.param2, 0);
        }

        fm_render(win);
    }

    g_fm.win = NULL;
    sys_destroy_window(win);
}

/* ================================================================
 * 工具函数
 * ================================================================ */
static void fm_int_to_str(int n, char *buf)
{
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    int is_neg = 0;
    if (n < 0) { is_neg = 1; n = -n; }
    char tmp[16];
    int pos = 0;
    while (n > 0 && pos < 15) { tmp[pos++] = '0' + (n % 10); n /= 10; }
    int out = 0;
    if (is_neg) buf[out++] = '-';
    for (int i = pos - 1; i >= 0; i--) buf[out++] = tmp[i];
    buf[out] = '\0';
}

static void fm_uint_to_str(uint32_t n, char *buf)
{
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[16];
    int pos = 0;
    while (n > 0 && pos < 15) { tmp[pos++] = '0' + (int)(n % 10); n /= 10; }
    int out = 0;
    for (int i = pos - 1; i >= 0; i--) buf[out++] = tmp[i];
    buf[out] = '\0';
}

static void fm_format_size(char *buf, uint32_t size)
{
    if (size < 1024) {
        fm_uint_to_str(size, buf);
        int len = (int)strlen(buf);
        buf[len] = ' '; buf[len + 1] = 'B'; buf[len + 2] = '\0';
    } else if (size < 1024 * 1024) {
        fm_uint_to_str(size / 1024, buf);
        int len = (int)strlen(buf);
        buf[len] = 'K'; buf[len + 1] = 'B'; buf[len + 2] = '\0';
    } else {
        fm_uint_to_str(size / (1024 * 1024), buf);
        int len = (int)strlen(buf);
        buf[len] = 'M'; buf[len + 1] = 'B'; buf[len + 2] = '\0';
    }
}