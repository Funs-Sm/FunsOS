/* taskbar.c - FunsOS 任务栏实现
 * macOS-inspired 顶部任务栏, 左侧显示系统信息, 右侧显示开始菜单和应用按钮
 * 使用直接 framebuffer 渲染, 像素格式 0xAABBGGRR
 */

#include "taskbar.h"
#include "font.h"
#include "rtc.h"
#include "sys_api.h"
#include "start_menu.h"
#include "gui_core.h"
#include "stddef.h"

/* ── 颜色常量 (0xAABBGGRR) ── */
#define COLOR_BAR_BG        0xCC1E1E2E
#define COLOR_BAR_BORDER    0xFF3A3A4A
#define COLOR_TEXT          0xFFD0D0E0
#define COLOR_TEXT_BRIGHT   0xFFFFFFFF
#define COLOR_LOGO_FG       0xFF6C8CFF
#define COLOR_START_BTN_BG  0xFF2A2A3A
#define COLOR_START_BTN_HOVER 0xFF3A3A5A
#define COLOR_START_BTN_ACTIVE 0xFF4A4A6A
#define COLOR_NET_ONLINE    0xFF44CC44
#define COLOR_NET_OFFLINE   0xFFCC4444
#define COLOR_BATTERY       0xFF44CC44
#define COLOR_WIN_BTN_BG    0xFF2A2A3A
#define COLOR_WIN_BTN_ACTIVE 0xFF444460
#define COLOR_MENU_BG       0xDD1E1E2E
#define COLOR_MENU_HOVER    0xFF3A6FF5
#define COLOR_MENU_SEP      0xFF4A4A5A
#define COLOR_MENU_SHUTDOWN 0xFFFF6666

/* ── 任务栏按钮最大数 ── */
#define MAX_WIN_BUTTONS 32

/* ── 窗口按钮 ── */
typedef struct {
    uint32_t win_id;
    char title[64];
    int x, w;
} win_button_t;

/* ── 静态全局变量 ── */
static int      g_screen_w        = 0;
static int      g_screen_h        = 0;
static uint32_t *g_fb             = 0;
static uint32_t g_pitch           = 0;
static char     g_clock_str[16]   = "00:00";
static char     g_date_str[16]    = "";
static int      g_network_active  = 0;     /* 0=离线, 1=在线 */
static int      g_battery_level   = 100;   /* 0-100 */
static int      g_hovered_item    = -1;    /* -1=无, 0=logo, 1=start, 100+=窗口按钮 */
static taskbar_menu_t *g_active_menu = 0;

static win_button_t g_win_btns[MAX_WIN_BUTTONS];
static int          g_win_btn_count = 0;

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

/* ── 辅助: 测量字符串宽度 ── */
static int text_width(const char *str) {
    int w = 0;
    while (*str) { w += FONT_GLYPH_WIDTH; str++; }
    return w;
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

/* ═══════════════════════════════════════════════════════
 * 初始化 / 清理
 * ═══════════════════════════════════════════════════════ */

void taskbar_init(int screen_w, int screen_h, uint32_t *fb, uint32_t pitch) {
    g_screen_w       = screen_w;
    g_screen_h       = screen_h;
    g_fb             = fb;
    g_pitch          = pitch;
    g_hovered_item   = -1;
    g_active_menu    = 0;
    g_win_btn_count  = 0;
    g_network_active = 1;  /* 默认在线 */
    g_battery_level  = 85;

    g_clock_str[0] = '0'; g_clock_str[1] = '0';
    g_clock_str[2] = ':'; g_clock_str[3] = '0';
    g_clock_str[4] = '0'; g_clock_str[5] = 0;

    g_date_str[0] = 0;

    taskbar_update_clock();
}

void taskbar_cleanup(void) {
    g_win_btn_count = 0;
    g_hovered_item  = -1;
    g_active_menu   = 0;
}

/* ═══════════════════════════════════════════════════════
 * 时钟更新
 * ═══════════════════════════════════════════════════════ */

void taskbar_update_clock(void) {
    rtc_time_t tm;
    rtc_read_time(&tm);

    /* 格式化时间 HH:MM */
    g_clock_str[0] = '0' + (tm.hour / 10);
    g_clock_str[1] = '0' + (tm.hour % 10);
    g_clock_str[2] = ':';
    g_clock_str[3] = '0' + (tm.minute / 10);
    g_clock_str[4] = '0' + (tm.minute % 10);
    g_clock_str[5] = 0;

    /* 格式化日期 */
    g_date_str[0] = '0' + (tm.month / 10);
    g_date_str[1] = '0' + (tm.month % 10);
    g_date_str[2] = '/';
    g_date_str[3] = '0' + (tm.day / 10);
    g_date_str[4] = '0' + (tm.day % 10);
    g_date_str[5] = 0;
}

/* ═══════════════════════════════════════════════════════
 * 渲染
 * ═══════════════════════════════════════════════════════ */

void taskbar_render(void) {
    int bar_h = GUI_TASKBAR_HEIGHT;
    int bar_w = g_screen_w;

    /* 1. 任务栏背景 (半透明深色) */
    fb_fill_rect(0, 0, bar_w, bar_h, COLOR_BAR_BG);

    /* 2. 底部边框线 (1px) */
    fb_fill_rect(0, bar_h - 1, bar_w, 1, COLOR_BAR_BORDER);

    /* ── 左侧区域 ── */
    int lx = 10;

    /* Apple-like "F" logo */
    {
        uint32_t logo_color = COLOR_LOGO_FG;
        if (g_hovered_item == 0) {
            logo_color = 0xFFA0B8FF;  /* hover 高亮 */
        }
        /* 绘制一个简单的 "F" 图标: 8x12 像素 */
        int logo_x = lx;
        int logo_y = (bar_h - 12) / 2;
        /* F 的竖线 */
        for (int row = 0; row < 12; row++) {
            fb_pixel(logo_x + 1, logo_y + row, logo_color);
            fb_pixel(logo_x + 2, logo_y + row, logo_color);
        }
        /* F 的顶部横线 */
        for (int col = 1; col < 8; col++) {
            fb_pixel(logo_x + col, logo_y, logo_color);
            fb_pixel(logo_x + col, logo_y + 1, logo_color);
        }
        /* F 的中部横线 */
        for (int col = 1; col < 6; col++) {
            fb_pixel(logo_x + col, logo_y + 5, logo_color);
            fb_pixel(logo_x + col, logo_y + 6, logo_color);
        }
        lx = logo_x + 18;
    }

    /* 时钟 HH:MM */
    {
        int text_y = (bar_h - FONT_GLYPH_HEIGHT) / 2;
        lx += fb_draw_text(lx, text_y, g_clock_str, COLOR_TEXT_BRIGHT, COLOR_BAR_BG);
        lx += 2;
    }

    /* 日期 (可选, 如果空间足够) */
    if (g_screen_w > 640 && g_date_str[0]) {
        int text_y = (bar_h - FONT_GLYPH_HEIGHT) / 2;
        lx += 4;
        lx += fb_draw_text(lx, text_y, g_date_str, COLOR_TEXT, COLOR_BAR_BG);
        lx += 4;
    }

    /* 网络指示器 (小圆点) */
    {
        int dot_x = lx + 2;
        int dot_y = bar_h / 2 - 2;
        uint32_t dot_color = g_network_active ? COLOR_NET_ONLINE : COLOR_NET_OFFLINE;
        /* 绘制 4x4 小圆点 */
        for (int dy = 0; dy < 4; dy++) {
            for (int dx = 0; dx < 4; dx++) {
                int d = (dx - 1) * (dx - 1) + (dy - 1) * (dy - 1);
                if (d <= 3) {
                    fb_pixel(dot_x + dx, dot_y + dy, dot_color);
                }
            }
        }
        lx = dot_x + 8;
    }

    /* 电池指示器 */
    {
        int text_y = (bar_h - FONT_GLYPH_HEIGHT) / 2;
        /* 电池: 显示百分比数字 */
        char bat_str[8];
        bat_str[0] = '0' + (g_battery_level / 100);
        bat_str[1] = '0' + ((g_battery_level / 10) % 10);
        bat_str[2] = '0' + (g_battery_level % 10);
        bat_str[3] = '%';
        bat_str[4] = 0;
        lx += fb_draw_text(lx, text_y, bat_str, COLOR_TEXT, COLOR_BAR_BG);
    }

    /* ── 右侧区域 ── */
    int rx = g_screen_w - 10;

    /* 窗口按钮 (从右向左排列, 在 Start 按钮左边) */
    {
        int start_btn_w = text_width("Start") + 16;
        int right_limit = rx - start_btn_w - 4;

        for (int i = g_win_btn_count - 1; i >= 0; i--) {
            win_button_t *btn = &g_win_btns[i];
            int bw = 120;
            if (bw > 200) bw = 200;
            int bx = right_limit - bw;
            if (bx < g_screen_w / 2) break;  /* 不给左侧留太少的空间 */

            btn->x = bx;
            btn->w = bw;

            uint32_t btn_bg = COLOR_WIN_BTN_BG;
            if (g_hovered_item == (100 + i)) {
                btn_bg = COLOR_WIN_BTN_ACTIVE;
            }

            fb_fill_rect(bx, 4, bw, bar_h - 8, btn_bg);

            int text_y = (bar_h - FONT_GLYPH_HEIGHT) / 2;
            /* 截断过长标题 */
            char disp_title[32];
            int tlen = str_len(btn->title);
            int max_chars = (bw - 8) / FONT_GLYPH_WIDTH;
            if (max_chars > 30) max_chars = 30;
            if (max_chars < 1) max_chars = 1;
            if (tlen > max_chars) {
                for (int j = 0; j < max_chars - 3; j++)
                    disp_title[j] = btn->title[j];
                disp_title[max_chars - 3] = '.';
                disp_title[max_chars - 2] = '.';
                disp_title[max_chars - 1] = '.';
                disp_title[max_chars] = 0;
            } else {
                for (int j = 0; j < tlen; j++)
                    disp_title[j] = btn->title[j];
                disp_title[tlen] = 0;
            }
            fb_draw_text(bx + 4, text_y, disp_title, COLOR_TEXT_BRIGHT, btn_bg);

            right_limit = bx - 4;
        }
    }

    /* Start 按钮 (最右侧) */
    {
        int start_btn_w = text_width("Start") + 16;
        int start_btn_x = rx - start_btn_w;

        uint32_t start_bg = COLOR_START_BTN_BG;
        if (start_menu_is_visible()) {
            start_bg = COLOR_START_BTN_ACTIVE;
        } else if (g_hovered_item == 1) {
            start_bg = COLOR_START_BTN_HOVER;
        }

        fb_fill_rect(start_btn_x, 0, start_btn_w, bar_h, start_bg);

        int text_y = (bar_h - FONT_GLYPH_HEIGHT) / 2;
        fb_draw_text(start_btn_x + 8, text_y, "Start", COLOR_TEXT_BRIGHT, start_bg);
    }
}

/* ═══════════════════════════════════════════════════════
 * 事件处理
 * ═══════════════════════════════════════════════════════ */

int taskbar_handle_click(int x, int y) {
    int bar_h = GUI_TASKBAR_HEIGHT;

    /* 不在任务栏区域 */
    if (y < 0 || y >= bar_h)
        return 0;

    /* 检查 Start 按钮 (右侧) */
    {
        int start_btn_w = text_width("Start") + 16;
        int start_btn_x = g_screen_w - 10 - start_btn_w;
        if (point_in_rect(x, y, start_btn_x, 0, start_btn_w, bar_h)) {
            start_menu_toggle();
            return 1;
        }
    }

    /* 检查窗口按钮 */
    for (int i = g_win_btn_count - 1; i >= 0; i--) {
        win_button_t *btn = &g_win_btns[i];
        if (point_in_rect(x, y, btn->x, 4, btn->w, bar_h - 8)) {
            /* 激活对应窗口 */
            sys_spawn("", "");  /* 占位: 后续应切换窗口焦点 */
            return 1;
        }
    }

    /* 检查 Logo 区域 */
    if (x >= 10 && x < 28 && y >= 0 && y < bar_h) {
        /* 点击 Logo: 可切换为系统菜单 */
        return 1;
    }

    return 1;  /* 在任务栏内 */
}

int taskbar_handle_hover(int x, int y) {
    int bar_h = GUI_TASKBAR_HEIGHT;

    if (y < 0 || y >= bar_h) {
        g_hovered_item = -1;
        return 0;
    }

    /* 检查 Logo */
    if (x >= 10 && x < 28) {
        g_hovered_item = 0;
        return 1;
    }

    /* 检查 Start 按钮 */
    {
        int start_btn_w = text_width("Start") + 16;
        int start_btn_x = g_screen_w - 10 - start_btn_w;
        if (point_in_rect(x, y, start_btn_x, 0, start_btn_w, bar_h)) {
            g_hovered_item = 1;
            return 1;
        }
    }

    /* 检查窗口按钮 */
    for (int i = g_win_btn_count - 1; i >= 0; i--) {
        win_button_t *btn = &g_win_btns[i];
        if (point_in_rect(x, y, btn->x, 4, btn->w, bar_h - 8)) {
            g_hovered_item = 100 + i;
            return 1;
        }
    }

    g_hovered_item = -1;
    return 0;
}

/* ═══════════════════════════════════════════════════════
 * 菜单管理
 * ═══════════════════════════════════════════════════════ */

void taskbar_menu_show(taskbar_menu_t *menu) {
    g_active_menu = menu;
    if (menu) menu->visible = 1;
}

void taskbar_menu_hide(void) {
    if (g_active_menu) g_active_menu->visible = 0;
    g_active_menu = 0;
}

int taskbar_menu_is_visible(void) {
    return g_active_menu != 0 && g_active_menu->visible;
}

taskbar_menu_t *taskbar_get_active_menu(void) {
    return g_active_menu;
}

/* ═══════════════════════════════════════════════════════
 * 应用启动
 * ═══════════════════════════════════════════════════════ */

void taskbar_launch_app(const char *exec_path) {
    if (exec_path && exec_path[0]) {
        sys_spawn(exec_path, "");
    }
}

/* ═══════════════════════════════════════════════════════
 * 窗口按钮管理
 * ═══════════════════════════════════════════════════════ */

void taskbar_add_window_button(uint32_t win_id, const char *title) {
    if (g_win_btn_count >= MAX_WIN_BUTTONS)
        return;

    win_button_t *btn = &g_win_btns[g_win_btn_count];
    btn->win_id = win_id;
    btn->x = 0;
    btn->w = 120;
    str_copy(btn->title, title, 64);
    g_win_btn_count++;
}

void taskbar_remove_window_button(uint32_t win_id) {
    for (int i = 0; i < g_win_btn_count; i++) {
        if (g_win_btns[i].win_id == win_id) {
            for (int k = i; k < g_win_btn_count - 1; k++)
                g_win_btns[k] = g_win_btns[k + 1];
            g_win_btn_count--;
            return;
        }
    }
}