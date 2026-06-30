/* gui_demo.c - GUI 界面示例程序
 * 演示 FUNSOS 中的图形界面编程，包括 2D 图形绘制、形状、文字等。
 *
 * 功能说明：
 *   - 2D 基础图形绘制（矩形、圆形、直线、椭圆）
 *   - 颜色渐变和填充
 *   - 文字渲染和字体效果
 *   - 鼠标交互响应
 *
 * 使用的主要 API：
 *   - funsos_create_window() - 创建窗口
 *   - funsos_fill_window() - 填充窗口背景
 *   - funsos_draw_rect() - 绘制矩形
 *   - funsos_fill_rect() - 填充矩形
 *   - funsos_draw_line() - 绘制直线
 *   - funsos_draw_text() - 绘制文本
 *   - funsos_wait_event() - 事件处理
 */

#include "funsos.h"

/* 鼠标位置记录 */
static int g_mouse_x = 0;
static int g_mouse_y = 0;
static int g_mouse_down = 0;

/* 重绘整个窗口内容 */
static void redraw_window(funsos_window_t win)
{
    int i;
    funsos_color_t colors[] = {
        FUNSOS_COLOR_RED,
        FUNSOS_COLOR_ORANGE,
        FUNSOS_COLOR_YELLOW,
        FUNSOS_COLOR_GREEN,
        FUNSOS_COLOR_CYAN,
        FUNSOS_COLOR_BLUE,
        FUNSOS_COLOR_MAGENTA
    };

    /* 清空背景为浅灰色 */
    funsos_fill_window(win, 0xF0F0F0);

    /* --- 标题区域 --- */
    funsos_draw_text(win, 180, 15, "FUNSOS GUI Demo", FUNSOS_COLOR_BLUE);
    funsos_draw_line(win, 20, 40, 580, 40, FUNSOS_COLOR_GRAY);

    /* --- 第一行：矩形演示 */
    funsos_draw_text(win, 30, 55, "1. Rectangles:", FUNSOS_COLOR_BLACK);

    /* 空心矩形 */
    funsos_draw_rect(win, 30, 75, 80, 50, FUNSOS_COLOR_RED);
    funsos_draw_text(win, 45, 95, "Hollow", FUNSOS_COLOR_DARK_GRAY);

    /* 填充矩形 */
    funsos_fill_rect(win, 130, 75, 80, 50, FUNSOS_COLOR_GREEN);
    funsos_draw_text(win, 145, 95, "Filled", FUNSOS_COLOR_WHITE);

    /* 渐变矩形 (用多个矩形拼出渐变效果）*/
    for (i = 0; i < 7; i++) {
        funsos_fill_rect(win, 230 + i * 12, 75, 12, 50, colors[i]);
    }
    funsos_draw_text(win, 250, 95, "Gradient", FUNSOS_COLOR_BLACK);

    /* --- 第二行：线条演示 --- */
    funsos_draw_text(win, 30, 145, "2. Lines & Shapes:", FUNSOS_COLOR_BLACK);

    /* 水平线 */
    funsos_draw_line(win, 30, 175, 120, 175, FUNSOS_COLOR_BLACK);
    funsos_draw_line(win, 30, 185, 120, 165, FUNSOS_COLOR_RED);

    /* 网格线 */
    for (i = 0; i < 5; i++) {
        funsos_draw_line(win, 150, 165 + i * 8, 240, 165 + i * 8, FUNSOS_COLOR_BLUE);
    }

    /* 交叉线 */
    funsos_draw_line(win, 270, 160, 350, 195, FUNSOS_COLOR_GREEN);
    funsos_draw_line(win, 270, 195, 350, 160, FUNSOS_COLOR_GREEN);

    /* 三角形 */
    funsos_draw_line(win, 380, 195, 420, 160, FUNSOS_COLOR_MAGENTA);
    funsos_draw_line(win, 420, 160, 460, 195, FUNSOS_COLOR_MAGENTA);
    funsos_draw_line(win, 380, 195, 460, 195, FUNSOS_COLOR_MAGENTA);

    /* --- 第三行：颜色面板 */
    funsos_draw_text(win, 30, 220, "3. Color Palette:", FUNSOS_COLOR_BLACK);

    /* 颜色方块 */
    funsos_fill_rect(win, 30, 240, 30, 30, FUNSOS_COLOR_BLACK);
    funsos_fill_rect(win, 65, 240, 30, 30, FUNSOS_COLOR_WHITE);
    funsos_draw_rect(win, 65, 240, 30, 30, FUNSOS_COLOR_GRAY);
    funsos_fill_rect(win, 100, 240, 30, 30, FUNSOS_COLOR_RED);
    funsos_fill_rect(win, 135, 240, 30, 30, FUNSOS_COLOR_GREEN);
    funsos_fill_rect(win, 170, 240, 30, 30, FUNSOS_COLOR_BLUE);
    funsos_fill_rect(win, 205, 240, 30, 30, FUNSOS_COLOR_YELLOW);
    funsos_fill_rect(win, 240, 240, 30, 30, FUNSOS_COLOR_CYAN);
    funsos_fill_rect(win, 275, 240, 30, 30, FUNSOS_COLOR_MAGENTA);
    funsos_fill_rect(win, 310, 240, 30, 30, FUNSOS_COLOR_ORANGE);
    funsos_fill_rect(win, 345, 240, 30, 30, FUNSOS_COLOR_GRAY);
    funsos_fill_rect(win, 380, 240, 30, 30, FUNSOS_COLOR_DARK_GRAY);
    funsos_fill_rect(win, 415, 240, 30, 30, FUNSOS_COLOR_LIGHT_GRAY);

    /* --- 第四行：文本样式 --- */
    funsos_draw_text(win, 30, 290, "4. Text Styles:", FUNSOS_COLOR_BLACK);

    funsos_draw_text(win, 30, 315, "Normal text", FUNSOS_COLOR_BLACK);
    funsos_draw_text(win, 30, 335, "Bold (simulated)", FUNSOS_COLOR_RED);
    funsos_draw_text(win, 200, 315, "Small text", FUNSOS_COLOR_BLUE);
    funsos_draw_text(win, 200, 335, "Green text", FUNSOS_COLOR_GREEN);

    /* --- 第五行：鼠标跟踪 --- */
    funsos_draw_text(win, 30, 370, "5. Mouse Tracking:", FUNSOS_COLOR_BLACK);

    char pos_buf[64];
    char *p = pos_buf;
    const char *label = "Position: (";
    while (*label) *p++ = *label++;

    /* X 坐标转字符串 */
    int x = g_mouse_x;
    char tmp[8];
    int tpos = 0;
    if (x == 0) tmp[tpos++] = '0';
    while (x > 0 && tpos < 7) { tmp[tpos++] = '0' + (x % 10); x /= 10; }
    for (int i = tpos - 1; i >= 0; i--) *p++ = tmp[i];
    *p++ = ',';
    *p++ = ' ';

    /* Y 坐标转字符串 */
    int y = g_mouse_y;
    tpos = 0;
    if (y == 0) tmp[tpos++] = '0';
    while (y > 0 && tpos < 7) { tmp[tpos++] = '0' + (y % 10); y /= 10; }
    for (int i = tpos - 1; i >= 0; i--) *p++ = tmp[i];
    *p++ = ')';
    *p = '\0';

    funsos_draw_text(win, 30, 395, pos_buf, FUNSOS_COLOR_DARK_GRAY);

    /* 鼠标状态 */
    if (g_mouse_down) {
        funsos_draw_text(win, 250, 395, "Button: PRESSED", FUNSOS_COLOR_RED);
    } else {
        funsos_draw_text(win, 250, 395, "Button: released", FUNSOS_COLOR_GRAY);
    }

    /* --- 底部信息 --- */
    funsos_draw_line(win, 20, 430, 580, 430, FUNSOS_COLOR_GRAY);
    funsos_draw_text(win, 150, 445, "Move mouse around - Press ESC to exit",
                     FUNSOS_COLOR_DARK_GRAY);
}

int main(void)
{
    /* 创建窗口 */
    funsos_window_t win = funsos_create_window(100, 80, 600, 480, "GUI Demo");
    if (win == NULL) {
        return 1;
    }

    /* 初始绘制 */
    redraw_window(win);

    /* 事件循环 */
    funsos_event_t event;
    while (1) {
        if (funsos_wait_event(&event) != 0)
            continue;

        switch (event.type) {
            case FUNSOS_EVENT_WINDOW_CLOSE:
                goto exit_loop;

            case FUNSOS_EVENT_KEY_PRESS:
                if (event.key == 0x1B)  /* ESC */
                    goto exit_loop;
                break;

            case FUNSOS_EVENT_MOUSE_MOVE:
                g_mouse_x = event.mouse.x;
                g_mouse_y = event.mouse.y;
                redraw_window(win);
                break;

            case FUNSOS_EVENT_MOUSE_DOWN:
                g_mouse_down = 1;
                redraw_window(win);
                break;

            case FUNSOS_EVENT_MOUSE_UP:
                g_mouse_down = 0;
                redraw_window(win);
                break;

            default:
                break;
        }
    }

exit_loop:
    funsos_destroy_window(win);
    return 0;
}
