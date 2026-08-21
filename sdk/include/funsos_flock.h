#ifndef FUNSOS_FLOCK_H
#define FUNSOS_FLOCK_H

/*
 * FUNSOS 文件锁 API (BSD flock)
 *
 * 文件锁用于在多进程环境中协调对共享文件的访问。
 * 支持共享锁和排他锁两种模式。
 *
 * 使用示例:
 *   int fd = funsos_file_open("/tmp/lockfile.txt", FUNSOS_O_CREAT | FUNSOS_O_RDWR);
 *   funsos_flock(fd, FUNSOS_LOCK_EX);           // 获取排他锁
 *   // ... 执行文件操作 ...
 *   funsos_flock(fd, FUNSOS_LOCK_UN);           // 释放锁
 *   funsos_file_close(fd);
 */

#include "stdint.h"

/* ---- 锁类型 ---- */
#define FUNSOS_LOCK_SH   1   /* 共享锁 - 允许多个进程同时持有 */
#define FUNSOS_LOCK_EX   2   /* 排他锁 - 同时只能有一个进程持有 */
#define FUNSOS_LOCK_UN   3   /* 解锁 */

/* ---- 锁标志 ---- */
#define FUNSOS_LOCK_NB   4   /* 非阻塞 - 如果无法立即获取锁则立即返回 EWOULDBLOCK */

/* ---- 错误码 ---- */
#define FUNSOS_EWOULDBLOCK  -11  /* 操作会被阻塞 (在非阻塞模式下) */

/*
 * 文件锁系统调用
 *
 * 参数:
 *   fd        - 文件描述符 (必须已打开)
 *   operation - 锁操作:
 *                FUNSOS_LOCK_SH | FUNSOS_LOCK_NB  - 非阻塞获取共享锁
 *                FUNSOS_LOCK_EX | FUNSOS_LOCK_NB  - 非阻塞获取排他锁
 *                FUNSOS_LOCK_SH                    - 阻塞获取共享锁
 *                FUNSOS_LOCK_EX                   - 阻塞获取排他锁
 *                FUNSOS_LOCK_UN                   - 释放锁
 *
 * 返回:
 *   0          成功
 *   -1          失败 (非阻塞模式下无法获取锁时返回 -1，设置 errno 为 EAGAIN)
 *
 * 注意:
 *   - 锁与文件描述符关联，关闭文件描述符会自动释放锁
 *   - 进程退出时会自动释放该进程持有的所有锁
 *   - 共享锁可以升级为排他锁 (同一进程)
 *   - 排他锁不能降级为共享锁
 */
int funsos_flock(int fd, int operation);

#endif /* FUNSOS_FLOCK_H */
