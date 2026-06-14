/* calculator.c - FUNSOS 计算器实现
 * 完整的图形化计算器，支持标准模式和科学模式、
 * 内存功能、运算历史、键盘输入等。
 */

#include "calculator.h"
#include "sys_api.h"
#include "stddef.h"
#include "string.h"
#include "stdlib.h"
#include "math.h"

/* ================================================================
 * 配置常量
 * ================================================================ */
#define WIN_W             340         /* 窗口宽度 */
#define WIN_H             420         /* 标准模式窗口高度 */
#define WIN_H_SCIENTIFIC  520         /* 科学模式窗口高度 */
#define DISPLAY_H         60          /* 显示屏高度 */
#define BUTTON_W          60          /* 按钮宽度 */
#define BUTTON_H          40          /* 按钮高度 */
#define BUTTON_GAP        4           /* 按钮间距 */
#define BUTTON_START_X    8           /* 按钮区起始 X */
#define BUTTON_START_Y    72          /* 按钮区起始 Y */
#define HISTORY_H         100         /* 历史面板高度 */
#define HISTORY_W         (WIN_W - 16) /* 历史面板宽度 */

/* 按钮类型 */
#define BTN_DIGIT    0
#define BTN_OPERATOR 1
#define BTN_FUNCTION 2
#define BTN_EQUALS   3
#define BTN_CLEAR    4
#define BTN_MEMORY   5

/* ================================================================
 * 颜色定义
 * ================================================================ */
static const sys_color_t COLOR_BG          = { 0xF0, 0xF0, 0xF0, 0xFF };
static const sys_color_t COLOR_DISPLAY_BG  = { 0xE8, 0xEE, 0xF0, 0xFF };
static const sys_color_t COLOR_DISPLAY_FG  = { 0x00, 0x00, 0x00, 0xFF };
static const sys_color_t COLOR_BTN_DIGIT   = { 0xFA, 0xFA, 0xFA, 0xFF };
static const sys_color_t COLOR_BTN_OP      = { 0xE3, 0xEE, 0xF7, 0xFF };
static const sys_color_t COLOR_BTN_FUNC    = { 0xE8, 0xE8, 0xE8, 0xFF };
static const sys_color_t COLOR_BTN_EQ      = { 0x00, 0x78, 0xD4, 0xFF };
static const sys_color_t COLOR_BTN_CLR     = { 0xD8, 0x3B, 0x01, 0xFF };
static const sys_color_t COLOR_BTN_MEM     = { 0xE8, 0xE0, 0xD0, 0xFF };
static const sys_color_t COLOR_BTN_TEXT    = { 0x00, 0x00, 0x00, 0xFF };
static const sys_color_t COLOR_BTN_TEXT_W  = { 0xFF, 0xFF, 0xFF, 0xFF };
static const sys_color_t COLOR_STATUSBAR   = { 0xE0, 0xE0, 0xE0, 0xFF };
static const sys_color_t COLOR_STATUSBAR_FG = { 0x66, 0x66, 0x66, 0xFF };
static const sys_color_t COLOR_HISTORY_BG  = { 0xFF, 0xFF, 0xF0, 0xFF };
static const sys_color_t COLOR_HISTORY_FG  = { 0x55, 0x55, 0x55, 0xFF };
static const sys_color_t COLOR_MODE_IND    = { 0x00, 0x88, 0x00, 0xFF };

/* ================================================================
 * 按钮定义
 * ================================================================ */
typedef struct {
    const char *label;
    int         type;
    int         x;
    int         y;
    int         w;
    int         h;
    uint32_t    value;  /* 如果是数字则为数字值，否则为操作码 */
} calc_button_t;

/* 标准模式按钮布局 */
static calc_button_t g_basic_buttons[] = {
    /* 内存行 */
    {"MC",  BTN_MEMORY,   8,   72,  48, 32, 0},
    {"MR",  BTN_MEMORY,  60,   72,  48, 32, 0},
    {"MS",  BTN_MEMORY, 112,   72,  48, 32, 0},
    {"M+",  BTN_MEMORY, 164,   72,  48, 32, 0},
    /* 清除行 */
    {"C",   BTN_CLEAR,    8,  108,  48, 32, 0},
    {"CE",  BTN_CLEAR,   60,  108,  48, 32, 0},
    {"BS",  BTN_CLEAR,  112,  108,  48, 32, 0},
    {"/",   BTN_OPERATOR,164, 108,  48, 32, CALC_OP_DIV},
    /* 第一行 */
    {"7",   BTN_DIGIT,    8,  144,  48, 32, 7},
    {"8",   BTN_DIGIT,   60,  144,  48, 32, 8},
    {"9",   BTN_DIGIT,  112,  144,  48, 32, 9},
    {"*",   BTN_OPERATOR,164, 144,  48, 32, CALC_OP_MUL},
    /* 第二行 */
    {"4",   BTN_DIGIT,    8,  180,  48, 32, 4},
    {"5",   BTN_DIGIT,   60,  180,  48, 32, 5},
    {"6",   BTN_DIGIT,  112,  180,  48, 32, 6},
    {"-",   BTN_OPERATOR,164, 180,  48, 32, CALC_OP_SUB},
    /* 第三行 */
    {"1",   BTN_DIGIT,    8,  216,  48, 32, 1},
    {"2",   BTN_DIGIT,   60,  216,  48, 32, 2},
    {"3",   BTN_DIGIT,  112,  216,  48, 32, 3},
    {"+",   BTN_OPERATOR,164, 216,  48, 32, CALC_OP_ADD},
    /* 第四行 */
    {"0",   BTN_DIGIT,    8,  252, 100, 32, 0},
    {".",   BTN_FUNCTION,112, 252,  48, 32, 0},
    {"=",   BTN_EQUALS,  164, 252,  48, 32, 0},
    /* 模式切换和历史 */
    {"Sci", BTN_FUNCTION,  8, 288,  48, 28, 0},
    {"+/-", BTN_FUNCTION, 60, 288,  48, 28, CALC_OP_NEGATE},
    {"1/x", BTN_FUNCTION,112, 288,  48, 28, CALC_OP_RECIPROCAL},
    {"%",   BTN_FUNCTION,164, 288,  48, 28, CALC_OP_PERCENT},
    {NULL, 0, 0, 0, 0, 0, 0}
};

/* 科学模式按钮布局（基础行下方额外行） */
static calc_button_t g_sci_buttons[] = {
    /* 科学计算第一行 */
    {"sin", BTN_FUNCTION,  8, 324, 48, 28, CALC_OP_SIN},
    {"cos", BTN_FUNCTION, 60, 324, 48, 28, CALC_OP_COS},
    {"tan", BTN_FUNCTION,112, 324, 48, 28, CALC_OP_TAN},
    {"(",   BTN_FUNCTION,164, 324, 48, 28, 0},
    /* 科学计算第二行 */
    {"log", BTN_FUNCTION,  8, 356, 48, 28, CALC_OP_LOG},
    {"ln",  BTN_FUNCTION, 60, 356, 48, 28, CALC_OP_LN},
    {"x^y", BTN_FUNCTION,112, 356, 48, 28, CALC_OP_POW},
    {")",   BTN_FUNCTION,164, 356, 48, 28, 0},
    /* 科学计算第三行 */
    {"sqrt",BTN_FUNCTION,  8, 388, 48, 28, CALC_OP_SQRT},
    {"x^2", BTN_FUNCTION, 60, 388, 48, 28, 0},
    {"pi",  BTN_FUNCTION,112, 388, 48, 28, 0},
    {"e",   BTN_FUNCTION,164, 388, 48, 28, 0},
    /* 模式切换按钮 */
    {"Std", BTN_FUNCTION,  8, 420, 80, 28, 0},
    {NULL, 0, 0, 0, 0, 0, 0}
};

/* 计算历史条目 */
typedef struct {
    char expression[64];
    char result[64];
} calc_history_t;

/* ================================================================
 * 计算器状态
 * ================================================================ */
static calculator_state_t g_calc;
static calc_history_t g_history[10];
static int g_history_count = 0;
static int g_history_index = -1;
static uint8_t g_show_history = 0;
static double g_last_result = 0.0;
static int g_new_input = 1;

/* 表达式缓冲区 */
static char g_expr_buf[256];
static int g_expr_len = 0;

/* 窗口引用 */
static sys_window_t *g_calc_win = NULL;

/* 窗口高度 */
static int g_win_h = WIN_H;

/* ================================================================
 * 内部函数声明
 * ================================================================ */
static void calc_render(sys_window_t *win);
static void calc_render_display(sys_window_t *win);
static void calc_render_buttons(sys_window_t *win);
static void calc_render_history(sys_window_t *win);
static void calc_render_statusbar(sys_window_t *win);
static void calc_draw_button(sys_window_t *win, calc_button_t *btn);
static void calc_process_button(calc_button_t *btn);
static void calc_process_digit(int digit);
static void calc_process_operator(uint32_t op);
static void calc_process_equals(void);
static void calc_process_clear(void);
static void calc_process_clear_entry(void);
static void calc_process_backspace(void);
static void calc_process_memory(int func);
static void calc_process_function(uint32_t op);
static void calc_add_history(const char *expr, const char *result);
static void calc_eval_expression(void);
static double calc_eval_simple(void);
static void calc_handle_key(uint32_t key);
static void calc_handle_mouse(int x, int y);
static int  calc_find_button(int x, int y, calc_button_t *buttons);
static void calc_double_to_str(double value, char *buf, int size);
static void calc_int_to_str(int n, char *buf);

/* ================================================================
 * 初始化和主循环
 * ================================================================ */
int calculator_init(void)
{
    memset(&g_calc, 0, sizeof(g_calc));
    g_calc.mode = CALC_MODE_BASIC;
    g_calc.current_value = 0;
    g_calc.stored_value = 0;
    g_calc.memory_value = 0;
    g_calc.operation = CALC_OP_NONE;
    g_calc.has_pending_op = 0;
    g_calc.has_memory = 0;
    g_calc.clear_on_input = 0;
    g_calc.decimal_mode = 0;
    g_calc.decimal_places = 0;
    g_calc.display_buf[0] = '0';
    g_calc.display_buf[1] = '\0';

    g_history_count = 0;
    g_history_index = -1;
    g_show_history = 0;
    g_last_result = 0.0;
    g_new_input = 1;

    g_expr_buf[0] = '\0';
    g_expr_len = 0;

    g_win_h = WIN_H;

    calculator_key_press('0'); /* 初始化显示 */
    calculator_key_press(27);  /* 清除 */
    return 0;
}

void calculator_run(void)
{
    g_win_h = (g_calc.mode == CALC_MODE_SCIENTIFIC) ? WIN_H_SCIENTIFIC : WIN_H;
    sys_window_t *win = sys_create_window(180, 60, WIN_W, g_win_h, "FUNSOS Calculator");
    if (win == NULL) return;

    g_calc_win = win;

    sys_event_t event;
    while (1) {
        if (sys_poll_event(&event) != 0) {
            calc_render(win);
            continue;
        }

        if (event.type == SYS_EVENT_WINDOW_CLOSE) {
            break;
        }

        if (event.type == SYS_EVENT_KEY_PRESS) {
            calc_handle_key(event.param1);
        }

        if (event.type == SYS_EVENT_MOUSE_CLICK) {
            calc_handle_mouse((int)event.param1, (int)event.param2);
        }

        calc_render(win);
    }

    g_calc_win = NULL;
    sys_destroy_window(win);
}

int calculator_key_press(uint32_t key)
{
    calc_handle_key(key);
    return 0;
}

const calculator_state_t *calculator_get_state(void)
{
    return &g_calc;
}

const char *calculator_get_display(void)
{
    return g_calc.display_buf;
}

void calculator_close(void)
{
    memset(&g_calc, 0, sizeof(g_calc));
}

/* ================================================================
 * 按钮处理
 * ================================================================ */
static void calc_process_button(calc_button_t *btn)
{
    switch (btn->type) {
    case BTN_DIGIT:
        calc_process_digit((int)btn->value);
        break;
    case BTN_OPERATOR:
        calc_process_operator(btn->value);
        break;
    case BTN_EQUALS:
        calc_process_equals();
        break;
    case BTN_CLEAR:
        if (btn->value == 1) calc_process_clear_entry();
        else if (btn->value == 2) calc_process_backspace();
        else calc_process_clear();
        break;
    case BTN_MEMORY:
        calc_process_memory((int)btn->value);
        break;
    case BTN_FUNCTION:
        calc_process_function(btn->value);
        break;
    }
}

static void calc_process_digit(int digit)
{
    if (g_new_input) {
        g_calc.current_value = 0;
        g_calc.decimal_mode = 0;
        g_calc.decimal_places = 0;
        g_new_input = 0;
        g_expr_len = 0;
        g_expr_buf[0] = '\0';
    }

    if (g_calc.decimal_mode) {
        g_calc.decimal_places++;
        double factor = 1.0;
        for (uint32_t i = 0; i < g_calc.decimal_places; i++) factor *= 0.1;
        if (g_calc.current_value >= 0)
            g_calc.current_value += digit * factor;
        else
            g_calc.current_value -= digit * factor;
    } else {
        if (g_calc.current_value >= 0)
            g_calc.current_value = g_calc.current_value * 10.0 + digit;
        else
            g_calc.current_value = g_calc.current_value * 10.0 - digit;
    }

    calc_double_to_str(g_calc.current_value, g_calc.display_buf, sizeof(g_calc.display_buf));
}

static void calc_process_operator(uint32_t op)
{
    /* 先执行之前的运算 */
    if (g_calc.has_pending_op && !g_new_input) {
        calc_process_equals();
    }

    g_calc.stored_value = g_calc.current_value;
    g_calc.operation = op;
    g_calc.has_pending_op = 1;
    g_new_input = 1;
}

static void calc_process_equals(void)
{
    if (g_calc.has_pending_op) {
        double result = g_calc.stored_value;

        switch (g_calc.operation) {
        case CALC_OP_ADD: result += g_calc.current_value; break;
        case CALC_OP_SUB: result -= g_calc.current_value; break;
        case CALC_OP_MUL: result *= g_calc.current_value; break;
        case CALC_OP_DIV:
            if (g_calc.current_value != 0) result /= g_calc.current_value;
            else { strcpy(g_calc.display_buf, "Error: Div/0"); g_new_input = 1; return; }
            break;
        case CALC_OP_MOD:
            if (g_calc.current_value != 0)
                result = (double)((int64_t)g_calc.stored_value % (int64_t)g_calc.current_value);
            break;
        case CALC_OP_POW:
            result = pow(g_calc.stored_value, g_calc.current_value);
            break;
        default: break;
        }

        /* 保存历史 */
        char expr_str[128];
        calc_double_to_str(g_calc.stored_value, expr_str, 32);
        int len = (int)strlen(expr_str);
        switch (g_calc.operation) {
        case CALC_OP_ADD: expr_str[len] = '+'; break;
        case CALC_OP_SUB: expr_str[len] = '-'; break;
        case CALC_OP_MUL: expr_str[len] = '*'; break;
        case CALC_OP_DIV: expr_str[len] = '/'; break;
        default: expr_str[len] = '?'; break;
        }
        expr_str[len + 1] = '\0';
        calc_double_to_str(g_calc.current_value, expr_str + len + 1, 32);

        char result_str[64];
        calc_double_to_str(result, result_str, sizeof(result_str));
        calc_add_history(expr_str, result_str);

        g_calc.current_value = result;
        g_last_result = result;
    }

    calc_double_to_str(g_calc.current_value, g_calc.display_buf, sizeof(g_calc.display_buf));
    g_calc.has_pending_op = 0;
    g_calc.operation = CALC_OP_NONE;
    g_new_input = 1;
}

static void calc_process_clear(void)
{
    g_calc.current_value = 0;
    g_calc.stored_value = 0;
    g_calc.operation = CALC_OP_NONE;
    g_calc.has_pending_op = 0;
    g_calc.decimal_mode = 0;
    g_calc.decimal_places = 0;
    g_calc.display_buf[0] = '0';
    g_calc.display_buf[1] = '\0';
    g_new_input = 1;
    g_expr_len = 0;
    g_expr_buf[0] = '\0';
    g_last_result = 0;
}

static void calc_process_clear_entry(void)
{
    g_calc.current_value = 0;
    g_calc.decimal_mode = 0;
    g_calc.decimal_places = 0;
    g_calc.display_buf[0] = '0';
    g_calc.display_buf[1] = '\0';
}

static void calc_process_backspace(void)
{
    if (g_new_input) return;

    if (g_calc.decimal_mode) {
        if (g_calc.decimal_places > 0) {
            double factor = 1.0;
            for (uint32_t i = 0; i < g_calc.decimal_places; i++) factor *= 10.0;
            g_calc.current_value = floor(g_calc.current_value * factor / 10.0) / (factor / 10.0);
            g_calc.decimal_places--;
        } else {
            g_calc.decimal_mode = 0;
        }
    } else {
        g_calc.current_value = floor(g_calc.current_value / 10.0);
    }
    calc_double_to_str(g_calc.current_value, g_calc.display_buf, sizeof(g_calc.display_buf));
}

static void calc_process_memory(int func)
{
    switch (func) {
    case 0: /* MC */
        g_calc.memory_value = 0;
        g_calc.has_memory = 0;
        break;
    case 1: /* MR */
        if (g_calc.has_memory) {
            g_calc.current_value = g_calc.memory_value;
            g_new_input = 1;
            calc_double_to_str(g_calc.current_value, g_calc.display_buf, sizeof(g_calc.display_buf));
        }
        break;
    case 2: /* MS */
        g_calc.memory_value = g_calc.current_value;
        g_calc.has_memory = 1;
        break;
    case 3: /* M+ */
        g_calc.memory_value += g_calc.current_value;
        g_calc.has_memory = 1;
        break;
    }
}

static void calc_process_function(uint32_t op)
{
    switch (op) {
    case CALC_OP_SQRT:
        if (g_calc.current_value >= 0) {
            g_calc.current_value = sqrt(g_calc.current_value);
        } else {
            strcpy(g_calc.display_buf, "Error");
            return;
        }
        break;
    case CALC_OP_SIN:
        g_calc.current_value = sin(g_calc.current_value);
        break;
    case CALC_OP_COS:
        g_calc.current_value = cos(g_calc.current_value);
        break;
    case CALC_OP_TAN:
        g_calc.current_value = tan(g_calc.current_value);
        break;
    case CALC_OP_LOG:
        if (g_calc.current_value > 0)
            g_calc.current_value = log10(g_calc.current_value);
        else { strcpy(g_calc.display_buf, "Error"); return; }
        break;
    case CALC_OP_LN:
        if (g_calc.current_value > 0)
            g_calc.current_value = log(g_calc.current_value);
        else { strcpy(g_calc.display_buf, "Error"); return; }
        break;
    case CALC_OP_POW:
        g_calc.stored_value = g_calc.current_value;
        g_calc.operation = CALC_OP_POW;
        g_calc.has_pending_op = 1;
        g_new_input = 1;
        return;
    case CALC_OP_NEGATE:
        g_calc.current_value = -g_calc.current_value;
        break;
    case CALC_OP_RECIPROCAL:
        if (g_calc.current_value != 0)
            g_calc.current_value = 1.0 / g_calc.current_value;
        else { strcpy(g_calc.display_buf, "Error: Div/0"); return; }
        break;
    case CALC_OP_PERCENT:
        g_calc.current_value = g_calc.current_value / 100.0;
        break;
    default:
        break;
    }

    g_new_input = 1;
    calc_double_to_str(g_calc.current_value, g_calc.display_buf, sizeof(g_calc.display_buf));
}

/* ================================================================
 * 历史管理
 * ================================================================ */
static void calc_add_history(const char *expr, const char *result)
{
    if (g_history_count >= 10) {
        for (int i = 1; i < 10; i++) {
            g_history[i - 1] = g_history[i];
        }
        g_history_count = 9;
    }
    strncpy(g_history[g_history_count].expression, expr, 63);
    g_history[g_history_count].expression[63] = '\0';
    strncpy(g_history[g_history_count].result, result, 63);
    g_history[g_history_count].result[63] = '\0';
    g_history_count++;
}

/* ================================================================
 * 渲染函数
 * ================================================================ */
static void calc_render(sys_window_t *win)
{
    g_win_h = (g_calc.mode == CALC_MODE_SCIENTIFIC) ? WIN_H_SCIENTIFIC : WIN_H;
    sys_fill_window(win, COLOR_BG);
    calc_render_display(win);
    calc_render_buttons(win);
    if (g_show_history) {
        calc_render_history(win);
    }
    calc_render_statusbar(win);
}

static void calc_render_display(sys_window_t *win)
{
    sys_draw_rect(win, 4, 4, WIN_W - 8, DISPLAY_H, COLOR_DISPLAY_BG);

    /* 显示表达式(如果有待处理运算) */
    if (g_calc.has_pending_op) {
        char expr[128];
        calc_double_to_str(g_calc.stored_value, expr, 64);
        int len = (int)strlen(expr);
        switch (g_calc.operation) {
        case CALC_OP_ADD: expr[len] = '+'; break;
        case CALC_OP_SUB: expr[len] = '-'; break;
        case CALC_OP_MUL: expr[len] = '*'; break;
        case CALC_OP_DIV: expr[len] = '/'; break;
        case CALC_OP_POW: expr[len] = '^'; break;
        default: expr[len] = ' '; break;
        }
        expr[len + 1] = '\0';
        sys_draw_text(win, 12, 8, expr, COLOR_DISPLAY_FG);
    }

    /* 显示当前值 */
    const char *display = g_calc.display_buf;
    /* 右对齐 */
    int dlen = (int)strlen(display);
    int dx = WIN_W - 16 - dlen * 8;
    if (dx < 12) dx = 12;

    sys_color_t fg = COLOR_DISPLAY_FG;
    sys_draw_text(win, dx, 32, display, fg);

    /* 内存指示器 */
    if (g_calc.has_memory) {
        sys_draw_text(win, 8, 32, "M", COLOR_MODE_IND);
    }
}

static void calc_render_buttons(sys_window_t *win)
{
    /* 渲染标准按钮 */
    for (int i = 0; g_basic_buttons[i].label; i++) {
        calc_draw_button(win, &g_basic_buttons[i]);
    }

    /* 渲染科学模式按钮 */
    if (g_calc.mode == CALC_MODE_SCIENTIFIC) {
        for (int i = 0; g_sci_buttons[i].label; i++) {
            calc_draw_button(win, &g_sci_buttons[i]);
        }
    }

    /* 更新模式切换按钮文本 */
    if (g_calc.mode == CALC_MODE_SCIENTIFIC) {
        /* 科学模式下显示Std按钮而不是Sci按钮 */
    }
}

static void calc_draw_button(sys_window_t *win, calc_button_t *btn)
{
    sys_color_t btn_color;
    sys_color_t text_color = COLOR_BTN_TEXT;

    switch (btn->type) {
    case BTN_DIGIT:   btn_color = COLOR_BTN_DIGIT; break;
    case BTN_OPERATOR:btn_color = COLOR_BTN_OP; break;
    case BTN_FUNCTION:btn_color = COLOR_BTN_FUNC; break;
    case BTN_EQUALS:  btn_color = COLOR_BTN_EQ; text_color = COLOR_BTN_TEXT_W; break;
    case BTN_CLEAR:   btn_color = COLOR_BTN_CLR; text_color = COLOR_BTN_TEXT_W; break;
    case BTN_MEMORY:  btn_color = COLOR_BTN_MEM; break;
    default:          btn_color = COLOR_BTN_DIGIT; break;
    }

    sys_draw_rect(win, btn->x, btn->y, btn->w, btn->h, btn_color);

    /* 居中绘制文本 */
    int text_len = (int)strlen(btn->label);
    int tx = btn->x + (btn->w - text_len * 8) / 2;
    int ty = btn->y + (btn->h - 16) / 2;
    sys_draw_text(win, tx, ty, btn->label, text_color);
}

static void calc_render_history(sys_window_t *win)
{
    int hx = 8;
    int hy = g_win_h - HISTORY_H - 30;
    int hw = HISTORY_W;
    int hh = HISTORY_H;

    sys_draw_rect(win, hx, hy, hw, hh, COLOR_HISTORY_BG);
    sys_draw_text(win, hx + 4, hy + 2, "History:", COLOR_HISTORY_FG);

    int line_y = hy + 20;
    for (int i = g_history_count - 1; i >= 0 && line_y < hy + hh - 4; i--) {
        char line[128];
        strncpy(line, g_history[i].expression, 30);
        int len = (int)strlen(line);
        line[len] = ' ';
        line[len + 1] = '=';
        line[len + 2] = ' ';
        line[len + 3] = '\0';
        strncat(line, g_history[i].result, 95 - strlen(line));
        sys_draw_text(win, hx + 8, line_y, line, COLOR_HISTORY_FG);
        line_y += 16;
    }
}

static void calc_render_statusbar(sys_window_t *win)
{
    int sy = g_win_h - 22;
    sys_draw_rect(win, 0, sy, WIN_W, 22, COLOR_STATUSBAR);

    const char *mode_text = (g_calc.mode == CALC_MODE_SCIENTIFIC) ? "Scientific" : "Standard";
    char status[64];
    status[0] = 'F'; status[1] = '2'; status[2] = ':'; status[3] = ' ';
    status[4] = 'T'; status[5] = 'o'; status[6] = 'g'; status[7] = 'g'; status[8] = 'l'; status[9] = 'e'; status[10] = ' ';
    status[11] = '|'; status[12] = ' ';
    int pos = 13;
    while (*mode_text && pos < 55) status[pos++] = *mode_text++;
    status[pos++] = ' ';
    status[pos++] = '|';
    status[pos++] = ' ';
    status[pos++] = 'H'; status[pos++] = 'i'; status[pos++] = 's'; status[pos++] = 't'; status[pos++] = ':'; status[pos++] = ' ';
    status[pos++] = 'H'; status[pos++] = ' ';
    status[pos++] = 'b'; status[pos++] = 'u'; status[pos++] = 't'; status[pos++] = 't'; status[pos++] = 'o'; status[pos++] = 'n';
    status[pos] = '\0';
    sys_draw_text(win, 4, sy + 3, status, COLOR_STATUSBAR_FG);
}

/* ================================================================
 * 鼠标事件处理
 * ================================================================ */
static void calc_handle_mouse(int x, int y)
{
    /* 检查标准按钮 */
    int idx = calc_find_button(x, y, g_basic_buttons);
    if (idx >= 0) {
        /* 特殊处理模式切换和历史按钮 */
        if (strcmp(g_basic_buttons[idx].label, "Sci") == 0) {
            g_calc.mode = CALC_MODE_SCIENTIFIC;
            return;
        }
        if (strcmp(g_basic_buttons[idx].label, "Std") == 0) {
            g_calc.mode = CALC_MODE_BASIC;
            return;
        }
        calc_process_button(&g_basic_buttons[idx]);
        return;
    }

    /* 检查科学模式按钮 */
    if (g_calc.mode == CALC_MODE_SCIENTIFIC) {
        idx = calc_find_button(x, y, g_sci_buttons);
        if (idx >= 0) {
            if (strcmp(g_sci_buttons[idx].label, "Std") == 0) {
                g_calc.mode = CALC_MODE_BASIC;
                return;
            }
            calc_process_button(&g_sci_buttons[idx]);
            return;
        }
    }
}

static int calc_find_button(int x, int y, calc_button_t *buttons)
{
    for (int i = 0; buttons[i].label; i++) {
        if (x >= buttons[i].x && x < buttons[i].x + buttons[i].w &&
            y >= buttons[i].y && y < buttons[i].y + buttons[i].h) {
            return i;
        }
    }
    return -1;
}

/* ================================================================
 * 键盘事件处理
 * ================================================================ */
static void calc_handle_key(uint32_t key)
{
    switch (key) {
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
        calc_process_digit(key - '0');
        break;
    case '.':
        if (!g_calc.decimal_mode) {
            g_calc.decimal_mode = 1;
            g_calc.decimal_places = 0;
            g_new_input = 0;
        }
        break;
    case '+': calc_process_operator(CALC_OP_ADD); break;
    case '-': calc_process_operator(CALC_OP_SUB); break;
    case '*': calc_process_operator(CALC_OP_MUL); break;
    case '/': calc_process_operator(CALC_OP_DIV); break;
    case '=': case '\r':
        calc_process_equals();
        break;
    case 27: /* ESC */ calc_process_clear(); break;
    case 0x08: /* Backspace */ calc_process_backspace(); break;
    case 'c': case 'C':
        calc_process_clear_entry();
        break;
    case '%': calc_process_function(CALC_OP_PERCENT); break;
    case 's': case 'S':
        calc_process_function(CALC_OP_SIN);
        break;
    case 'o': case 'O':
        calc_process_function(CALC_OP_COS);
        break;
    case 't': case 'T':
        calc_process_function(CALC_OP_TAN);
        break;
    case 'l': case 'L':
        calc_process_function(CALC_OP_LOG);
        break;
    case 'n': case 'N':
        calc_process_function(CALC_OP_LN);
        break;
    case 'q': case 'Q':
        calc_process_function(CALC_OP_SQRT);
        break;
    case 'r': case 'R':
        calc_process_function(CALC_OP_RECIPROCAL);
        break;
    case 'p': case 'P':
        /* PI */
        g_calc.current_value = M_PI;
        g_new_input = 1;
        calc_double_to_str(g_calc.current_value, g_calc.display_buf, sizeof(g_calc.display_buf));
        break;
    case 'e': case 'E':
        /* e */
        g_calc.current_value = M_E;
        g_new_input = 1;
        calc_double_to_str(g_calc.current_value, g_calc.display_buf, sizeof(g_calc.display_buf));
        break;
    case 'f': case 'F': /* F2: 切换模式 */
        g_calc.mode = (g_calc.mode == CALC_MODE_BASIC) ? CALC_MODE_SCIENTIFIC : CALC_MODE_BASIC;
        break;
    case 'h': case 'H': /* H: 切换历史 */
        g_show_history = !g_show_history;
        break;
    case 'm': case 'M': /* M: 内存存储 */
        calc_process_memory(2);
        break;
    default:
        break;
    }
}

/* ================================================================
 * 工具函数
 * ================================================================ */
static void calc_double_to_str(double value, char *buf, int size)
{
    if (buf == NULL || size <= 0) return;
    if (size <= 1) { buf[0] = '\0'; return; }

    /* 检查特殊值 */
    if (isnan(value)) { strncpy(buf, "NaN", size - 1); return; }
    if (isinf(value)) { strncpy(buf, value > 0 ? "Inf" : "-Inf", size - 1); return; }

    /* 对于非常大的数或非常小的数使用科学计数法简化 */
    int pos = 0;
    int is_neg = 0;
    if (value < 0) { is_neg = 1; value = -value; }
    if (is_neg && pos < size - 1) buf[pos++] = '-';

    int64_t int_part = (int64_t)value;
    double frac_part = value - (double)int_part;

    /* 整数部分 */
    if (int_part == 0) {
        if (pos < size - 1) buf[pos++] = '0';
    } else {
        char tmp[32];
        int tmp_pos = 0;
        int64_t ip = int_part;
        while (ip > 0 && tmp_pos < 30) {
            tmp[tmp_pos++] = (char)('0' + (int)(ip % 10));
            ip /= 10;
        }
        for (int i = tmp_pos - 1; i >= 0 && pos < size - 1; i--) {
            buf[pos++] = tmp[i];
        }
    }

    /* 小数部分 */
    if (frac_part > 0.0000001 && pos < size - 1) {
        buf[pos++] = '.';
        int frac_digits = 0;
        while (frac_part > 0.0000001 && frac_digits < 8 && pos < size - 1) {
            frac_part *= 10.0;
            int digit = (int)frac_part;
            buf[pos++] = (char)('0' + (digit % 10));
            frac_part -= (double)digit;
            frac_digits++;
        }
        /* 去除尾随零 */
        while (pos > 1 && buf[pos - 1] == '0' && buf[pos - 2] != '.') pos--;
    }

    buf[pos] = '\0';
}

static void calc_int_to_str(int n, char *buf)
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