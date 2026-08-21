#ifndef FUNSOS_PIPE_H
#define FUNSOS_PIPE_H

/*
 * FUNSOS 管道和 FIFO API
 * 提供匿名管道、命名管道(FIFO)的创建和操作。
 * 基于 kernel/pipe.h 的系统调用封装。
 */

#include "stdint.h"

/* ---- 管道相关常量 ---- */
#define FUNSOS_PIPE_BUF       4096    /* 管道缓冲区大小 */
#define FUNSOS_PIPE_MAX       65536   /* 管道最大容量 */

/* 管道标志 */
#define FUNSOS_O_NONBLOCK     0x4000  /* 非阻塞模式 */
#define FUNSOS_O_CLOEXEC     0x8000  /* 执行时关闭 */

/* ---- 匿名管道 ---- */

/*
 * 创建匿名管道
 * 参数: pipefd - 接收两个文件描述符的数组 [0]=读端, [1]=写端
 * 返回: 0 成功, -1 失败
 */
int funsos_pipe(int pipefd[2]);

/*
 * 创建带标志的管道
 * 参数: pipefd - 接收两个文件描述符的数组; flags - 标志位
 * 返回: 0 成功, -1 失败
 */
int funsos_pipe2(int pipefd[2], int flags);

/* ---- 命名管道 (FIFO) ---- */

/*
 * 创建命名管道
 * 参数: pathname - FIFO路径; mode - 权限模式
 * 返回: 0 成功, -1 失败
 */
int funsos_mkfifo(const char *pathname, uint32_t mode);

/*
 * 创建命名管道（带特殊文件类型）
 * 参数: pathname - FIFO路径; mode - 权限模式; dev - 设备号（FIFO用不到）
 * 返回: 0 成功, -1 失败
 */
int funsos_mknod(const char *pathname, uint32_t mode, uint32_t dev);

/* ---- 管道操作 ---- */

/*
 * 从管道读取数据
 * 参数: fd - 文件描述符; buf - 接收缓冲区; count - 读取字节数
 * 返回: 实际读取的字节数, -1 失败
 */
int funsos_pipe_read(int fd, void *buf, uint32_t count);

/*
 * 向管道写入数据
 * 参数: fd - 文件描述符; buf - 写入数据; count - 写入字节数
 * 返回: 实际写入的字节数, -1 失败
 */
int funsos_pipe_write(int fd, const void *buf, uint32_t count);

/*
 * 关闭管道
 * 参数: fd - 文件描述符
 * 返回: 0 成功, -1 失败
 */
int funsos_pipe_close(int fd);

/* ---- 管道容量控制 ---- */

/*
 * 获取管道可读字节数
 * 参数: fd - 文件描述符
 * 返回: 可读字节数, -1 失败
 */
int funsos_pipe_bytes_available(int fd);

/*
 * 设置管道容量
 * 参数: fd - 文件描述符; size - 新的容量大小
 * 返回: 0 成功, -1 失败
 */
int funsos_pipe_set_size(int fd, uint32_t size);

/*
 * 获取管道容量
 * 参数: fd - 文件描述符
 * 返回: 管道容量, -1 失败
 */
int funsos_pipe_get_size(int fd);

/* ---- 管道操作命令 (fcntl 风格) ---- */

/* fcntl 命令 */
#define FUNSOS_F_DUPFD       0   /* 复制文件描述符 */
#define FUNSOS_F_GETFD       1   /* 获取文件描述符标志 */
#define FUNSOS_F_SETFD       2   /* 设置文件描述符标志 */
#define FUNSOS_F_GETFL       3   /* 获取文件状态标志 */
#define FUNSOS_F_SETFL       4   /* 设置文件状态标志 */
#define FUNSOS_F_GETLK       5   /* 获取记录锁 */
#define FUNSOS_F_SETLK       6   /* 设置记录锁 */
#define FUNSOS_F_SETLKW      7   /* 设置记录锁（阻塞） */

/*
 * 文件控制操作
 * 参数: fd - 文件描述符; cmd - 命令; ... - 命令参数
 * 返回: 取决于命令, -1 失败
 */
int funsos_fcntl(int fd, int cmd, int arg);

/* ---- 标准I/O重定向 ---- */

/*
 * 复制文件描述符
 * 参数: oldfd - 原文件描述符
 * 返回: 新文件描述符, -1 失败
 */
int funsos_dup(int oldfd);

/*
 * 复制文件描述符到指定编号
 * 参数: oldfd - 原文件描述符; newfd - 目标编号
 * 返回: 新文件描述符, -1 失败
 */
int funsos_dup2(int oldfd, int newfd);

#endif /* FUNSOS_PIPE_H */
