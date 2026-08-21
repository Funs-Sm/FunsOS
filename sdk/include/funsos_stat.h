#ifndef FUNSOS_STAT_H
#define FUNSOS_STAT_H

/*
 * FUNSOS 文件状态和统计 API
 * 提供文件状态查询、文件系统统计、文件属性操作等功能。
 * 基于 kernel/vfs.h 和 fs/fs_stat.h 的系统调用封装。
 */

#include "stdint.h"

/* ---- 文件类型宏 ---- */
#define FUNSOS_S_IFMT      0xF000   /* 文件类型掩码 */
#define FUNSOS_S_IFREG     0x8000   /* 普通文件 */
#define FUNSOS_S_IFDIR     0x4000   /* 目录 */
#define FUNSOS_S_IFLNK     0xA000   /* 符号链接 */
#define FUNSOS_S_IFCHR     0x2000   /* 字符设备 */
#define FUNSOS_S_IFBLK     0x6000   /* 块设备 */
#define FUNSOS_S_IFIFO     0x1000   /* FIFO/管道 */
#define FUNSOS_S_IFSOCK    0xC000   /* 套接字 */

/* ---- 文件类型判断宏 ---- */
#define FUNSOS_S_ISREG(m)  (((m) & FUNSOS_S_IFMT) == FUNSOS_S_IFREG)   /* 普通文件 */
#define FUNSOS_S_ISDIR(m)  (((m) & FUNSOS_S_IFMT) == FUNSOS_S_IFDIR)   /* 目录 */
#define FUNSOS_S_ISLNK(m)  (((m) & FUNSOS_S_IFMT) == FUNSOS_S_IFLNK)   /* 符号链接 */
#define FUNSOS_S_ISCHR(m)  (((m) & FUNSOS_S_IFMT) == FUNSOS_S_IFCHR)   /* 字符设备 */
#define FUNSOS_S_ISBLK(m)  (((m) & FUNSOS_S_IFMT) == FUNSOS_S_IFBLK)   /* 块设备 */
#define FUNSOS_S_ISFIFO(m) (((m) & FUNSOS_S_IFMT) == FUNSOS_S_IFIFO)  /* FIFO/管道 */
#define FUNSOS_S_ISSOCK(m) (((m) & FUNSOS_S_IFMT) == FUNSOS_S_IFSOCK)  /* 套接字 */

/* ---- 权限位 ---- */
#define FUNSOS_S_IRWXU     0700   /* 所有者读写执行 */
#define FUNSOS_S_IRUSR     0400   /* 所有者读 */
#define FUNSOS_S_IWUSR     0200   /* 所有者写 */
#define FUNSOS_S_IXUSR     0100   /* 所有者执行 */
#define FUNSOS_S_IRWXG     0070   /* 组用户读写执行 */
#define FUNSOS_S_IRGRP     0040   /* 组用户读 */
#define FUNSOS_S_IWGRP     0020   /* 组用户写 */
#define FUNSOS_S_IXGRP     0010   /* 组用户执行 */
#define FUNSOS_S_IRWXO     0007   /* 其他用户读写执行 */
#define FUNSOS_S_IROTH     0004   /* 其他用户读 */
#define FUNSOS_S_IWOTH     0002   /* 其他用户写 */
#define FUNSOS_S_IXOTH     0001   /* 其他用户执行 */
#define FUNSOS_S_ISUID     04000  /* 设置用户ID */
#define FUNSOS_S_ISGID     02000  /* 设置组ID */
#define FUNSOS_S_ISVTX     01000  /* 粘滞位 */

/* ---- 文件 stat 结构 ---- */
typedef struct {
    uint32_t st_dev;         /* 设备ID */
    uint32_t st_ino;         /* inode 编号 */
    uint32_t st_mode;        /* 文件模式 */
    uint32_t st_nlink;       /* 硬链接数 */
    uint32_t st_uid;         /* 所有者用户ID */
    uint32_t st_gid;         /* 所有者组ID */
    uint32_t st_rdev;        /* 设备号（特殊文件） */
    uint32_t st_size;        /* 文件大小（字节） */
    uint32_t st_blksize;     /* 块大小 */
    uint32_t st_blocks;      /* 占用块数 */
    uint32_t st_atime;       /* 最后访问时间 */
    uint32_t st_mtime;       /* 最后修改时间 */
    uint32_t st_ctime;       /* 创建/状态改变时间 */
} funsos_stat_t;

/* ---- 文件系统 stat 结构 ---- */
typedef struct {
    uint32_t f_type;         /* 文件系统类型 */
    uint32_t f_bsize;        /* 块大小 */
    uint32_t f_blocks;       /* 总块数 */
    uint32_t f_bfree;        /* 空闲块数 */
    uint32_t f_bavail;       /* 可用块数（普通用户） */
    uint32_t f_files;        /* 总文件节点数 */
    uint32_t f_ffree;        /* 空闲文件节点数 */
    uint32_t f_fsid;         /* 文件系统ID */
    uint32_t f_namelen;      /* 最大文件名长度 */
    uint32_t f_frsize;       /* 基本文件系统块大小 */
} funsos_statfs_t;

/* ---- 文件系统类型常量 ---- */
#define FUNSOS_FS_EXT2      0xEF53
#define FUNSOS_FS_EXT3      0xEF53
#define FUNSOS_FS_EXT4      0xEF53
#define FUNSOS_FS_FAT32     0x4D44
#define FUNSOS_FS_NTFS      0x5346544E
#define FUNSOS_FS_ISO9660   0x9660
#define FUNSOS_FS_PROC      0x9FA0
#define FUNSOS_FS_SYSFS     0x62656572
#define FUNSOS_FS_TMPFS     0x01021994
#define FUNSOS_FS_RAMFS     0x858458F6
#define FUNSOS_FS_DEVFTS    0x42494E4D
#define FUNSOS_FS_BTRFS     0x9123683E
#define FUNSOS_FS_XFS       0x58465342

/* ---- utime 访问时间/修改时间结构 ---- */
typedef struct {
    uint32_t actime;         /* 访问时间 */
    uint32_t modtime;        /* 修改时间 */
} funsos_utimbuf_t;

/* ---- 时间规格结构（纳秒精度） ---- */
typedef struct {
    uint32_t tv_sec;         /* 秒 */
    uint32_t tv_nsec;        /* 纳秒 */
} funsos_timespec_t;

/*
 * 获取文件状态信息
 * 参数: path - 文件路径; buf - 接收 stat 信息的缓冲区
 * 返回: 0 成功, -1 失败
 */
int funsos_stat(const char *path, funsos_stat_t *buf);

/*
 * 获取文件状态信息（不跟随符号链接）
 * 参数: path - 文件路径; buf - 接收 stat 信息的缓冲区
 * 返回: 0 成功, -1 失败
 */
int funsos_lstat(const char *path, funsos_stat_t *buf);

/*
 * 获取文件状态信息（通过文件描述符）
 * 参数: fd - 文件描述符; buf - 接收 stat 信息的缓冲区
 * 返回: 0 成功, -1 失败
 */
int funsos_fstat(int fd, funsos_stat_t *buf);

/*
 * 获取文件系统统计信息
 * 参数: path - 文件系统上的任意路径; buf - 接收统计信息的缓冲区
 * 返回: 0 成功, -1 失败
 */
int funsos_statfs(const char *path, funsos_statfs_t *buf);

/*
 * 获取文件系统统计信息（通过文件描述符）
 * 参数: fd - 文件描述符; buf - 接收统计信息的缓冲区
 * 返回: 0 成功, -1 失败
 */
int funsos_fstatfs(int fd, funsos_statfs_t *buf);

/*
 * 改变文件权限
 * 参数: path - 文件路径; mode - 新的权限模式
 * 返回: 0 成功, -1 失败
 */
int funsos_chmod(const char *path, uint32_t mode);

/*
 * 改变文件权限（通过文件描述符）
 * 参数: fd - 文件描述符; mode - 新的权限模式
 * 返回: 0 成功, -1 失败
 */
int funsos_fchmod(int fd, uint32_t mode);

/*
 * 改变文件所有者和组
 * 参数: path - 文件路径; owner - 所有者ID; group - 组ID
 * 返回: 0 成功, -1 失败
 */
int funsos_chown(const char *path, uint32_t owner, uint32_t group);

/*
 * 改变文件所有者和组（不跟随符号链接）
 * 参数: path - 文件路径; owner - 所有者ID; group - 组ID
 * 返回: 0 成功, -1 失败
 */
int funsos_lchown(const char *path, uint32_t owner, uint32_t group);

/*
 * 改变文件所有者和组（通过文件描述符）
 * 参数: fd - 文件描述符; owner - 所有者ID; group - 组ID
 * 返回: 0 成功, -1 失败
 */
int funsos_fchown(int fd, uint32_t owner, uint32_t group);

/*
 * 改变文件访问和修改时间
 * 参数: path - 文件路径; times - 时间结构（NULL 表示使用当前时间）
 * 返回: 0 成功, -1 失败
 */
int funsos_utime(const char *path, const funsos_utimbuf_t *times);

/*
 * 改变文件访问和修改时间（纳秒精度）
 * 参数: path - 文件路径; times - 时间规格数组 [0]=访问时间, [1]=修改时间
 * 返回: 0 成功, -1 失败
 */
int funsos_utimens(const char *path, const funsos_timespec_t times[2]);

/*
 * 截断文件到指定大小
 * 参数: path - 文件路径; length - 新的文件大小
 * 返回: 0 成功, -1 失败
 */
int funsos_truncate(const char *path, uint32_t length);

/*
 * 截断文件到指定大小（通过文件描述符）
 * 参数: fd - 文件描述符; length - 新的文件大小
 * 返回: 0 成功, -1 失败
 */
int funsos_ftruncate(int fd, uint32_t length);

/*
 * 检查文件访问权限
 * 参数: path - 文件路径; mode - 访问模式 (F_OK=存在, R_OK, W_OK, X_OK)
 * 返回: 0 成功, -1 失败
 */
int funsos_access(const char *path, int mode);

/* 访问模式常量 */
#define FUNSOS_F_OK        0   /* 检查文件是否存在 */
#define FUNSOS_R_OK        4   /* 检查读权限 */
#define FUNSOS_W_OK        2   /* 检查写权限 */
#define FUNSOS_X_OK        1   /* 检查执行权限 */

/*
 * 同步文件数据到磁盘
 * 参数: fd - 文件描述符
 * 返回: 0 成功, -1 失败
 */
int funsos_fsync(int fd);

/*
 * 同步文件数据（不含元数据）到磁盘
 * 参数: fd - 文件描述符
 * 返回: 0 成功, -1 失败
 */
int funsos_fdatasync(int fd);

#endif /* FUNSOS_STAT_H */
