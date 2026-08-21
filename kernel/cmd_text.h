/*
 * kernel/cmd_text.h - P4: 文本工具模块
 */
#ifndef _KERNEL_CMD_TEXT_H
#define _KERNEL_CMD_TEXT_H

void cmd_cut(const char *args);
void cmd_paste(const char *args);
void cmd_tr(const char *args);
void cmd_rev(const char *file);
void cmd_fold(const char *args);
void cmd_expand(const char *args);
void cmd_unexpand(const char *args);
void cmd_nl(const char *file);
void cmd_look(const char *args);
void cmd_comm(const char *args);
void cmd_tsort(const char *file);

#endif
