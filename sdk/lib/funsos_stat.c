/*
 * FUNSOS 文件状态和统计 API 实现
 * ==============================
 * 封装内核 VFS 系统调用，提供文件状态查询、文件系统统计等功能。
 *
 */

#include "funsos.h"
#include "funsos_stat.h"
#include "stddef.h"
#include "string.h"

/* ---- 系统调用号 ---- */
#define SYS_OPEN      5
#define SYS_CLOSE     6
#define SYS_READ      3
#define SYS_WRITE     4
#define SYS_IOCTL     17

/* 系统调用包装 */
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

static inline int syscall4(int num, int a1, int a2, int a3, int a4) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4)
        : "memory"
    );
    return ret;
}

/*
 * 获取文件状态信息
 */
int funsos_stat(const char *path, funsos_stat_t *buf)
{
    if (path == NULL || buf == NULL) return -1;
    return funsos_file_stat(path, (funsos_stat_t *)buf);
}

/*
 * 获取文件状态信息（不跟随符号链接）
 */
int funsos_lstat(const char *path, funsos_stat_t *buf)
{
    if (path == NULL || buf == NULL) return -1;
    /* 简化：当前实现不区分 stat/lstat */
    return funsos_file_stat(path, (funsos_stat_t *)buf);
}

/*
 * 获取文件状态信息（通过文件描述符）
 */
int funsos_fstat(int fd, funsos_stat_t *buf)
{
    if (buf == NULL) return -1;
    /* 使用 ioctl 获取 fstat */
    return syscall3(SYS_IOCTL, fd, 10, (int)buf);
}

/*
 * 获取文件系统统计信息
 */
int funsos_statfs(const char *path, funsos_statfs_t *buf)
{
    if (path == NULL || buf == NULL) return -1;
    /* 通过内核系统调用获取 */
    return funsos_statfs(path, buf);
}

/*
 * 获取文件系统统计信息（通过文件描述符）
 */
int funsos_fstatfs(int fd, funsos_statfs_t *buf)
{
    if (buf == NULL) return -1;
    /* 使用 ioctl 获取 fstatfs */
    return syscall3(SYS_IOCTL, fd, 11, (int)buf);
}

/*
 * 改变文件权限
 */
int funsos_chmod(const char *path, uint32_t mode)
{
    if (path == NULL) return -1;
    return funsos_chmod(path, mode);
}

/*
 * 改变文件权限（通过文件描述符）
 */
int funsos_fchmod(int fd, uint32_t mode)
{
    return syscall3(SYS_IOCTL, fd, 12, (int)&mode);
}

/*
 * 改变文件所有者和组
 */
int funsos_chown(const char *path, uint32_t owner, uint32_t group)
{
    if (path == NULL) return -1;
    return funsos_chown(path, owner, group);
}

/*
 * 改变文件所有者和组（不跟随符号链接）
 */
int funsos_lchown(const char *path, uint32_t owner, uint32_t group)
{
    if (path == NULL) return -1;
    /* 简化：同 chown */
    return funsos_chown(path, owner, group);
}

/*
 * 改变文件所有者和组（通过文件描述符）
 */
int funsos_fchown(int fd, uint32_t owner, uint32_t group)
{
    uint32_t buf[2] = {owner, group};
    return syscall3(SYS_IOCTL, fd, 13, (int)buf);
}

/*
 * 改变文件访问和修改时间
 */
int funsos_utime(const char *path, const funsos_utimbuf_t *times)
{
    if (path == NULL) return -1;
    return syscall2(SYS_IOCTL, -1, 14, (int)path);
}

/*
 * 改变文件访问和修改时间（纳秒精度）
 */
int funsos_utimens(const char *path, const funsos_timespec_t times[2])
{
    if (path == NULL) return -1;
    /* 简化：使用 utime 接口 */
    return syscall2(SYS_IOCTL, -1, 14, (int)path);
}

/*
 * 截断文件到指定大小
 */
int funsos_truncate(const char *path, uint32_t length)
{
    if (path == NULL) return -1;
    /* 通过打开文件然后 ftruncate 实现 */
    int fd = funsos_file_open(path, FUNSOS_O_WRONLY);
    if (fd < 0) return -1;
    int ret = funsos_ftruncate(fd, length);
    funsos_file_close(fd);
    return ret;
}

/*
 * 截断文件到指定大小（通过文件描述符）
 */
int funsos_ftruncate(int fd, uint32_t length)
{
    return syscall3(SYS_IOCTL, fd, 15, (int)&length);
}

/*
 * 检查文件访问权限
 */
int funsos_access(const char *path, int mode)
{
    if (path == NULL) return -1;
    /* 简化：尝试打开文件来检查 */
    if (mode == FUNSOS_F_OK) {
        return funsos_file_exists(path) ? 0 : -1;
    }
    /* 简化实现：始终返回 0 */
    return 0;
}

/*
 * 同步文件数据到磁盘
 */
int funsos_fsync(int fd)
{
    return funsos_file_sync(fd);
}

/*
 * 同步文件数据（不含元数据）到磁盘
 */
int funsos_fdatasync(int fd)
{
    /* 简化：同 fsync */
    return funsos_file_sync(fd);
}
