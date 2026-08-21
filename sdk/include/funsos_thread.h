#ifndef FUNSOS_THREAD_H
#define FUNSOS_THREAD_H

/*
 * FUNSOS 线程管理 API
 * 提供线程创建、同步、互斥、条件变量等功能。
 * 基于 kernel/thread.h 和 kernel/pthread.h 的系统调用封装。
 */

#include "stdint.h"

/* ---- 线程相关常量 ---- */
#define FUNSOS_THREAD_MIN_STACK    16384     /* 最小栈大小 (16KB) */
#define FUNSOS_THREAD_STACK_DEFAULT 65536    /* 默认栈大小 (64KB) */

/* 线程优先级范围 */
#define FUNSOS_THREAD_PRIO_MIN     1
#define FUNSOS_THREAD_PRIO_MAX     99
#define FUNSOS_THREAD_PRIO_DEFAULT 50

/* 线程状态 */
#define FUNSOS_THREAD_RUNNING      0
#define FUNSOS_THREAD_READY        1
#define FUNSOS_THREAD_SLEEPING     2
#define FUNSOS_THREAD_WAITING      3
#define FUNSOS_THREAD_STOPPED      4
#define FUNSOS_THREAD_ZOMBIE       5
#define FUNSOS_THREAD_DEAD         6

/* 线程不透明类型 */
typedef struct funsos_thread funsos_thread_t;
typedef uint32_t funsos_tid_t;

/* 线程函数类型 */
typedef void *(*funsos_thread_func_t)(void *arg);

/* ---- 互斥锁 ---- */
#define FUNSOS_MUTEX_NORMAL       0   /* 普通互斥锁 */
#define FUNSOS_MUTEX_RECURSIVE    1   /* 递归互斥锁 */
#define FUNSOS_MUTEX_ERRORCHECK   2   /* 错误检查互斥锁 */
#define FUNSOS_MUTEX_DEFAULT      FUNSOS_MUTEX_NORMAL

typedef struct {
    uint32_t lock;        /* 锁状态 */
    uint32_t owner;       /* 所有者线程ID */
    int      type;        /* 锁类型 */
    int      count;       /* 递归计数 */
} funsos_mutex_t;

/* 互斥锁静态初始化器 */
#define FUNSOS_MUTEX_INITIALIZER  {0, 0, FUNSOS_MUTEX_DEFAULT, 0}

/* ---- 条件变量 ---- */
typedef struct {
    uint32_t waiters;     /* 等待线程数 */
    uint32_t signal;      /* 信号标志 */
    void    *queue;       /* 等待队列指针 */
} funsos_cond_t;

/* 条件变量静态初始化器 */
#define FUNSOS_COND_INITIALIZER  {0, 0, NULL}

/* ---- 读写锁 ---- */
typedef struct {
    funsos_mutex_t mutex;    /* 保护互斥锁 */
    funsos_cond_t  rcond;    /* 读条件变量 */
    funsos_cond_t  wcond;    /* 写条件变量 */
    int            readers;  /* 当前读者数 */
    int            writers;  /* 当前写者数 */
    int            wr_wait;  /* 等待写者数 */
} funsos_rwlock_t;

/* 读写锁静态初始化器 */
#define FUNSOS_RWLOCK_INITIALIZER  {FUNSOS_MUTEX_INITIALIZER, FUNSOS_COND_INITIALIZER, FUNSOS_COND_INITIALIZER, 0, 0, 0}

/* ---- 信号量 ---- */
typedef struct {
    uint32_t value;    /* 信号量值 */
    void    *waiters;  /* 等待队列 */
} funsos_sem_t;

/* ---- 自旋锁 ---- */
typedef struct {
    volatile uint32_t lock;   /* 锁状态 */
} funsos_spinlock_t;

#define FUNSOS_SPINLOCK_INITIALIZER  {0}

/* ---- 屏障 ---- */
typedef struct {
    funsos_mutex_t mutex;     /* 保护互斥锁 */
    funsos_cond_t  cond;      /* 条件变量 */
    uint32_t       count;     /* 需要等待的线程数 */
    uint32_t       waiting;   /* 当前等待的线程数 */
    uint32_t       phase;     /* 相位（防止循环使用错误） */
} funsos_barrier_t;

/* ---- 线程属性 ---- */
typedef struct {
    uint32_t stack_size;    /* 栈大小 */
    int      priority;      /* 线程优先级 */
    int      detached;      /* 是否分离状态 */
    void    *stack_addr;    /* 栈地址 */
} funsos_thread_attr_t;

/* ---- 线程创建和管理 ---- */

/*
 * 创建线程
 * 参数: thread - 接收线程指针; attr - 线程属性 (NULL=默认); func - 线程函数; arg - 函数参数
 * 返回: 0 成功, -1 失败
 */
int funsos_thread_create(funsos_thread_t **thread, 
                         const funsos_thread_attr_t *attr,
                         funsos_thread_func_t func, void *arg);

/*
 * 等待线程结束
 * 参数: thread - 线程指针; retval - 接收线程返回值的指针
 * 返回: 0 成功, -1 失败
 */
int funsos_thread_join(funsos_thread_t *thread, void **retval);

/*
 * 分离线程（线程结束后自动释放资源）
 * 参数: thread - 线程指针
 * 返回: 0 成功, -1 失败
 */
int funsos_thread_detach(funsos_thread_t *thread);

/*
 * 获取当前线程ID
 * 返回: 线程ID
 */
funsos_tid_t funsos_thread_self(void);

/*
 * 退出当前线程
 * 参数: retval - 返回值
 */
void funsos_thread_exit(void *retval);

/*
 * 让出CPU
 */
void funsos_thread_yield(void);

/*
 * 取消线程
 * 参数: thread - 线程指针
 * 返回: 0 成功, -1 失败
 */
int funsos_thread_cancel(funsos_thread_t *thread);

/*
 * 设置线程优先级
 * 参数: thread - 线程指针; priority - 新优先级
 * 返回: 0 成功, -1 失败
 */
int funsos_thread_setprio(funsos_thread_t *thread, int priority);

/*
 * 获取线程优先级
 * 参数: thread - 线程指针; priority - 接收优先级
 * 返回: 0 成功, -1 失败
 */
int funsos_thread_getprio(funsos_thread_t *thread, int *priority);

/*
 * 设置线程名称
 * 参数: thread - 线程指针; name - 线程名称
 * 返回: 0 成功, -1 失败
 */
int funsos_thread_setname(funsos_thread_t *thread, const char *name);

/*
 * 获取线程名称
 * 参数: thread - 线程指针; name - 接收名称的缓冲区; len - 缓冲区大小
 * 返回: 0 成功, -1 失败
 */
int funsos_thread_getname(funsos_thread_t *thread, char *name, uint32_t len);

/* ---- 互斥锁操作 ---- */

/*
 * 初始化互斥锁
 * 参数: mutex - 互斥锁指针; type - 锁类型
 * 返回: 0 成功, -1 失败
 */
int funsos_mutex_init(funsos_mutex_t *mutex, int type);

/*
 * 销毁互斥锁
 * 参数: mutex - 互斥锁指针
 * 返回: 0 成功, -1 失败
 */
int funsos_mutex_destroy(funsos_mutex_t *mutex);

/*
 * 加锁
 * 参数: mutex - 互斥锁指针
 * 返回: 0 成功, -1 失败
 */
int funsos_mutex_lock(funsos_mutex_t *mutex);

/*
 * 尝试加锁
 * 参数: mutex - 互斥锁指针
 * 返回: 0 成功, -1 失败 (锁已被持有)
 */
int funsos_mutex_trylock(funsos_mutex_t *mutex);

/*
 * 解锁
 * 参数: mutex - 互斥锁指针
 * 返回: 0 成功, -1 失败
 */
int funsos_mutex_unlock(funsos_mutex_t *mutex);

/* ---- 条件变量操作 ---- */

/*
 * 初始化条件变量
 * 参数: cond - 条件变量指针
 * 返回: 0 成功, -1 失败
 */
int funsos_cond_init(funsos_cond_t *cond);

/*
 * 销毁条件变量
 * 参数: cond - 条件变量指针
 * 返回: 0 成功, -1 失败
 */
int funsos_cond_destroy(funsos_cond_t *cond);

/*
 * 等待条件变量
 * 参数: cond - 条件变量指针; mutex - 关联的互斥锁
 * 返回: 0 成功, -1 失败
 */
int funsos_cond_wait(funsos_cond_t *cond, funsos_mutex_t *mutex);

/*
 * 超时等待条件变量
 * 参数: cond - 条件变量指针; mutex - 关联的互斥锁; timeout_ms - 超时时间(毫秒)
 * 返回: 0 成功, -1 超时或失败
 */
int funsos_cond_timedwait(funsos_cond_t *cond, funsos_mutex_t *mutex, uint32_t timeout_ms);

/*
 * 唤醒一个等待线程
 * 参数: cond - 条件变量指针
 * 返回: 0 成功, -1 失败
 */
int funsos_cond_signal(funsos_cond_t *cond);

/*
 * 唤醒所有等待线程
 * 参数: cond - 条件变量指针
 * 返回: 0 成功, -1 失败
 */
int funsos_cond_broadcast(funsos_cond_t *cond);

/* ---- 读写锁操作 ---- */

/*
 * 初始化读写锁
 * 参数: rwlock - 读写锁指针
 * 返回: 0 成功, -1 失败
 */
int funsos_rwlock_init(funsos_rwlock_t *rwlock);

/*
 * 销毁读写锁
 * 参数: rwlock - 读写锁指针
 * 返回: 0 成功, -1 失败
 */
int funsos_rwlock_destroy(funsos_rwlock_t *rwlock);

/*
 * 获取读锁
 * 参数: rwlock - 读写锁指针
 * 返回: 0 成功, -1 失败
 */
int funsos_rwlock_rdlock(funsos_rwlock_t *rwlock);

/*
 * 获取写锁
 * 参数: rwlock - 读写锁指针
 * 返回: 0 成功, -1 失败
 */
int funsos_rwlock_wrlock(funsos_rwlock_t *rwlock);

/*
 * 尝试获取读锁
 * 参数: rwlock - 读写锁指针
 * 返回: 0 成功, -1 失败
 */
int funsos_rwlock_tryrdlock(funsos_rwlock_t *rwlock);

/*
 * 尝试获取写锁
 * 参数: rwlock - 读写锁指针
 * 返回: 0 成功, -1 失败
 */
int funsos_rwlock_trywrlock(funsos_rwlock_t *rwlock);

/*
 * 释放读写锁
 * 参数: rwlock - 读写锁指针
 * 返回: 0 成功, -1 失败
 */
int funsos_rwlock_unlock(funsos_rwlock_t *rwlock);

/* ---- 信号量操作 ---- */

/*
 * 初始化信号量
 * 参数: sem - 信号量指针; pshared - 是否进程间共享; value - 初始值
 * 返回: 0 成功, -1 失败
 */
int funsos_sem_init(funsos_sem_t *sem, int pshared, uint32_t value);

/*
 * 销毁信号量
 * 参数: sem - 信号量指针
 * 返回: 0 成功, -1 失败
 */
int funsos_sem_destroy(funsos_sem_t *sem);

/*
 * 信号量等待 (P操作)
 * 参数: sem - 信号量指针
 * 返回: 0 成功, -1 失败
 */
int funsos_sem_wait(funsos_sem_t *sem);

/*
 * 信号量尝试等待
 * 参数: sem - 信号量指针
 * 返回: 0 成功, -1 失败
 */
int funsos_sem_trywait(funsos_sem_t *sem);

/*
 * 信号量发布 (V操作)
 * 参数: sem - 信号量指针
 * 返回: 0 成功, -1 失败
 */
int funsos_sem_post(funsos_sem_t *sem);

/*
 * 获取信号量当前值
 * 参数: sem - 信号量指针; sval - 接收当前值
 * 返回: 0 成功, -1 失败
 */
int funsos_sem_getvalue(funsos_sem_t *sem, int *sval);

/* ---- 自旋锁操作 ---- */

/*
 * 初始化自旋锁
 * 参数: lock - 自旋锁指针
 * 返回: 0 成功, -1 失败
 */
int funsos_spin_init(funsos_spinlock_t *lock);

/*
 * 自旋锁加锁
 * 参数: lock - 自旋锁指针
 */
void funsos_spin_lock(funsos_spinlock_t *lock);

/*
 * 自旋锁尝试加锁
 * 参数: lock - 自旋锁指针
 * 返回: 0 成功, -1 失败
 */
int funsos_spin_trylock(funsos_spinlock_t *lock);

/*
 * 自旋锁解锁
 * 参数: lock - 自旋锁指针
 */
void funsos_spin_unlock(funsos_spinlock_t *lock);

/* ---- 屏障操作 ---- */

/*
 * 初始化屏障
 * 参数: barrier - 屏障指针; count - 等待线程数
 * 返回: 0 成功, -1 失败
 */
int funsos_barrier_init(funsos_barrier_t *barrier, uint32_t count);

/*
 * 销毁屏障
 * 参数: barrier - 屏障指针
 * 返回: 0 成功, -1 失败
 */
int funsos_barrier_destroy(funsos_barrier_t *barrier);

/*
 * 屏障等待
 * 参数: barrier - 屏障指针
 * 返回: 0 成功, -1 失败
 */
int funsos_barrier_wait(funsos_barrier_t *barrier);

#endif /* FUNSOS_THREAD_H */
