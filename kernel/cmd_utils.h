/*
 * kernel/cmd_utils.h - P1: 工具命令模块
 */
#ifndef _KERNEL_CMD_UTILS_H
#define _KERNEL_CMD_UTILS_H

void cmd_cal(const char *arg);
void cmd_yes(const char *str);
void cmd_seq(const char *first, const char *last);
void cmd_factor(const char *num_str);
void cmd_shuf(const char *file);
void cmd_false_cmd(void);
void cmd_true_cmd(void);
void cmd_test_cmd(const char *expr);
void cmd_expr_cmd(const char *math);

#endif
