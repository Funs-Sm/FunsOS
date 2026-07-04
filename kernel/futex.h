#ifndef FUTEX_H
#define FUTEX_H

#include "stdint.h"
#include "kernel_types.h"
#include "sync.h"
#include "spinlock.h"

/* ============================================================
 * Futex (Fast Userspace Mutex) 子系统
 *
 * 提供用户态和内核态协作的快速同步原语。
 * 用户态在无竞争时直接操作，竞争时陷入内核等待。
 * ============================================================ */

/* Futex 操作类型 */
#define FUTEX_WAIT        0    /* 等待 futex 值等于 val */
#define FUTEX_WAKE        1    /* 唤醒最多 val 个等待者 */
#define FUTEX_FD          2    /* 为 futex 创建文件描述符 */
#define FUTEX_REQUEUE     3    /* 重新排队等待者到另一个 futex */
#define FUTEX_CMP_REQUEUE 4    /* 比较后重新排队 */
#define FUTEX_WAKE_OP     5    /* 唤醒 + 原子操作 */
#define FUTEX_LOCK_PI     6    /* PI 锁获取 */
#define FUTEX_UNLOCK_PI   7    /* PI 锁释放 */
#define FUTEX_TRYLOCK_PI  8    /* PI 锁尝试获取 */
#define FUTEX_WAIT_BITSET 9    /* 带位集的等待 */
#define FUTEX_WAKE_BITSET 10   /* 带位集的唤醒 */

/* Futex 标志位 */
#define FUTEX_PRIVATE_FLAG   128  /* 进程私有 futex */
#define FUTEX_CLOCK_REALTIME 256  /* 使用 CLOCK_REALTIME */

/* 最大 futex 哈希桶数 */
#define FUTEX_HASH_SIZE  256

/* 等待者状态 */
typedef enum {
    FUTEX_WAITING = 0,
    FUTEX_WOKEN,
    FUTEX_REQUEUED,
    FUTEX_TIMEDOUT,
    FUTEX_RETRY
} futex_state_t;

/* Futex 等待队列节点 */
typedef struct futex_q {
    uint32_t       *uaddr;       /* 用户态地址 */
    pid_t           pid;         /* 等待进程 PID */
    uint32_t        bitset;      /* 等待位集 */
    futex_state_t   state;       /* 等待状态 */
    struct futex_q *next;        /* 链表下一个 */
    struct futex_q *prev;        /* 链表上一个 */
    void           *task;        /* 任务指针 */
} futex_q_t;

/* Futex 哈希桶 */
typedef struct {
    futex_q_t *head;
    futex_q_t *tail;
    uint32_t   count;
    spinlock_t lock;
} futex_bucket_t;

/* Futex 全局状态 */
typedef struct {
    futex_bucket_t buckets[FUTEX_HASH_SIZE];
    uint32_t       total_waiters;
    uint64_t       wake_count;
    uint64_t       wait_count;
    uint64_t       requeue_count;
} futex_global_state_t;

/* ============================================================
 * 初始化
 * ============================================================ */
void futex_init(void);

/* ============================================================
 * 核心操作
 * ============================================================ */

/*
 * futex_wait - 等待 futex 值等于 val
 * uaddr: 用户态 futex 变量地址
 * val:   期望值
 * timeout: 超时时间(ms), 0 表示无限等待
 * 返回: 0 成功, 负数错误码
 */
int futex_wait(uint32_t *uaddr, uint32_t val, uint32_t timeout_ms);

/*
 * futex_wake - 唤醒等待者
 * uaddr: 用户态 futex 变量地址
 * count: 最多唤醒数量
 * 返回: 实际唤醒数量, 负数错误码
 */
int futex_wake(uint32_t *uaddr, int count);

/*
 * futex_requeue - 重新排队
 * uaddr: 源 futex 地址
 * val:  唤醒数量
 * val2: 重新排队数量
 * uaddr2: 目标 futex 地址
 * 返回: 总操作数, 负数错误码
 */
int futex_requeue(uint32_t *uaddr, int val, int val2, uint32_t *uaddr2);

/*
 * futex_cmp_requeue - 比较后重新排队
 * uaddr: 源 futex 地址
 * val:  唤醒数量
 * val2: 重新排队数量
 * uaddr2: 目标 futex 地址
 * val3: 比较值
 * 返回: 总操作数, 负数错误码
 */
int futex_cmp_requeue(uint32_t *uaddr, int val, int val2,
                      uint32_t *uaddr2, uint32_t val3);

/*
 * futex_wait_bitset - 带位集等待
 */
int futex_wait_bitset(uint32_t *uaddr, uint32_t val,
                      uint32_t timeout_ms, uint32_t bitset);

/*
 * futex_wake_bitset - 带位集唤醒
 */
int futex_wake_bitset(uint32_t *uaddr, int count, uint32_t bitset);

/* ============================================================
 * 系统调用接口
 * ============================================================ */
int sys_futex(uint32_t *uaddr, int op, uint32_t val,
              uint32_t timeout, uint32_t *uaddr2, uint32_t val3);

/* ============================================================
 * 统计与调试
 * ============================================================ */
void futex_get_stats(uint64_t *wait_count, uint64_t *wake_count,
                     uint64_t *requeue_count, uint32_t *total_waiters);
void futex_dump(void);

#endif /* FUTEX_H */
