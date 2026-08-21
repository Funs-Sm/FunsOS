/*
 * kernel/cmd_proc.h - PR-3 iter3
 * cmd_proc 模块对外接口
 */
#ifndef _KERNEL_CMD_PROC_H
#define _KERNEL_CMD_PROC_H

void cmd_ps(void);
void cmd_kill(const char *pid_str);
void cmd_top(void);
void cmd_proc_module_init(void);

#endif