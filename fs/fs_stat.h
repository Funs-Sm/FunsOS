#ifndef FS_STAT_H
#define FS_STAT_H

#include "stdint.h"

/* ============================================================
 * Filesystem Statistics - 文件系统统计与监控
 *
 * 提供文件系统级别的统计信息：
 * - I/O 统计（读/写次数、字节数）
 * - 操作计数（open/close/read/write/create/delete 等）
 * - 缓存统计
 * - 错误统计
 * ============================================================ */

/* I/O 统计 */
typedef struct fs_io_stats {
    uint64_t reads;          /* 读操作次数 */
    uint64_t writes;         /* 写操作次数 */
    uint64_t read_bytes;     /* 读取字节总数 */
    uint64_t write_bytes;    /* 写入字节总数 */
    uint64_t read_errors;    /* 读错误次数 */
    uint64_t write_errors;   /* 写错误次数 */
    uint64_t max_read_latency;  /* 最大读延迟 (ms) */
    uint64_t max_write_latency; /* 最大写延迟 (ms) */
} fs_io_stats_t;

/* 操作统计 */
typedef struct fs_op_stats {
    uint64_t opens;          /* open 调用次数 */
    uint64_t closes;         /* close 调用次数 */
    uint64_t reads;          /* read 调用次数 */
    uint64_t writes;         /* write 调用次数 */
    uint64_t seeks;          /* seek 调用次数 */
    uint64_t creates;        /* create 调用次数 */
    uint64_t deletes;        /* unlink/rmdir 调用次数 */
    uint64_t mkdirs;         /* mkdir 调用次数 */
    uint64_t renames;        /* rename 调用次数 */
    uint64_t stats;          /* stat 调用次数 */
    uint64_t ioctls;         /* ioctl 调用次数 */
    uint64_t mounts;         /* mount 调用次数 */
    uint64_t umounts;        /* umount 调用次数 */
    uint64_t lookups;        /* 路径查找次数 */
    uint64_t cache_hits;     /* dentry 缓存命中 */
    uint64_t cache_misses;   /* dentry 缓存未命中 */
} fs_op_stats_t;

/* 错误统计 */
typedef struct fs_error_stats {
    uint64_t enoent;         /* 文件不存在 */
    uint64_t eacces;         /* 权限拒绝 */
    uint64_t enospc;         /* 空间不足 */
    uint64_t eio;            /* I/O 错误 */
    uint64_t enomem;         /* 内存不足 */
    uint64_t einval;         /* 无效参数 */
    uint64_t ebusy;          /* 设备忙 */
    uint64_t enotdir;        /* 不是目录 */
    uint64_t eisdir;         /* 是目录 */
    uint64_t eexist;         /* 文件已存在 */
    uint64_t other;          /* 其他错误 */
} fs_error_stats_t;

/* 完整的文件系统统计 */
typedef struct fs_stats {
    fs_io_stats_t     io;
    fs_op_stats_t     ops;
    fs_error_stats_t  errors;
    uint64_t          total_files;     /* 当前打开文件数 */
    uint64_t          max_files;       /* 最大同时打开文件数 */
    uint64_t          uptime_ticks;    /* 运行时间（滴答数）*/
} fs_stats_t;

/* ---- API ---- */

/* 初始化统计子系统 */
void fs_stat_init(void);

/* 获取统计信息 */
void fs_stat_get(fs_stats_t *stats);

/* 重置统计信息 */
void fs_stat_reset(void);

/* ---- 操作计数函数 ---- */

/* 记录读操作 */
void fs_stat_read(uint32_t bytes, int error);

/* 记录写操作 */
void fs_stat_write(uint32_t bytes, int error);

/* 记录 open */
void fs_stat_open(void);

/* 记录 close */
void fs_stat_close(void);

/* 记录 create */
void fs_stat_create(void);

/* 记录 delete */
void fs_stat_delete(void);

/* 记录 mkdir */
void fs_stat_mkdir(void);

/* 记录 rename */
void fs_stat_rename(void);

/* 记录 lookup */
void fs_stat_lookup(int hit);

/* 记录错误 */
void fs_stat_error(int errno_val);

/* 记录 mount/umount */
void fs_stat_mount(void);
void fs_stat_umount(void);

/* 获取缓存命中率（百分比） */
uint32_t fs_stat_cache_hit_rate(void);

#endif /* FS_STAT_H */
