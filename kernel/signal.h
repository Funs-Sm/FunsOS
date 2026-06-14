#ifndef SIGNAL_H
#define SIGNAL_H

#include "kernel_types.h"
#include "kernel_proc.h"

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

#define SIG_DFL ((void(*)(int))0)
#define SIG_IGN ((void(*)(int))1)

#define NSIG 32

/* sigaction 标志 */
#define SA_NOCLDSTOP  1
#define SA_RESTART    2
#define SA_SIGINFO    4

/* sigprocmask 操作 */
#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

/* sigset_t 类型 */
typedef uint32_t sigset_t;

/* sigaction 结构 */
struct sigaction {
    void     (*sa_handler)(int);
    sigset_t   sa_mask;
    int        sa_flags;
    void     (*sa_restorer)(void);
};

void signal_init(void);
void signal_init_proc(pcb_t *proc);
int signal_send(pid_t pid, int sig);
int signal_check(pcb_t *proc);
void signal_deliver(pcb_t *proc, int sig);
int signal_register(pcb_t *proc, int sig, void (*handler)(int));
void signal_block(pcb_t *proc, int sig);
void signal_unblock(pcb_t *proc, int sig);

/* 新增: sigaction 系统调用支持 */
int signal_sigaction(pcb_t *proc, int sig, const struct sigaction *act, struct sigaction *oact);

/* 新增: sigprocmask 系统调用支持 */
int signal_sigprocmask(pcb_t *proc, int how, const sigset_t *set, sigset_t *oset);

/* 新增: alarm 系统调用支持 */
int signal_alarm(pcb_t *proc, unsigned int seconds);

/* 新增: pause 系统调用支持 */
int signal_pause(pcb_t *proc);

/* 新增: sigreturn 机制 */
void signal_sigreturn(pcb_t *proc);

/* 新增: 检查并投递信号 (含 sigreturn 恢复) */
int signal_check_and_deliver(pcb_t *proc);

#endif
