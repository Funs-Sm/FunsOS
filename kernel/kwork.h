#ifndef KWORK_H
#define KWORK_H

#include "stdint.h"
#include "stddef.h"
#include "sync.h"

/* ============================================================
 * Kernel Workqueue Subsystem - 内核工作队列子系统
 *
 * 提供延迟执行机制：允许中断处理程序或内核代码把函数
 * 推迟到 worker 线程上下文执行。
 *
 * 特点：
 *   - 默认 system_wq 工作队列，启动时由 kwork_init() 创建
 *   - 支持立即执行与延迟执行（delay_ms）
 *   - 支持周期性工作（period_ms）
 *   - worker 线程阻塞在信号量上，有工作到达时立即唤醒
 *   - 延迟到时的工作由 worker 线程轮询（最小粒度 1ms）
 *   - 统计信息可查询
 *
 * 典型用法：
 *   kwork_init();                                    // 启动时
 *   static kwork_t work;
 *   kwork_init_work(&work, my_handler, ctx);
 *   kwork_queue_work(&work);                          // 立即排队
 *   kwork_queue_delayed_work(&work, 100);             // 100ms 后执行
 *
 *   // 周期性工作（每 50ms 运行一次）
 *   kwork_queue_periodic_work(&work, 50);
 *
 *   kwork_cancel_work(&work);                         // 取消排队
 * ============================================================ */

/* 工作项标志位 */
#define KWORK_FLAG_NONE       0x00u
#define KWORK_FLAG_DELAYED    0x01u   /* 延迟执行 */
#define KWORK_FLAG_PERIODIC   0x02u   /* 周期性 */
#define KWORK_FLAG_CANCELLED  0x04u   /* 已取消 */
#define KWORK_FLAG_RUNNING    0x08u   /* 正在执行 */
#define KWORK_FLAG_STATIC     0x10u   /* 静态分配（不可 kfree） */

/* 工作回调函数类型 */
typedef void (*kwork_func_t)(void *data);

/* 工作项 */
typedef struct kwork {
    kwork_func_t func;          /* 回调函数 */
    void        *data;          /* 用户数据 */
    uint32_t     flags;         /* 标志位 */
    uint64_t     schedule_tick; /* 计划执行时刻（ticks） */
    uint32_t     period_ms;     /* 周期（毫秒，仅 KWORK_FLAG_PERIODIC） */
    uint32_t     id;            /* 工作项 ID（用于追踪） */
    struct kwork *next;          /* 链表指针 */
} kwork_t;

/* 工作队列 */
typedef struct workqueue {
    char        name[32];        /* 队列名称 */
    kwork_t    *immediate_head;  /* 立即执行队列（FIFO） */
    kwork_t    *immediate_tail;
    kwork_t    *delayed_head;    /* 延迟/周期工作链表（按 schedule_tick 升序） */
    uint32_t    immediate_count; /* 立即队列中的项数 */
    uint32_t    delayed_count;   /* 延迟队列中的项数 */
    uint32_t    max_depth;       /* 历史最大深度 */
    spinlock_t  lock;            /* 保护链表的自旋锁 */
    sem_t       sem;             /* worker 唤醒信号量 */
    volatile int shutdown;       /* 关闭标志 */
    volatile int worker_running; /* worker 是否在运行 */
    pcb_t      *worker;          /* worker 线程的 PCB */
    /* 统计 */
    uint64_t    queued;          /* 总排队次数 */
    uint64_t    executed;        /* 总执行次数 */
    uint64_t    errors;          /* 执行错误次数（回调返回或异常） */
    uint64_t    cancelled;       /* 取消次数 */
    uint64_t    max_queue_depth; /* 历史最大队列深度（含延迟） */
} workqueue_t;

/* 工作队列统计（聚合） */
typedef struct kwork_stats {
    uint64_t queued;
    uint64_t executed;
    uint64_t errors;
    uint64_t cancelled;
    uint64_t max_queue_depth;
    uint32_t immediate_pending;
    uint32_t delayed_pending;
    uint32_t worker_alive;
} kwork_stats_t;

/* ---- 初始化 ---- */
void kwork_init(void);                 /* 初始化默认工作队列 */
workqueue_t *kwork_create_wq(const char *name);  /* 创建自定义工作队列 */
void kwork_destroy_wq(workqueue_t *wq);

/* ---- 工作项初始化 ---- */
void kwork_init_work(kwork_t *work, kwork_func_t func, void *data);

/* ---- 排队（默认工作队列） ---- */
int kwork_queue_work(kwork_t *work);
int kwork_queue_delayed_work(kwork_t *work, uint32_t delay_ms);
int kwork_queue_periodic_work(kwork_t *work, uint32_t period_ms);

/* ---- 排队（自定义工作队列） ---- */
int kwork_queue(workqueue_t *wq, kwork_t *work);
int kwork_queue_delayed(workqueue_t *wq, kwork_t *work, uint32_t delay_ms);
int kwork_queue_periodic(workqueue_t *wq, kwork_t *work, uint32_t period_ms);

/* ---- 取消 ---- */
int kwork_cancel_work(kwork_t *work);
int kwork_cancel(workqueue_t *wq, kwork_t *work);

/* ---- 刷新：等待所有已排队工作完成 ---- */
void kwork_flush(void);
void kwork_flush_wq(workqueue_t *wq);

/* ---- 查询 ---- */
workqueue_t *kwork_get_default(void);
void kwork_get_stats(kwork_stats_t *stats);
void kwork_reset_stats(void);

/* ---- 调试 ---- */
void kwork_dump(workqueue_t *wq);

#endif /* KWORK_H */
