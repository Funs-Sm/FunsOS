/* hello_world.c - Hello World 示例程序
 * 最简单的 FUNSOS 应用程序，展示如何创建窗口、绘制文字和处理事件。
 *
 * 功能说明：
 *   - 创建一个基础窗口
 *   - 显示欢迎文字和 SDK 版本信息
 *   - 响应键盘和窗口关闭事件
 *
 * 使用的主要 API：
 *   - funsos_create_window() - 创建窗口
 *   - funsos_fill_window() - 填充窗口背景
 *   - funsos_draw_text() - 绘制文本
 *   - funsos_wait_event() - 等待事件
 *   - funsos_destroy_window() - 销毁窗口
 */

#include "funsos.h"

int main(void)
{
    /* 创建一个 400x300 的窗口，标题为 "Hello World"
     * 参数：x坐标, y坐标, 宽度, 高度, 标题
     */
    funsos_window_t win = funsos_create_window(200, 150, 400, 300, "Hello World");
    if (win == NULL) {
        /* 窗口创建失败，直接返回 */
        return 1;
    }

    /* 用白色填充整个窗口背景 */
    funsos_fill_window(win, FUNSOS_COLOR_WHITE);

    /* 定义颜色变量 */
    funsos_color_t black = FUNSOS_COLOR_BLACK;
    funsos_color_t blue  = FUNSOS_COLOR_BLUE;
    funsos_color_t green = FUNSOS_COLOR_GREEN;
    funsos_color_t red   = FUNSOS_COLOR_RED;

    /* 绘制标题文字 */
    funsos_draw_text(win, 120, 60, "Hello, FUNSOS!", blue);

    /* 绘制副标题 */
    funsos_draw_text(win, 90, 100, "Welcome to the SDK v1.3.0", black);

    /* 绘制分隔线 */
    funsos_draw_line(win, 30, 130, 370, 130, FUNSOS_COLOR_GRAY);

    /* 显示操作系统信息 */
    funsos_draw_text(win, 40, 150, "OS Name:    ", black);
    funsos_draw_text(win, 140, 150, FUNSOS_OS_NAME, green);

    funsos_draw_text(win, 40, 180, "Kernel:     ", black);
    funsos_draw_text(win, 140, 180, FUNSOS_KERNEL_NAME, green);

    funsos_draw_text(win, 40, 210, "Kernel Ver: ", black);
    funsos_draw_text(win, 140, 210, FUNSOS_KERNEL_VERSION, green);

    funsos_draw_text(win, 40, 240, "SDK Ver:    ", black);
    funsos_draw_text(win, 140, 240, FUNSOS_SDK_VERSION, red);

    /* 底部提示信息 */
    funsos_draw_text(win, 80, 275, "Press ESC or close window to exit",
                     FUNSOS_COLOR_DARK_GRAY);

    /* 事件循环：等待并处理用户输入事件 */
    funsos_event_t event;
    while (1) {
        /* 阻塞等待下一个事件 */
        if (funsos_wait_event(&event) != 0) {
            continue;
        }

        /* 检查事件类型 */
        switch (event.type) {
            case FUNSOS_EVENT_WINDOW_CLOSE:
                /* 用户点击了窗口关闭按钮 */
                goto exit_loop;

            case FUNSOS_EVENT_KEY_PRESS:
                /* 按键按下事件 */
                if (event.key == 0x1B) {  /* ESC 键的扫描码 */
                    goto exit_loop;
                }
                break;

            default:
                break;
        }
    }

exit_loop:
    /* 清理资源：销毁窗口 */
    funsos_destroy_window(win);
    return 0;
}
