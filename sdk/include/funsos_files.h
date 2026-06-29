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
#define FUNSOS_O_EXCL      0x20000 /* 与 CREAT 一起使用，文件已存在则失败 */

/* ---- 文件寻址方式 ---- */
#define FUNSOS_SEEK_SET    0  /* 从文件开头 */
#define FUNSOS_SEEK_CUR    1  /* 从当前位置 */
#define FUNSOS_SEEK_END    2  /* 从文件末尾 */

/* ---- 文件类型（用于 stat） ---- */
#define FUNSOS_S_IFREG     0x8000 /* 普通文件 */
#define FUNSOS_S_IFDIR     0x4000 /* 目录 */
#define FUNSOS_S_IFLNK     0xA000 /* 符号链接 */

/* ---- 目录项结构 ---- */
typedef struct {
    char     name[256];   /* 文件名 */
    uint32_t type;        /* 文件类型 */
    uint32_t size;        /* 文件大小 */
} funsos_dirent_t;

/* ---- 文件 stat 结构 ---- */
typedef struct {
    uint32_t st_mode;     /* 文件模式 */
    uint32_t st_size;     /* 文件大小（字节） */
    uint32_t st_atime;    /* 最后访问时间 */
    uint32_t st_mtime;    /* 最后修改时间 */
    uint32_t st_ctime;    /* 创建时间 */
} funsos_stat_t;

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
 * 删除空目录
 * 参数: path - 目录路径
 * 返回: 0 成功, -1 失败
 */
int funsos_file_rmdir(const char *path);

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

/*
 * 获取文件状态信息
 * 参数: path - 文件路径; buf - 接收 stat 信息的缓冲区
 * 返回: 0 成功, -1 失败
 */
int funsos_file_stat(const char *path, funsos_stat_t *buf);

/*
 * 重命名文件
 * 参数: old_path - 原路径; new_path - 新路径
 * 返回: 0 成功, -1 失败
 */
int funsos_file_rename(const char *old_path, const char *new_path);

/*
 * 检查文件是否存在
 * 参数: path - 文件路径
 * 返回: 1 存在, 0 不存在
 */
int funsos_file_exists(const char *path);

/*
 * 检查路径是否为目录
 * 参数: path - 路径
 * 返回: 1 是目录, 0 不是或不存在
 */
int funsos_file_isdir(const char *path);

/*
 * 获取文件大小
 * 参数: path - 文件路径
 * 返回: 文件大小（字节）, -1 失败
 */
int funsos_file_size(const char *path);

/*
 * 创建符号链接
 * 参数: target - 目标路径; linkpath - 链接路径
 * 返回: 0 成功, -1 失败
 */
int funsos_file_symlink(const char *target, const char *linkpath);

/*
 * 读取符号链接
 * 参数: path - 链接路径; buf - 接收缓冲区; size - 缓冲区大小
 * 返回: 读取的字符数, -1 失败
 */
int funsos_file_readlink(const char *path, char *buf, uint32_t size);

/*
 * 同步文件到磁盘
 * 参数: fd - 文件描述符
 * 返回: 0 成功, -1 失败
 */
int funsos_file_sync(int fd);

/*
 * 同步整个文件系统
 * 返回: 0 成功
 */
int funsos_file_syncfs(void);

#endif /* FUNSOS_FILES_H */
