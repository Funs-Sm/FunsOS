#ifndef FUNSOS_DIR_H
#define FUNSOS_DIR_H

/*
 * FUNSOS 目录操作 API
 * 提供目录遍历、目录创建/删除、目录项操作等功能。
 * 基于 kernel/vfs.h 和 apps/user_syscall.h 的文件系统调用。
 */

#include "stdint.h"

/* ---- 目录操作常量 ---- */
#define FUNSOS_DT_UNKNOWN     0    /* 未知类型 */
#define FUNSOS_DT_REG         1    /* 普通文件 */
#define FUNSOS_DT_DIR         2    /* 目录 */
#define FUNSOS_DT_LNK         3    /* 符号链接 */
#define FUNSOS_DT_CHR         4    /* 字符设备 */
#define FUNSOS_DT_BLK         5    /* 块设备 */
#define FUNSOS_DT_FIFO        6    /* FIFO/管道 */
#define FUNSOS_DT_SOCK        7    /* 套接字 */

/* 目录流不透明类型 */
typedef struct funsos_dir FUNSOS_DIR;

/* ---- 目录项结构 ---- */
typedef struct {
    uint32_t   d_ino;          /* inode 编号 */
    uint32_t   d_off;          /* 到下一个目录项的偏移 */
    uint16_t   d_reclen;       /* 此记录的长度 */
    uint8_t    d_type;         /* 文件类型 */
    char       d_name[256];    /* 文件名 */
} funsos_dirent_t;

/*
 * 打开目录
 * 参数: name - 目录路径
 * 返回: 目录流指针, NULL 表示失败
 */
FUNSOS_DIR *funsos_opendir(const char *name);

/*
 * 关闭目录
 * 参数: dirp - 目录流指针
 * 返回: 0 成功, -1 失败
 */
int funsos_closedir(FUNSOS_DIR *dirp);

/*
 * 读取目录项
 * 参数: dirp - 目录流指针
 * 返回: 目录项指针, NULL 表示到达目录末尾或出错
 */
funsos_dirent_t *funsos_readdir(FUNSOS_DIR *dirp);

/*
 * 重置目录流位置到开头
 * 参数: dirp - 目录流指针
 */
void funsos_rewinddir(FUNSOS_DIR *dirp);

/*
 * 获取目录流当前位置
 * 参数: dirp - 目录流指针
 * 返回: 当前位置
 */
long funsos_telldir(FUNSOS_DIR *dirp);

/*
 * 设置目录流位置
 * 参数: dirp - 目录流指针; loc - 位置 (从 telldir 获取)
 */
void funsos_seekdir(FUNSOS_DIR *dirp, long loc);

/*
 * 创建目录（可指定权限）
 * 参数: path - 目录路径; mode - 权限模式
 * 返回: 0 成功, -1 失败
 */
int funsos_mkdir(const char *path, uint32_t mode);

/*
 * 删除空目录
 * 参数: path - 目录路径
 * 返回: 0 成功, -1 失败
 */
int funsos_rmdir(const char *path);

/*
 * 递归创建目录
 * 参数: path - 目录路径; mode - 权限模式
 * 返回: 0 成功, -1 失败
 */
int funsos_mkdir_p(const char *path, uint32_t mode);

/*
 * 递归删除目录
 * 参数: path - 目录路径
 * 返回: 0 成功, -1 失败
 */
int funsos_rmdir_r(const char *path);

/*
 * 检查目录是否为空
 * 参数: path - 目录路径
 * 返回: 1 空, 0 非空, -1 错误
 */
int funsos_dir_empty(const char *path);

/*
 * 获取目录大小（递归统计所有文件大小）
 * 参数: path - 目录路径
 * 返回: 总大小（字节）, -1 失败
 */
int64_t funsos_dir_size(const char *path);

/*
 * 目录遍历回调函数类型
 * 参数: path - 当前路径; name - 文件名; type - 文件类型; user_data - 用户数据
 * 返回: 0 继续遍历, 非0 停止遍历
 */
typedef int (*funsos_dir_walk_callback_t)(const char *path, const char *name, 
                                          uint32_t type, void *user_data);

/*
 * 递归遍历目录
 * 参数: path - 起始路径; callback - 回调函数; user_data - 用户数据
 * 返回: 0 成功, -1 失败
 */
int funsos_dir_walk(const char *path, funsos_dir_walk_callback_t callback, void *user_data);

/*
 * 非递归遍历目录（只遍历一层）
 * 参数: path - 目录路径; callback - 回调函数; user_data - 用户数据
 * 返回: 0 成功, -1 失败
 */
int funsos_dir_foreach(const char *path, funsos_dir_walk_callback_t callback, void *user_data);

#endif /* FUNSOS_DIR_H */
