#ifndef FUNSOS_PROCESS_H
#define FUNSOS_PROCESS_H

/*
 * FUNSOS 进程管理 API
 * 提供进程创建、终止、等待、信号等功能。
 * 基于 kernel/process.h 和 apps/user_syscall.h 的进程系统调用。
 */

#include "stdint.h"

/* ---- 信号常量 ---- */
#define FUNSOS_SIGHUP   1   /* 终端挂起 */
#define FUNSOS_SIGINT   2   /* 中断 (Ctrl+C) */
#define FUNSOS_SIGQUIT  3   /* 退出 (Ctrl+\) */
#define FUNSOS_SIGILL   4   /* 非法指令 */
#define FUNSOS_SIGTRAP  5   /* 断点陷阱 */
#define FUNSOS_SIGABRT  6   /* 异常终止 */
#define FUNSOS_SIGBUS   7   /* 总线错误 */
#define FUNSOS_SIGFPE   8   /* 浮点异常 */
#define FUNSOS_SIGKILL  9   /* 强制终止（不可捕获） */
#define FUNSOS_SIGUSR1  10  /* 用户自定义信号1 */
#define FUNSOS_SIGSEGV  11  /* 段错误 */
#define FUNSOS_SIGUSR2  12  /* 用户自定义信号2 */
#define FUNSOS_SIGPIPE  13  /* 管道破裂 */
#define FUNSOS_SIGALRM  14  /* 定时器信号 */
#define FUNSOS_SIGTERM  15  /* 终止信号 */
#define FUNSOS_SIGCHLD  17  /* 子进程状态改变 */
#define FUNSOS_SIGCONT  18  /* 继续执行 */
#define FUNSOS_SIGSTOP  19  /* 暂停（不可捕获） */
#define FUNSOS_SIGTSTP  20  /* 终端暂停 (Ctrl+Z) */

/* 信号处理函数默认行为 */
#define FUNSOS_SIG_DFL ((void(*)(int))0)  /* 默认处理 */
#define FUNSOS_SIG_IGN ((void(*)(int))1)  /* 忽略信号 */

/* 最大信号数 */
#define FUNSOS_NSIG 32

/* 信号处理函数类型 */
typedef void (*funsos_sighandler_t)(int);

/*
 * 获取当前进程 ID
 * 返回: 进程 ID
 */
uint32_t funsos_get_pid(void);

/*
 * 创建子进程（复制当前进程）
 * 返回: 在父进程中返回子进程 ID, 在子进程中返回 0, -1 失败
 */
int funsos_fork(void);

/*
 * 执行新程序（替换当前进程映像）
 * 参数: path - 可执行文件路径; argv - 参数列表
 * 返回: 成功不返回, -1 失败
 */
int funsos_exec(const char *path, char *const argv[]);

/*
 * 执行新程序（带环境变量）
 * 参数: path - 可执行文件路径; argv - 参数列表; envp - 环境变量列表
 * 返回: 成功不返回, -1 失败
 */
int funsos_execve(const char *path, char *const argv[], char *const envp[]);

/*
 * 等待子进程结束
 * 参数: status - 接收子进程退出状态（可为 NULL）
 * 返回: 结束的子进程 ID, -1 失败
 */
int funsos_wait(int *status);

/*
 * 等待指定子进程
 * 参数: pid - 要等待的子进程 ID; status - 接收退出状态
 * 返回: 结束的子进程 ID, -1 失败
 */
int funsos_waitpid(int pid, int *status);

/*
 * 启动新程序（不替换当前进程）
 * 参数: path - 可执行文件路径; args - 命令行参数
 * 返回: 新进程 ID, -1 失败
 */
int funsos_spawn(const char *path, const char *args);

/*
 * 终止指定进程
 * 参数: pid - 进程 ID
 * 返回: 0 成功, -1 失败
 */
int funsos_terminate(uint32_t pid);

/*
 * 退出当前进程
 * 参数: status - 退出状态码
 */
void funsos_exit(int status);

/*
 * 注册信号处理函数
 * 参数: sig - 信号编号; handler - 处理函数
 * 返回: 0 成功, -1 失败
 */
int funsos_signal(int sig, funsos_sighandler_t handler);

/*
 * 向指定进程发送信号
 * 参数: pid - 目标进程 ID; sig - 信号编号
 * 返回: 0 成功, -1 失败
 */
int funsos_kill(int pid, int sig);

/*
 * 创建管道
 * 参数: fd - 接收两个文件描述符的数组 (fd[0] 读端, fd[1] 写端)
 * 返回: 0 成功, -1 失败
 */
int funsos_pipe(int fd[2]);

/*
 * 进程休眠
 * 参数: seconds - 休眠秒数
 * 返回: 0 成功
 */
int funsos_sleep(uint32_t seconds);

/*
 * 让出 CPU 时间片
 */
void funsos_yield(void);

#endif /* FUNSOS_PROCESS_H */
