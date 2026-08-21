/*
 * flock.c - BSD 风格文件锁实现
 *
 * 支持 LOCK_SH (共享锁), LOCK_EX (排他锁), LOCK_UN (解锁)
 * 以及 LOCK_NB (非阻塞) 标志
 *
 * 实现策略:
 * - 使用链表维护全局锁表
 * - 共享锁可以多个进程同时持有
 * - 排他锁只能一个进程持有
 * - 锁与文件描述符关联，关闭文件自动释放锁
 */

#include "flock.h"
#include "file_desc.h"
#include "vfs.h"
#include "kernel_proc.h"
#include "sched.h"
#include "kheap.h"
#include "string.h"

#define MAX_FILE_LOCKS 256

static flock_table_t global_locks;
static spinlock_t flock_table_lock;

/* 获取当前进程的 PID */
static pid_t current_pid(void) {
    pcb_t *proc = sched_get_current();
    return proc ? proc->pid : 0;
}

/*
 * 初始化文件锁系统
 */
void flock_init(void) {
    spinlock_init(&flock_table_lock);
    global_locks.head = NULL;
    global_locks.count = 0;
}

/*
 * 查找指定文件描述符对应的锁
 */
file_lock_t *flock_find(int32_t fd) {
    spinlock_lock(&flock_table_lock);

    file_lock_t *lock = global_locks.head;
    while (lock) {
        if (lock->fd == fd) {
            spinlock_unlock(&flock_table_lock);
            return lock;
        }
        lock = lock->next;
    }

    spinlock_unlock(&flock_table_lock);
    return NULL;
}

/*
 * 为文件描述符创建或更新锁
 * 返回: 0 成功, -1 失败
 */
static int32_t flock_acquire(int32_t fd, int32_t operation) {
    int32_t lock_type = operation & ~LOCK_NB;
    int32_t nonblock = (operation & LOCK_NB) != 0;

    pcb_t *proc = sched_get_current();
    if (!proc) return -1;

    spinlock_lock(&flock_table_lock);

    /* 先查找是否已有该 fd 的锁 */
    file_lock_t **prev = &global_locks.head;
    file_lock_t *lock = global_locks.head;
    while (lock) {
        if (lock->fd == fd) {
            break;
        }
        prev = &lock->next;
        lock = lock->next;
    }

    /* 如果已存在锁 */
    if (lock) {
        pid_t my_pid = current_pid();

        /* 解锁请求 */
        if (lock_type == LOCK_UN) {
            if (lock->lock_type == LOCK_SH) {
                lock->holder_count--;
                if (lock->holder_count == 0) {
                    /* 移除锁 */
                    *prev = lock->next;
                    kfree(lock);
                    global_locks.count--;
                }
            } else if (lock->lock_type == LOCK_EX) {
                if (lock->exclusive_pid == my_pid) {
                    *prev = lock->next;
                    kfree(lock);
                    global_locks.count--;
                }
            }
            spinlock_unlock(&flock_table_lock);
            return 0;
        }

        /* 请求共享锁 */
        if (lock_type == LOCK_SH) {
            /* 已有排他锁，不能获取共享锁 */
            if (lock->lock_type == LOCK_EX && lock->exclusive_pid != my_pid) {
                spinlock_unlock(&flock_table_lock);
                return nonblock ? -1 : -1;  /* 忙或阻塞 */
            }
            /* 可以升级为共享锁或增加共享锁计数 */
            lock->holder_count++;
            lock->lock_type = LOCK_SH;
            spinlock_unlock(&flock_table_lock);
            return 0;
        }

        /* 请求排他锁 */
        if (lock_type == LOCK_EX) {
            /* 已有排他锁且是自己，直接成功 */
            if (lock->lock_type == LOCK_EX && lock->exclusive_pid == my_pid) {
                spinlock_unlock(&flock_table_lock);
                return 0;
            }
            /* 已有共享锁且有多个持有者，不能获取排他锁 */
            if (lock->lock_type == LOCK_SH && lock->holder_count > 1) {
                spinlock_unlock(&flock_table_lock);
                return nonblock ? -1 : -1;
            }
            /* 已有锁但可以升级为排他锁 */
            lock->lock_type = LOCK_EX;
            lock->exclusive_pid = my_pid;
            lock->holder_count = 1;
            spinlock_unlock(&flock_table_lock);
            return 0;
        }

        spinlock_unlock(&flock_table_lock);
        return -1;
    }

    /* 不存在锁，创建新锁 */
    if (lock_type == LOCK_UN) {
        spinlock_unlock(&flock_table_lock);
        return 0;  /* 解锁不存在的锁，视为成功 */
    }

    if (global_locks.count >= MAX_FILE_LOCKS) {
        spinlock_unlock(&flock_table_lock);
        return -1;  /* 锁表满 */
    }

    lock = (file_lock_t *)kmalloc(sizeof(file_lock_t));
    if (!lock) {
        spinlock_unlock(&flock_table_lock);
        return -1;
    }

    memset(lock, 0, sizeof(file_lock_t));
    lock->fd = fd;
    lock->lock_type = lock_type;
    lock->exclusive_pid = (lock_type == LOCK_EX) ? current_pid() : 0;
    lock->holder_count = 1;
    lock->ref_count = 1;
    lock->next = global_locks.head;

    global_locks.head = lock;
    global_locks.count++;

    spinlock_unlock(&flock_table_lock);
    return 0;
}

/*
 * 文件锁系统调用
 *
 * 参数:
 *   fd       - 文件描述符
 *   operation - 操作类型 (LOCK_SH | LOCK_EX | LOCK_UN) 加上可选的 LOCK_NB
 *
 * 返回:
 *   0  成功
 *  -1  失败 (EWOULDBLOCK 如果是非阻塞且无法获取锁)
 */
int32_t flock_syscall(int32_t fd, int32_t operation) {
    return flock_acquire(fd, operation);
}

/*
 * 当文件描述符关闭时，释放所有关联的锁
 * 由 fd_close 调用
 */
int32_t flock_release_fd(int32_t fd) {
    spinlock_lock(&flock_table_lock);

    file_lock_t **prev = &global_locks.head;
    file_lock_t *lock = global_locks.head;
    pid_t my_pid = current_pid();

    while (lock) {
        if (lock->fd == fd) {
            /* 检查是否是当前进程持有的锁 */
            if (lock->lock_type == LOCK_EX && lock->exclusive_pid != my_pid) {
                /* 不是自己持有的排他锁，跳过 */
                prev = &lock->next;
                lock = lock->next;
                continue;
            }

            /* 释放锁 */
            if (lock->lock_type == LOCK_SH) {
                lock->holder_count--;
                if (lock->holder_count == 0) {
                    *prev = lock->next;
                    kfree(lock);
                    global_locks.count--;
                    lock = *prev;
                    continue;
                }
            } else {
                *prev = lock->next;
                kfree(lock);
                global_locks.count--;
                lock = *prev;
                continue;
            }
        }
        prev = &lock->next;
        lock = lock->next;
    }

    spinlock_unlock(&flock_table_lock);
    return 0;
}

/*
 * 获取当前持有锁的数量 (调试用)
 */
uint32_t flock_count(void) {
    spinlock_lock(&flock_table_lock);
    uint32_t count = global_locks.count;
    spinlock_unlock(&flock_table_lock);
    return count;
}
