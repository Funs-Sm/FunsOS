/* signal.h - Unix风格信号处理头文件 */

#ifndef SIGNAL_H
#define SIGNAL_H

#include "kernel_types.h"
#include "kernel_proc.h"

/* ============================================================
 * 信号编号定义 (符合POSIX标准)
 * ============================================================ */

#define SIGHUP    1   /* 终端挂起或控制进程终止 */
#define SIGINT    2   /* 来自键盘的中断 (Ctrl+C) */
#define SIGQUIT   3   /* 来自键盘的退出 (Ctrl+\) */
#define SIGILL    4   /* 非法指令 */
#define SIGTRAP   5   /* 跟踪/断点陷阱 */
#define SIGABRT   6   /* 调用abort()发出的信号 */
#define SIGBUS    7   /* 总线错误 (错误内存访问) */
#define SIGFPE    8   /* 浮点异常 */
#define SIGKILL   9   /* 杀死信号 (不可捕获或忽略) */
#define SIGUSR1   10  /* 用户定义信号1 */
#define SIGSEGV   11  /* 无效内存引用 */
#define SIGUSR2   12  /* 用户定义信号2 */
#define SIGPIPE   13  /* 写向无读端的管道 */
#define SIGALRM   14  /* 定时器信号来自alarm() */
#define SIGTERM   15  /* 终止信号 */
#define SIGSTKFLT 16  /* 栈错误 */
#define SIGCHLD   17  /* 子进程停止或终止 */
#define SIGCONT   18  /* 如果停止则继续执行 */
#define SIGSTOP   19  /* 停止执行 (不可捕获或忽略) */
#define SIGTSTP   20  /* 来自终端的停止信号 (Ctrl+Z) */
#define SIGTTIN   21  /* 后台进程尝试读取 */
#define SIGTTOU   22  /* 后台进程尝试写入 */
#define SIGURG    23  /* 套接字上的紧急数据 */
#define SIGXCPU   24  /* 超出CPU时间限制 */
#define SIGXFSZ   25  /* 超出文件大小限制 */
#define SIGVTALRM 26  /* 虚拟定时器到期 */
#define SIGPROF   27  /* 性能分析定时器到期 */
#define SIGWINCH 28  /* 终端窗口大小变化 */
#define SIGIO     29  /* I/O现在可以在描述符上进行 */
#define SIGPWR    30  /* 电源故障 */
#define SIGSYS    31  /* 无效系统调用 */

#define NSIG 32      /* 信号总数 */

/* ============================================================
 * 特殊信号处理动作
 * ============================================================ */

#define SIG_DFL ((void (*)(int))0)   /* 默认处理动作 */
#define SIG_IGN ((void (*)(int))1)   /* 忽略信号 */
#define SIG_ERR ((void (*)(int))(-1)) /* 错误返回值 */

/* 信号处理函数类型 */
typedef void (*sighandler_t)(int);

/* ============================================================
 * sigaction标志
 * ============================================================ */

#define SA_NOCLDSTOP  0x00000001  /* 当子进程停止时不产生SIGCHLD */
#define SA_RESTART    0x00000002  /* 被信号中断的系统调用自动重启 */
#define SA_SIGINFO    0x00000004  /* 提供额外信息给处理程序 */
#define SA_NODEFER    0x00000008  /* 在处理程序运行时不要阻止信号 */
#define SA_RESETHAND  0x00000010  /* 在进入处理程序时将动作重置为SIG_DFL */
#define SA_ONSTACK    0x00000020  /* 在备用信号栈上调用处理程序 */

/* ============================================================
 * sigprocmask操作码
 * ============================================================ */

#define SIG_BLOCK   0  /* 将set添加到信号掩码中 */
#define SIG_UNBLOCK 1  /* 从信号掩码中删除set */
#define SIG_SETMASK 2  /* 将信号掩码设置为set */

/* ============================================================
 * 类型定义
 * ============================================================ */

/* 信号集类型 */
typedef uint32_t sigset_t;

/* sigaction结构 - 用于sigaction系统调用 */
typedef struct sigaction {
    sighandler_t sa_handler;   /* 信号处理函数指针 */
    sigset_t     sa_mask;      /* 执行处理函数时要阻塞的信号集 */
    int          sa_flags;     /* 特殊标志 */
    void       (*sa_restorer)(void); /* 内部使用的恢复函数 */
} sigaction_t;

/* 简化的sigaction结构（用于内部实现） */
typedef struct sigaction_t {
    sighandler_t sa_handler;
    uint32_t     sa_mask;    /* 信号屏蔽码 */
    uint32_t     sa_flags;   /* SA_SIGINFO等 */
} sigaction_internal_t;

/* sigaction_entry_t 和 sig_queue_entry_t 定义在 kernel_proc.h 中（pcb_t内嵌使用） */

/* 待处理信号位图（每个进程一个） */
typedef struct sig_pending {
    uint32_t bits[(NSIG + 31) / 32]; /* 位图表示待处理信号 */
} sig_pending_t;

/* 实时信号队列条目 */
#ifndef MAX_SIG_QUEUE_SIZE
#define MAX_SIG_QUEUE_SIZE 32
#endif

typedef struct sig_queue_entry {
    int signo;           /* 信号编号 */
    int value;           /* 伴随值 (用于实时信号) */
} sig_queue_entry_t;

/* ============================================================
 * 信号子系统初始化
 * ============================================================ */

/* 初始化全局信号子系统 */
void signal_init(void);

/* 初始化进程的信号处理数据结构 */
void signal_init_proc(pcb_t *proc);

/* ============================================================
 * 发送信号
 * ============================================================ */

/* 向指定进程发送信号 (kill系统调用) */
int kill(pid_t pid, int sig);

/* 向指定线程发送信号 (tkill系统调用) */
int tkill(pid_t tid, int sig);

/* 向指定线程组中的特定线程发送信号 (tgkill系统调用) */
int tgkill(pid_t tgid, pid_t tid, int sig);

/* 发送带值的实时信号 (sigqueue系统调用) */
int sigqueue(pid_t pid, int sig, int value);

/* ============================================================
 * 信号处理注册
 * ============================================================ */

/* 注册/查询信号处理动作 (sigaction系统调用) */
int sigaction(int signum, const sigaction_t *act, sigaction_t *oldact);

/* 简化的信号注册函数 (signal系统调用) */
sighandler_t signal(int signum, sighandler_t handler);

/* 设置/获取信号屏蔽码 (sigprocmask系统调用) */
int sigprocmask(int how, const uint32_t *set, uint32_t *oldset);

/* 获取待处理信号集合 (sigpending系统调用) */
int sigpending(sig_pending_t *set);

/* 临时替换信号掩码并等待信号 (sigsuspend系统调用) */
int sigsuspend(const uint32_t *mask);

/* ============================================================
 * 信号检查和投递
 * ============================================================ */

/* 检查并投递待处理信号（在sched_tick中调用） */
void signal_check_deliver(pcb_t *proc);

/* 检查是否有指定的待处理信号 */
int signal_has_pending(pcb_t *proc, int signum);

/* 投递信号到进程（内部使用） */
void signal_deliver(pcb_t *proc, int sig);

/* 默认信号动作处理（内部使用） */
void signal_handle_default(pcb_t *proc, int sig);

/* ============================================================
 * 兼容旧接口的包装函数
 * ============================================================ */

/* 发送信号（兼容旧接口） */
int signal_send(pid_t pid, int sig);

/* 检查信号（兼容旧接口） */
int signal_check(pcb_t *proc);

/* 注册信号处理器（兼容旧接口） */
int signal_register(pcb_t *proc, int sig, void (*handler)(int));

/* 阻塞信号（兼容旧接口） */
void signal_block(pcb_t *proc, int sig);

/* 解除信号阻塞（兼容旧接口） */
void signal_unblock(pcb_t *proc, int sig);

/* sigaction系统调用支持（兼容旧接口） */
int signal_sigaction(pcb_t *proc, int sig, const struct sigaction *act, struct sigaction *oact);

/* sigprocmask系统调用支持（兼容旧接口） */
int signal_sigprocmask(pcb_t *proc, int how, const sigset_t *set, sigset_t *oset);

/* alarm系统调用支持 */
int signal_alarm(pcb_t *proc, unsigned int seconds);

/* pause系统调用支持 */
int signal_pause(pcb_t *proc);

/* sigreturn机制 */
void signal_sigreturn(pcb_t *proc);

/* 检查并投递信号（含sigreturn恢复） */
int signal_check_and_deliver(pcb_t *proc);

#endif /* SIGNAL_H */
