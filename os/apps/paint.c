/* paint.c - FUNSOS 画图应用实现
 * 完整的画图程序，支持多种绘图工具、颜色选择、撤销/重做、
 * BMP 文件保存/加载等功能。
 */

#include "paint.h"
#include "sys_api.h"
#include "stddef.h"
#include "string.h"
#include "stdlib.h"
#include "math.h"

/* ================================================================
 * 配置常量
 * ================================================================ */
#define WIN_W             640         /* 窗口宽度 */
#define WIN_H             480         /* 窗口高度 */
#define STATUSBAR_H       22          /* 状态栏高度 */
#define TOOLBAR_W         56          /* 工具栏宽度 */
#define COLORBAR_H        28          /* 颜色栏高度 */
#define CANVAS_X          TOOLBAR_W   /* 画布起始 X */
#define CANVAS_Y          COLORBAR_H  /* 画布起始 Y */
#define CANVAS_W          (WIN_W - TOOLBAR_W) /* 画布宽度 */
#define CANVAS_H          (WIN_H - COLORBAR_H - STATUSBAR_H) /* 画布高度 */
#define CANVAS_SIZE       256         /* 实际画布尺寸(像素) */
#define ZOOM_MIN          1           /* 最小缩放 */
#define ZOOM_MAX          8           /* 最大缩放 */
#define DEFAULT_ZOOM       2           /* 默认缩放 */
#define MAX_UNDO          20          /* 最大撤销步数 */

/* 工具定义 */
#define TOOL_PENCIL       0
#define TOOL_LINE         1
#define TOOL_RECT         2
#define TOOL_FILL_RECT    3
#define TOOL_ELLIPSE      4
#define TOOL_FILL_ELLIPSE 5
#define TOOL_ERASER       6
#define TOOL_FILL         7
#define TOOL_PICKER       8
#define TOOL_TEXT         9

/* ================================================================
 * 颜色定义
 * ================================================================ */
static const sys_color_t COLOR_BG          = { 0xD8, 0xD8, 0xD8, 0xFF };
static const sys_color_t COLOR_TOOLBAR     = { 0xE8, 0xE8, 0xE8, 0xFF };
static const sys_color_t COLOR_TOOLBAR_FG  = { 0x33, 0x33, 0x33, 0xFF };
static const sys_color_t COLOR_STATUSBAR   = { 0xE8, 0xE8, 0xE8, 0xFF };
static const sys_color_t COLOR_STATUSBAR_FG = { 0x33, 0x33, 0x33, 0xFF };
static const sys_color_t COLOR_CANVAS_BG   = { 0xFF, 0xFF, 0xFF, 0xFF };
static const sys_color_t COLOR_GRID        = { 0xDD, 0xDD, 0xDD, 0xFF };
static const sys_color_t COLOR_BTN_SEL     = { 0xCC, 0xDD, 0xFF, 0xFF };
static const sys_color_t COLOR_BTN_BG      = { 0xF0, 0xF0, 0xF0, 0xFF };
static const sys_color_t COLOR_BTN_BORDER  = { 0xAA, 0xAA, 0xAA, 0xFF };

/* 16 基础颜色 */
static const sys_color_t g_palette[16] = {
    { 0x00, 0x00, 0x00, 0xFF }, /* 黑色 */
    { 0xFF, 0xFF, 0xFF, 0xFF }, /* 白色 */
    { 0xFF, 0x00, 0x00, 0xFF }, /* 红色 */
    { 0x00, 0xFF, 0x00, 0xFF }, /* 绿色 */
    { 0x00, 0x00, 0xFF, 0xFF }, /* 蓝色 */
    { 0xFF, 0xFF, 0x00, 0xFF }, /* 黄色 */
    { 0x00, 0xFF, 0xFF, 0xFF }, /* 青色 */
    { 0xFF, 0x00, 0xFF, 0xFF }, /* 品红 */
    { 0x80, 0x80, 0x80, 0xFF }, /* 灰色 */
    { 0xC0, 0xC0, 0xC0, 0xFF }, /* 亮灰 */
    { 0x80, 0x00, 0x00, 0xFF }, /* 暗红 */
    { 0x00, 0x80, 0x00, 0xFF }, /* 暗绿 */
    { 0x00, 0x00, 0x80, 0xFF }, /* 暗蓝 */
    { 0x80, 0x80, 0x00, 0xFF }, /* 橄榄 */
    { 0x00, 0x80, 0x80, 0xFF }, /* 蓝绿 */
    { 0x80, 0x00, 0x80, 0xFF }, /* 紫色 */
};

/* 工具名称 */
static const char *g_tool_names[] = {
    "Pencil", "Line", "Rect", "FillRect",
    "Ellipse", "FillEll", "Eraser", "Fill",
    "Picker", "Text", NULL
};

/* ================================================================
 * 画布存储（简单像素数组）
 * ================================================================ */
static sys_color_t g_canvas[CANVAS_SIZE][CANVAS_SIZE];

/* 撤销堆栈：存储整个画布 */
static sys_color_t g_undo_stack[MAX_UNDO][CANVAS_SIZE][CANVAS_SIZE];
static int g_undo_pos = -1;
static int g_undo_count = 0;

/* 重做堆栈 */
static sys_color_t g_redo_stack[MAX_UNDO][CANVAS_SIZE][CANVAS_SIZE];
static int g_redo_count = 0;

/* ================================================================
 * 绘图状态
 * ================================================================ */
typedef struct {
    int       tool;             /* 当前工具 */
    int       fg_color_idx;     /* 前景色索引(0-15) */
    int       bg_color_idx;     /* 背景色索引(0-15) */
    int       line_width;       /* 线宽(1,2,3,5) */
    int       zoom;             /* 当前缩放级别 */
    uint8_t   drawing;          /* 正在绘制 */
    int       start_x;          /* 绘制起始画布坐标 */
    int       start_y;
    int       last_x;           /* 上次画布坐标 */
    int       last_y;
    uint8_t   modified;         /* 是否有未保存修改 */
    uint8_t   show_grid;        /* 显示网格 */
    char      filename[256];    /* 文件名 */
} paint_ui_state_t;

static paint_ui_state_t g_paint;

/* 窗口 */
static sys_window_t *g_paint_win = NULL;

/* ================================================================
 * 内部函数声明
 * ================================================================ */
static void paint_push_undo(void);
static void paint_pop_undo(void);
static void paint_push_redo(void);
static void paint_pop_redo(void);
static void paint_clear_canvas(void);
static void paint_draw_pixel(int x, int y);
static void paint_draw_brush(int x, int y);
static sys_color_t paint_get_pixel(int x, int y);
static void paint_draw_line_impl(int x1, int y1, int x2, int y2);
static void paint_draw_rect_impl(int x1, int y1, int x2, int y2, int fill);
static void paint_draw_ellipse_impl(int cx, int cy, int rx, int ry, int fill);
static void paint_flood_fill(int x, int y, sys_color_t target);
static void paint_render(sys_window_t *win);
static void paint_render_toolbar(sys_window_t *win);
static void paint_render_colorbar(sys_window_t *win);
static void paint_render_canvas(sys_window_t *win);
static void paint_render_statusbar(sys_window_t *win);
static void paint_handle_mouse_down(int x, int y);
static void paint_handle_mouse_move(int x, int y);
static void paint_handle_mouse_up(int x, int y);
static void paint_handle_key(uint32_t key, uint32_t mod);
static void paint_handle_tool_click(int x, int y);
static void paint_handle_color_click(int x, int y);
static void paint_screen_to_canvas(int sx, int sy, int *cx, int *cy);
static void paint_save_bmp(const char *filename);
static void paint_load_bmp(const char *filename);
static int  paint_in_canvas(int cx, int cy);

/* ================================================================
 * 初始化
 * ================================================================ */
int paint_init(void)
{
    memset(&g_paint, 0, sizeof(g_paint));
    g_paint.tool = TOOL_PENCIL;
    g_paint.fg_color_idx = 0;  /* 黑色 */
    g_paint.bg_color_idx = 1;  /* 白色 */
    g_paint.line_width = 1;
    g_paint.zoom = DEFAULT_ZOOM;
    g_paint.show_grid = 0;
    g_paint.modified = 0;
    g_paint.filename[0] = '\0';

    g_undo_pos = -1;
    g_undo_count = 0;
    g_redo_count = 0;

    paint_clear_canvas();
    return 0;
}

void paint_run(void)
{
    sys_window_t *win = sys_create_window(100, 30, WIN_W, WIN_H, "FUNSOS Paint");
    if (win == NULL) return;

    g_paint_win = win;

    sys_event_t event;
    while (1) {
        if (sys_poll_event(&event) != 0) {
            paint_render(win);
            continue;
        }

        if (event.type == SYS_EVENT_WINDOW_CLOSE) {
            break;
        }

        if (event.type == SYS_EVENT_KEY_PRESS) {
            paint_handle_key(event.param1, event.param2);
        }

        if (event.type == SYS_EVENT_MOUSE_CLICK) {
            int mx = (int)event.param1;
            int my = (int)event.param2;
            /* 检查工具栏和颜色栏点击 */
            if (mx < TOOLBAR_W && my >= COLORBAR_H) {
                paint_handle_tool_click(mx, my);
            } else if (my < COLORBAR_H) {
                paint_handle_color_click(mx, my);
            } else {
                paint_handle_mouse_down(mx, my);
            }
        }

        if (event.type == SYS_EVENT_MOUSE_MOVE) {
            if (g_paint.drawing) {
                paint_handle_mouse_move((int)event.param1, (int)event.param2);
            }
        }

        paint_render(win);
    }

    g_paint_win = NULL;
    sys_destroy_window(win);
}

void paint_set_tool(uint32_t tool)
{
    if (tool <= TOOL_TEXT) g_paint.tool = (int)tool;
}

void paint_set_fg_color(paint_color_t color)
{
    (void)color;
}

void paint_set_bg_color(paint_color_t color)
{
    (void)color;
}

void paint_set_brush_size(uint32_t size)
{
    if (size >= 1 && size <= 5) g_paint.line_width = (int)size;
}

const paint_state_t *paint_get_state(void)
{
    return NULL; /* 简化：返回 NULL */
}

int paint_new_canvas(uint32_t w, uint32_t h)
{
    (void)w; (void)h;
    paint_clear_canvas();
    g_undo_pos = -1;
    g_undo_count = 0;
    g_redo_count = 0;
    g_paint.modified = 0;
    return 0;
}

int paint_save(const char *filename)
{
    if (filename) {
        strncpy(g_paint.filename, filename, 255);
        g_paint.filename[255] = '\0';
    }
    paint_save_bmp(g_paint.filename);
    g_paint.modified = 0;
    return 0;
}

int paint_load(const char *filename)
{
    if (filename) {
        strncpy(g_paint.filename, filename, 255);
        g_paint.filename[255] = '\0';
    }
    paint_load_bmp(g_paint.filename);
    g_paint.modified = 0;
    return 0;
}

int paint_undo(void)
{
    if (g_undo_pos >= 0) {
        /* 将当前状态推入重做栈 */
        paint_push_redo();
        /* 恢复上个状态 */
        for (int y = 0; y < CANVAS_SIZE; y++) {
            for (int x = 0; x < CANVAS_SIZE; x++) {
                g_canvas[y][x] = g_undo_stack[g_undo_pos][y][x];
            }
        }
        g_undo_pos--;
        return 0;
    }
    return -1;
}

int paint_redo(void)
{
    if (g_redo_count > 0) {
        /* 将当前状态推入撤销栈 */
        {
            if (g_undo_pos < MAX_UNDO - 1) {
                g_undo_pos++;
                for (int y = 0; y < CANVAS_SIZE; y++) {
                    for (int x = 0; x < CANVAS_SIZE; x++) {
                        g_undo_stack[g_undo_pos][y][x] = g_canvas[y][x];
                    }
                }
                g_undo_count = g_undo_pos + 1;
            }
        }
        /* 恢复重做状态 */
        g_redo_count--;
        for (int y = 0; y < CANVAS_SIZE; y++) {
            for (int x = 0; x < CANVAS_SIZE; x++) {
                g_canvas[y][x] = g_redo_stack[g_redo_count][y][x];
            }
        }
        return 0;
    }
    return -1;
}

void paint_close(void)
{
    memset(&g_paint, 0, sizeof(g_paint));
}

/* ================================================================
 * 撤销/重做
 * ================================================================ */
static void paint_push_undo(void)
{
    if (g_undo_pos < MAX_UNDO - 1) {
        g_undo_pos++;
        for (int y = 0; y < CANVAS_SIZE; y++) {
            for (int x = 0; x < CANVAS_SIZE; x++) {
                g_undo_stack[g_undo_pos][y][x] = g_canvas[y][x];
            }
        }
        g_undo_count = g_undo_pos + 1;
    } else {
        /* 满了，移动 */
        for (int i = 1; i < MAX_UNDO; i++) {
            for (int y = 0; y < CANVAS_SIZE; y++) {
                for (int x = 0; x < CANVAS_SIZE; x++) {
                    g_undo_stack[i - 1][y][x] = g_undo_stack[i][y][x];
                }
            }
        }
        for (int y = 0; y < CANVAS_SIZE; y++) {
            for (int x = 0; x < CANVAS_SIZE; x++) {
                g_undo_stack[MAX_UNDO - 1][y][x] = g_canvas[y][x];
            }
        }
    }
    g_redo_count = 0; /* 新操作清除重做栈 */
}

static void paint_push_redo(void)
{
    if (g_redo_count < MAX_UNDO) {
        for (int y = 0; y < CANVAS_SIZE; y++) {
            for (int x = 0; x < CANVAS_SIZE; x++) {
                g_redo_stack[g_redo_count][y][x] = g_canvas[y][x];
            }
        }
        g_redo_count++;
    }
}

/* ================================================================
 * 画布操作
 * ================================================================ */
static void paint_clear_canvas(void)
{
    for (int y = 0; y < CANVAS_SIZE; y++) {
        for (int x = 0; x < CANVAS_SIZE; x++) {
            g_canvas[y][x] = g_palette[1]; /* 白色 */
        }
    }
}

static sys_color_t paint_get_pixel(int x, int y)
{
    if (x >= 0 && x < CANVAS_SIZE && y >= 0 && y < CANVAS_SIZE) {
        return g_canvas[y][x];
    }
    return g_palette[1]; /* 白色 */
}

static void paint_draw_pixel(int x, int y)
{
    if (x < 0 || x >= CANVAS_SIZE || y < 0 || y >= CANVAS_SIZE) return;
    g_canvas[y][x] = g_palette[g_paint.fg_color_idx];
}

static void paint_draw_brush(int x, int y)
{
    /* 根据线宽绘制画笔 */
    int r = g_paint.line_width / 2;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r) {
                paint_draw_pixel(x + dx, y + dy);
            }
        }
    }
}

static void paint_draw_line_impl(int x1, int y1, int x2, int y2)
{
    int dx = x2 - x1;
    int dy = y2 - y1;
    int abs_dx = dx >= 0 ? dx : -dx;
    int abs_dy = dy >= 0 ? dy : -dy;
    int steep = abs_dy > abs_dx;

    if (steep) {
        int t = x1; x1 = y1; y1 = t;
        t = x2; x2 = y2; y2 = t;
        dx = x2 - x1;
        dy = y2 - y1;
        abs_dx = dx >= 0 ? dx : -dx;
        abs_dy = dy >= 0 ? dy : -dy;
    }

    int step_x = dx >= 0 ? 1 : -1;
    int step_y = dy >= 0 ? 1 : -1;
    int err = abs_dx / 2;
    int x = x1, y = y1;

    for (int i = 0; i <= abs_dx; i++) {
        if (steep) {
            paint_draw_brush(y, x);
        } else {
            paint_draw_brush(x, y);
        }
        err -= abs_dy;
        if (err < 0) {
            y += step_y;
            err += abs_dx;
        }
        x += step_x;
    }
}

static void paint_draw_rect_impl(int x1, int y1, int x2, int y2, int fill)
{
    int x = x1 < x2 ? x1 : x2;
    int y = y1 < y2 ? y1 : y2;
    int w = x1 < x2 ? (x2 - x1) : (x1 - x2);
    int h = y1 < y2 ? (y2 - y1) : (y1 - y2);

    if (fill) {
        for (int dy = 0; dy <= h; dy++) {
            for (int dx = 0; dx <= w; dx++) {
                paint_draw_pixel(x + dx, y + dy);
            }
        }
    } else {
        for (int i = 0; i <= w; i++) {
            paint_draw_brush(x + i, y);
            paint_draw_brush(x + i, y + h);
        }
        for (int i = 0; i <= h; i++) {
            paint_draw_brush(x, y + i);
            paint_draw_brush(x + w, y + i);
        }
    }
}

static void paint_draw_ellipse_impl(int cx, int cy, int rx, int ry, int fill)
{
    if (rx <= 0 || ry <= 0) return;

    int x = 0, y = ry;
    int64_t a2 = (int64_t)rx * rx;
    int64_t b2 = (int64_t)ry * ry;
    int64_t d1 = b2 - a2 * ry + a2 / 4;
    int64_t dx = 0;
    int64_t dy = 2 * a2 * y;

    while (dx < dy) {
        if (fill) {
            for (int px = cx - x; px <= cx + x; px++) {
                paint_draw_pixel(px, cy - y);
                paint_draw_pixel(px, cy + y);
            }
        } else {
            paint_draw_brush(cx + x, cy - y);
            paint_draw_brush(cx - x, cy - y);
            paint_draw_brush(cx + x, cy + y);
            paint_draw_brush(cx - x, cy + y);
        }
        if (d1 < 0) {
            x++; dx += 2 * b2; d1 += dx + b2;
        } else {
            x++; y--; dx += 2 * b2; dy -= 2 * a2; d1 += dx - dy + b2;
        }
    }

    int64_t d2 = b2 * (x + 1) * (x + 1) + a2 * (y - 1) * (y - 1) - a2 * b2;
    while (y >= 0) {
        if (fill) {
            for (int px = cx - x; px <= cx + x; px++) {
                paint_draw_pixel(px, cy - y);
                paint_draw_pixel(px, cy + y);
            }
        } else {
            paint_draw_brush(cx + x, cy - y);
            paint_draw_brush(cx - x, cy - y);
            paint_draw_brush(cx + x, cy + y);
            paint_draw_brush(cx - x, cy + y);
        }
        if (d2 > 0) {
            y--; dy -= 2 * a2; d2 += a2 - dy;
        } else {
            y--; x++; dx += 2 * b2; dy -= 2 * a2; d2 += dx - dy + a2;
        }
    }
}

static void paint_flood_fill(int x, int y, sys_color_t target)
{
    if (x < 0 || x >= CANVAS_SIZE || y < 0 || y >= CANVAS_SIZE) return;

    sys_color_t current = paint_get_pixel(x, y);
    sys_color_t fill_color = g_palette[g_paint.fg_color_idx];

    if (current.r == fill_color.r && current.g == fill_color.g &&
        current.b == fill_color.b && current.a == fill_color.a) return;
    if (current.r != target.r || current.g != target.g ||
        current.b != target.b || current.a != target.a) return;

    /* 使用栈进行填充 */
    #define FILL_STACK 4096
    static int stack_x[FILL_STACK];
    static int stack_y[FILL_STACK];
    int sp = 0;

    stack_x[sp] = x;
    stack_y[sp] = y;
    sp++;

    while (sp > 0 && sp < FILL_STACK) {
        sp--;
        int cx = stack_x[sp];
        int cy = stack_y[sp];

        if (cx < 0 || cx >= CANVAS_SIZE || cy < 0 || cy >= CANVAS_SIZE) continue;

        sys_color_t pc = g_canvas[cy][cx];
        if (pc.r != target.r || pc.g != target.g || pc.b != target.b || pc.a != target.a) continue;
        if (pc.r == fill_color.r && pc.g == fill_color.g && pc.b == fill_color.b && pc.a == fill_color.a) continue;

        g_canvas[cy][cx] = fill_color;

        if (sp + 4 < FILL_STACK) {
            stack_x[sp] = cx + 1; stack_y[sp] = cy; sp++;
            stack_x[sp] = cx - 1; stack_y[sp] = cy; sp++;
            stack_x[sp] = cx; stack_y[sp] = cy + 1; sp++;
            stack_x[sp] = cx; stack_y[sp] = cy - 1; sp++;
        }
    }
}

/* ================================================================
 * 坐标转换
 * ================================================================ */
static void paint_screen_to_canvas(int sx, int sy, int *cx, int *cy)
{
    *cx = (sx - CANVAS_X) / g_paint.zoom;
    *cy = (sy - CANVAS_Y) / g_paint.zoom;
}

static int paint_in_canvas(int cx, int cy)
{
    return (cx >= 0 && cx < CANVAS_SIZE && cy >= 0 && cy < CANVAS_SIZE);
}

/* ================================================================
 * 鼠标事件处理
 * ================================================================ */
static void paint_handle_mouse_down(int x, int y)
{
    /* 只在画布区域处理 */
    if (x < CANVAS_X || y < CANVAS_Y) return;

    int cx, cy;
    paint_screen_to_canvas(x, y, &cx, &cy);

    if (!paint_in_canvas(cx, cy)) return;

    g_paint.drawing = 1;
    g_paint.start_x = cx;
    g_paint.start_y = cy;
    g_paint.last_x = cx;
    g_paint.last_y = cy;

    /* 特殊工具即时处理 */
    if (g_paint.tool == TOOL_FILL) {
        paint_push_undo();
        sys_color_t target = paint_get_pixel(cx, cy);
        paint_flood_fill(cx, cy, target);
        g_paint.modified = 1;
        g_paint.drawing = 0;
        return;
    }

    if (g_paint.tool == TOOL_PICKER) {
        sys_color_t picked = paint_get_pixel(cx, cy);
        /* 查找最接近的调色板颜色 */
        int best = 0;
        int best_dist = 999999;
        for (int i = 0; i < 16; i++) {
            int dr = (int)picked.r - (int)g_palette[i].r;
            int dg = (int)picked.g - (int)g_palette[i].g;
            int db = (int)picked.b - (int)g_palette[i].b;
            int dist = dr * dr + dg * dg + db * db;
            if (dist < best_dist) {
                best_dist = dist;
                best = i;
            }
        }
        g_paint.fg_color_idx = best;
        g_paint.drawing = 0;
        return;
    }

    /* 铅笔/橡皮擦即时绘制 */
    if (g_paint.tool == TOOL_PENCIL || g_paint.tool == TOOL_ERASER) {
        paint_push_undo();
        if (g_paint.tool == TOOL_ERASER) {
            int saved = g_paint.fg_color_idx;
            g_paint.fg_color_idx = 1; /* 白色 */
            paint_draw_brush(cx, cy);
            g_paint.fg_color_idx = saved;
        } else {
            paint_draw_brush(cx, cy);
        }
        g_paint.modified = 1;
    }
}

static void paint_handle_mouse_move(int x, int y)
{
    if (!g_paint.drawing) return;

    int cx, cy;
    paint_screen_to_canvas(x, y, &cx, &cy);

    if (!paint_in_canvas(cx, cy)) {
        /* 钳制到画布边界 */
        if (cx < 0) cx = 0;
        if (cx >= CANVAS_SIZE) cx = CANVAS_SIZE - 1;
        if (cy < 0) cy = 0;
        if (cy >= CANVAS_SIZE) cy = CANVAS_SIZE - 1;
    }

    if (g_paint.tool == TOOL_PENCIL || g_paint.tool == TOOL_ERASER) {
        /* 连线绘制 */
        if (g_paint.tool == TOOL_ERASER) {
            int saved = g_paint.fg_color_idx;
            g_paint.fg_color_idx = 1;
            paint_draw_line_impl(g_paint.last_x, g_paint.last_y, cx, cy);
            g_paint.fg_color_idx = saved;
        } else {
            paint_draw_line_impl(g_paint.last_x, g_paint.last_y, cx, cy);
        }
        g_paint.last_x = cx;
        g_paint.last_y = cy;
    }
}

static void paint_handle_mouse_up(int x, int y)
{
    if (!g_paint.drawing) return;

    int cx, cy;
    paint_screen_to_canvas(x, y, &cx, &cy);

    if (!paint_in_canvas(cx, cy)) {
        if (cx < 0) cx = 0;
        if (cx >= CANVAS_SIZE) cx = CANVAS_SIZE - 1;
        if (cy < 0) cy = 0;
        if (cy >= CANVAS_SIZE) cy = CANVAS_SIZE - 1;
    }

    /* 形状工具 */
    if (g_paint.tool == TOOL_LINE ||
        g_paint.tool == TOOL_RECT || g_paint.tool == TOOL_FILL_RECT ||
        g_paint.tool == TOOL_ELLIPSE || g_paint.tool == TOOL_FILL_ELLIPSE) {

        paint_push_undo();

        switch (g_paint.tool) {
        case TOOL_LINE:
            paint_draw_line_impl(g_paint.start_x, g_paint.start_y, cx, cy);
            break;
        case TOOL_RECT:
            paint_draw_rect_impl(g_paint.start_x, g_paint.start_y, cx, cy, 0);
            break;
        case TOOL_FILL_RECT:
            paint_draw_rect_impl(g_paint.start_x, g_paint.start_y, cx, cy, 1);
            break;
        case TOOL_ELLIPSE: {
            int rx = (cx - g_paint.start_x) / 2;
            int ry = (cy - g_paint.start_y) / 2;
            if (rx < 0) rx = -rx;
            if (ry < 0) ry = -ry;
            int center_x = (g_paint.start_x + cx) / 2;
            int center_y = (g_paint.start_y + cy) / 2;
            paint_draw_ellipse_impl(center_x, center_y, rx, ry, 0);
            break;
        }
        case TOOL_FILL_ELLIPSE: {
            int rx = (cx - g_paint.start_x) / 2;
            int ry = (cy - g_paint.start_y) / 2;
            if (rx < 0) rx = -rx;
            if (ry < 0) ry = -ry;
            int center_x = (g_paint.start_x + cx) / 2;
            int center_y = (g_paint.start_y + cy) / 2;
            paint_draw_ellipse_impl(center_x, center_y, rx, ry, 1);
            break;
        }
        }
        g_paint.modified = 1;
    }

    g_paint.drawing = 0;
}

static void paint_handle_tool_click(int x, int y)
{
    int tool_index = (y - COLORBAR_H) / 36;
    if (tool_index >= 0 && tool_index <= TOOL_TEXT) {
        g_paint.tool = tool_index;
    }
}

static void paint_handle_color_click(int x, int y)
{
    /* 前16个颜色 */
    int color_index = x / 28;
    if (color_index >= 0 && color_index < 16) {
        /* 左键前景色，右键背景色 */
        g_paint.fg_color_idx = color_index;
    }
    /* 线宽选择 */
    if (x >= 16 * 28 && x < 16 * 28 + 4 * 24) {
        int lw_index = (x - 16 * 28) / 24;
        int widths[] = {1, 2, 3, 5};
        if (lw_index >= 0 && lw_index < 4) {
            g_paint.line_width = widths[lw_index];
        }
    }
}

static void paint_handle_key(uint32_t key, uint32_t mod)
{
    if (mod == 1) { /* Ctrl 组合键 */
        switch (key) {
        case 'z': case 'Z': paint_undo(); return;
        case 'y': case 'Y': paint_redo(); return;
        case 'n': case 'N': /* 新建 */
            paint_push_undo();
            paint_clear_canvas();
            g_paint.modified = 0;
            return;
        case 's': case 'S': /* 保存 */
            paint_save_bmp(g_paint.filename);
            return;
        case 'o': case 'O': /* 打开 */
            paint_load_bmp(g_paint.filename);
            return;
        default: break;
        }
    }
    /* 快捷键 */
    switch (key) {
    case 'g': case 'G': g_paint.show_grid = !g_paint.show_grid; break;
    case '+': case '=':
        if (g_paint.zoom < ZOOM_MAX) g_paint.zoom++;
        break;
    case '-':
        if (g_paint.zoom > ZOOM_MIN) g_paint.zoom--;
        break;
    default: break;
    }
}

/* ================================================================
 * 渲染函数
 * ================================================================ */
static void paint_render(sys_window_t *win)
{
    sys_fill_window(win, COLOR_BG);
    paint_render_toolbar(win);
    paint_render_colorbar(win);
    paint_render_canvas(win);
    paint_render_statusbar(win);
}

static void paint_render_toolbar(sys_window_t *win)
{
    sys_draw_rect(win, 0, COLORBAR_H, TOOLBAR_W, WIN_H - COLORBAR_H, COLOR_TOOLBAR);

    for (int i = 0; i <= TOOL_TEXT; i++) {
        int by = COLORBAR_H + i * 36;
        sys_color_t bg = (g_paint.tool == i) ? COLOR_BTN_SEL : COLOR_BTN_BG;
        sys_draw_rect(win, 2, by + 2, TOOLBAR_W - 4, 32, bg);

        /* 工具图标（简化：使用文字缩写） */
        const char *icons[] = {
            "P", "L", "R", "FR", "E", "FE", "Er", "Fl", "Pk", "T"
        };
        sys_draw_text(win, 4, by + 6, icons[i], COLOR_TOOLBAR_FG);
    }
}

static void paint_render_colorbar(sys_window_t *win)
{
    sys_draw_rect(win, 0, 0, WIN_W, COLORBAR_H, COLOR_TOOLBAR);

    /* 16 色调色板 */
    for (int i = 0; i < 16; i++) {
        sys_draw_rect(win, i * 28 + 2, 2, 24, 20, g_palette[i]);
        if (i == g_paint.fg_color_idx) {
            /* 选中指示 */
            sys_draw_rect(win, i * 28 + 2, 2, 24, 2, g_palette[0]);
            sys_draw_rect(win, i * 28 + 2, 2, 2, 20, g_palette[0]);
            sys_draw_rect(win, i * 28 + 24, 2, 2, 20, g_palette[0]);
            sys_draw_rect(win, i * 28 + 2, 20, 24, 2, g_palette[0]);
        }
    }

    /* 线宽选择 */
    int lw_x = 16 * 28 + 8;
    int widths[] = {1, 2, 3, 5};
    for (int i = 0; i < 4; i++) {
        sys_color_t bg = (g_paint.line_width == widths[i]) ? COLOR_BTN_SEL : COLOR_BTN_BG;
        sys_draw_rect(win, lw_x + i * 24, 2, 20, 20, bg);
        char lw_text[4];
        lw_text[0] = '0' + widths[i];
        lw_text[1] = '\0';
        sys_draw_text(win, lw_x + i * 24 + 6, 4, lw_text, COLOR_TOOLBAR_FG);
    }
}

static void paint_render_canvas(sys_window_t *win)
{
    /* 画布背景 */
    int canvas_screen_w = CANVAS_SIZE * g_paint.zoom;
    int canvas_screen_h = CANVAS_SIZE * g_paint.zoom;
    sys_draw_rect(win, CANVAS_X, CANVAS_Y, canvas_screen_w, canvas_screen_h, COLOR_CANVAS_BG);

    /* 使用矩形绘制来模拟像素 */
    for (int cy = 0; cy < CANVAS_SIZE; cy++) {
        for (int cx = 0; cx < CANVAS_SIZE; cx++) {
            sys_color_t px = g_canvas[cy][cx];
            /* 检查是否与背景色相同（白色）来优化 */
            if (px.r != 0xFF || px.g != 0xFF || px.b != 0xFF) {
                if (g_paint.zoom == 1) {
                    sys_draw_rect(win, CANVAS_X + cx, CANVAS_Y + cy, 1, 1, px);
                } else {
                    sys_draw_rect(win, CANVAS_X + cx * g_paint.zoom, CANVAS_Y + cy * g_paint.zoom,
                                  g_paint.zoom, g_paint.zoom, px);
                }
            }
        }
    }

    /* 绘制网格 */
    if (g_paint.show_grid) {
        int step = g_paint.zoom * 8; /* 每8像素一个网格 */
        for (int gx = 0; gx < CANVAS_SIZE * g_paint.zoom; gx += step) {
            sys_draw_line(win, CANVAS_X + gx, CANVAS_Y,
                          CANVAS_X + gx, CANVAS_Y + canvas_screen_h, COLOR_GRID);
        }
        for (int gy = 0; gy < CANVAS_SIZE * g_paint.zoom; gy += step) {
            sys_draw_line(win, CANVAS_X, CANVAS_Y + gy,
                          CANVAS_X + canvas_screen_w, CANVAS_Y + gy, COLOR_GRID);
        }
    }
}

static void paint_render_statusbar(sys_window_t *win)
{
    int sy = WIN_H - STATUSBAR_H;
    sys_draw_rect(win, 0, sy, WIN_W, STATUSBAR_H, COLOR_STATUSBAR);

    char status[128];
    int pos = 0;
    const char *tool = g_tool_names[g_paint.tool];
    while (*tool && pos < 30) status[pos++] = *tool++;
    status[pos++] = ' ';
    status[pos++] = '|';
    status[pos++] = ' ';
    status[pos++] = 'Z'; status[pos++] = 'o'; status[pos++] = 'o'; status[pos++] = 'm'; status[pos++] = ':'; status[pos++] = ' ';
    status[pos++] = '0' + g_paint.zoom;
    status[pos++] = 'x';
    status[pos++] = ' ';
    status[pos++] = '|';
    status[pos++] = ' ';
    status[pos++] = 'L'; status[pos++] = 'W'; status[pos++] = ':'; status[pos++] = ' ';
    status[pos++] = '0' + g_paint.line_width;
    status[pos++] = ' ';
    status[pos++] = '|';
    status[pos++] = ' ';
    if (g_paint.modified) status[pos++] = '*';
    status[pos] = '\0';
    sys_draw_text(win, 4, sy + 3, status, COLOR_STATUSBAR_FG);
}

/* ================================================================
 * BMP 保存/加载 (简化)
 * ================================================================ */
static void paint_save_bmp(const char *filename)
{
    if (filename == NULL || filename[0] == '\0') return;
    int fd = sys_file_open(filename, 1);
    if (fd < 0) return;

    /* BMP 文件头 */
    uint32_t file_size = 54 + CANVAS_SIZE * CANVAS_SIZE * 3;
    uint8_t header[54];
    memset(header, 0, 54);
    header[0] = 'B'; header[1] = 'M';
    *(uint32_t *)(header + 2) = file_size;
    *(uint32_t *)(header + 10) = 54;
    *(uint32_t *)(header + 14) = 40;
    *(uint32_t *)(header + 18) = CANVAS_SIZE;
    *(uint32_t *)(header + 22) = CANVAS_SIZE;
    *(uint16_t *)(header + 26) = 1;
    *(uint16_t *)(header + 28) = 24;
    *(uint32_t *)(header + 34) = CANVAS_SIZE * CANVAS_SIZE * 3;

    sys_file_write(fd, header, 54);

    /* BMP 像素数据（从下到上） */
    for (int y = CANVAS_SIZE - 1; y >= 0; y--) {
        uint8_t row[CANVAS_SIZE * 3];
        for (int x = 0; x < CANVAS_SIZE; x++) {
            sys_color_t px = g_canvas[y][x];
            row[x * 3 + 0] = px.b;
            row[x * 3 + 1] = px.g;
            row[x * 3 + 2] = px.r;
        }
        sys_file_write(fd, row, CANVAS_SIZE * 3);
    }

    sys_file_close(fd);
}

static void paint_load_bmp(const char *filename)
{
    if (filename == NULL || filename[0] == '\0') return;
    int fd = sys_file_open(filename, 0);
    if (fd < 0) return;

    /* 读取 BMP 头 */
    uint8_t header[54];
    int n = sys_file_read(fd, header, 54);
    if (n < 54) { sys_file_close(fd); return; }

    if (header[0] != 'B' || header[1] != 'M') { sys_file_close(fd); return; }

    int bmp_w = (int)*(uint32_t *)(header + 18);
    int bmp_h = (int)*(uint32_t *)(header + 22);
    int data_offset = (int)*(uint32_t *)(header + 10);

    /* 跳到像素数据 */
    /* 简化：假设偏移为54 */
    /* 读取像素数据 */
    int max_w = bmp_w < CANVAS_SIZE ? bmp_w : CANVAS_SIZE;
    int max_h = bmp_h < CANVAS_SIZE ? bmp_h : CANVAS_SIZE;

    paint_push_undo();
    paint_clear_canvas();

    for (int y = max_h - 1; y >= 0; y--) {
        uint8_t row[768]; /* 最多256*3 */
        int row_size = max_w * 3;
        int n_read = sys_file_read(fd, row, (uint32_t)row_size);
        if (n_read < row_size) break;

        for (int x = 0; x < max_w; x++) {
            g_canvas[y][x].b = row[x * 3 + 0];
            g_canvas[y][x].g = row[x * 3 + 1];
            g_canvas[y][x].r = row[x * 3 + 2];
            g_canvas[y][x].a = 0xFF;
        }
    }

    sys_file_close(fd);
    g_paint.modified = 1;
}