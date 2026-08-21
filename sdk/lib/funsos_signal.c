/*
 * FUNSOS 信号处理 API 实现
 * ========================
 * 封装内核信号系统调用，提供信号注册、发送、等待等功能。
 *
 */

#include "funsos.h"
#include "funsos_signal.h"
#include "stddef.h"
#include "string.h"

/* ---- 系统调用号 ---- */
#define SYS_SIGNAL    13
#define SYS_KILL      14
#define SYS_SIGPROCMASK 310
#define SYS_SIGACTION  311
#define SYS_PAUSE     312
#define SYS_SIGSUSPEND 313
#define SYS_ALARM     314
#define SYS_SIGALTSTACK 315

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

/* ---- 信号集操作 ---- */

/*
 * 清空信号集
 */
int funsos_sigemptyset(funsos_sigset_t *set)
{
    if (set == NULL) return -1;
    set->sig[0] = 0;
    set->sig[1] = 0;
    return 0;
}

/*
 * 填充信号集（包含所有信号）
 */
int funsos_sigfillset(funsos_sigset_t *set)
{
    if (set == NULL) return -1;
    set->sig[0] = 0xFFFFFFFF;
    set->sig[1] = 0xFFFFFFFF;
    return 0;
}

/*
 * 添加信号到信号集
 */
int funsos_sigaddset(funsos_sigset_t *set, int signum)
{
    if (set == NULL || signum < 1 || signum > FUNSOS_NSIG) return -1;
    signum--;
    if (signum < 32) {
        set->sig[0] |= (1U << signum);
    } else {
        set->sig[1] |= (1U << (signum - 32));
    }
    return 0;
}

/*
 * 从信号集删除信号
 */
int funsos_sigdelset(funsos_sigset_t *set, int signum)
{
    if (set == NULL || signum < 1 || signum > FUNSOS_NSIG) return -1;
    signum--;
    if (signum < 32) {
        set->sig[0] &= ~(1U << signum);
    } else {
        set->sig[1] &= ~(1U << (signum - 32));
    }
    return 0;
}

/*
 * 检查信号是否在信号集中
 */
int funsos_sigismember(const funsos_sigset_t *set, int signum)
{
    if (set == NULL || signum < 1 || signum > FUNSOS_NSIG) return 0;
    signum--;
    if (signum < 32) {
        return (set->sig[0] & (1U << signum)) ? 1 : 0;
    } else {
        return (set->sig[1] & (1U << (signum - 32))) ? 1 : 0;
    }
}

/* ---- 信号处理 ---- */

/*
 * 注册信号处理函数（简单版本）
 */
funsos_sighandler_t funsos_signal(int signum, funsos_sighandler_t handler)
{
    return (funsos_sighandler_t)syscall2(SYS_SIGNAL, signum, (int)handler);
}

/*
 * 注册信号处理函数（高级版本）
 */
int funsos_sigaction(int signum, const funsos_sigaction_t *act,
                     funsos_sigaction_t *oldact)
{
    (void)signum; (void)act; (void)oldact;
    return -1;
}

/*
 * 设置信号屏蔽字
 */
int funsos_sigprocmask(int how, const funsos_sigset_t *set,
                       funsos_sigset_t *oldset)
{
    (void)how; (void)set; (void)oldset;
    return 0;
}

/*
 * 等待信号
 */
int funsos_pause(void)
{
    return syscall0(SYS_PAUSE);
}

/*
 * 等待信号（指定信号集）
 */
int funsos_sigsuspend(const funsos_sigset_t *set)
{
    (void)set;
    return syscall0(SYS_SIGSUSPEND);
}

/*
 * 等待信号并返回信号信息
 */
int funsos_sigwaitinfo(const funsos_sigset_t *set, void *info)
{
    (void)set; (void)info;
    return -1;
}

/*
 * 超时等待信号
 */
int funsos_sigtimedwait(const funsos_sigset_t *set, void *info,
                        uint32_t timeout_ms)
{
    (void)set; (void)info; (void)timeout_ms;
    return -1;
}

/* ---- 信号发送 ---- */

/*
 * 向进程发送信号
 */
int funsos_kill(int pid, int sig)
{
    return syscall2(SYS_KILL, pid, sig);
}

/*
 * 向当前进程发送信号
 */
int funsos_raise(int sig)
{
    return funsos_kill((int)funsos_get_pid(), sig);
}

/*
 * 向进程组发送信号
 */
int funsos_killpg(int pgrp, int sig)
{
    (void)pgrp; (void)sig;
    return -1;
}

/* ---- 闹钟和定时器 ---- */

/*
 * 设置闹钟（秒）
 */
uint32_t funsos_alarm(uint32_t seconds)
{
    return (uint32_t)syscall1(SYS_ALARM, (int)seconds);
}

/*
 * 高精度休眠（信号可中断）
 */
uint32_t funsos_sleep(uint32_t seconds)
{
    funsos_sleep(seconds);
    return 0;
}

/*
 * 高精度休眠（信号可中断，纳秒级）
 */
int funsos_nanosleep(uint32_t req_ns, uint32_t *rem_ns)
{
    (void)req_ns; (void)rem_ns;
    return 0;
}

/* ---- 信号栈 ---- */

/*
 * 设置/获取替代信号栈
 */
int funsos_sigaltstack(const funsos_stack_t *ss, funsos_stack_t *oss)
{
    (void)ss; (void)oss;
    return -1;
}
