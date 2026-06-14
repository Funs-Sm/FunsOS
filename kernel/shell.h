#ifndef SHELL_H
#define SHELL_H

void shell_init(void);
void shell_run(void);
void shell_execute(const char *cmd);
void shell_set_vbe_mode(int active);
void shell_print(const char *str);

#endif
