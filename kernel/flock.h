#ifndef FLock_H
#define FLock_H

#include "stdint.h"

/* ============================================================
 * File Locking Subsystem - 文件锁子系统
 *
 * 支持：
 * - 共享读锁 (F_RDLCK) 和独占写锁 (F_WRLCK)
 * - 解锁 (F_UNLCK)
 * - 建议性锁 (Advisory Lock)
 * - 按进程/文件/字节范围管理
 * - 阻塞和非阻塞模式
 * ============================================================ */

/* 锁类型 */
#define F_UNLCK  0  /* 解锁 */
#define F_RDLCK  1  /* 共享读锁 */
#define F_WRLCK  2  /* 独占写锁 */

/* 锁命令 */
#define F_GETLK  5   /* 获取锁信息 */
#define F_SETLK  6   /* 设置锁（非阻塞） */
#define F_SETLKW 7   /* 设置锁（阻塞等待） */

/* whence 值 */
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

/* 锁结构（类似 POSIX flock / struct flock） */
typedef struct file_lock {
    uint16_t l_type;     /* 锁类型: F_RDLCK, F_WRLCK, F_UNLCK */
    uint16_t l_whence;   /* 起始位置: SEEK_SET, SEEK_CUR, SEEK_END */
    uint32_t l_start;    /* 起始偏移 */
    uint32_t l_len;      /* 长度（0 = 到文件末尾） */
    uint32_t l_pid;      /* 持有锁的进程 PID */
} file_lock_t;

/* 内部锁记录 */
typedef struct lock_record {
    uint32_t inode;       /* inode 编号 */
    uint32_t dev;         /* 设备号 */
    uint16_t type;        /* 锁类型 */
    uint32_t start;       /* 起始偏移 */
    uint32_t end;         /* 结束偏移（包含）*/
    uint32_t pid;         /* 进程 PID */
    struct lock_record *next;
    struct lock_record *prev;
} lock_record_t;

/* 锁统计 */
typedef struct lock_stats {
    uint32_t total_locks;      /* 当前锁总数 */
    uint32_t read_locks;       /* 读锁数量 */
    uint32_t write_locks;      /* 写锁数量 */
    uint32_t max_locks;        /* 历史最大锁数 */
    uint64_t lock_acquires;    /* 成功获取锁次数 */
    uint64_t lock_releases;    /* 释放锁次数 */
    uint64_t lock_waits;       /* 等待锁次数 */
    uint64_t lock_conflicts;   /* 锁冲突次数 */
} lock_stats_t;

/* ---- 初始化 ---- */
void flock_init(void);

/* ---- 核心 API ---- */

/* 获取锁信息（检查是否有冲突锁）
 * 返回值：
 *   0 = 没有冲突锁（可获取）
 *   1 = 有冲突锁（flock 结构被填充为冲突锁信息）
 *   <0 = 错误 */
int flock_get(uint32_t dev, uint32_t inode, file_lock_t *flock);

/* 设置锁（非阻塞）
 * 返回值：
 *   0 = 成功
 *  -1 = 失败（锁冲突）
 *   < -1 = 其他错误 */
int flock_set(uint32_t dev, uint32_t inode, const file_lock_t *flock, uint32_t pid);

/* 设置锁（阻塞等待）
 * 返回值：
 *   0 = 成功
 *   <0 = 错误 */
int flock_set_wait(uint32_t dev, uint32_t inode, const file_lock_t *flock, uint32_t pid);

/* 释放指定进程在指定 inode 上的所有锁 */
int flock_release_all(uint32_t dev, uint32_t inode, uint32_t pid);

/* 释放指定进程的所有锁（进程退出时调用） */
void flock_release_pid(uint32_t pid);

/* ---- 统计与调试 ---- */

/* 获取锁统计 */
void flock_get_stats(lock_stats_t *stats);

/* 重置统计 */
void flock_reset_stats(void);

/* 列出指定 inode 上的所有锁（调试用）
 * 返回锁的数量 */
int flock_list(uint32_t dev, uint32_t inode, file_lock_t *locks, int max_locks);

#endif /* FLock_H */
