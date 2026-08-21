/*
 * kernel/cmd_path.h - P3: 路径工具模块
 */
#ifndef _KERNEL_CMD_PATH_H
#define _KERNEL_CMD_PATH_H

void cmd_basename(const char *path, const char *suffix);
void cmd_dirname(const char *path);
void cmd_realpath(const char *path);
void cmd_truncate(const char *path, const char *size_str);

#endif
