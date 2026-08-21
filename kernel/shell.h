#ifndef SHELL_H
#define SHELL_H

void shell_init(void);
void shell_run(void);
void shell_execute(const char *cmd);
void shell_set_vbe_mode(int active);
void shell_print(const char *str);

/* shell.c 提供的全局变量 (cmd_* 模块访问) */
extern int shell_last_exit_code;
extern char shell_current_dir[256];
extern int vbe_mode_active;

#endif
