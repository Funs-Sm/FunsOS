/* settings.c - FUNSOS 系统设置实现
 * 完整的图形化系统配置工具，支持显示、鼠标、键盘、日期时间和
 * 关于信息五个分类，提供滑块、下拉框、复选框等交互控件，
 * 支持配置持久化保存和加载。
 */

#include "settings.h"
#include "sys_api.h"
#include "stddef.h"
#include "string.h"
#include "stdlib.h"

/* ================================================================
 * 配置常量
 * ================================================================ */
#define WIN_W             500         /* 窗口宽度 */
#define WIN_H             420         /* 窗口高度 */
#define SIDEBAR_W         140         /* 左侧分类栏宽度 */
#define CONTENT_X         156         /* 内容区起始 X */
#define CONTENT_W         (WIN_W - CONTENT_X - 16) /* 内容区宽度 */
#define CONTENT_Y         20          /* 内容区起始 Y */
#define BOTTOM_BAR_H      44          /* 底部按钮栏高度 */
#define BOTTOM_BAR_Y      (WIN_H - BOTTOM_BAR_H) /* 底部按钮栏 Y */
#define LINE_H            26          /* 行高 */
#define SLIDER_W          180         /* 滑块宽度 */
#define SLIDER_H          10          /* 滑块轨道高度 */
#define SLIDER_THUMB_W    12          /* 滑块拇指宽度 */
#define BTN_W             80          /* 按钮宽度 */
#define BTN_H             28          /* 按钮高度 */
#define BTN_GAP           8           /* 按钮间距 */
#define DROPDOWN_W        180         /* 下拉框宽度 */
#define DROPDOWN_H        22          /* 下拉框高度 */
#define CHECKBOX_SIZE     14          /* 复选框大小 */
#define MAX_CONFIG_KEYS   64          /* 最大配置键数 */
#define MAX_KEY_LEN       64          /* 最大配置键名长度 */
#define MAX_VAL_LEN       256         /* 最大配置值长度 */
#define CONFIG_FILE       "/etc/funsos/config.cfg"  /* 配置文件路径 */

/* ================================================================
 * 颜色定义
 * ================================================================ */
static const sys_color_t COLOR_BG          = { 0xF5, 0xF5, 0xF5, 0xFF };
static const sys_color_t COLOR_SIDEBAR     = { 0xE8, 0xE8, 0xE8, 0xFF };
static const sys_color_t COLOR_SIDEBAR_ACT = { 0xD0, 0xD0, 0xD0, 0xFF };
static const sys_color_t COLOR_SIDEBAR_HOV = { 0xDD, 0xDD, 0xDD, 0xFF };
static const sys_color_t COLOR_TEXT        = { 0x1A, 0x1A, 0x1A, 0xFF };
static const sys_color_t COLOR_TEXT_DIM    = { 0x66, 0x66, 0x66, 0xFF };
static const sys_color_t COLOR_TITLE       = { 0x00, 0x44, 0x88, 0xFF };
static const sys_color_t COLOR_LABEL       = { 0x33, 0x33, 0x33, 0xFF };
static const sys_color_t COLOR_BTN         = { 0xE0, 0xE0, 0xE0, 0xFF };
static const sys_color_t COLOR_BTN_HOVER   = { 0xCC, 0xDD, 0xFF, 0xFF };
static const sys_color_t COLOR_BTN_PRIMARY = { 0x00, 0x78, 0xD4, 0xFF };
static const sys_color_t COLOR_BTN_TEXT    = { 0x00, 0x00, 0x00, 0xFF };
static const sys_color_t COLOR_BTN_TEXT_W  = { 0xFF, 0xFF, 0xFF, 0xFF };
static const sys_color_t COLOR_BTN_BORDER  = { 0xAA, 0xAA, 0xAA, 0xFF };
static const sys_color_t COLOR_SLIDER_TRACK = { 0xCC, 0xCC, 0xCC, 0xFF };
static const sys_color_t COLOR_SLIDER_FILL = { 0x00, 0x78, 0xD4, 0xFF };
static const sys_color_t COLOR_SLIDER_THUMB = { 0x00, 0x78, 0xD4, 0xFF };
static const sys_color_t COLOR_DROPDOWN_BG = { 0xFF, 0xFF, 0xFF, 0xFF };
static const sys_color_t COLOR_DROPDOWN_BORDER = { 0xAA, 0xAA, 0xAA, 0xFF };
static const sys_color_t COLOR_DROPDOWN_OPEN = { 0xE8, 0xEE, 0xFF, 0xFF };
static const sys_color_t COLOR_CHECKBOX_BG = { 0xFF, 0xFF, 0xFF, 0xFF };
static const sys_color_t COLOR_CHECKBOX_BORDER = { 0x88, 0x88, 0x88, 0xFF };
static const sys_color_t COLOR_CHECKBOX_CHECK = { 0x00, 0x78, 0xD4, 0xFF };
static const sys_color_t COLOR_SEPARATOR   = { 0xDD, 0xDD, 0xDD, 0xFF };
static const sys_color_t COLOR_WHITE       = { 0xFF, 0xFF, 0xFF, 0xFF };

/* ================================================================
 * 控件类型
 * ================================================================ */
#define CTRL_LABEL      0    /* 标签 */
#define CTRL_SLIDER     1    /* 滑块 */
#define CTRL_DROPDOWN   2    /* 下拉框 */
#define CTRL_CHECKBOX   3    /* 复选框 */
#define CTRL_BUTTON     4    /* 按钮 */
#define CTRL_TEXT       5    /* 只读文本 */
#define CTRL_SEPARATOR  6    /* 分隔线 */

/* 控件定义 */
typedef struct settings_control {
    uint32_t type;          /* 控件类型 */
    int      x;             /* 在内容区内的 X 偏移 */
    int      y;             /* 在内容区内的 Y 偏移 */
    int      w;             /* 宽度 */
    int      h;             /* 高度 */
    const char *label;      /* 标签文本 */
    const char *config_key; /* 配置键名 */
    /* 数据 */
    int      min_val;       /* 最小值(滑块/下拉框) */
    int      max_val;       /* 最大值(滑块/下拉框) */
    int      cur_val;       /* 当前值 */
    const char **options;   /* 下拉框选项列表 */
    int      option_count;  /* 选项数量 */
    uint8_t  checked;       /* 复选框是否选中 */
    uint8_t  dropdown_open; /* 下拉框是否展开 */
    uint8_t  hover;         /* 鼠标悬停 */
    /* 滑块相关 */
    int      slider_dragging; /* 滑块是否正在拖动 */
} settings_ctrl_t;

/* ================================================================
 * 配置存储
 * ================================================================ */
typedef struct {
    char key[MAX_KEY_LEN];
    char value[MAX_VAL_LEN];
} config_entry_t;

/* ================================================================
 * 设置状态
 * ================================================================ */
typedef struct {
    uint32_t category;          /* 当前分类 */
    int      category_hover;    /* 鼠标悬停的分类索引 */
    settings_ctrl_t *controls;  /* 当前分类的控件数组 */
    int      control_count;     /* 控件数量 */
    int      hover_ctrl;        /* 鼠标悬停的控件索引 */
    int      active_ctrl;       /* 当前激活的控件索引 */
    uint8_t  modified;          /* 是否有未保存修改 */
    uint8_t  need_reload;       /* 需要重新加载控件 */
    /* 配置存储 */
    config_entry_t configs[MAX_CONFIG_KEYS];
    int      config_count;
    /* 窗口 */
    sys_window_t *win;
    /* 底部按钮状态 */
    int      btn_ok_hover;
    int      btn_cancel_hover;
    int      btn_apply_hover;
} settings_state_t;

static settings_state_t g_settings;

/* ================================================================
 * 下拉框选项数据
 * ================================================================ */
static const char *g_resolution_options[] = {
    "640x480", "800x600", "1024x768", "1280x720",
    "1366x768", "1600x900", "1920x1080", "2560x1440"
};
static const char *g_theme_options[] = {
    "Default", "Light", "Dark", "Classic", "High Contrast"
};
static const char *g_refresh_options[] = {
    "30 Hz", "60 Hz", "75 Hz", "120 Hz", "144 Hz"
};
static const char *g_speed_options[] = {
    "Very Slow", "Slow", "Normal", "Fast", "Very Fast"
};
static const char *g_delay_options[] = {
    "Long", "Medium Long", "Medium", "Medium Short", "Short"
};
static const char *g_rate_options[] = {
    "Slow", "Medium", "Fast", "Very Fast"
};
static const char *g_layout_options[] = {
    "US English", "UK English", "French (AZERTY)",
    "German (QWERTZ)", "Japanese", "Chinese (Pinyin)"
};
static const char *g_date_format_options[] = {
    "YYYY-MM-DD", "MM/DD/YYYY", "DD/MM/YYYY",
    "YYYY.MM.DD", "DD.MM.YYYY"
};
static const char *g_time_format_options[] = {
    "24-Hour", "12-Hour (AM/PM)"
};
static const char *g_timezone_options[] = {
    "UTC-12:00", "UTC-08:00 (PST)", "UTC-05:00 (EST)",
    "UTC+00:00 (GMT)", "UTC+01:00 (CET)", "UTC+03:00 (MSK)",
    "UTC+05:30 (IST)", "UTC+08:00 (CST)", "UTC+09:00 (JST)",
    "UTC+10:00 (AEST)"
};

/* ================================================================
 * 内部函数声明
 * ================================================================ */
static void settings_build_controls(void);
static void settings_build_display_controls(void);
static void settings_build_mouse_controls(void);
static void settings_build_keyboard_controls(void);
static void settings_build_datetime_controls(void);
static void settings_build_about_controls(void);
static void settings_render(sys_window_t *win);
static void settings_render_sidebar(sys_window_t *win);
static void settings_render_content(sys_window_t *win);
static void settings_render_bottom_bar(sys_window_t *win);
static void settings_render_control(sys_window_t *win, settings_ctrl_t *ctrl);
static void settings_render_slider(sys_window_t *win, settings_ctrl_t *ctrl);
static void settings_render_dropdown(sys_window_t *win, settings_ctrl_t *ctrl);
static void settings_render_checkbox(sys_window_t *win, settings_ctrl_t *ctrl);
static void settings_render_button(sys_window_t *win, settings_ctrl_t *ctrl);
static void settings_render_label(sys_window_t *win, settings_ctrl_t *ctrl);
static void settings_render_text(sys_window_t *win, settings_ctrl_t *ctrl);
static void settings_handle_mouse_click(int x, int y);
static void settings_handle_mouse_move(int x, int y);
static void settings_handle_mouse_up(void);
static void settings_handle_key(uint32_t key);
static int  settings_find_control(int x, int y);
static int  settings_find_sidebar(int y);
static void settings_apply_changes(void);
static void settings_cancel_changes(void);
static void settings_save_config(void);
static void settings_load_config(void);
static int  settings_get_config_int(const char *key, int default_val);
static void settings_set_config_int(const char *key, int value);
static const char *settings_get_config_str(const char *key, const char *default_val);
static void settings_set_config_str(const char *key, const char *value);
static int  settings_str_to_int(const char *str, int default_val);
static void settings_int_to_str(int val, char *buf, int size);
static void settings_strip_newline(char *str);

/* 控件数组（动态分配，每个分类有不同的控件） */
static settings_ctrl_t g_controls[32];
static int g_control_count = 0;

/* ================================================================
 * 初始化和主循环
 * ================================================================ */
int settings_init(void)
{
    memset(&g_settings, 0, sizeof(g_settings));
    g_settings.category = SETTINGS_CAT_DISPLAY;
    g_settings.modified = 0;
    g_settings.need_reload = 1;
    g_settings.hover_ctrl = -1;
    g_settings.active_ctrl = -1;
    g_settings.category_hover = -1;
    g_settings.btn_ok_hover = 0;
    g_settings.btn_cancel_hover = 0;
    g_settings.btn_apply_hover = 0;
    g_settings.config_count = 0;

    /* 加载配置 */
    settings_load_config();

    return 0;
}

void settings_run(void)
{
    sys_window_t *win = sys_create_window(160, 60, WIN_W, WIN_H, "FUNSOS System Settings");
    if (win == NULL) return;

    g_settings.win = win;
    g_settings.need_reload = 1;

    sys_event_t event;
    while (1) {
        if (g_settings.need_reload) {
            settings_build_controls();
            g_settings.need_reload = 0;
        }

        if (sys_poll_event(&event) != 0) {
            settings_render(win);
            continue;
        }

        if (event.type == SYS_EVENT_WINDOW_CLOSE) {
            if (g_settings.modified) {
                /* 有未保存修改，提示保存？这里简化处理 */
            }
            break;
        }

        if (event.type == SYS_EVENT_KEY_PRESS) {
            settings_handle_key(event.param1);
        }

        if (event.type == SYS_EVENT_MOUSE_CLICK) {
            settings_handle_mouse_click((int)event.param1, (int)event.param2);
        }

        if (event.type == SYS_EVENT_MOUSE_MOVE) {
            settings_handle_mouse_move((int)event.param1, (int)event.param2);
        }

        settings_render(win);
    }

    g_settings.win = NULL;
    sys_destroy_window(win);
}

int settings_show_display(void)  { g_settings.category = SETTINGS_CAT_DISPLAY; g_settings.need_reload = 1; return 0; }
int settings_show_mouse(void)    { g_settings.category = SETTINGS_CAT_MOUSE; g_settings.need_reload = 1; return 0; }
int settings_show_keyboard(void) { g_settings.category = SETTINGS_CAT_KEYBOARD; g_settings.need_reload = 1; return 0; }
int settings_show_datetime(void) { g_settings.category = SETTINGS_CAT_DATETIME; g_settings.need_reload = 1; return 0; }
int settings_show_about(void)    { g_settings.category = SETTINGS_CAT_ABOUT; g_settings.need_reload = 1; return 0; }

int settings_get_int(const char *key, int default_val)
{
    return settings_get_config_int(key, default_val);
}

void settings_set_int(const char *key, int value)
{
    settings_set_config_int(key, value);
}

const char *settings_get_str(const char *key, const char *default_val)
{
    return settings_get_config_str(key, default_val);
}

void settings_set_str(const char *key, const char *value)
{
    settings_set_config_str(key, value);
}

/* ================================================================
 * 控件构建
 * ================================================================ */
static void settings_build_controls(void)
{
    g_control_count = 0;
    switch (g_settings.category) {
    case SETTINGS_CAT_DISPLAY:  settings_build_display_controls(); break;
    case SETTINGS_CAT_MOUSE:    settings_build_mouse_controls(); break;
    case SETTINGS_CAT_KEYBOARD: settings_build_keyboard_controls(); break;
    case SETTINGS_CAT_DATETIME: settings_build_datetime_controls(); break;
    case SETTINGS_CAT_ABOUT:    settings_build_about_controls(); break;
    }
    g_settings.controls = g_controls;
    g_settings.control_count = g_control_count;
    g_settings.hover_ctrl = -1;
    g_settings.active_ctrl = -1;
}

/* 添加控件的辅助宏 */
#define ADD_CTRL(t, lx, ly, lw, lh, lbl, ckey) do { \
    if (g_control_count >= 32) break; \
    settings_ctrl_t *c = &g_controls[g_control_count++]; \
    memset(c, 0, sizeof(settings_ctrl_t)); \
    c->type = (t); c->x = (lx); c->y = (ly); c->w = (lw); c->h = (lh); \
    c->label = (lbl); c->config_key = (ckey); \
    c->min_val = 0; c->max_val = 100; c->cur_val = 50; \
    c->options = NULL; c->option_count = 0; \
    c->checked = 0; c->dropdown_open = 0; c->hover = 0; \
    c->slider_dragging = 0; \
} while (0)

static void settings_build_display_controls(void)
{
    int y = 0;

    /* 分类标题 */
    ADD_CTRL(CTRL_LABEL, 0, y, CONTENT_W, 28, "Display Settings", NULL);
    y += 36;

    ADD_CTRL(CTRL_SEPARATOR, 0, y, CONTENT_W, 2, NULL, NULL);
    y += 12;

    /* 分辨率 */
    ADD_CTRL(CTRL_LABEL, 0, y, 120, 20, "Resolution:", NULL);
    {
        settings_ctrl_t *c = &g_controls[g_control_count - 1];
        c->min_val = 0;
    }
    y += 20;
    ADD_CTRL(CTRL_DROPDOWN, 0, y, DROPDOWN_W, DROPDOWN_H, NULL, "display.resolution");
    {
        settings_ctrl_t *c = &g_controls[g_control_count - 1];
        c->options = g_resolution_options;
        c->option_count = 8;
        c->cur_val = settings_get_config_int("display.resolution", 6); /* 默认1920x1080 */
        if (c->cur_val < 0) c->cur_val = 0;
        if (c->cur_val >= c->option_count) c->cur_val = c->option_count - 1;
    }
    y += 32;

    /* 主题 */
    ADD_CTRL(CTRL_LABEL, 0, y, 120, 20, "Theme:", NULL);
    y += 20;
    ADD_CTRL(CTRL_DROPDOWN, 0, y, DROPDOWN_W, DROPDOWN_H, NULL, "display.theme");
    {
        settings_ctrl_t *c = &g_controls[g_control_count - 1];
        c->options = g_theme_options;
        c->option_count = 5;
        c->cur_val = settings_get_config_int("display.theme", 0);
        if (c->cur_val < 0) c->cur_val = 0;
        if (c->cur_val >= c->option_count) c->cur_val = c->option_count - 1;
    }
    y += 32;

    /* 刷新率 */
    ADD_CTRL(CTRL_LABEL, 0, y, 140, 20, "Refresh Rate:", NULL);
    y += 20;
    ADD_CTRL(CTRL_DROPDOWN, 0, y, DROPDOWN_W, DROPDOWN_H, NULL, "display.refresh");
    {
        settings_ctrl_t *c = &g_controls[g_control_count - 1];
        c->options = g_refresh_options;
        c->option_count = 5;
        c->cur_val = settings_get_config_int("display.refresh", 1); /* 默认60Hz */
        if (c->cur_val < 0) c->cur_val = 0;
        if (c->cur_val >= c->option_count) c->cur_val = c->option_count - 1;
    }
    y += 32;

    ADD_CTRL(CTRL_SEPARATOR, 0, y, CONTENT_W, 2, NULL, NULL);
    y += 12;

    /* 壁纸路径 */
    ADD_CTRL(CTRL_LABEL, 0, y, 140, 20, "Wallpaper:", NULL);
    y += 20;
    ADD_CTRL(CTRL_TEXT, 0, y, CONTENT_W, 20,
             settings_get_config_str("display.wallpaper", "/usr/share/wallpapers/default.bmp"), NULL);
    y += 28;
    ADD_CTRL(CTRL_BUTTON, 0, y, 100, 24, "Browse...", "display.wallpaper");
    {
        settings_ctrl_t *c = &g_controls[g_control_count - 1];
        c->cur_val = 0; /* 文件选择器 */
    }
    y += 34;

    /* 显示网格 */
    ADD_CTRL(CTRL_CHECKBOX, 0, y, CONTENT_W, 20, "Show desktop grid", "display.grid");
    {
        settings_ctrl_t *c = &g_controls[g_control_count - 1];
        c->checked = settings_get_config_int("display.grid", 0);
    }
    y += 28;

    /* 图标大小 */
    ADD_CTRL(CTRL_LABEL, 0, y, 140, 20, "Icon Size:", NULL);
    y += 20;
    ADD_CTRL(CTRL_SLIDER, 0, y, SLIDER_W, SLIDER_H, NULL, "display.icon_size");
    {
        settings_ctrl_t *c = &g_controls[g_control_count - 1];
        c->min_val = 16;
        c->max_val = 128;
        c->cur_val = settings_get_config_int("display.icon_size", 48);
    }
}

static void settings_build_mouse_controls(void)
{
    int y = 0;

    ADD_CTRL(CTRL_LABEL, 0, y, CONTENT_W, 28, "Mouse Settings", NULL);
    y += 36;

    ADD_CTRL(CTRL_SEPARATOR, 0, y, CONTENT_W, 2, NULL, NULL);
    y += 12;

    /* 指针速度 */
    ADD_CTRL(CTRL_LABEL, 0, y, 140, 20, "Pointer Speed:", NULL);
    y += 20;
    ADD_CTRL(CTRL_SLIDER, 0, y, SLIDER_W, SLIDER_H, NULL, "mouse.speed");
    {
        settings_ctrl_t *c = &g_controls[g_control_count - 1];
        c->min_val = 1;
        c->max_val = 20;
        c->cur_val = settings_get_config_int("mouse.speed", 10);
    }
    y += 20;

    /* 速度文本 */
    {
        char speed_text[32];
        settings_int_to_str(settings_get_config_int("mouse.speed", 10), speed_text, 32);
        ADD_CTRL(CTRL_TEXT, SLIDER_W + 8, y - 16, 60, 16, speed_text, NULL);
    }
    y += 16;

    /* 双击速度 */
    ADD_CTRL(CTRL_LABEL, 0, y, 180, 20, "Double-Click Speed:", NULL);
    y += 20;
    ADD_CTRL(CTRL_SLIDER, 0, y, SLIDER_W, SLIDER_H, NULL, "mouse.double_click");
    {
        settings_ctrl_t *c = &g_controls[g_control_count - 1];
        c->min_val = 1;
        c->max_val = 10;
        c->cur_val = settings_get_config_int("mouse.double_click", 5);
    }
    y += 20;

    {
        char dbl_text[32];
        settings_int_to_str(settings_get_config_int("mouse.double_click", 5), dbl_text, 32);
        ADD_CTRL(CTRL_TEXT, SLIDER_W + 8, y - 16, 60, 16, dbl_text, NULL);
    }
    y += 24;

    ADD_CTRL(CTRL_SEPARATOR, 0, y, CONTENT_W, 2, NULL, NULL);
    y += 12;

    /* 交换鼠标按键 */
    ADD_CTRL(CTRL_CHECKBOX, 0, y, CONTENT_W, 20, "Swap primary and secondary buttons", "mouse.swap_buttons");
    {
        settings_ctrl_t *c = &g_controls[g_control_count - 1];
        c->checked = settings_get_config_int("mouse.swap_buttons", 0);
    }
    y += 28;

    /* 指针轨迹 */
    ADD_CTRL(CTRL_CHECKBOX, 0, y, CONTENT_W, 20, "Enable pointer trails", "mouse.pointer_trails");
    {
        settings_ctrl_t *c = &g_controls[g_control_count - 1];
        c->checked = settings_get_config_int("mouse.pointer_trails", 0);
    }
    y += 28;

    /* 智能移动 */
    ADD_CTRL(CTRL_CHECKBOX, 0, y, CONTENT_W, 20, "Snap to default button", "mouse.snap_to");
    {
        settings_ctrl_t *c = &g_controls[g_control_count - 1];
        c->checked = settings_get_config_int("mouse.snap_to", 1);
    }
    y += 28;

    /* 滚轮行数 */
    ADD_CTRL(CTRL_LABEL, 0, y, 180, 20, "Scroll lines per notch:", NULL);
    y += 20;
    ADD_CTRL(CTRL_SLIDER, 0, y, SLIDER_W, SLIDER_H, NULL, "mouse.scroll_lines");
    {
        settings_ctrl_t *c = &g_controls[g_control_count - 1];
        c->min_val = 1;
        c->max_val = 20;
        c->cur_val = settings_get_config_int("mouse.scroll_lines", 3);
    }
    y += 20;

    {
        char scroll_text[32];
        settings_int_to_str(settings_get_config_int("mouse.scroll_lines", 3), scroll_text, 32);
        ADD_CTRL(CTRL_TEXT, SLIDER_W + 8, y - 16, 60, 16, scroll_text, NULL);
    }
}

static void settings_build_keyboard_controls(void)
{
    int y = 0;

    ADD_CTRL(CTRL_LABEL, 0, y, CONTENT_W, 28, "Keyboard Settings", NULL);
    y += 36;

    ADD_CTRL(CTRL_SEPARATOR, 0, y, CONTENT_W, 2, NULL, NULL);
    y += 12;

    /* 重复延迟 */
    ADD_CTRL(CTRL_LABEL, 0, y, 140, 20, "Repeat Delay:", NULL);
    y += 20;
    ADD_CTRL(CTRL_DROPDOWN, 0, y, DROPDOWN_W, DROPDOWN_H, NULL, "keyboard.repeat_delay");
    {
        settings_ctrl_t *c = &g_controls[g_control_count - 1];
        c->options = g_delay_options;
        c->option_count = 5;
        c->cur_val = settings_get_config_int("keyboard.repeat_delay", 2);
        if (c->cur_val < 0) c->cur_val = 0;
        if (c->cur_val >= c->option_count) c->cur_val = c->option_count - 1;
    }
    y += 32;

    /* 重复速率 */
    ADD_CTRL(CTRL_LABEL, 0, y, 140, 20, "Repeat Rate:", NULL);
    y += 20;
    ADD_CTRL(CTRL_DROPDOWN, 0, y, DROPDOWN_W, DROPDOWN_H, NULL, "keyboard.repeat_rate");
    {
        settings_ctrl_t *c = &g_controls[g_control_count - 1];
        c->options = g_rate_options;
        c->option_count = 4;
        c->cur_val = settings_get_config_int("keyboard.repeat_rate", 2);
        if (c->cur_val < 0) c->cur_val = 0;
        if (c->cur_val >= c->option_count) c->cur_val = c->option_count - 1;
    }
    y += 32;

    ADD_CTRL(CTRL_SEPARATOR, 0, y, CONTENT_W, 2, NULL, NULL);
    y += 12;

    /* 键盘布局 */
    ADD_CTRL(CTRL_LABEL, 0, y, 140, 20, "Keyboard Layout:", NULL);
    y += 20;
    ADD_CTRL(CTRL_DROPDOWN, 0, y, DROPDOWN_W, DROPDOWN_H, NULL, "keyboard.layout");
    {
        settings_ctrl_t *c = &g_controls[g_control_count - 1];
        c->options = g_layout_options;
        c->option_count = 6;
        c->cur_val = settings_get_config_int("keyboard.layout", 0);
        if (c->cur_val < 0) c->cur_val = 0;
        if (c->cur_val >= c->option_count) c->cur_val = c->option_count - 1;
    }
    y += 38;

    ADD_CTRL(CTRL_SEPARATOR, 0, y, CONTENT_W, 2, NULL, NULL);
    y += 12;

    /* 选项 */
    ADD_CTRL(CTRL_CHECKBOX, 0, y, CONTENT_W, 20, "Show Caps Lock indicator", "keyboard.caps_indicator");
    {
        settings_ctrl_t *c = &g_controls[g_control_count - 1];
        c->checked = settings_get_config_int("keyboard.caps_indicator", 1);
    }
    y += 28;

    ADD_CTRL(CTRL_CHECKBOX, 0, y, CONTENT_W, 20, "Show Num Lock indicator", "keyboard.num_indicator");
    {
        settings_ctrl_t *c = &g_controls[g_control_count - 1];
        c->checked = settings_get_config_int("keyboard.num_indicator", 1);
    }
    y += 28;

    ADD_CTRL(CTRL_CHECKBOX, 0, y, CONTENT_W, 20, "Enable sticky keys", "keyboard.sticky_keys");
    {
        settings_ctrl_t *c = &g_controls[g_control_count - 1];
        c->checked = settings_get_config_int("keyboard.sticky_keys", 0);
    }
    y += 28;

    ADD_CTRL(CTRL_CHECKBOX, 0, y, CONTENT_W, 20, "Enable filter keys", "keyboard.filter_keys");
    {
        settings_ctrl_t *c = &g_controls[g_control_count - 1];
        c->checked = settings_get_config_int("keyboard.filter_keys", 0);
    }
    y += 28;

    /* 光标闪烁速度 */
    ADD_CTRL(CTRL_LABEL, 0, y, 180, 20, "Cursor Blink Rate:", NULL);
    y += 20;
    ADD_CTRL(CTRL_SLIDER, 0, y, SLIDER_W, SLIDER_H, NULL, "keyboard.cursor_blink");
    {
        settings_ctrl_t *c = &g_controls[g_control_count - 1];
        c->min_val = 1;
        c->max_val = 10;
        c->cur_val = settings_get_config_int("keyboard.cursor_blink", 5);
    }
    y += 20;

    {
        char blink_text[32];
        settings_int_to_str(settings_get_config_int("keyboard.cursor_blink", 5), blink_text, 32);
        ADD_CTRL(CTRL_TEXT, SLIDER_W + 8, y - 16, 60, 16, blink_text, NULL);
    }
}

static void settings_build_datetime_controls(void)
{
    int y = 0;

    ADD_CTRL(CTRL_LABEL, 0, y, CONTENT_W, 28, "Date & Time Settings", NULL);
    y += 36;

    ADD_CTRL(CTRL_SEPARATOR, 0, y, CONTENT_W, 2, NULL, NULL);
    y += 12;

    /* 日期格式 */
    ADD_CTRL(CTRL_LABEL, 0, y, 140, 20, "Date Format:", NULL);
    y += 20;
    ADD_CTRL(CTRL_DROPDOWN, 0, y, DROPDOWN_W, DROPDOWN_H, NULL, "datetime.date_format");
    {
        settings_ctrl_t *c = &g_controls[g_control_count - 1];
        c->options = g_date_format_options;
        c->option_count = 5;
        c->cur_val = settings_get_config_int("datetime.date_format", 0);
        if (c->cur_val < 0) c->cur_val = 0;
        if (c->cur_val >= c->option_count) c->cur_val = c->option_count - 1;
    }
    y += 32;

    /* 时间格式 */
    ADD_CTRL(CTRL_LABEL, 0, y, 140, 20, "Time Format:", NULL);
    y += 20;
    ADD_CTRL(CTRL_DROPDOWN, 0, y, DROPDOWN_W, DROPDOWN_H, NULL, "datetime.time_format");
    {
        settings_ctrl_t *c = &g_controls[g_control_count - 1];
        c->options = g_time_format_options;
        c->option_count = 2;
        c->cur_val = settings_get_config_int("datetime.time_format", 0);
        if (c->cur_val < 0) c->cur_val = 0;
        if (c->cur_val >= c->option_count) c->cur_val = c->option_count - 1;
    }
    y += 32;

    /* 时区 */
    ADD_CTRL(CTRL_LABEL, 0, y, 140, 20, "Timezone:", NULL);
    y += 20;
    ADD_CTRL(CTRL_DROPDOWN, 0, y, DROPDOWN_W, DROPDOWN_H, NULL, "datetime.timezone");
    {
        settings_ctrl_t *c = &g_controls[g_control_count - 1];
        c->options = g_timezone_options;
        c->option_count = 10;
        c->cur_val = settings_get_config_int("datetime.timezone", 7); /* 默认UTC+8 */
        if (c->cur_val < 0) c->cur_val = 0;
        if (c->cur_val >= c->option_count) c->cur_val = c->option_count - 1;
    }
    y += 38;

    ADD_CTRL(CTRL_SEPARATOR, 0, y, CONTENT_W, 2, NULL, NULL);
    y += 12;

    /* 自动同步 */
    ADD_CTRL(CTRL_CHECKBOX, 0, y, CONTENT_W, 20, "Automatically sync with time server", "datetime.auto_sync");
    {
        settings_ctrl_t *c = &g_controls[g_control_count - 1];
        c->checked = settings_get_config_int("datetime.auto_sync", 1);
    }
    y += 28;

    /* 时间服务器 */
    ADD_CTRL(CTRL_LABEL, 0, y, 160, 20, "NTP Server:", NULL);
    y += 20;
    ADD_CTRL(CTRL_TEXT, 0, y, CONTENT_W, 20,
             settings_get_config_str("datetime.ntp_server", "pool.ntp.org"), NULL);
    y += 28;

    /* 显示秒 */
    ADD_CTRL(CTRL_CHECKBOX, 0, y, CONTENT_W, 20, "Show seconds in clock", "datetime.show_seconds");
    {
        settings_ctrl_t *c = &g_controls[g_control_count - 1];
        c->checked = settings_get_config_int("datetime.show_seconds", 0);
    }
    y += 28;

    /* 一周起始日 */
    ADD_CTRL(CTRL_LABEL, 0, y, 160, 20, "First day of week:", NULL);
    y += 20;
    ADD_CTRL(CTRL_DROPDOWN, 0, y, DROPDOWN_W, DROPDOWN_H, NULL, "datetime.first_day");
    {
        static const char *day_options[] = { "Sunday", "Monday" };
        settings_ctrl_t *c = &g_controls[g_control_count - 1];
        c->options = day_options;
        c->option_count = 2;
        c->cur_val = settings_get_config_int("datetime.first_day", 0);
        if (c->cur_val < 0) c->cur_val = 0;
        if (c->cur_val >= c->option_count) c->cur_val = c->option_count - 1;
    }
}

static void settings_build_about_controls(void)
{
    int y = 0;

    ADD_CTRL(CTRL_LABEL, 0, y, CONTENT_W, 28, "About FUNSOS", NULL);
    y += 36;

    ADD_CTRL(CTRL_SEPARATOR, 0, y, CONTENT_W, 2, NULL, NULL);
    y += 12;

    /* 系统信息 */
    ADD_CTRL(CTRL_LABEL, 0, y, 100, 20, "Operating System:", NULL);
    ADD_CTRL(CTRL_TEXT, 110, y, CONTENT_W - 110, 20, "FUNSOS", NULL);
    y += 24;

    ADD_CTRL(CTRL_LABEL, 0, y, 100, 20, "Version:", NULL);
    {
        const char *ver = sys_get_version();
        ADD_CTRL(CTRL_TEXT, 110, y, CONTENT_W - 110, 20, ver, NULL);
    }
    y += 24;

    ADD_CTRL(CTRL_LABEL, 0, y, 100, 20, "Kernel:", NULL);
    ADD_CTRL(CTRL_TEXT, 110, y, CONTENT_W - 110, 20, "FunsCore v1.0", NULL);
    y += 24;

    ADD_CTRL(CTRL_LABEL, 0, y, 100, 20, "Architecture:", NULL);
    ADD_CTRL(CTRL_TEXT, 110, y, CONTENT_W - 110, 20, "x86 (32-bit)", NULL);
    y += 24;

    ADD_CTRL(CTRL_LABEL, 0, y, 100, 20, "Build Date:", NULL);
    ADD_CTRL(CTRL_TEXT, 110, y, CONTENT_W - 110, 20, "2026-06-13", NULL);
    y += 24;

    ADD_CTRL(CTRL_LABEL, 0, y, 100, 20, "Compiler:", NULL);
    ADD_CTRL(CTRL_TEXT, 110, y, CONTENT_W - 110, 20, "GCC 14.2.0", NULL);
    y += 30;

    ADD_CTRL(CTRL_SEPARATOR, 0, y, CONTENT_W, 2, NULL, NULL);
    y += 12;

    /* 内存信息 */
    ADD_CTRL(CTRL_LABEL, 0, y, CONTENT_W, 20, "System Resources", NULL);
    y += 24;

    ADD_CTRL(CTRL_LABEL, 0, y, 120, 20, "Total Memory:", NULL);
    {
        uint32_t total = 0, used = 0;
        sys_get_memory_info(&total, &used);
        char mem_text[64];
        settings_int_to_str((int)(total / 1024), mem_text, 32);
        int len = (int)strlen(mem_text);
        mem_text[len] = ' '; mem_text[len + 1] = 'M'; mem_text[len + 2] = 'B'; mem_text[len + 3] = '\0';
        ADD_CTRL(CTRL_TEXT, 130, y, CONTENT_W - 130, 20, mem_text, NULL);
    }
    y += 24;

    ADD_CTRL(CTRL_LABEL, 0, y, 120, 20, "Used Memory:", NULL);
    {
        uint32_t total = 0, used = 0;
        sys_get_memory_info(&total, &used);
        char mem_text[64];
        settings_int_to_str((int)(used / 1024), mem_text, 32);
        int len = (int)strlen(mem_text);
        mem_text[len] = ' '; mem_text[len + 1] = 'M'; mem_text[len + 2] = 'B'; mem_text[len + 3] = '\0';
        ADD_CTRL(CTRL_TEXT, 130, y, CONTENT_W - 130, 20, mem_text, NULL);
    }
    y += 24;

    ADD_CTRL(CTRL_LABEL, 0, y, 120, 20, "System Uptime:", NULL);
    {
        uint32_t ticks = sys_get_ticks();
        char uptime_text[64];
        int seconds = (int)(ticks / 1000);
        int hours = seconds / 3600;
        int mins = (seconds % 3600) / 60;
        settings_int_to_str(hours, uptime_text, 16);
        int len = (int)strlen(uptime_text);
        uptime_text[len] = 'h'; uptime_text[len + 1] = ' ';
        settings_int_to_str(mins, uptime_text + len + 2, 16);
        len = (int)strlen(uptime_text);
        uptime_text[len] = 'm'; uptime_text[len + 1] = '\0';
        ADD_CTRL(CTRL_TEXT, 130, y, CONTENT_W - 130, 20, uptime_text, NULL);
    }
    y += 30;

    ADD_CTRL(CTRL_SEPARATOR, 0, y, CONTENT_W, 2, NULL, NULL);
    y += 12;

    /* 版权信息 */
    ADD_CTRL(CTRL_LABEL, 0, y, CONTENT_W, 20, "License & Copyright", NULL);
    y += 24;
    ADD_CTRL(CTRL_TEXT, 0, y, CONTENT_W, 20, "Copyright (C) 2026 FunsOS Project", NULL);
    y += 20;
    ADD_CTRL(CTRL_TEXT, 0, y, CONTENT_W, 20, "Licensed under the MIT License.", NULL);
    y += 20;
    ADD_CTRL(CTRL_TEXT, 0, y, CONTENT_W, 20, "All rights reserved.", NULL);
    y += 28;

    ADD_CTRL(CTRL_TEXT, 0, y, CONTENT_W, 16, "Built with love for learning OS development.", NULL);
    y += 18;
    ADD_CTRL(CTRL_TEXT, 0, y, CONTENT_W, 16, "https://github.com/funsos", NULL);
}

#undef ADD_CTRL

/* ================================================================
 * 渲染函数
 * ================================================================ */
static void settings_render(sys_window_t *win)
{
    sys_fill_window(win, COLOR_BG);
    settings_render_sidebar(win);
    settings_render_content(win);
    settings_render_bottom_bar(win);
}

static void settings_render_sidebar(sys_window_t *win)
{
    /* 侧边栏背景 */
    sys_draw_rect(win, 0, 0, SIDEBAR_W, WIN_H, COLOR_SIDEBAR);

    /* 侧边栏标题 */
    sys_draw_text(win, 10, 8, "Settings", COLOR_TEXT);

    sys_color_t separator = { 0xCC, 0xCC, 0xCC, 0xFF };
    sys_draw_line(win, 8, 28, SIDEBAR_W - 8, 28, separator);

    /* 分类列表 */
    const char *categories[] = {
        "Display", "Mouse", "Keyboard", "Date & Time", "About"
    };

    for (int i = 0; i < SETTINGS_CAT_COUNT; i++) {
        int by = 40 + i * 36;
        int bh = 32;

        /* 悬停/选中效果 */
        sys_color_t bg = COLOR_SIDEBAR;
        if ((uint32_t)i == g_settings.category) {
            bg = COLOR_SIDEBAR_ACT;
        } else if (i == g_settings.category_hover) {
            bg = COLOR_SIDEBAR_HOV;
        }

        sys_draw_rect(win, 4, by, SIDEBAR_W - 8, bh, bg);

        /* 选中指示条 */
        if ((uint32_t)i == g_settings.category) {
            sys_color_t accent = { 0x00, 0x78, 0xD4, 0xFF };
            sys_draw_rect(win, 4, by + 2, 4, bh - 4, accent);
        }

        sys_draw_text(win, 16, by + 8, categories[i], COLOR_TEXT);
    }

    /* 版本信息 */
    const char *ver = sys_get_version();
    char ver_text[48] = "v";
    int vpos = 1;
    while (*ver && vpos < 32) { ver_text[vpos] = *ver; vpos++; ver++; }
    ver_text[vpos] = '\0';
    sys_draw_text(win, 10, WIN_H - 28, ver_text, COLOR_TEXT_DIM);
}

static void settings_render_content(sys_window_t *win)
{
    /* 内容区背景 */
    sys_draw_rect(win, CONTENT_X - 8, 0, CONTENT_W + 16, WIN_H - BOTTOM_BAR_H, COLOR_WHITE);

    /* 渲染所有控件 */
    for (int i = 0; i < g_settings.control_count; i++) {
        settings_ctrl_t *ctrl = &g_settings.controls[i];
        settings_render_control(win, ctrl);
    }
}

static void settings_render_control(sys_window_t *win, settings_ctrl_t *ctrl)
{
    int abs_x = CONTENT_X + ctrl->x;
    int abs_y = CONTENT_Y + ctrl->y;

    switch (ctrl->type) {
    case CTRL_LABEL:    settings_render_label(win, ctrl); break;
    case CTRL_SLIDER:   settings_render_slider(win, ctrl); break;
    case CTRL_DROPDOWN: settings_render_dropdown(win, ctrl); break;
    case CTRL_CHECKBOX: settings_render_checkbox(win, ctrl); break;
    case CTRL_BUTTON:   settings_render_button(win, ctrl); break;
    case CTRL_TEXT:     settings_render_text(win, ctrl); break;
    case CTRL_SEPARATOR:
        sys_draw_line(win, abs_x, abs_y, abs_x + ctrl->w, abs_y, COLOR_SEPARATOR);
        break;
    }
}

static void settings_render_label(sys_window_t *win, settings_ctrl_t *ctrl)
{
    int abs_x = CONTENT_X + ctrl->x;
    int abs_y = CONTENT_Y + ctrl->y;

    /* 标题使用特殊颜色 */
    sys_color_t fg = COLOR_LABEL;
    if (ctrl->y == 0) {
        fg = COLOR_TITLE;
    }

    sys_draw_text(win, abs_x, abs_y, ctrl->label, fg);
}

static void settings_render_slider(sys_window_t *win, settings_ctrl_t *ctrl)
{
    int abs_x = CONTENT_X + ctrl->x;
    int abs_y = CONTENT_Y + ctrl->y + (ctrl->h - SLIDER_H) / 2;

    /* 轨道 */
    sys_draw_rect(win, abs_x, abs_y, ctrl->w, SLIDER_H, COLOR_SLIDER_TRACK);

    /* 填充部分 */
    int range = ctrl->max_val - ctrl->min_val;
    if (range > 0) {
        int fill_w = (ctrl->cur_val - ctrl->min_val) * ctrl->w / range;
        if (fill_w > 0) {
            sys_draw_rect(win, abs_x, abs_y, fill_w, SLIDER_H, COLOR_SLIDER_FILL);
        }
    }

    /* 拇指 */
    int thumb_x = abs_x + (ctrl->cur_val - ctrl->min_val) * ctrl->w / range - SLIDER_THUMB_W / 2;
    if (thumb_x < abs_x) thumb_x = abs_x;
    if (thumb_x > abs_x + ctrl->w - SLIDER_THUMB_W) thumb_x = abs_x + ctrl->w - SLIDER_THUMB_W;

    sys_color_t thumb_color = ctrl->slider_dragging ? COLOR_BTN_PRIMARY : COLOR_SLIDER_THUMB;
    sys_draw_rect(win, thumb_x, abs_y - 2, SLIDER_THUMB_W, SLIDER_H + 4, thumb_color);

    /* 当前值文本 */
    char val_text[16];
    settings_int_to_str(ctrl->cur_val, val_text, 16);
    sys_draw_text(win, abs_x + ctrl->w + 8, abs_y - 2, val_text, COLOR_TEXT);
}

static void settings_render_dropdown(sys_window_t *win, settings_ctrl_t *ctrl)
{
    int abs_x = CONTENT_X + ctrl->x;
    int abs_y = CONTENT_Y + ctrl->y;

    /* 下拉框背景 */
    sys_color_t bg = ctrl->dropdown_open ? COLOR_DROPDOWN_OPEN : COLOR_DROPDOWN_BG;
    if (ctrl->hover) {
        bg.r = (uint8_t)(bg.r > 20 ? bg.r - 20 : 0);
        bg.g = (uint8_t)(bg.g > 20 ? bg.g - 20 : 0);
        bg.b = (uint8_t)(bg.b > 20 ? bg.b - 20 : 0);
    }
    sys_draw_rect(win, abs_x, abs_y, ctrl->w, ctrl->h, bg);

    /* 边框 */
    sys_draw_rect(win, abs_x, abs_y, ctrl->w, 1, COLOR_DROPDOWN_BORDER);
    sys_draw_rect(win, abs_x, abs_y + ctrl->h - 1, ctrl->w, 1, COLOR_DROPDOWN_BORDER);
    sys_draw_rect(win, abs_x, abs_y, 1, ctrl->h, COLOR_DROPDOWN_BORDER);
    sys_draw_rect(win, abs_x + ctrl->w - 1, abs_y, 1, ctrl->h, COLOR_DROPDOWN_BORDER);

    /* 当前选项文本 */
    const char *sel_text = "";
    if (ctrl->options && ctrl->cur_val >= 0 && ctrl->cur_val < ctrl->option_count) {
        sel_text = ctrl->options[ctrl->cur_val];
    }
    sys_draw_text(win, abs_x + 6, abs_y + 3, sel_text, COLOR_TEXT);

    /* 下拉箭头 */
    sys_draw_text(win, abs_x + ctrl->w - 18, abs_y + 3, "v", COLOR_TEXT_DIM);

    /* 下拉列表 */
    if (ctrl->dropdown_open && ctrl->options) {
        int list_h = ctrl->option_count * 20 + 4;
        sys_draw_rect(win, abs_x, abs_y + ctrl->h, ctrl->w, list_h, COLOR_DROPDOWN_BG);
        sys_draw_rect(win, abs_x, abs_y + ctrl->h, ctrl->w, 1, COLOR_DROPDOWN_BORDER);
        sys_draw_rect(win, abs_x, abs_y + ctrl->h + list_h, ctrl->w, 1, COLOR_DROPDOWN_BORDER);
        sys_draw_rect(win, abs_x, abs_y + ctrl->h, 1, list_h, COLOR_DROPDOWN_BORDER);
        sys_draw_rect(win, abs_x + ctrl->w - 1, abs_y + ctrl->h, 1, list_h, COLOR_DROPDOWN_BORDER);

        for (int i = 0; i < ctrl->option_count; i++) {
            int oy = abs_y + ctrl->h + 2 + i * 20;
            if (i == ctrl->cur_val) {
                sys_draw_rect(win, abs_x + 2, oy, ctrl->w - 4, 18, COLOR_BTN_PRIMARY);
                sys_draw_text(win, abs_x + 6, oy + 1, ctrl->options[i], COLOR_BTN_TEXT_W);
            } else {
                sys_draw_text(win, abs_x + 6, oy + 1, ctrl->options[i], COLOR_TEXT);
            }
        }
    }
}

static void settings_render_checkbox(sys_window_t *win, settings_ctrl_t *ctrl)
{
    int abs_x = CONTENT_X + ctrl->x;
    int abs_y = CONTENT_Y + ctrl->y;

    /* 复选框方框 */
    sys_color_t box_bg = ctrl->hover ? COLOR_DROPDOWN_OPEN : COLOR_CHECKBOX_BG;
    sys_draw_rect(win, abs_x, abs_y + 2, CHECKBOX_SIZE, CHECKBOX_SIZE, box_bg);
    sys_draw_rect(win, abs_x, abs_y + 2, CHECKBOX_SIZE, 1, COLOR_CHECKBOX_BORDER);
    sys_draw_rect(win, abs_x, abs_y + 2, 1, CHECKBOX_SIZE, COLOR_CHECKBOX_BORDER);
    sys_draw_rect(win, abs_x + CHECKBOX_SIZE - 1, abs_y + 2, 1, CHECKBOX_SIZE, COLOR_CHECKBOX_BORDER);
    sys_draw_rect(win, abs_x, abs_y + 2 + CHECKBOX_SIZE - 1, CHECKBOX_SIZE, 1, COLOR_CHECKBOX_BORDER);

    /* 勾选标记 */
    if (ctrl->checked) {
        /* 绘制对勾 */
        sys_draw_rect(win, abs_x + 2, abs_y + 6, CHECKBOX_SIZE - 4, CHECKBOX_SIZE - 8, COLOR_CHECKBOX_CHECK);
    }

    /* 标签文本 */
    sys_draw_text(win, abs_x + CHECKBOX_SIZE + 6, abs_y + 1, ctrl->label, COLOR_LABEL);
}

static void settings_render_button(sys_window_t *win, settings_ctrl_t *ctrl)
{
    int abs_x = CONTENT_X + ctrl->x;
    int abs_y = CONTENT_Y + ctrl->y;

    sys_color_t bg = ctrl->hover ? COLOR_BTN_HOVER : COLOR_BTN;
    sys_draw_rect(win, abs_x, abs_y, ctrl->w, ctrl->h, bg);
    sys_draw_rect(win, abs_x, abs_y, ctrl->w, 1, COLOR_BTN_BORDER);
    sys_draw_rect(win, abs_x, abs_y, 1, ctrl->h, COLOR_BTN_BORDER);
    sys_draw_rect(win, abs_x + ctrl->w - 1, abs_y, 1, ctrl->h, COLOR_BTN_BORDER);
    sys_draw_rect(win, abs_x, abs_y + ctrl->h - 1, ctrl->w, 1, COLOR_BTN_BORDER);

    /* 居中文本 */
    int text_len = (int)strlen(ctrl->label);
    int tx = abs_x + (ctrl->w - text_len * 8) / 2;
    int ty = abs_y + (ctrl->h - 16) / 2;
    sys_draw_text(win, tx, ty, ctrl->label, COLOR_BTN_TEXT);
}

static void settings_render_text(sys_window_t *win, settings_ctrl_t *ctrl)
{
    int abs_x = CONTENT_X + ctrl->x;
    int abs_y = CONTENT_Y + ctrl->y;

    if (ctrl->label) {
        sys_draw_text(win, abs_x, abs_y, ctrl->label, COLOR_TEXT_DIM);
    }
}

static void settings_render_bottom_bar(sys_window_t *win)
{
    int by = BOTTOM_BAR_Y;

    /* 分割线 */
    sys_draw_line(win, 0, by, WIN_W, by, COLOR_SEPARATOR);

    /* 底部栏背景 */
    sys_draw_rect(win, 0, by + 1, WIN_W, BOTTOM_BAR_H - 1, COLOR_SIDEBAR);

    /* 按钮区域 */
    int btn_y = by + 8;
    int btn_w = BTN_W;
    int btn_h = BTN_H;

    /* OK 按钮 */
    {
        int ok_x = WIN_W - btn_w - 12;
        sys_color_t bg = g_settings.btn_ok_hover ? COLOR_BTN_HOVER : COLOR_BTN_PRIMARY;
        sys_draw_rect(win, ok_x, btn_y, btn_w, btn_h, bg);
        sys_draw_text(win, ok_x + 28, btn_y + 5, "OK", COLOR_BTN_TEXT_W);
    }

    /* Cancel 按钮 */
    {
        int cancel_x = WIN_W - btn_w - 12 - btn_w - BTN_GAP;
        sys_color_t bg = g_settings.btn_cancel_hover ? COLOR_BTN_HOVER : COLOR_BTN;
        sys_draw_rect(win, cancel_x, btn_y, btn_w, btn_h, bg);
        sys_draw_rect(win, cancel_x, btn_y, btn_w, 1, COLOR_BTN_BORDER);
        sys_draw_rect(win, cancel_x, btn_y, 1, btn_h, COLOR_BTN_BORDER);
        sys_draw_rect(win, cancel_x + btn_w - 1, btn_y, 1, btn_h, COLOR_BTN_BORDER);
        sys_draw_rect(win, cancel_x, btn_y + btn_h - 1, btn_w, 1, COLOR_BTN_BORDER);
        sys_draw_text(win, cancel_x + 20, btn_y + 5, "Cancel", COLOR_BTN_TEXT);
    }

    /* Apply 按钮 */
    {
        int apply_x = WIN_W - btn_w - 12 - (btn_w + BTN_GAP) * 2;
        sys_color_t bg = g_settings.btn_apply_hover ? COLOR_BTN_HOVER : COLOR_BTN;
        sys_draw_rect(win, apply_x, btn_y, btn_w, btn_h, bg);
        sys_draw_rect(win, apply_x, btn_y, btn_w, 1, COLOR_BTN_BORDER);
        sys_draw_rect(win, apply_x, btn_y, 1, btn_h, COLOR_BTN_BORDER);
        sys_draw_rect(win, apply_x + btn_w - 1, btn_y, 1, btn_h, COLOR_BTN_BORDER);
        sys_draw_rect(win, apply_x, btn_y + btn_h - 1, btn_w, 1, COLOR_BTN_BORDER);
        sys_draw_text(win, apply_x + 22, btn_y + 5, "Apply", COLOR_BTN_TEXT);
    }

    /* 修改提示 */
    if (g_settings.modified) {
        sys_draw_text(win, 12, btn_y + 5, "* Settings have been modified", COLOR_TEXT_DIM);
    }
}

/* ================================================================
 * 鼠标事件处理
 * ================================================================ */
static void settings_handle_mouse_click(int x, int y)
{
    /* 每次点击时停止所有滑块拖动 */
    settings_handle_mouse_up();

    /* 关闭所有下拉框（除非点击到下拉框） */
    int clicked_dropdown = 0;

    /* 检查底部按钮 */
    if (y >= BOTTOM_BAR_Y) {
        int btn_y = BOTTOM_BAR_Y + 8;
        int ok_x = WIN_W - BTN_W - 12;
        int cancel_x = WIN_W - BTN_W - 12 - BTN_W - BTN_GAP;
        int apply_x = WIN_W - BTN_W - 12 - (BTN_W + BTN_GAP) * 2;

        if (x >= ok_x && x < ok_x + BTN_W && y >= btn_y && y < btn_y + BTN_H) {
            /* OK 按钮 */
            settings_apply_changes();
            return;
        }
        if (x >= cancel_x && x < cancel_x + BTN_W && y >= btn_y && y < btn_y + BTN_H) {
            /* Cancel 按钮 */
            settings_cancel_changes();
            return;
        }
        if (x >= apply_x && x < apply_x + BTN_W && y >= btn_y && y < btn_y + BTN_H) {
            /* Apply 按钮 */
            settings_apply_changes();
            return;
        }
        return;
    }

    /* 检查侧边栏 */
    if (x < SIDEBAR_W) {
        int idx = settings_find_sidebar(y);
        if (idx >= 0 && idx < SETTINGS_CAT_COUNT && (uint32_t)idx != g_settings.category) {
            g_settings.category = (uint32_t)idx;
            g_settings.need_reload = 1;
        }
        return;
    }

    /* 检查控件区 */
    int ctrl_idx = settings_find_control(x, y);
    if (ctrl_idx >= 0) {
        settings_ctrl_t *ctrl = &g_settings.controls[ctrl_idx];

        switch (ctrl->type) {
        case CTRL_CHECKBOX:
            ctrl->checked = !ctrl->checked;
            g_settings.modified = 1;
            break;

        case CTRL_DROPDOWN:
            /* 如果下拉框已打开，检查是否点击了下拉列表项 */
            if (ctrl->dropdown_open && ctrl->options) {
                int abs_x = CONTENT_X + ctrl->x;
                int abs_y = CONTENT_Y + ctrl->y;
                int list_y = abs_y + ctrl->h;
                if (y >= list_y && y < list_y + ctrl->option_count * 20 + 4) {
                    int opt_idx = (y - list_y - 2) / 20;
                    if (opt_idx >= 0 && opt_idx < ctrl->option_count) {
                        ctrl->cur_val = opt_idx;
                        g_settings.modified = 1;
                    }
                }
                ctrl->dropdown_open = 0;
            } else {
                ctrl->dropdown_open = 1;
            }
            clicked_dropdown = 1;
            break;

        case CTRL_SLIDER:
            /* 点击滑块轨道：设置值 */
            {
                int abs_x = CONTENT_X + ctrl->x;
                int range = ctrl->max_val - ctrl->min_val;
                if (range > 0) {
                    int rel_x = x - abs_x;
                    if (rel_x < 0) rel_x = 0;
                    if (rel_x > ctrl->w) rel_x = ctrl->w;
                    ctrl->cur_val = ctrl->min_val + rel_x * range / ctrl->w;
                    g_settings.modified = 1;
                }
                ctrl->slider_dragging = 1;
            }
            break;

        case CTRL_BUTTON:
            /* 按钮点击：简化处理 */
            g_settings.modified = 1;
            break;

        default:
            break;
        }
    }

    /* 关闭其他下拉框 */
    if (!clicked_dropdown) {
        for (int i = 0; i < g_settings.control_count; i++) {
            if (i != ctrl_idx && g_settings.controls[i].type == CTRL_DROPDOWN) {
                g_settings.controls[i].dropdown_open = 0;
            }
        }
    }
}

static void settings_handle_mouse_move(int x, int y)
{
    /* 更新侧边栏悬停 */
    g_settings.category_hover = -1;
    if (x < SIDEBAR_W) {
        g_settings.category_hover = settings_find_sidebar(y);
    }

    /* 更新底部按钮悬停 */
    g_settings.btn_ok_hover = 0;
    g_settings.btn_cancel_hover = 0;
    g_settings.btn_apply_hover = 0;

    if (y >= BOTTOM_BAR_Y) {
        int btn_y = BOTTOM_BAR_Y + 8;
        int ok_x = WIN_W - BTN_W - 12;
        int cancel_x = WIN_W - BTN_W - 12 - BTN_W - BTN_GAP;
        int apply_x = WIN_W - BTN_W - 12 - (BTN_W + BTN_GAP) * 2;

        if (x >= ok_x && x < ok_x + BTN_W && y >= btn_y && y < btn_y + BTN_H)
            g_settings.btn_ok_hover = 1;
        else if (x >= cancel_x && x < cancel_x + BTN_W && y >= btn_y && y < btn_y + BTN_H)
            g_settings.btn_cancel_hover = 1;
        else if (x >= apply_x && x < apply_x + BTN_W && y >= btn_y && y < btn_y + BTN_H)
            g_settings.btn_apply_hover = 1;
    }

    /* 更新控件悬停 */
    int new_hover = settings_find_control(x, y);
    if (new_hover != g_settings.hover_ctrl) {
        if (g_settings.hover_ctrl >= 0 && g_settings.hover_ctrl < g_settings.control_count) {
            g_settings.controls[g_settings.hover_ctrl].hover = 0;
        }
        g_settings.hover_ctrl = new_hover;
        if (new_hover >= 0 && new_hover < g_settings.control_count) {
            g_settings.controls[new_hover].hover = 1;
        }
    }

    /* 处理滑块拖动 */
    if (g_settings.hover_ctrl >= 0 && g_settings.hover_ctrl < g_settings.control_count) {
        settings_ctrl_t *ctrl = &g_settings.controls[g_settings.hover_ctrl];
        if (ctrl->type == CTRL_SLIDER && ctrl->slider_dragging) {
            int abs_x = CONTENT_X + ctrl->x;
            int range = ctrl->max_val - ctrl->min_val;
            if (range > 0) {
                int rel_x = x - abs_x;
                if (rel_x < 0) rel_x = 0;
                if (rel_x > ctrl->w) rel_x = ctrl->w;
                ctrl->cur_val = ctrl->min_val + rel_x * range / ctrl->w;
                g_settings.modified = 1;
            }
        }
    }
}

/* 鼠标释放：停止滑块拖动 */
static void settings_handle_mouse_up(void)
{
    for (int i = 0; i < g_settings.control_count; i++) {
        if (g_settings.controls[i].type == CTRL_SLIDER) {
            g_settings.controls[i].slider_dragging = 0;
        }
    }
}

/* ================================================================
 * 键盘事件处理
 * ================================================================ */
static void settings_handle_key(uint32_t key)
{
    switch (key) {
    case 27: /* ESC */
        settings_cancel_changes();
        break;

    case '\r': /* Enter */
        settings_apply_changes();
        break;

    /* Tab 切换分类 */
    case '\t':
        g_settings.category = (g_settings.category + 1) % SETTINGS_CAT_COUNT;
        g_settings.need_reload = 1;
        break;

    /* 数字键切换分类 */
    case '1': g_settings.category = SETTINGS_CAT_DISPLAY; g_settings.need_reload = 1; break;
    case '2': g_settings.category = SETTINGS_CAT_MOUSE; g_settings.need_reload = 1; break;
    case '3': g_settings.category = SETTINGS_CAT_KEYBOARD; g_settings.need_reload = 1; break;
    case '4': g_settings.category = SETTINGS_CAT_DATETIME; g_settings.need_reload = 1; break;
    case '5': g_settings.category = SETTINGS_CAT_ABOUT; g_settings.need_reload = 1; break;

    default:
        break;
    }
}

/* ================================================================
 * 控件查找
 * ================================================================ */
static int settings_find_control(int x, int y)
{
    /* 转换为内容区相对坐标 */
    int rel_x = x - CONTENT_X;
    int rel_y = y - CONTENT_Y;

    for (int i = g_settings.control_count - 1; i >= 0; i--) {
        settings_ctrl_t *ctrl = &g_settings.controls[i];

        /* 检查是否在控件范围内 */
        if (rel_x >= ctrl->x && rel_x < ctrl->x + ctrl->w &&
            rel_y >= ctrl->y && rel_y < ctrl->y + ctrl->h) {
            return i;
        }

        /* 下拉框展开时检查下拉列表 */
        if (ctrl->type == CTRL_DROPDOWN && ctrl->dropdown_open && ctrl->options) {
            int list_y = ctrl->y + ctrl->h;
            int list_h = ctrl->option_count * 20 + 4;
            if (rel_x >= ctrl->x && rel_x < ctrl->x + ctrl->w &&
                rel_y >= list_y && rel_y < list_y + list_h) {
                return i;
            }
        }
    }

    return -1;
}

static int settings_find_sidebar(int y)
{
    int idx = (y - 40) / 36;
    if (idx < 0) idx = 0;
    if (idx >= SETTINGS_CAT_COUNT) idx = SETTINGS_CAT_COUNT - 1;
    return idx;
}

/* ================================================================
 * 设置应用和取消
 * ================================================================ */
static void settings_apply_changes(void)
{
    /* 遍历所有控件，保存配置 */
    for (int i = 0; i < g_settings.control_count; i++) {
        settings_ctrl_t *ctrl = &g_settings.controls[i];
        if (ctrl->config_key == NULL) continue;

        switch (ctrl->type) {
        case CTRL_SLIDER:
        case CTRL_DROPDOWN:
            settings_set_config_int(ctrl->config_key, ctrl->cur_val);
            break;
        case CTRL_CHECKBOX:
            settings_set_config_int(ctrl->config_key, ctrl->checked ? 1 : 0);
            break;
        default:
            break;
        }
    }

    /* 保存到文件 */
    settings_save_config();
    g_settings.modified = 0;
}

static void settings_cancel_changes(void)
{
    /* 重新加载控件 */
    g_settings.modified = 0;
    g_settings.need_reload = 1;
}

/* ================================================================
 * 配置持久化
 * ================================================================ */
static void settings_save_config(void)
{
    int fd = sys_file_open(CONFIG_FILE, 1); /* 写入模式 */
    if (fd < 0) return;

    for (int i = 0; i < g_settings.config_count; i++) {
        config_entry_t *entry = &g_settings.configs[i];
        if (entry->key[0] == '\0') continue;
        sys_file_write(fd, entry->key, (uint32_t)strlen(entry->key));
        sys_file_write(fd, "=", 1);
        sys_file_write(fd, entry->value, (uint32_t)strlen(entry->value));
        sys_file_write(fd, "\n", 1);
    }

    sys_file_close(fd);
}

static void settings_load_config(void)
{
    int fd = sys_file_open(CONFIG_FILE, 0); /* 读取模式 */
    if (fd < 0) return;

    g_settings.config_count = 0;

    char buf[512];
    int n = sys_file_read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) { sys_file_close(fd); return; }
    buf[n] = '\0';

    sys_file_close(fd);

    /* 解析 key=value 行 */
    char *line = buf;
    while (*line && g_settings.config_count < MAX_CONFIG_KEYS) {
        char *eq = NULL;
        char *nl = NULL;

        /* 查找等号 */
        for (char *p = line; *p && *p != '\n'; p++) {
            if (*p == '=' && eq == NULL) eq = p;
        }
        /* 查找换行 */
        for (char *p = line; *p; p++) {
            if (*p == '\n' || *p == '\r') { nl = p; break; }
        }
        if (nl == NULL) nl = line + strlen(line);

        if (eq != NULL && eq > line && eq < nl) {
            config_entry_t *entry = &g_settings.configs[g_settings.config_count];
            int key_len = (int)(eq - line);
            int val_len = (int)(nl - eq - 1);

            if (key_len >= MAX_KEY_LEN) key_len = MAX_KEY_LEN - 1;
            if (val_len >= MAX_VAL_LEN) val_len = MAX_VAL_LEN - 1;

            memcpy(entry->key, line, (uint32_t)key_len);
            entry->key[key_len] = '\0';
            memcpy(entry->value, eq + 1, (uint32_t)val_len);
            entry->value[val_len] = '\0';

            /* 去除换行 */
            settings_strip_newline(entry->value);

            g_settings.config_count++;
        }

        /* 移动到下一行 */
        line = nl;
        while (*line == '\n' || *line == '\r') line++;
        if (*line == '\0') break;
    }
}

static int settings_get_config_int(const char *key, int default_val)
{
    for (int i = 0; i < g_settings.config_count; i++) {
        if (strcmp(g_settings.configs[i].key, key) == 0) {
            return settings_str_to_int(g_settings.configs[i].value, default_val);
        }
    }
    return default_val;
}

static void settings_set_config_int(const char *key, int value)
{
    char val_str[32];
    settings_int_to_str(value, val_str, 32);
    settings_set_config_str(key, val_str);
}

static const char *settings_get_config_str(const char *key, const char *default_val)
{
    for (int i = 0; i < g_settings.config_count; i++) {
        if (strcmp(g_settings.configs[i].key, key) == 0) {
            return g_settings.configs[i].value;
        }
    }
    return default_val;
}

static void settings_set_config_str(const char *key, const char *value)
{
    /* 查找现有条目 */
    for (int i = 0; i < g_settings.config_count; i++) {
        if (strcmp(g_settings.configs[i].key, key) == 0) {
            strncpy(g_settings.configs[i].value, value, MAX_VAL_LEN - 1);
            g_settings.configs[i].value[MAX_VAL_LEN - 1] = '\0';
            return;
        }
    }

    /* 添加新条目 */
    if (g_settings.config_count < MAX_CONFIG_KEYS) {
        config_entry_t *entry = &g_settings.configs[g_settings.config_count];
        strncpy(entry->key, key, MAX_KEY_LEN - 1);
        entry->key[MAX_KEY_LEN - 1] = '\0';
        strncpy(entry->value, value, MAX_VAL_LEN - 1);
        entry->value[MAX_VAL_LEN - 1] = '\0';
        g_settings.config_count++;
    }
}

/* ================================================================
 * 工具函数
 * ================================================================ */
static int settings_str_to_int(const char *str, int default_val)
{
    if (str == NULL || *str == '\0') return default_val;

    int result = 0;
    int sign = 1;
    int i = 0;

    if (str[0] == '-') { sign = -1; i = 1; }
    else if (str[0] == '+') { i = 1; }

    int has_digit = 0;
    for (; str[i]; i++) {
        if (str[i] >= '0' && str[i] <= '9') {
            result = result * 10 + (str[i] - '0');
            has_digit = 1;
        } else {
            break;
        }
    }

    if (!has_digit) return default_val;
    return result * sign;
}

static void settings_int_to_str(int val, char *buf, int size)
{
    if (buf == NULL || size <= 0) return;
    if (size <= 1) { buf[0] = '\0'; return; }

    int pos = 0;
    int is_neg = 0;

    if (val < 0) {
        is_neg = 1;
        val = -val;
    }

    /* 处理0 */
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }

    char tmp[16];
    int tmp_pos = 0;
    while (val > 0 && tmp_pos < 15) {
        tmp[tmp_pos++] = (char)('0' + (val % 10));
        val /= 10;
    }

    if (is_neg && pos < size - 1) buf[pos++] = '-';
    for (int i = tmp_pos - 1; i >= 0 && pos < size - 1; i--) {
        buf[pos++] = tmp[i];
    }
    buf[pos] = '\0';
}

static void settings_strip_newline(char *str)
{
    if (str == NULL) return;
    int len = (int)strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r')) {
        str[len - 1] = '\0';
        len--;
    }
}