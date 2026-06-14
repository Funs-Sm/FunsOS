/* start_menu.c - FunsOS 开始菜单实现
 * 从顶部任务栏右侧弹出的应用程序启动菜单
 * 使用直接 framebuffer 渲染, 像素格式 0xAABBGGRR
 */

#include "start_menu.h"
#include "font.h"
#include "sys_api.h"
#include "gui_core.h"
#include "stddef.h"

/* ── 颜色常量 (0xAABBGGRR) ── */
#define COLOR_MENU_BG        0xDD1E1E2E
#define COLOR_MENU_BORDER    0xFF3A3A5A
#define COLOR_ITEM_HOVER     0x443A6FF5
#define COLOR_ITEM_TEXT      0xFFD0D0E0
#define COLOR_ITEM_TEXT_BRIGHT 0xFFFFFFFF
#define COLOR_SEPARATOR      0xFF4A4A5A
#define COLOR_SHUTDOWN_TEXT  0xFFFF6666
#define COLOR_RESTART_TEXT   0xFFFFAA44

/* ── 菜单项最大数 ── */
#define MAX_ITEMS 32

/* ── 静态全局变量 ── */
static int      g_visible     = 0;
static int      g_screen_w    = 0;
static int      g_screen_h    = 0;
static uint32_t *g_fb         = 0;
static uint32_t g_pitch       = 0;
static int      g_hovered_idx = -1;  /* 当前悬停的菜单项索引 */

static start_menu_item_t g_items[MAX_ITEMS];
static int               g_item_count = 0;

/* ── 辅助: 钳制 ── */
static int clamp_i(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ── 辅助: 像素写入 ── */
static inline void fb_pixel(int x, int y, uint32_t color) {
    if (x >= 0 && x < g_screen_w && y >= 0 && y < g_screen_h) {
        g_fb[y * (g_pitch / 4) + x] = color;
    }
}

/* ── 辅助: 填充矩形 ── */
static void fb_fill_rect(int x, int y, int w, int h, uint32_t color) {
    int x0 = clamp_i(x, 0, g_screen_w);
    int y0 = clamp_i(y, 0, g_screen_h);
    int x1 = clamp_i(x + w, 0, g_screen_w);
    int y1 = clamp_i(y + h, 0, g_screen_h);
    int stride = g_pitch / 4;
    for (int py = y0; py < y1; py++) {
        uint32_t *row = &g_fb[py * stride + x0];
        for (int px = x0; px < x1; px++) {
            *row++ = color;
        }
    }
}

/* ── 辅助: 绘制文字 ── */
static int fb_draw_text(int x, int y, const char *str, uint32_t fg, uint32_t bg) {
    int start_x = x;
    while (*str) {
        char c = *str++;
        if (c < 32 || c > 127) continue;
        const uint8_t *glyph = font_data[(int)(c - 32)];
        for (int row = 0; row < FONT_GLYPH_HEIGHT; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < FONT_GLYPH_WIDTH; col++) {
                if (bits & (0x80 >> col)) {
                    fb_pixel(x + col, y + row, fg);
                } else if (bg != 0xFFFFFFFF) {
                    fb_pixel(x + col, y + row, bg);
                }
            }
        }
        x += FONT_GLYPH_WIDTH;
    }
    return x - start_x;
}

/* ── 辅助: 点是否在矩形内 ── */
static int point_in_rect(int px, int py, int rx, int ry, int rw, int rh) {
    return (px >= rx && px < rx + rw && py >= ry && py < ry + rh);
}

/* ── 辅助: 字符串长度 ── */
static int str_len(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

/* ── 辅助: 字符串复制 ── */
static void str_copy(char *dst, const char *src, int max) {
    int i;
    for (i = 0; i < max - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = 0;
}

/* ── 计算菜单总高度 ── */
static int menu_content_height(void) {
    int h = 0;
    for (int i = 0; i < g_item_count; i++) {
        h += START_MENU_ITEM_H;
    }
    return h;
}

/* ── 获取菜单左上角坐标 ── */
static void menu_get_pos(int *mx, int *my) {
    *mx = g_screen_w - START_MENU_WIDTH;
    if (*mx < 0) *mx = 0;
    *my = GUI_TASKBAR_HEIGHT;
}

/* ═══════════════════════════════════════════════════════
 * 初始化 / 清理
 * ═══════════════════════════════════════════════════════ */

void start_menu_init(int screen_w, int screen_h, uint32_t *fb, uint32_t pitch) {
    g_screen_w    = screen_w;
    g_screen_h    = screen_h;
    g_fb          = fb;
    g_pitch       = pitch;
    g_visible     = 0;
    g_hovered_idx = -1;
    g_item_count  = 0;

    /* 添加默认菜单项 */
    start_menu_item_t item;

    /* Terminal */
    for (int i = 0; i < 64; i++) item.label[i] = 0;
    for (int i = 0; i < 128; i++) { item.icon_path[i] = 0; item.exec_path[i] = 0; }
    item.type = MENU_ITEM_APP;
    item.label[0] = 'T'; item.label[1] = 'e'; item.label[2] = 'r';
    item.label[3] = 'm'; item.label[4] = 'i'; item.label[5] = 'n';
    item.label[6] = 'a'; item.label[7] = 'l'; item.label[8] = 0;
    item.exec_path[0] = '/'; item.exec_path[1] = 'a'; item.exec_path[2] = 'p';
    item.exec_path[3] = 'p'; item.exec_path[4] = 's'; item.exec_path[5] = '/';
    item.exec_path[6] = 't'; item.exec_path[7] = 'e'; item.exec_path[8] = 'r';
    item.exec_path[9] = 'm'; item.exec_path[10] = 0;
    start_menu_add_item(&item);

    /* Editor */
    for (int i = 0; i < 64; i++) item.label[i] = 0;
    for (int i = 0; i < 128; i++) { item.icon_path[i] = 0; item.exec_path[i] = 0; }
    item.type = MENU_ITEM_APP;
    item.label[0] = 'E'; item.label[1] = 'd'; item.label[2] = 'i';
    item.label[3] = 't'; item.label[4] = 'o'; item.label[5] = 'r';
    item.label[6] = 0;
    item.exec_path[0] = '/'; item.exec_path[1] = 'a'; item.exec_path[2] = 'p';
    item.exec_path[3] = 'p'; item.exec_path[4] = 's'; item.exec_path[5] = '/';
    item.exec_path[6] = 'e'; item.exec_path[7] = 'd'; item.exec_path[8] = 'i';
    item.exec_path[9] = 't'; item.exec_path[10] = 0;
    start_menu_add_item(&item);

    /* Files */
    for (int i = 0; i < 64; i++) item.label[i] = 0;
    for (int i = 0; i < 128; i++) { item.icon_path[i] = 0; item.exec_path[i] = 0; }
    item.type = MENU_ITEM_APP;
    item.label[0] = 'F'; item.label[1] = 'i'; item.label[2] = 'l';
    item.label[3] = 'e'; item.label[4] = 's'; item.label[5] = 0;
    item.exec_path[0] = '/'; item.exec_path[1] = 'a'; item.exec_path[2] = 'p';
    item.exec_path[3] = 'p'; item.exec_path[4] = 's'; item.exec_path[5] = '/';
    item.exec_path[6] = 'f'; item.exec_path[7] = 'm'; item.exec_path[8] = 0;
    start_menu_add_item(&item);

    /* Calculator */
    for (int i = 0; i < 64; i++) item.label[i] = 0;
    for (int i = 0; i < 128; i++) { item.icon_path[i] = 0; item.exec_path[i] = 0; }
    item.type = MENU_ITEM_APP;
    item.label[0] = 'C'; item.label[1] = 'a'; item.label[2] = 'l';
    item.label[3] = 'c'; item.label[4] = 'u'; item.label[5] = 'l';
    item.label[6] = 'a'; item.label[7] = 't'; item.label[8] = 'o';
    item.label[9] = 'r'; item.label[10] = 0;
    item.exec_path[0] = '/'; item.exec_path[1] = 'a'; item.exec_path[2] = 'p';
    item.exec_path[3] = 'p'; item.exec_path[4] = 's'; item.exec_path[5] = '/';
    item.exec_path[6] = 'c'; item.exec_path[7] = 'a'; item.exec_path[8] = 'l';
    item.exec_path[9] = 'c'; item.exec_path[10] = 0;
    start_menu_add_item(&item);

    /* Paint */
    for (int i = 0; i < 64; i++) item.label[i] = 0;
    for (int i = 0; i < 128; i++) { item.icon_path[i] = 0; item.exec_path[i] = 0; }
    item.type = MENU_ITEM_APP;
    item.label[0] = 'P'; item.label[1] = 'a'; item.label[2] = 'i';
    item.label[3] = 'n'; item.label[4] = 't'; item.label[5] = 0;
    item.exec_path[0] = '/'; item.exec_path[1] = 'a'; item.exec_path[2] = 'p';
    item.exec_path[3] = 'p'; item.exec_path[4] = 's'; item.exec_path[5] = '/';
    item.exec_path[6] = 'p'; item.exec_path[7] = 'a'; item.exec_path[8] = 'i';
    item.exec_path[9] = 'n'; item.exec_path[10] = 't'; item.exec_path[11] = 0;
    start_menu_add_item(&item);

    /* Settings */
    for (int i = 0; i < 64; i++) item.label[i] = 0;
    for (int i = 0; i < 128; i++) { item.icon_path[i] = 0; item.exec_path[i] = 0; }
    item.type = MENU_ITEM_APP;
    item.label[0] = 'S'; item.label[1] = 'e'; item.label[2] = 't';
    item.label[3] = 't'; item.label[4] = 'i'; item.label[5] = 'n';
    item.label[6] = 'g'; item.label[7] = 's'; item.label[8] = 0;
    item.exec_path[0] = '/'; item.exec_path[1] = 'a'; item.exec_path[2] = 'p';
    item.exec_path[3] = 'p'; item.exec_path[4] = 's'; item.exec_path[5] = '/';
    item.exec_path[6] = 's'; item.exec_path[7] = 'e'; item.exec_path[8] = 't';
    item.exec_path[9] = 't'; item.exec_path[10] = 'i'; item.exec_path[11] = 'n';
    item.exec_path[12] = 'g'; item.exec_path[13] = 's'; item.exec_path[14] = 0;
    start_menu_add_item(&item);

    /* ── 分隔线 ── */
    {
        start_menu_item_t sep;
        for (int i = 0; i < 64; i++) sep.label[i] = 0;
        for (int i = 0; i < 128; i++) { sep.icon_path[i] = 0; sep.exec_path[i] = 0; }
        sep.type = MENU_ITEM_SEPARATOR;
        sep.label[0] = 0;
        start_menu_add_item(&sep);
    }

    /* Shutdown */
    for (int i = 0; i < 64; i++) item.label[i] = 0;
    for (int i = 0; i < 128; i++) { item.icon_path[i] = 0; item.exec_path[i] = 0; }
    item.type = MENU_ITEM_ACTION;
    item.label[0] = 'S'; item.label[1] = 'h'; item.label[2] = 'u';
    item.label[3] = 't'; item.label[4] = 'd'; item.label[5] = 'o';
    item.label[6] = 'w'; item.label[7] = 'n'; item.label[8] = 0;
    item.exec_path[0] = 0;
    start_menu_add_item(&item);

    /* Restart */
    for (int i = 0; i < 64; i++) item.label[i] = 0;
    for (int i = 0; i < 128; i++) { item.icon_path[i] = 0; item.exec_path[i] = 0; }
    item.type = MENU_ITEM_ACTION;
    item.label[0] = 'R'; item.label[1] = 'e'; item.label[2] = 's';
    item.label[3] = 't'; item.label[4] = 'a'; item.label[5] = 'r';
    item.label[6] = 't'; item.label[7] = 0;
    item.exec_path[0] = 0;
    start_menu_add_item(&item);
}

void start_menu_cleanup(void) {
    g_item_count  = 0;
    g_visible     = 0;
    g_hovered_idx = -1;
}

/* ═══════════════════════════════════════════════════════
 * 显示 / 隐藏 / 切换
 * ═══════════════════════════════════════════════════════ */

void start_menu_show(void) {
    g_visible = 1;
}

void start_menu_hide(void) {
    g_visible     = 0;
    g_hovered_idx = -1;
}

void start_menu_toggle(void) {
    g_visible = !g_visible;
    if (!g_visible) {
        g_hovered_idx = -1;
    }
}

int start_menu_is_visible(void) {
    return g_visible;
}

/* ═══════════════════════════════════════════════════════
 * 渲染
 * ═══════════════════════════════════════════════════════ */

void start_menu_render(void) {
    if (!g_visible)
        return;

    int menu_x, menu_y;
    menu_get_pos(&menu_x, &menu_y);

    int menu_w = START_MENU_WIDTH;
    int menu_h = menu_content_height();
    if (menu_h > START_MENU_HEIGHT)
        menu_h = START_MENU_HEIGHT;
    if (menu_h < START_MENU_ITEM_H * 3)
        menu_h = START_MENU_ITEM_H * 3;

    /* 菜单背景 */
    fb_fill_rect(menu_x, menu_y, menu_w, menu_h, COLOR_MENU_BG);

    /* 菜单边框 */
    fb_fill_rect(menu_x, menu_y, menu_w, 1, COLOR_MENU_BORDER);
    fb_fill_rect(menu_x, menu_y + menu_h - 1, menu_w, 1, COLOR_MENU_BORDER);
    fb_fill_rect(menu_x, menu_y, 1, menu_h, COLOR_MENU_BORDER);
    fb_fill_rect(menu_x + menu_w - 1, menu_y, 1, menu_h, COLOR_MENU_BORDER);

    /* 绘制菜单项 */
    int iy = menu_y + 4;
    for (int i = 0; i < g_item_count; i++) {
        start_menu_item_t *it = &g_items[i];

        it->x = menu_x + 2;
        it->y = iy;
        it->w = menu_w - 4;
        it->h = START_MENU_ITEM_H;

        if (it->type == MENU_ITEM_SEPARATOR) {
            /* 分隔线 */
            int sep_y = iy + START_MENU_ITEM_H / 2;
            fb_fill_rect(menu_x + 16, sep_y, menu_w - 32, 1, COLOR_SEPARATOR);
            iy += START_MENU_ITEM_H;
            continue;
        }

        /* 悬停高亮 */
        if (g_hovered_idx == i) {
            fb_fill_rect(it->x, it->y, it->w, it->h, COLOR_ITEM_HOVER);
        }

        /* 文字颜色 */
        uint32_t text_color = COLOR_ITEM_TEXT;
        uint32_t text_bg = (g_hovered_idx == i) ? COLOR_ITEM_HOVER : COLOR_MENU_BG;

        /* 判断是否为关机/重启 */
        {
            int is_shutdown = 1;
            int is_restart = 1;
            const char *sl = "Shutdown";
            const char *rl = "Restart";
            for (int j = 0; j < 8 && sl[j]; j++) {
                if (it->label[j] != sl[j]) { is_shutdown = 0; break; }
            }
            if (is_shutdown && it->label[8] != 0) is_shutdown = 0;
            for (int j = 0; j < 7 && rl[j]; j++) {
                if (it->label[j] != rl[j]) { is_restart = 0; break; }
            }
            if (is_restart && it->label[7] != 0) is_restart = 0;

            if (is_shutdown) {
                text_color = COLOR_SHUTDOWN_TEXT;
            } else if (is_restart) {
                text_color = COLOR_RESTART_TEXT;
            }
        }

        /* 文字位置: 垂直居中 */
        int text_y = iy + (START_MENU_ITEM_H - FONT_GLYPH_HEIGHT) / 2;
        fb_draw_text(menu_x + 16, text_y, it->label, text_color, text_bg);

        iy += START_MENU_ITEM_H;
    }
}

/* ═══════════════════════════════════════════════════════
 * 事件处理
 * ═══════════════════════════════════════════════════════ */

int start_menu_handle_click(int x, int y) {
    if (!g_visible)
        return 0;

    int menu_x, menu_y;
    menu_get_pos(&menu_x, &menu_y);

    int menu_h = menu_content_height();
    if (menu_h > START_MENU_HEIGHT)
        menu_h = START_MENU_HEIGHT;

    /* 点击菜单外部则关闭 */
    if (!point_in_rect(x, y, menu_x, menu_y, START_MENU_WIDTH, menu_h)) {
        start_menu_hide();
        return 0;
    }

    /* 检查点击了哪个菜单项 */
    for (int i = 0; i < g_item_count; i++) {
        start_menu_item_t *it = &g_items[i];

        if (it->type == MENU_ITEM_SEPARATOR)
            continue;

        if (point_in_rect(x, y, it->x, it->y, it->w, it->h)) {
            /* 应用启动项 */
            if (it->type == MENU_ITEM_APP && it->exec_path[0]) {
                sys_spawn(it->exec_path, "");
            }

            start_menu_hide();
            return 1;
        }
    }

    return 1;
}

int start_menu_handle_hover(int x, int y) {
    if (!g_visible)
        return 0;

    int menu_x, menu_y;
    menu_get_pos(&menu_x, &menu_y);

    int menu_h = menu_content_height();
    if (menu_h > START_MENU_HEIGHT)
        menu_h = START_MENU_HEIGHT;

    /* 鼠标在菜单外 */
    if (!point_in_rect(x, y, menu_x, menu_y, START_MENU_WIDTH, menu_h)) {
        g_hovered_idx = -1;
        return 0;
    }

    /* 检查悬停在哪个菜单项 */
    for (int i = 0; i < g_item_count; i++) {
        start_menu_item_t *it = &g_items[i];

        if (it->type == MENU_ITEM_SEPARATOR)
            continue;

        if (point_in_rect(x, y, it->x, it->y, it->w, it->h)) {
            g_hovered_idx = i;
            return 1;
        }
    }

    g_hovered_idx = -1;
    return 0;
}

/* ═══════════════════════════════════════════════════════
 * 菜单项管理
 * ═══════════════════════════════════════════════════════ */

int start_menu_add_item(const start_menu_item_t *item) {
    if (g_item_count >= MAX_ITEMS)
        return -1;

    g_items[g_item_count] = *item;
    g_items[g_item_count].x = 0;
    g_items[g_item_count].y = 0;
    g_items[g_item_count].w = 0;
    g_items[g_item_count].h = 0;
    g_item_count++;
    return 0;
}

int start_menu_remove_item(const char *label) {
    for (int i = 0; i < g_item_count; i++) {
        int match = 1;
        for (int j = 0; j < 64 && (label[j] || g_items[i].label[j]); j++) {
            if (label[j] != g_items[i].label[j]) { match = 0; break; }
        }
        if (match) {
            for (int k = i; k < g_item_count - 1; k++)
                g_items[k] = g_items[k + 1];
            g_item_count--;
            return 0;
        }
    }
    return -1;
}