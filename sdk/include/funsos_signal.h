#ifndef FUNSOS_SIGNAL_H
#define FUNSOS_SIGNAL_H

/*
 * FUNSOS 信号处理 API
 * 提供信号注册、信号发送、信号等待、信号集操作等功能。
 * 基于 kernel/signal.h 的系统调用封装。
 */

#include "stdint.h"

/* ---- 信号编号 ---- */
#define FUNSOS_SIGHUP     1   /* 终端挂起 */
#define FUNSOS_SIGINT     2   /* 中断 (Ctrl+C) */
#define FUNSOS_SIGQUIT    3   /* 退出 (Ctrl+\) */
#define FUNSOS_SIGILL     4   /* 非法指令 */
#define FUNSOS_SIGTRAP    5   /* 断点陷阱 */
#define FUNSOS_SIGABRT    6   /* 异常终止 */
#define FUNSOS_SIGBUS     7   /* 总线错误 */
#define FUNSOS_SIGFPE     8   /* 浮点异常 */
#define FUNSOS_SIGKILL    9   /* 强制终止（不可捕获、不可忽略） */
#define FUNSOS_SIGUSR1    10  /* 用户自定义信号1 */
#define FUNSOS_SIGSEGV    11  /* 段错误 */
#define FUNSOS_SIGUSR2    12  /* 用户自定义信号2 */
#define FUNSOS_SIGPIPE    13  /* 管道破裂 */
#define FUNSOS_SIGALRM    14  /* 定时器信号 */
#define FUNSOS_SIGTERM    15  /* 终止信号 */
#define FUNSOS_SIGSTKFLT  16  /* 栈故障 */
#define FUNSOS_SIGCHLD    17  /* 子进程状态改变 */
#define FUNSOS_SIGCONT    18  /* 继续执行 */
#define FUNSOS_SIGSTOP    19  /* 暂停（不可捕获、不可忽略） */
#define FUNSOS_SIGTSTP    20  /* 终端暂停 (Ctrl+Z) */
#define FUNSOS_SIGTTIN    21  /* 后台进程读终端 */
#define FUNSOS_SIGTTOU    22  /* 后台进程写终端 */
#define FUNSOS_SIGURG     23  /* 套接字紧急数据 */
#define FUNSOS_SIGXCPU    24  /* 超出CPU时间限制 */
#define FUNSOS_SIGXFSZ    25  /* 文件大小超出限制 */
#define FUNSOS_SIGVTALRM  26  /* 虚拟定时器到期 */
#define FUNSOS_SIGPROF    27  /* 统计剖析定时器到期 */
#define FUNSOS_SIGWINCH   28  /* 窗口大小改变 */
#define FUNSOS_SIGIO      29  /* I/O可用 */
#define FUNSOS_SIGPWR     30  /* 电源故障 */
#define FUNSOS_SIGSYS     31  /* 坏的系统调用 */

#define FUNSOS_NSIG       64  /* 最大信号数 */
#define FUNSOS_SIGRTMIN   32  /* 实时信号最小值 */
#define FUNSOS_SIGRTMAX   63  /* 实时信号最大值 */

/* 信号处理函数默认行为 */
#define FUNSOS_SIG_DFL   ((void(*)(int))0)   /* 默认处理 */
#define FUNSOS_SIG_IGN   ((void(*)(int))1)   /* 忽略信号 */
#define FUNSOS_SIG_ERR   ((void(*)(int))-1)  /* 错误返回 */

/* 信号处理函数类型 */
typedef void (*funsos_sighandler_t)(int);

/* ---- 信号集操作 ---- */

/* 信号集类型 (64个信号) */
typedef struct {
    uint32_t sig[2];    /* 信号位掩码 (每个位代表一个信号) */
} funsos_sigset_t;

/*
 * 清空信号集
 * 参数: set - 信号集指针
 * 返回: 0 成功, -1 失败
 */
int funsos_sigemptyset(funsos_sigset_t *set);

/*
 * 填充信号集（包含所有信号）
 * 参数: set - 信号集指针
 * 返回: 0 成功, -1 失败
 */
int funsos_sigfillset(funsos_sigset_t *set);

/*
 * 添加信号到信号集
 * 参数: set - 信号集指针; signum - 信号编号
 * 返回: 0 成功, -1 失败
 */
int funsos_sigaddset(funsos_sigset_t *set, int signum);

/*
 * 从信号集删除信号
 * 参数: set - 信号集指针; signum - 信号编号
 * 返回: 0 成功, -1 失败
 */
int funsos_sigdelset(funsos_sigset_t *set, int signum);

/*
 * 检查信号是否在信号集中
 * 参数: set - 信号集指针; signum - 信号编号
 * 返回: 1 存在, 0 不存在, -1 失败
 */
int funsos_sigismember(const funsos_sigset_t *set, int signum);

/* ---- 信号处理 ---- */

/* 信号处理动作结构 */
typedef struct {
    funsos_sighandler_t sa_handler;    /* 信号处理函数 */
    funsos_sigset_t    sa_mask;        /* 信号屏蔽字 */
    int                sa_flags;       /* 信号处理标志 */
    void             (*sa_sigaction)(int, void *, void *);  /* 实时信号处理 */
} funsos_sigaction_t;

/* 信号处理标志 */
#define FUNSOS_SA_NOCLDSTOP  0x0001   /* 子进程停止时不产生SIGCHLD */
#define FUNSOS_SA_NOCLDWAIT  0x0002   /* 不产生僵尸进程 */
#define FUNSOS_SA_SIGINFO    0x0004   /* 使用 sa_sigaction 代替 sa_handler */
#define FUNSOS_SA_RESTART    0x0008   /* 自动重启被中断的系统调用 */
#define FUNSOS_SA_NODEFER    0x0010   /* 执行处理函数时不屏蔽信号 */
#define FUNSOS_SA_RESETHAND  0x0020   /* 处理函数执行一次后重置为默认 */

/*
 * 注册信号处理函数（简单版本）
 * 参数: signum - 信号编号; handler - 处理函数
 * 返回: 之前的处理函数, SIG_ERR 失败
 */
funsos_sighandler_t funsos_signal(int signum, funsos_sighandler_t handler);

/*
 * 注册信号处理函数（高级版本）
 * 参数: signum - 信号编号; act - 新动作; oldact - 接收旧动作 (可为NULL)
 * 返回: 0 成功, -1 失败
 */
int funsos_sigaction(int signum, const funsos_sigaction_t *act, 
                     funsos_sigaction_t *oldact);

/*
 * 设置信号屏蔽字
 * 参数: how - 方式 (SIG_BLOCK, SIG_UNBLOCK, SIG_SETMASK)
 *       set - 新的信号屏蔽字; oldset - 接收旧的屏蔽字 (可为NULL)
 * 返回: 0 成功, -1 失败
 */
int funsos_sigprocmask(int how, const funsos_sigset_t *set, 
                       funsos_sigset_t *oldset);

/* how 参数值 */
#define FUNSOS_SIG_BLOCK    0   /* 添加到当前屏蔽字 */
#define FUNSOS_SIG_UNBLOCK  1   /* 从当前屏蔽字移除 */
#define FUNSOS_SIG_SETMASK  2   /* 设置为当前屏蔽字 */

/*
 * 等待信号
 * 挂起进程直到有信号到达
 * 返回: 总是 -1，设置 errno 为 EINTR
 */
int funsos_pause(void);

/*
 * 等待信号（指定信号集）
 * 参数: set - 信号集（等待这些信号之外的信号）
 * 返回: 总是 -1，设置 errno 为 EINTR
 */
int funsos_sigsuspend(const funsos_sigset_t *set);

/*
 * 等待信号并返回信号信息
 * 参数: set - 等待的信号集; info - 接收信号信息
 * 返回: 信号编号, -1 失败
 */
int funsos_sigwaitinfo(const funsos_sigset_t *set, void *info);

/*
 * 超时等待信号
 * 参数: set - 等待的信号集; info - 接收信号信息; timeout - 超时时间
 * 返回: 信号编号, -1 超时或失败
 */
int funsos_sigtimedwait(const funsos_sigset_t *set, void *info, 
                        uint32_t timeout_ms);

/* ---- 信号发送 ---- */

/*
 * 向进程发送信号
 * 参数: pid - 目标进程ID; sig - 信号编号
 * 返回: 0 成功, -1 失败
 */
int funsos_kill(int pid, int sig);

/*
 * 向当前进程发送信号
 * 参数: sig - 信号编号
 * 返回: 0 成功, -1 失败
 */
int funsos_raise(int sig);

/*
 * 向进程组发送信号
 * 参数: pgrp - 进程组ID; sig - 信号编号
 * 返回: 0 成功, -1 失败
 */
int funsos_killpg(int pgrp, int sig);

/* ---- 闹钟和定时器 ---- */

/*
 * 设置闹钟（秒）
 * 参数: seconds - 秒数 (0 表示取消)
 * 返回: 之前剩余的秒数
 */
uint32_t funsos_alarm(uint32_t seconds);

/*
 * 高精度休眠（信号可中断）
 * 参数: seconds - 秒数
 * 返回: 剩余未休眠的秒数
 */
uint32_t funsos_sleep(uint32_t seconds);

/*
 * 高精度休眠（信号可中断，纳秒级）
 * 参数: req - 请求休眠时间; rem - 接收剩余时间 (可为NULL)
 * 返回: 0 成功, -1 被中断
 */
int funsos_nanosleep(uint32_t req_ns, uint32_t *rem_ns);

/* ---- 信号栈 ---- */

/* 信号栈结构 */
typedef struct {
    void    *ss_sp;         /* 栈基地址 */
    int      ss_flags;      /* 标志 */
    uint32_t ss_size;       /* 栈大小 */
} funsos_stack_t;

#define FUNSOS_SS_ONSTACK   1   /* 在替代信号栈上 */
#define FUNSOS_SS_DISABLE   2   /* 禁用替代信号栈 */

/*
 * 设置/获取替代信号栈
 * 参数: ss - 新栈 (NULL表示获取); oss - 接收旧栈 (可为NULL)
 * 返回: 0 成功, -1 失败
 */
int funsos_sigaltstack(const funsos_stack_t *ss, funsos_stack_t *oss);

#endif /* FUNSOS_SIGNAL_H */
