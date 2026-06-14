#ifndef LIB_SIGNAL_H
#define LIB_SIGNAL_H

#include "stdint.h"

/* POSIX 信号常量 - 与内核 signal.h 保持一致 */
#define SIGHUP   1
#define SIGINT   2
#define SIGQUIT  3
#define SIGILL   4
#define SIGTRAP  5
#define SIGABRT  6
#define SIGBUS   7
#define SIGFPE   8
#define SIGKILL  9
#define SIGUSR1  10
#define SIGSEGV  11
#define SIGUSR2  12
#define SIGPIPE  13
#define SIGALRM  14
#define SIGTERM  15
#define SIGSTKFLT 16
#define SIGCHLD  17
#define SIGCONT  18
#define SIGSTOP  19
#define SIGTSTP  20
#define SIGTTIN  21
#define SIGTTOU  22
#define SIGURG   23
#define SIGXCPU  24
#define SIGXFSZ  25
#define SIGVTALRM 26
#define SIGPROF  27
#define SIGWINCH 28
#define SIGIO    29
#define SIGPWR   30
#define SIGSYS   31

#define NSIG     32

/* 信号处理方式 */
#define SIG_DFL  ((void (*)(int))0)
#define SIG_IGN  ((void (*)(int))1)
#define SIG_ERR  ((void (*)(int))-1)

/* sigset_t 和操作 */
typedef uint32_t sigset_t;

#define sigemptyset(set)    (*(set) = 0)
#define sigfillset(set)     (*(set) = 0xFFFFFFFF)
#define sigaddset(set,sig)  (*(set) |= (1U << ((sig)-1)))
#define sigdelset(set,sig)  (*(set) &= ~(1U << ((sig)-1)))
#define sigismember(set,sig) ((*(set) >> ((sig)-1)) & 1)

/* sigaction 结构 */
typedef void (*sighandler_t)(int);
typedef int32_t pid_t;

struct sigaction {
    sighandler_t sa_handler;
    sigset_t     sa_mask;
    int          sa_flags;
    void       (*sa_restorer)(void);
};

#define SA_NOCLDSTOP  1
#define SA_RESTART    2
#define SA_SIGINFO    4

/* POSIX 信号函数 */
sighandler_t signal(int sig, sighandler_t handler);
int raise(int sig);
int kill(pid_t pid, int sig);

/* sigaction */
int sigaction(int sig, const struct sigaction *act, struct sigaction *oact);

/* 信号集操作 */
int sigprocmask(int how, const sigset_t *set, sigset_t *oset);
#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

/* 暂停/等待 */
int pause(void);
unsigned int alarm(unsigned int seconds);

#endif
