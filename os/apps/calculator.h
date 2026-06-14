/* calculator.h - FUNSOS 计算器应用
 * 图形化计算器，支持基本运算和科学计算
 */

#ifndef CALCULATOR_H
#define CALCULATOR_H

#include "stdint.h"

/* 计算器模式 */
#define CALC_MODE_BASIC    0  /* 基本运算 */
#define CALC_MODE_SCIENTIFIC 1 /* 科学计算 */

/* 运算类型 */
#define CALC_OP_NONE       0
#define CALC_OP_ADD        1
#define CALC_OP_SUB        2
#define CALC_OP_MUL        3
#define CALC_OP_DIV        4
#define CALC_OP_MOD        5
#define CALC_OP_POW        6
#define CALC_OP_SQRT       7
#define CALC_OP_SIN        8
#define CALC_OP_COS        9
#define CALC_OP_TAN        10
#define CALC_OP_LOG        11
#define CALC_OP_LN         12
#define CALC_OP_FACTORIAL  13
#define CALC_OP_PERCENT    14
#define CALC_OP_NEGATE     15
#define CALC_OP_RECIPROCAL 16

/* 计算器状态 */
typedef struct {
    double  current_value;    /* 当前显示值 */
    double  stored_value;     /* 存储值（用于连算） */
    double  memory_value;     /* 内存值 */
    uint32_t operation;       /* 待执行的运算 */
    uint32_t mode;            /* 计算器模式 */
    uint8_t  has_pending_op;  /* 是否有待执行运算 */
    uint8_t  has_memory;      /* 是否有内存值 */
    uint8_t  clear_on_input;  /* 下一次输入时是否清屏 */
    uint8_t  decimal_mode;    /* 是否处于小数输入模式 */
    uint32_t decimal_places;  /* 小数位数 */
    char     display_buf[64]; /* 显示缓冲区 */
} calculator_state_t;

/* 初始化计算器 */
int calculator_init(void);

/* 运行计算器主循环 */
void calculator_run(void);

/* 处理按键 */
int calculator_key_press(uint32_t key);

/* 获取当前状态 */
const calculator_state_t *calculator_get_state(void);

/* 获取显示内容 */
const char *calculator_get_display(void);

/* 关闭计算器 */
void calculator_close(void);

#endif /* CALCULATOR_H */