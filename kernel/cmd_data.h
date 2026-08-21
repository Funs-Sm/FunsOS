/*
 * kernel/cmd_data.h - P5: 数据工具模块
 */
#ifndef _KERNEL_CMD_DATA_H
#define _KERNEL_CMD_DATA_H

void cmd_dd(const char *args);
void cmd_split(const char *args);
void cmd_join(const char *args);
void cmd_hexdump(const char *file);
void cmd_strings(const char *file);
void cmd_cksum(const char *args);

#endif
