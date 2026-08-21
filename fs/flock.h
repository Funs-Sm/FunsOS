#ifndef FLOCK_H
#define FLOCK_H

#include "stdint.h"
#include "sync.h"

/*
 * 文件锁 (flock) 实现
 * 支持 BSD 风格的文件锁: LOCK_SH (共享锁), LOCK_EX (排他锁)
 * 以及 LOCK_NB (非阻塞) 标志
 */

/* 锁类型 */
#define LOCK_SH   1  /* 共享锁 - 允许多个进程同时持有 */
#define LOCK_EX   2  /* 排他锁 - 同时只能有一个进程持有 */
#define LOCK_UN   3  /* 解锁 */

/* 锁标志 */
#define LOCK_NB   4  /* 非阻塞 - 如果无法立即获取锁则立即返回 */

/* 锁状态 */
#define LOCK_AVAILABLE  0
#define LOCK_SHARED     1
#define LOCK_EXCLUSIVE   2

typedef struct file_lock {
    char          path[256];     /* 被锁定的文件路径 */
    int32_t       fd;            /* 文件描述符 */
    uint32_t      lock_type;     /* LOCK_SH 或 LOCK_EX */
    uint32_t      holder_count;  /* 共享锁持有者数量 */
    pid_t         exclusive_pid; /* 排他锁持有者 PID */
    uint32_t      ref_count;     /* 引用计数 */
    struct file_lock *next;      /* 链表 next */
} file_lock_t;

/* 全局锁表 */
typedef struct {
    file_lock_t   *head;         /* 锁链表头 */
    uint32_t      count;         /* 当前锁数量 */
    spinlock_t    lock;          /* 保护锁表的 spinlock */
} flock_table_t;

void flock_init(void);
int32_t flock_syscall(int32_t fd, int32_t operation);
file_lock_t *flock_find(int32_t fd);
int32_t flock_release_fd(int32_t fd);

#endif /* FLOCK_H */
