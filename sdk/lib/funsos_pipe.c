/*
 * FUNSOS 管道和 FIFO API 实现
 * ==========================
 * 封装内核管道系统调用，提供匿名管道、命名管道操作。
 *
 */

#include "funsos.h"
#include "funsos_pipe.h"
#include "stddef.h"
#include "string.h"

/* ---- 系统调用号 ---- */
#define SYS_PIPE      12
#define SYS_READ      3
#define SYS_WRITE     4
#define SYS_CLOSE     6
#define SYS_IOCTL     17
#define SYS_OPEN      5
#define SYS_FCNTL     320
#define SYS_DUP       321
#define SYS_DUP2      322

/* 系统调用包装 */
static inline int syscall0(int num) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num)
        : "memory"
    );
    return ret;
}

static inline int syscall1(int num, int a1) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1)
        : "memory"
    );
    return ret;
}

static inline int syscall2(int num, int a1, int a2) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2)
        : "memory"
    );
    return ret;
}

static inline int syscall3(int num, int a1, int a2, int a3) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3)
        : "memory"
    );
    return ret;
}

/* ---- 匿名管道 ---- */

/*
 * 创建匿名管道
 */
int funsos_pipe(int pipefd[2])
{
    return syscall1(SYS_PIPE, (int)pipefd);
}

/*
 * 创建带标志的管道
 */
int funsos_pipe2(int pipefd[2], int flags)
{
    (void)flags;
    return funsos_pipe(pipefd);
}

/* ---- 命名管道 (FIFO) ---- */

/*
 * 创建命名管道
 */
int funsos_mkfifo(const char *pathname, uint32_t mode)
{
    (void)pathname; (void)mode;
    return -1;
}

/*
 * 创建命名管道（带特殊文件类型）
 */
int funsos_mknod(const char *pathname, uint32_t mode, uint32_t dev)
{
    (void)pathname; (void)mode; (void)dev;
    return -1;
}

/* ---- 管道操作 ---- */

/*
 * 从管道读取数据
 */
int funsos_pipe_read(int fd, void *buf, uint32_t count)
{
    return syscall3(SYS_READ, fd, (int)buf, (int)count);
}

/*
 * 向管道写入数据
 */
int funsos_pipe_write(int fd, const void *buf, uint32_t count)
{
    return syscall3(SYS_WRITE, fd, (int)buf, (int)count);
}

/*
 * 关闭管道
 */
int funsos_pipe_close(int fd)
{
    return syscall1(SYS_CLOSE, fd);
}

/* ---- 管道容量控制 ---- */

/*
 * 获取管道可读字节数
 */
int funsos_pipe_bytes_available(int fd)
{
    (void)fd;
    return 0;
}

/*
 * 设置管道容量
 */
int funsos_pipe_set_size(int fd, uint32_t size)
{
    (void)fd; (void)size;
    return -1;
}

/*
 * 获取管道容量
 */
int funsos_pipe_get_size(int fd)
{
    (void)fd;
    return FUNSOS_PIPE_BUF;
}

/* ---- 文件控制操作 ---- */

/*
 * 文件控制操作
 */
int funsos_fcntl(int fd, int cmd, int arg)
{
    return syscall3(SYS_FCNTL, fd, cmd, arg);
}

/* ---- 标准I/O重定向 ---- */

/*
 * 复制文件描述符
 */
int funsos_dup(int oldfd)
{
    return syscall1(SYS_DUP, oldfd);
}

/*
 * 复制文件描述符到指定编号
 */
int funsos_dup2(int oldfd, int newfd)
{
    return syscall2(SYS_DUP2, oldfd, newfd);
}
