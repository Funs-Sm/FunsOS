/*
 * FUNSOS 线程管理 API 实现
 * ========================
 * 封装内核线程系统调用，提供线程创建、同步、互斥等功能。
 *
 */

#include "funsos.h"
#include "funsos_thread.h"
#include "stddef.h"
#include "string.h"

/* ---- 系统调用号 ---- */
#define SYS_THREAD_CREATE   300
#define SYS_THREAD_EXIT     301
#define SYS_THREAD_JOIN     302
#define SYS_THREAD_SELF     303
#define SYS_THREAD_YIELD    11
#define SYS_THREAD_CANCEL   304

/* 系统调用包装 */
static inline int syscall0(int num) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num)
        : "memory"
    );
    return ret;
}

static inline int syscall1(int num, int a1) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1)
        : "memory"
    );
    return ret;
}

static inline int syscall2(int num, int a1, int a2) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2)
        : "memory"
    );
    return ret;
}

static inline int syscall3(int num, int a1, int a2, int a3) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3)
        : "memory"
    );
    return ret;
}

/* 线程内部结构 */
struct funsos_thread {
    uint32_t tid;           /* 线程 ID */
    void    *stack;         /* 栈指针 */
    uint32_t stack_size;    /* 栈大小 */
    int      detached;      /* 是否分离 */
    void    *retval;        /* 返回值 */
    int      exited;        /* 是否已退出 */
};

/* ---- 线程创建和管理 ---- */

/*
 * 创建线程
 */
int funsos_thread_create(funsos_thread_t **thread,
                         const funsos_thread_attr_t *attr,
                         funsos_thread_func_t func, void *arg)
{
    if (thread == NULL || func == NULL) return -1;

    struct funsos_thread *t = (struct funsos_thread *)funsos_alloc(sizeof(struct funsos_thread));
    if (t == NULL) return -1;

    uint32_t stack_size = FUNSOS_THREAD_STACK_DEFAULT;
    if (attr && attr->stack_size > 0) {
        stack_size = attr->stack_size;
    }

    t->stack = funsos_alloc(stack_size);
    if (t->stack == NULL) {
        funsos_free(t);
        return -1;
    }

    t->stack_size = stack_size;
    t->detached = attr ? attr->detached : 0;
    t->exited = 0;
    t->retval = NULL;

    /* 调用系统调用来创建线程 */
    int tid = syscall3(SYS_THREAD_CREATE, (int)func, (int)arg, (int)t);
    if (tid < 0) {
        funsos_free(t->stack);
        funsos_free(t);
        return -1;
    }

    t->tid = (uint32_t)tid;
    *thread = t;
    return 0;
}

/*
 * 等待线程结束
 */
int funsos_thread_join(funsos_thread_t *thread, void **retval)
{
    if (thread == NULL) return -1;

    int ret = syscall2(SYS_THREAD_JOIN, (int)thread->tid, (int)retval);
    if (ret == 0 && retval) {
        *retval = thread->retval;
    }

    funsos_free(thread->stack);
    funsos_free(thread);
    return ret;
}

/*
 * 分离线程
 */
int funsos_thread_detach(funsos_thread_t *thread)
{
    if (thread == NULL) return -1;
    thread->detached = 1;
    return 0;
}

/*
 * 获取当前线程ID
 */
funsos_tid_t funsos_thread_self(void)
{
    return (funsos_tid_t)syscall0(SYS_THREAD_SELF);
}

/*
 * 退出当前线程
 */
void funsos_thread_exit(void *retval)
{
    syscall1(SYS_THREAD_EXIT, (int)retval);
}

/*
 * 让出CPU
 */
void funsos_thread_yield(void)
{
    funsos_yield();
}

/*
 * 取消线程
 */
int funsos_thread_cancel(funsos_thread_t *thread)
{
    if (thread == NULL) return -1;
    return syscall1(SYS_THREAD_CANCEL, (int)thread->tid);
}

/*
 * 设置线程优先级
 */
int funsos_thread_setprio(funsos_thread_t *thread, int priority)
{
    (void)thread; (void)priority;
    return 0;
}

/*
 * 获取线程优先级
 */
int funsos_thread_getprio(funsos_thread_t *thread, int *priority)
{
    if (thread == NULL || priority == NULL) return -1;
    *priority = FUNSOS_THREAD_PRIO_DEFAULT;
    return 0;
}

/*
 * 设置线程名称
 */
int funsos_thread_setname(funsos_thread_t *thread, const char *name)
{
    (void)thread; (void)name;
    return 0;
}

/*
 * 获取线程名称
 */
int funsos_thread_getname(funsos_thread_t *thread, char *name, uint32_t len)
{
    if (thread == NULL || name == NULL || len == 0) return -1;
    if (len > 0) name[0] = '\0';
    return 0;
}

/* ---- 互斥锁操作 ---- */

/*
 * 初始化互斥锁
 */
int funsos_mutex_init(funsos_mutex_t *mutex, int type)
{
    if (mutex == NULL) return -1;
    mutex->lock = 0;
    mutex->owner = 0;
    mutex->type = type;
    mutex->count = 0;
    return 0;
}

/*
 * 销毁互斥锁
 */
int funsos_mutex_destroy(funsos_mutex_t *mutex)
{
    if (mutex == NULL) return -1;
    mutex->lock = 0;
    return 0;
}

/* 原子交换（用户态实现） */
static inline int atomic_xchg(volatile uint32_t *ptr, uint32_t val)
{
    uint32_t prev;
    __asm__ volatile (
        "xchg %0, %1"
        : "=a"(prev), "=m"(*ptr)
        : "0"(val), "m"(*ptr)
        : "memory"
    );
    return (int)prev;
}

/*
 * 加锁
 */
int funsos_mutex_lock(funsos_mutex_t *mutex)
{
    if (mutex == NULL) return -1;

    if (mutex->type == FUNSOS_MUTEX_RECURSIVE) {
        if (mutex->owner == funsos_gettid()) {
            mutex->count++;
            return 0;
        }
    }

    while (atomic_xchg(&mutex->lock, 1) != 0) {
        funsos_yield();
    }

    mutex->owner = funsos_gettid();
    if (mutex->type == FUNSOS_MUTEX_RECURSIVE) {
        mutex->count = 1;
    }
    return 0;
}

/*
 * 尝试加锁
 */
int funsos_mutex_trylock(funsos_mutex_t *mutex)
{
    if (mutex == NULL) return -1;

    if (mutex->type == FUNSOS_MUTEX_RECURSIVE) {
        if (mutex->owner == funsos_gettid()) {
            mutex->count++;
            return 0;
        }
    }

    if (atomic_xchg(&mutex->lock, 1) != 0) {
        return -1;
    }

    mutex->owner = funsos_gettid();
    if (mutex->type == FUNSOS_MUTEX_RECURSIVE) {
        mutex->count = 1;
    }
    return 0;
}

/*
 * 解锁
 */
int funsos_mutex_unlock(funsos_mutex_t *mutex)
{
    if (mutex == NULL) return -1;

    if (mutex->type == FUNSOS_MUTEX_RECURSIVE) {
        mutex->count--;
        if (mutex->count > 0) return 0;
    }

    mutex->owner = 0;
    atomic_xchg(&mutex->lock, 0);
    return 0;
}

/* ---- 条件变量操作 ---- */

/*
 * 初始化条件变量
 */
int funsos_cond_init(funsos_cond_t *cond)
{
    if (cond == NULL) return -1;
    cond->waiters = 0;
    cond->signal = 0;
    cond->queue = NULL;
    return 0;
}

/*
 * 销毁条件变量
 */
int funsos_cond_destroy(funsos_cond_t *cond)
{
    if (cond == NULL) return -1;
    cond->waiters = 0;
    return 0;
}

/*
 * 等待条件变量
 */
int funsos_cond_wait(funsos_cond_t *cond, funsos_mutex_t *mutex)
{
    if (cond == NULL || mutex == NULL) return -1;

    cond->waiters++;
    funsos_mutex_unlock(mutex);

    /* 简单自旋等待 */
    while (cond->signal == 0) {
        funsos_yield();
    }
    cond->signal = 0;
    cond->waiters--;

    funsos_mutex_lock(mutex);
    return 0;
}

/*
 * 超时等待条件变量
 */
int funsos_cond_timedwait(funsos_cond_t *cond, funsos_mutex_t *mutex, uint32_t timeout_ms)
{
    if (cond == NULL || mutex == NULL) return -1;

    cond->waiters++;
    funsos_mutex_unlock(mutex);

    uint32_t start = funsos_get_ticks();
    while (cond->signal == 0) {
        if (funsos_get_ticks() - start >= timeout_ms) {
            cond->waiters--;
            funsos_mutex_lock(mutex);
            return -1;
        }
        funsos_yield();
    }
    cond->signal = 0;
    cond->waiters--;

    funsos_mutex_lock(mutex);
    return 0;
}

/*
 * 唤醒一个等待线程
 */
int funsos_cond_signal(funsos_cond_t *cond)
{
    if (cond == NULL) return -1;
    cond->signal = 1;
    return 0;
}

/*
 * 唤醒所有等待线程
 */
int funsos_cond_broadcast(funsos_cond_t *cond)
{
    if (cond == NULL) return -1;
    cond->signal = 1;
    return 0;
}

/* ---- 读写锁操作 ---- */

/*
 * 初始化读写锁
 */
int funsos_rwlock_init(funsos_rwlock_t *rwlock)
{
    if (rwlock == NULL) return -1;
    funsos_mutex_init(&rwlock->mutex, FUNSOS_MUTEX_DEFAULT);
    funsos_cond_init(&rwlock->rcond);
    funsos_cond_init(&rwlock->wcond);
    rwlock->readers = 0;
    rwlock->writers = 0;
    rwlock->wr_wait = 0;
    return 0;
}

/*
 * 销毁读写锁
 */
int funsos_rwlock_destroy(funsos_rwlock_t *rwlock)
{
    if (rwlock == NULL) return -1;
    funsos_mutex_destroy(&rwlock->mutex);
    funsos_cond_destroy(&rwlock->rcond);
    funsos_cond_destroy(&rwlock->wcond);
    return 0;
}

/*
 * 获取读锁
 */
int funsos_rwlock_rdlock(funsos_rwlock_t *rwlock)
{
    if (rwlock == NULL) return -1;

    funsos_mutex_lock(&rwlock->mutex);
    while (rwlock->writers > 0 || rwlock->wr_wait > 0) {
        funsos_cond_wait(&rwlock->rcond, &rwlock->mutex);
    }
    rwlock->readers++;
    funsos_mutex_unlock(&rwlock->mutex);
    return 0;
}

/*
 * 获取写锁
 */
int funsos_rwlock_wrlock(funsos_rwlock_t *rwlock)
{
    if (rwlock == NULL) return -1;

    funsos_mutex_lock(&rwlock->mutex);
    rwlock->wr_wait++;
    while (rwlock->readers > 0 || rwlock->writers > 0) {
        funsos_cond_wait(&rwlock->wcond, &rwlock->mutex);
    }
    rwlock->wr_wait--;
    rwlock->writers++;
    funsos_mutex_unlock(&rwlock->mutex);
    return 0;
}

/*
 * 尝试获取读锁
 */
int funsos_rwlock_tryrdlock(funsos_rwlock_t *rwlock)
{
    if (rwlock == NULL) return -1;

    funsos_mutex_lock(&rwlock->mutex);
    if (rwlock->writers > 0 || rwlock->wr_wait > 0) {
        funsos_mutex_unlock(&rwlock->mutex);
        return -1;
    }
    rwlock->readers++;
    funsos_mutex_unlock(&rwlock->mutex);
    return 0;
}

/*
 * 尝试获取写锁
 */
int funsos_rwlock_trywrlock(funsos_rwlock_t *rwlock)
{
    if (rwlock == NULL) return -1;

    funsos_mutex_lock(&rwlock->mutex);
    if (rwlock->readers > 0 || rwlock->writers > 0) {
        funsos_mutex_unlock(&rwlock->mutex);
        return -1;
    }
    rwlock->writers++;
    funsos_mutex_unlock(&rwlock->mutex);
    return 0;
}

/*
 * 释放读写锁
 */
int funsos_rwlock_unlock(funsos_rwlock_t *rwlock)
{
    if (rwlock == NULL) return -1;

    funsos_mutex_lock(&rwlock->mutex);
    if (rwlock->writers > 0) {
        rwlock->writers--;
        funsos_cond_broadcast(&rwlock->rcond);
        funsos_cond_signal(&rwlock->wcond);
    } else if (rwlock->readers > 0) {
        rwlock->readers--;
        if (rwlock->readers == 0) {
            funsos_cond_signal(&rwlock->wcond);
        }
    }
    funsos_mutex_unlock(&rwlock->mutex);
    return 0;
}

/* ---- 信号量操作 ---- */

/*
 * 初始化信号量
 */
int funsos_sem_init(funsos_sem_t *sem, int pshared, uint32_t value)
{
    (void)pshared;
    if (sem == NULL) return -1;
    sem->value = value;
    sem->waiters = NULL;
    return 0;
}

/*
 * 销毁信号量
 */
int funsos_sem_destroy(funsos_sem_t *sem)
{
    if (sem == NULL) return -1;
    return 0;
}

/*
 * 信号量等待 (P操作)
 */
int funsos_sem_wait(funsos_sem_t *sem)
{
    if (sem == NULL) return -1;

    while (1) {
        if (sem->value > 0) {
            sem->value--;
            return 0;
        }
        funsos_yield();
    }
}

/*
 * 信号量尝试等待
 */
int funsos_sem_trywait(funsos_sem_t *sem)
{
    if (sem == NULL) return -1;

    if (sem->value > 0) {
        sem->value--;
        return 0;
    }
    return -1;
}

/*
 * 信号量发布 (V操作)
 */
int funsos_sem_post(funsos_sem_t *sem)
{
    if (sem == NULL) return -1;
    sem->value++;
    return 0;
}

/*
 * 获取信号量当前值
 */
int funsos_sem_getvalue(funsos_sem_t *sem, int *sval)
{
    if (sem == NULL || sval == NULL) return -1;
    *sval = (int)sem->value;
    return 0;
}

/* ---- 自旋锁操作 ---- */

/*
 * 初始化自旋锁
 */
int funsos_spin_init(funsos_spinlock_t *lock)
{
    if (lock == NULL) return -1;
    lock->lock = 0;
    return 0;
}

/*
 * 自旋锁加锁
 */
void funsos_spin_lock(funsos_spinlock_t *lock)
{
    if (lock == NULL) return;
    while (atomic_xchg(&lock->lock, 1) != 0) {
        /* 自旋 */
    }
}

/*
 * 自旋锁尝试加锁
 */
int funsos_spin_trylock(funsos_spinlock_t *lock)
{
    if (lock == NULL) return -1;
    return atomic_xchg(&lock->lock, 1) == 0 ? 0 : -1;
}

/*
 * 自旋锁解锁
 */
void funsos_spin_unlock(funsos_spinlock_t *lock)
{
    if (lock == NULL) return;
    atomic_xchg(&lock->lock, 0);
}

/* ---- 屏障操作 ---- */

/*
 * 初始化屏障
 */
int funsos_barrier_init(funsos_barrier_t *barrier, uint32_t count)
{
    if (barrier == NULL) return -1;
    funsos_mutex_init(&barrier->mutex, FUNSOS_MUTEX_DEFAULT);
    funsos_cond_init(&barrier->cond);
    barrier->count = count;
    barrier->waiting = 0;
    barrier->phase = 0;
    return 0;
}

/*
 * 销毁屏障
 */
int funsos_barrier_destroy(funsos_barrier_t *barrier)
{
    if (barrier == NULL) return -1;
    funsos_mutex_destroy(&barrier->mutex);
    funsos_cond_destroy(&barrier->cond);
    return 0;
}

/*
 * 屏障等待
 */
int funsos_barrier_wait(funsos_barrier_t *barrier)
{
    if (barrier == NULL) return -1;

    funsos_mutex_lock(&barrier->mutex);
    uint32_t phase = barrier->phase;
    barrier->waiting++;

    if (barrier->waiting >= barrier->count) {
        barrier->waiting = 0;
        barrier->phase++;
        funsos_cond_broadcast(&barrier->cond);
        funsos_mutex_unlock(&barrier->mutex);
        return 1;
    }

    while (barrier->phase == phase) {
        funsos_cond_wait(&barrier->cond, &barrier->mutex);
    }
    funsos_mutex_unlock(&barrier->mutex);
    return 0;
}
