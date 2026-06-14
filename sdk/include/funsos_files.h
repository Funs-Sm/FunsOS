#ifndef FUNSOS_FILES_H
#define FUNSOS_FILES_H

/*
 * FUNSOS 文件系统 API
 * 提供 POSIX 兼容的文件操作接口。
 * 基于 kernel/sys_api.h 和 apps/user_syscall.h 的文件系统调用。
 */

#include "stdint.h"

/* ---- 文件打开模式 ---- */
#define FUNSOS_O_RDONLY    0       /* 只读 */
#define FUNSOS_O_WRONLY    1       /* 只写 */
#define FUNSOS_O_RDWR      2       /* 读写 */
#define FUNSOS_O_CREAT     0x100   /* 若不存在则创建 */
#define FUNSOS_O_TRUNC     0x200   /* 截断为 0 */
#define FUNSOS_O_APPEND    0x400   /* 追加写入 */
#define FUNSOS_O_DIRECTORY 0x10000 /* 打开目录 */

/* ---- 文件寻址方式 ---- */
#define FUNSOS_SEEK_SET    0  /* 从文件开头 */
#define FUNSOS_SEEK_CUR    1  /* 从当前位置 */
#define FUNSOS_SEEK_END    2  /* 从文件末尾 */

/*
 * 打开文件
 * 参数: path - 文件路径; mode - 打开模式 (O_RDONLY | O_WRONLY | O_RDWR | O_CREAT 等)
 * 返回: 文件描述符 (>=0), -1 表示失败
 */
int funsos_file_open(const char *path, uint32_t mode);

/*
 * 读取文件
 * 参数: fd - 文件描述符; buf - 接收缓冲区; count - 读取字节数
 * 返回: 实际读取的字节数, -1 表示失败
 */
int funsos_file_read(int fd, void *buf, uint32_t count);

/*
 * 写入文件
 * 参数: fd - 文件描述符; buf - 写入数据; count - 写入字节数
 * 返回: 实际写入的字节数, -1 表示失败
 */
int funsos_file_write(int fd, const void *buf, uint32_t count);

/*
 * 关闭文件
 * 参数: fd - 文件描述符
 * 返回: 0 成功, -1 失败
 */
int funsos_file_close(int fd);

/*
 * 设置文件偏移量
 * 参数: fd - 文件描述符; offset - 偏移量; whence - 寻址方式
 * 返回: 新的偏移量, -1 失败
 */
int funsos_file_seek(int fd, int offset, int whence);

/*
 * 删除文件
 * 参数: path - 文件路径
 * 返回: 0 成功, -1 失败
 */
int funsos_file_remove(const char *path);

/*
 * 创建目录
 * 参数: path - 目录路径
 * 返回: 0 成功, -1 失败
 */
int funsos_file_mkdir(const char *path);

/*
 * 改变当前工作目录
 * 参数: path - 新的工作目录路径
 * 返回: 0 成功, -1 失败
 */
int funsos_file_chdir(const char *path);

/*
 * 获取当前工作目录
 * 参数: buf - 接收缓冲区; size - 缓冲区大小
 * 返回: 0 成功, -1 失败
 */
int funsos_file_getcwd(char *buf, uint32_t size);

/*
 * 读取目录内容
 * 参数: fd - 目录的文件描述符; buf - 接收缓冲区; count - 缓冲区大小
 * 返回: 实际读取的字节数, -1 失败
 */
int funsos_file_readdir(int fd, void *buf, uint32_t count);

#endif /* FUNSOS_FILES_H */
