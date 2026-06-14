#ifndef TERMINAL_H
#define TERMINAL_H
#include "stdint.h"

#define TERM_MAX_COLS      100
#define TERM_MAX_ROWS      50
#define TERM_SCROLLBACK    1000
#define TERM_CHAR_W        8
#define TERM_CHAR_H        16
#define TERM_HISTORY_MAX   50
#define TERM_CMD_MAX_LEN   256

/* 终端入口 */
int terminal_main(int argc, char **argv);

/* 终端窗口创建 */
uint32_t terminal_create_window(int x, int y, int w, int h);

/* 终端输入处理 */
void terminal_handle_key(char c);
void terminal_handle_special_key(uint32_t keycode);

/* 终端输出 */
void terminal_write(const char *str);
void terminal_putchar(char c);

/* 终端渲染 */
void terminal_render(void);

/* 终端事件处理 */
void terminal_handle_click(int x, int y);
void terminal_handle_resize(int w, int h);

#endif