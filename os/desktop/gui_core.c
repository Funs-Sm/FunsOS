/* gui_core.c - FunsOS GUI 核心引擎
 * 管理启动加载画面 -> 桌面环境的完整生命周期
 * 负责主事件循环、输入分发、帧渲染和应用启动
 */

#include "gui_core.h"
#include "loading.h"
#include "display_server.h"
#include "sys_api.h"
#include "stddef.h"
#include "string.h"
#include "desktop.h"
#include "taskbar.h"
#include "start_menu.h"
#include "window_mgr.h"
#include "keyboard.h"
#include "mouse.h"
#include "rtc.h"
#include "timer.h"
#include "font.h"

/* ── 颜色定义 (AARRGGBB 格式) ── */
#define COLOR_BG           0xFF1E1E2E   /* 桌面背景: 深蓝灰 */
#define COLOR_CURSOR_FILL  0xFFFFFFFF   /* 光标内部: 白色 */
#define COLOR_CURSOR_OUTLINE 0xFF000000 /* 光标轮廓: 黑色 */
#define COLOR_TASKBAR_BG   0xFF1C1C1C   /* 任务栏背景: 深灰 */
#define COLOR_TASKBAR_TEXT 0xFFFFFFFF   /* 任务栏文字: 白色 */
#define COLOR_LOADING_BG   0xFF0F0F23   /* 加载背景 */

/* ── 光标形状: 11x19 箭头, 1=填充, 0=透明 ── */
static const uint8_t cursor_shape[19][11] = {
    {1,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0},
    {1,1,1,0,0,0,0,0,0,0,0},
    {1,1,1,1,0,0,0,0,0,0,0},
    {1,1,1,1,1,0,0,0,0,0,0},
    {1,1,1,1,1,1,0,0,0,0,0},
    {1,1,1,1,1,1,1,0,0,0,0},
    {1,1,1,1,1,1,1,1,0,0,0},
    {1,1,1,1,1,1,1,1,1,0,0},
    {1,1,1,1,1,1,1,1,1,1,0},
    {1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,0,0,0,0},
    {1,1,0,0,1,1,1,0,0,0,0},
    {1,1,0,0,0,1,1,1,0,0,0},
    {0,1,1,0,0,0,1,1,1,0,0},
    {0,1,1,0,0,0,0,1,1,1,0},
    {0,0,1,1,0,0,0,1,1,0,0},
    {0,0,1,1,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0},
};

/* ── GUI 核心全局状态 ── */
static int      g_screen_w        = 0;
static int      g_screen_h        = 0;
static uint32_t *g_fb             = 0;
static uint32_t g_pitch           = 0;
static int      g_boot_state      = GUI_BOOT_LOADING;
static uint32_t g_desktop_win_id  = 0;
static int      g_running         = 0;

/* 光标状态 */
static int      g_cursor_x        = 0;
static int      g_cursor_y        = 0;
static int      g_cursor_visible  = 1;

/* 加载状态 */
static int      g_loading_frames  = 0;
static int      g_loading_target  = 180;  /* ~3秒 @ 60fps = 180帧 */

/* 鼠标按钮状态 */
static uint8_t  g_mouse_buttons   = 0;
static uint8_t  g_mouse_prev      = 0;

/* 桌面图标数组 */
#define GUI_MAX_ICONS 32
typedef struct {
    char    name[32];
    char    exec[64];
    int     x, y;
    int     w, h;
} gui_icon_t;

static gui_icon_t g_icons[GUI_MAX_ICONS];
static int        g_icon_count = 0;

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

/* ── 辅助: 绘制填充矩形到 framebuffer ── */
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

/* ── 辅助: 绘制文字到 framebuffer ── */
static void fb_draw_text(int x, int y, const char *str, uint32_t fg, uint32_t bg) {
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
}

/* ── 辅助: 测量字符串宽度 ── */
static int text_width(const char *str) {
    int w = 0;
    while (*str++) w += FONT_GLYPH_WIDTH;
    return w;
}

/* ── 辅助: 检查点是否在矩形内 ── */
static int point_in_rect(int px, int py, int rx, int ry, int rw, int rh) {
    return (px >= rx && px < rx + rw && py >= ry && py < ry + rh);
}

/* ── 桌面图标初始化 ── */
static void icons_init(void) {
    g_icon_count = 0;

    /* 图标间距和起始位置 */
    int ix = 40, iy = 40;
    int step_y = 80;

    /* 终端 */
    {
        gui_icon_t *ic = &g_icons[g_icon_count++];
        ic->x = ix; ic->y = iy; ic->w = 64; ic->h = 64;
        ic->name[0] = 'T'; ic->name[1] = 'e'; ic->name[2] = 'r';
        ic->name[3] = 'm'; ic->name[4] = 'i'; ic->name[5] = 'n';
        ic->name[6] = 'a'; ic->name[7] = 'l'; ic->name[8] = 0;
        ic->exec[0] = '/'; ic->exec[1] = 'a'; ic->exec[2] = 'p';
        ic->exec[3] = 'p'; ic->exec[4] = 's'; ic->exec[5] = '/';
        ic->exec[6] = 't'; ic->exec[7] = 'e'; ic->exec[8] = 'r';
        ic->exec[9] = 'm'; ic->exec[10] = 0;
        iy += step_y;
    }

    /* 文件管理器 */
    {
        gui_icon_t *ic = &g_icons[g_icon_count++];
        ic->x = ix; ic->y = iy; ic->w = 64; ic->h = 64;
        ic->name[0] = 'F'; ic->name[1] = 'i'; ic->name[2] = 'l';
        ic->name[3] = 'e'; ic->name[4] = 's'; ic->name[5] = 0;
        ic->exec[0] = '/'; ic->exec[1] = 'a'; ic->exec[2] = 'p';
        ic->exec[3] = 'p'; ic->exec[4] = 's'; ic->exec[5] = '/';
        ic->exec[6] = 'f'; ic->exec[7] = 'm'; ic->exec[8] = 0;
        iy += step_y;
    }

    /* 文本编辑器 */
    {
        gui_icon_t *ic = &g_icons[g_icon_count++];
        ic->x = ix; ic->y = iy; ic->w = 64; ic->h = 64;
        ic->name[0] = 'E'; ic->name[1] = 'd'; ic->name[2] = 'i';
        ic->name[3] = 't'; ic->name[4] = 'o'; ic->name[5] = 'r';
        ic->name[6] = 0;
        ic->exec[0] = '/'; ic->exec[1] = 'a'; ic->exec[2] = 'p';
        ic->exec[3] = 'p'; ic->exec[4] = 's'; ic->exec[5] = '/';
        ic->exec[6] = 'e'; ic->exec[7] = 'd'; ic->exec[8] = 'i';
        ic->exec[9] = 't'; ic->exec[10] = 0;
        iy += step_y;
    }

    /* 设置 */
    {
        gui_icon_t *ic = &g_icons[g_icon_count++];
        ic->x = ix; ic->y = iy; ic->w = 64; ic->h = 64;
        ic->name[0] = 'S'; ic->name[1] = 'e'; ic->name[2] = 't';
        ic->name[3] = 't'; ic->name[4] = 'i'; ic->name[5] = 'n';
        ic->name[6] = 'g'; ic->name[7] = 's'; ic->name[8] = 0;
        ic->exec[0] = '/'; ic->exec[1] = 'a'; ic->exec[2] = 'p';
        ic->exec[3] = 'p'; ic->exec[4] = 's'; ic->exec[5] = '/';
        ic->exec[6] = 's'; ic->exec[7] = 'e'; ic->exec[8] = 't';
        ic->exec[9] = 't'; ic->exec[10] = 'i'; ic->exec[11] = 'n';
        ic->exec[12] = 'g'; ic->exec[13] = 's'; ic->exec[14] = 0;
    }
}

/* ── 绘制桌面图标 ── */
static void icons_render(void) {
    for (int i = 0; i < g_icon_count; i++) {
        gui_icon_t *ic = &g_icons[i];
        int cx = ic->x + ic->w / 2;

        /* 绘制图标背景 (圆角矩形) */
        uint32_t icon_bg = 0xFF2D2D3F;
        fb_fill_rect(ic->x + 4, ic->y + 4, ic->w - 8, ic->h - 8, icon_bg);

        /* 图标内字母 (首字符) */
        char letter[2];
        letter[0] = ic->name[0];
        letter[1] = 0;
        int lw = text_width(letter);
        fb_draw_text(cx - lw / 2, ic->y + ic->h / 2 - FONT_GLYPH_HEIGHT / 2,
                     letter, 0xFFFFFFFF, icon_bg);

        /* 图标名称 */
        int nw = text_width(ic->name);
        fb_draw_text(cx - nw / 2, ic->y + ic->h + 4,
                     ic->name, 0xFFD0D0E0, 0xFFFFFFFF);
    }
}

/* ── 桌面图标点击检测 ── */
static int icons_handle_click(int mx, int my) {
    for (int i = g_icon_count - 1; i >= 0; i--) {
        gui_icon_t *ic = &g_icons[i];
        if (point_in_rect(mx, my, ic->x, ic->y, ic->w, ic->h + 20)) {
            if (ic->exec[0]) {
                sys_spawn(ic->exec, "");
            }
            return 1;
        }
    }
    return 0;
}

/* ── 渲染桌面背景 ── */
static void desktop_bg_render(void) {
    fb_fill_rect(0, 0, g_screen_w, g_screen_h, COLOR_BG);
}

/* ── 渲染任务栏 ── */
static void gui_taskbar_render(void) {
    int bar_y = g_screen_h - GUI_TASKBAR_HEIGHT;
    int bar_w = g_screen_w;
    int bar_h = GUI_TASKBAR_HEIGHT;

    /* 任务栏背景 */
    fb_fill_rect(0, bar_y, bar_w, bar_h, COLOR_TASKBAR_BG);

    /* 顶部高光线 */
    fb_fill_rect(0, bar_y, bar_w, 1, 0xFF3A3A4A);

    /* 开始按钮 (右侧) */
    int start_btn_w = 50;
    int start_btn_x = bar_w - start_btn_w;
    uint32_t start_color = start_menu_is_visible() ? 0xFF3A3A5A : 0xFF2A2A3A;
    fb_fill_rect(start_btn_x, bar_y, start_btn_w, bar_h, start_color);
    fb_draw_text(start_btn_x + 8, bar_y + 7, "Start", COLOR_TASKBAR_TEXT, start_color);

    /* 时间显示 (左侧) */
    rtc_time_t tm;
    rtc_read_time(&tm);
    char time_str[16];
    time_str[0] = '0' + (tm.hour / 10);
    time_str[1] = '0' + (tm.hour % 10);
    time_str[2] = ':';
    time_str[3] = '0' + (tm.minute / 10);
    time_str[4] = '0' + (tm.minute % 10);
    time_str[5] = 0;

    fb_draw_text(8, bar_y + 7, time_str, COLOR_TASKBAR_TEXT, COLOR_TASKBAR_BG);
}

/* ── 任务栏点击处理 ── */
static int gui_taskbar_handle_click(int mx, int my) {
    int bar_y = g_screen_h - GUI_TASKBAR_HEIGHT;
    if (my < bar_y || my >= g_screen_h) return 0;

    /* 开始按钮 (右侧) */
    int start_btn_w = 50;
    int start_btn_x = g_screen_w - start_btn_w;
    if (point_in_rect(mx, my, start_btn_x, bar_y, start_btn_w, GUI_TASKBAR_HEIGHT)) {
        start_menu_toggle();
        return 1;
    }

    return 1; /* 点击在任务栏区域内 */
}

/* ── 渲染开始菜单 ── */
static void gui_start_menu_render(void) {
    if (!start_menu_is_visible()) return;

    int menu_w = 220;
    int menu_h = 300;
    int menu_x = g_screen_w - menu_w;
    int menu_y = g_screen_h - GUI_TASKBAR_HEIGHT - menu_h;

    /* 菜单背景 */
    uint32_t menu_bg = 0xFF242436;
    fb_fill_rect(menu_x, menu_y, menu_w, menu_h, menu_bg);

    /* 边框 */
    fb_fill_rect(menu_x, menu_y, menu_w, 2, 0xFF0078D4);
    fb_fill_rect(menu_x, menu_y, 2, menu_h, 0xFF0078D4);
    fb_fill_rect(menu_x + menu_w - 2, menu_y, 2, menu_h, 0xFF0078D4);
    fb_fill_rect(menu_x, menu_y + menu_h - 2, menu_w, 2, 0xFF0078D4);

    /* 菜单项 */
    const char *items[] = {
        "Terminal", "File Manager", "Editor",
        "Settings", "Calculator", "Paint",
        "Notepad",  0
    };
    int iy = menu_y + 12;
    uint32_t text_color = 0xFFD0D0E0;

    for (int i = 0; items[i]; i++) {
        fb_draw_text(menu_x + 16, iy, items[i], text_color, menu_bg);
        iy += 26;
    }

    /* 底部分隔线 */
    iy += 4;
    fb_fill_rect(menu_x + 8, iy, menu_w - 16, 1, 0xFF3A3A5A);
    iy += 8;

    /* Shutdown */
    fb_draw_text(menu_x + 16, iy, "Shutdown", 0xFFFF6666, menu_bg);
}

/* ── 开始菜单点击处理 ── */
static int gui_start_menu_handle_click(int mx, int my) {
    if (!start_menu_is_visible()) return 0;

    int menu_w = 220;
    int menu_h = 300;
    int menu_x = g_screen_w - menu_w;
    int menu_y = g_screen_h - GUI_TASKBAR_HEIGHT - menu_h;

    /* 点击菜单外部则关闭 */
    if (!point_in_rect(mx, my, menu_x, menu_y, menu_w, menu_h)) {
        start_menu_hide();
        return 0;
    }

    /* 菜单项区域 */
    int item_h = 26;
    int item_y = menu_y + 12;
    const char *exec_paths[] = {
        "/apps/term", "/apps/fm", "/apps/edit",
        "/apps/settings", "/apps/calc", "/apps/paint",
        "/apps/notepad", 0
    };

    /* 检查每个菜单项 */
    for (int i = 0; i < 7; i++) {
        if (point_in_rect(mx, my, menu_x + 8, item_y, menu_w - 16, item_h)) {
            start_menu_hide();
            if (exec_paths[i]) {
                sys_spawn(exec_paths[i], "");
            }
            return 1;
        }
        item_y += 26;
    }

    /* 检查 Shutdown 按钮 */
    item_y += 8 + 4; /* 跳过分隔线 */
    if (point_in_rect(mx, my, menu_x + 8, item_y, menu_w - 16, item_h)) {
        start_menu_hide();
        g_running = 0;
        return 1;
    }

    return 1;
}

/* ── 渲染 display_server 窗口到 framebuffer ── */
static void ds_windows_render(void) {
    /* 调用 display_server 的窗口渲染 (不清屏不画光标) */
    ds_render_windows_only();
}

/* ── 总渲染函数 ── */
static void gui_full_render(void) {
    /* 1. 桌面背景 */
    desktop_bg_render();

    /* 2. 桌面图标 */
    icons_render();

    /* 3. 调用 window_mgr 更新窗口离屏缓冲区, 然后 blit 到 framebuffer */
    window_mgr_render();
    ds_windows_render();

    /* 4. 任务栏 */
    gui_taskbar_render();

    /* 5. 开始菜单 (在最上层, 覆盖任务栏部分) */
    gui_start_menu_render();

    /* 6. 鼠标光标 */
    gui_cursor_render();
}

/* ── 加载阶段渲染 ── */
static void loading_phase_render(void) {
    loading_screen_render_frame();
}

/* ── 键盘事件处理 ── */
static void process_keyboard(void) {
    if (!keyboard_has_data()) return;

    keyboard_event_t ke;
    while (keyboard_get_event(&ke)) {
        if (!(ke.flags & KEY_PRESSED)) continue;

        /* Win 键 (扫描码 0x5B) : 切换开始菜单 */
        if (ke.scancode == 0x5B) {
            start_menu_toggle();
            continue;
        }

        /* ESC: 关闭开始菜单 */
        if (ke.ascii == 27) {
            if (start_menu_is_visible()) {
                start_menu_hide();
            }
            continue;
        }

        /* 其他按键传递给焦点窗口或忽略 */
    }
}

/* ── 鼠标事件处理 ── */
static void process_mouse(void) {
    if (!mouse_has_data()) return;

    mouse_event_t me;
    while (mouse_get_event(&me)) {
        g_cursor_x += me.dx;
        g_cursor_y -= me.dy;
        g_cursor_x = clamp_i(g_cursor_x, 0, g_screen_w - 1);
        g_cursor_y = clamp_i(g_cursor_y, 0, g_screen_h - 1);

        g_mouse_prev = g_mouse_buttons;
        g_mouse_buttons = me.buttons;

        /* 鼠标左键按下 */
        if ((g_mouse_buttons & MOUSE_LEFT) && !(g_mouse_prev & MOUSE_LEFT)) {
            int mx = g_cursor_x, my = g_cursor_y;

            /* 1. 开始菜单优先处理 */
            if (start_menu_is_visible() && gui_start_menu_handle_click(mx, my))
                continue;

            /* 2. 任务栏 */
            if (gui_taskbar_handle_click(mx, my))
                continue;

            /* 3. 桌面图标 */
            if (icons_handle_click(mx, my))
                continue;

            /* 4. 窗口管理器 */
            window_mgr_handle_click(mx, my);
        }

        /* 鼠标移动 */
        if (me.dx != 0 || me.dy != 0) {
            window_mgr_handle_move(g_cursor_x, g_cursor_y);
        }
    }
}

/* ═══════════════════════════════════════════════════════
 * 公共接口实现
 * ═══════════════════════════════════════════════════════ */

void gui_core_init(int screen_w, int screen_h, uint32_t *framebuffer, uint32_t pitch) {
    g_screen_w       = screen_w;
    g_screen_h       = screen_h;
    g_fb             = framebuffer;
    g_pitch          = pitch;
    g_boot_state     = GUI_BOOT_LOADING;
    g_running        = 0;
    g_cursor_x       = screen_w / 2;
    g_cursor_y       = screen_h / 2;
    g_cursor_visible = 1;
    g_loading_frames = 0;

    /* 初始化 display_server 作为窗口后端 */
    display_server_init(framebuffer, (uint32_t)screen_w, (uint32_t)screen_h, pitch);

    /* 创建桌面根窗口 (全屏) */
    g_desktop_win_id = ds_create_window(0, 0, (uint32_t)screen_w,
                                        (uint32_t)screen_h, "FunsOS Desktop");
    if (g_desktop_win_id != 0) {
        /* 桌面作为背景层 */
        ds_draw_rect(g_desktop_win_id, 0, 0, (uint32_t)screen_w,
                     (uint32_t)screen_h, COLOR_BG);
    }

    /* 初始化加载画面 */
    loading_screen_init(screen_w, screen_h, framebuffer, pitch);

    /* 初始化桌面图标 */
    icons_init();
}

void gui_core_run(void) {
    g_running = 1;

    /* ══════════════════════════════════════════════
     * 阶段 1: 加载画面 (~3 秒)
     * ══════════════════════════════════════════════ */
    while (g_boot_state == GUI_BOOT_LOADING && g_running) {
        loading_phase_render();
        g_loading_frames++;

        /* 每约100ms更新一次进度状态 */
        if (g_loading_frames < g_loading_target / 3) {
            loading_screen_set_progress("Initializing kernel modules...", 33);
        } else if (g_loading_frames < g_loading_target * 2 / 3) {
            loading_screen_set_progress("Starting display server...", 66);
        } else {
            loading_screen_set_progress("Preparing desktop...", 90);
        }

        /* 帧延迟 */
        timer_sleep(16); /* ~60fps */

        if (g_loading_frames >= g_loading_target) {
            loading_screen_mark_done();
            g_boot_state = GUI_BOOT_DESKTOP;
            loading_screen_set_progress("Welcome to FunsOS", 100);
            /* 最后一帧加载画面 */
            loading_screen_render_frame();
            timer_sleep(500); /* 短暂停留 */
        }
    }

    /* ══════════════════════════════════════════════
     * 阶段 2: 初始化桌面组件
     * ══════════════════════════════════════════════ */
    desktop_init(g_screen_w, g_screen_h);

    /* 隐藏 gui_core 自己创建的桌面窗口 (我们用直接 framebuffer 渲染背景) */
    if (g_desktop_win_id != 0) {
        ds_hide_window(g_desktop_win_id);
    }

    /* 隐藏 desktop_init 创建的 "Desktop" 窗口 (同样不需要它覆盖背景) */
    {
        uint32_t dt_win = ds_find_window_by_title("Desktop");
        if (dt_win != 0) {
            ds_hide_window(dt_win);
        }
    }

    /* ══════════════════════════════════════════════
     * 阶段 3: 主事件循环
     * ══════════════════════════════════════════════ */
    while (g_running) {
        /* 处理输入事件 */
        process_keyboard();
        process_mouse();

        /* 渲染帧 */
        gui_full_render();

        /* 帧延迟 */
        timer_sleep(16);
    }

    /* 清理 */
    gui_core_shutdown();
}

void gui_core_shutdown(void) {
    g_running = 0;

    loading_screen_mark_done();

    if (g_desktop_win_id != 0) {
        ds_destroy_window(g_desktop_win_id);
        g_desktop_win_id = 0;
    }

    desktop_shutdown();

    /* 清屏为黑色 */
    fb_fill_rect(0, 0, g_screen_w, g_screen_h, 0xFF000000);
}

uint32_t gui_get_screen_w(void) {
    return (uint32_t)g_screen_w;
}

uint32_t gui_get_screen_h(void) {
    return (uint32_t)g_screen_h;
}

uint32_t *gui_get_framebuffer(void) {
    return g_fb;
}

uint32_t gui_get_pitch(void) {
    return g_pitch;
}

int gui_launch_app(const char *app_name, const char *exec_path) {
    (void)app_name;
    return sys_spawn(exec_path, "");
}

void gui_show_desktop(void) {
    g_boot_state = GUI_BOOT_DESKTOP;
}

void gui_hide_desktop(void) {
    /* 不直接关闭桌面, 但可以标记为后台 */
}

/* ── 鼠标光标接口 ── */

void gui_cursor_set_pos(int x, int y) {
    g_cursor_x = clamp_i(x, 0, g_screen_w - 1);
    g_cursor_y = clamp_i(y, 0, g_screen_h - 1);
}

void gui_cursor_get_pos(int *x, int *y) {
    if (x) *x = g_cursor_x;
    if (y) *y = g_cursor_y;
}

void gui_cursor_render(void) {
    if (!g_cursor_visible) return;

    int cx = g_cursor_x;
    int cy = g_cursor_y;

    /* 绘制11x19箭头光标 */
    for (int row = 0; row < 19; row++) {
        for (int col = 0; col < 11; col++) {
            int px = cx + col;
            int py = cy + row;
            if (px < 0 || px >= g_screen_w || py < 0 || py >= g_screen_h)
                continue;

            if (cursor_shape[row][col]) {
                /* 轮廓像素: 如果相邻位置有不填充的, 则是轮廓 */
                int is_outline = 0;
                /* 检查上下左右是否有0 */
                if (row > 0 && !cursor_shape[row-1][col]) is_outline = 1;
                if (row < 18 && !cursor_shape[row+1][col]) is_outline = 1;
                if (col > 0 && !cursor_shape[row][col-1]) is_outline = 1;
                if (col < 10 && !cursor_shape[row][col+1]) is_outline = 1;

                if (is_outline) {
                    fb_pixel(px, py, COLOR_CURSOR_OUTLINE);
                } else {
                    fb_pixel(px, py, COLOR_CURSOR_FILL);
                }
            }
        }
    }
}

void gui_cursor_set_visible(int visible) {
    g_cursor_visible = visible;
}

uint32_t gui_get_desktop_win(void) {
    return g_desktop_win_id;
}