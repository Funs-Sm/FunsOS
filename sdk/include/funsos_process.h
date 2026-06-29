#ifndef FUNSOS_PROCESS_H
#define FUNSOS_PROCESS_H

/*
 * FUNSOS 进程管理 API
 * 提供进程创建、终止、等待、信号、调度控制等功能。
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

/* ---- 调度策略 ---- */
#define FUNSOS_SCHED_OTHER    0x0001  /* 普通分时调度 */
#define FUNSOS_SCHED_FIFO     0x0002  /* 实时FIFO调度 */
#define FUNSOS_SCHED_RR       0x0004  /* 实时轮转调度 */
#define FUNSOS_SCHED_DEADLINE 0x0010  /* Deadline调度 */
#define FUNSOS_SCHED_BATCH    0x0020  /* 批处理调度 */
#define FUNSOS_SCHED_IDLE     0x0040  /* 空闲优先级 */

/* ---- 优先级范围 ---- */
#define FUNSOS_PRIO_MIN     1
#define FUNSOS_PRIO_MAX     99
#define FUNSOS_PRIO_DEFAULT 50

/* 信号处理函数类型 */
typedef void (*funsos_sighandler_t)(int);

/* ---- 调度参数结构体 ---- */
typedef struct funsos_sched_param {
    int      sched_priority;    /* 静态优先级 */
    uint64_t runtime;           /* Deadline 运行时间 (ns/ticks) */
    uint64_t deadline;          /* 相对截止时间 */
    uint64_t period;            /* 周期 */
} funsos_sched_param_t;

/* CPU 亲和性掩码 */
typedef struct funsos_cpu_affinity {
    uint32_t cpumask;           /* CPU 位掩码 */
    uint32_t cpu_count;         /* CPU 数量 */
} funsos_cpu_affinity_t;

/* ---- 进程信息结构 ---- */
typedef struct funsos_process_info {
    uint32_t pid;               /* 进程 ID */
    char     name[64];          /* 进程名 */
    uint32_t state;             /* 进程状态 */
    uint32_t priority;          /* 优先级 */
    uint32_t memory_kb;         /* 内存使用 (KB) */
    uint64_t cpu_time;          /* CPU 时间 (ticks) */
} funsos_process_info_t;

/*
 * 获取当前进程 ID
 * 返回: 进程 ID
 */
uint32_t funsos_get_pid(void);

/*
 * 获取父进程 ID
 * 返回: 父进程 ID
 */
uint32_t funsos_get_ppid(void);

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
 * 进程休眠（秒）
 * 参数: seconds - 休眠秒数
 * 返回: 0 成功
 */
int funsos_sleep(uint32_t seconds);

/*
 * 高精度休眠（毫秒）
 * 参数: ms - 休眠毫秒数
 * 返回: 0 成功
 */
int funsos_msleep(uint32_t ms);

/*
 * 让出 CPU 时间片
 */
void funsos_yield(void);

/* ---- 调度控制 API ---- */

/*
 * 设置进程调度策略和优先级
 * 参数: pid - 进程 ID (0 表示当前进程); policy - 调度策略; param - 调度参数
 * 返回: 0 成功, -1 失败
 */
int funsos_sched_setscheduler(uint32_t pid, int policy, const funsos_sched_param_t *param);

/*
 * 获取进程调度策略和优先级
 * 参数: pid - 进程 ID; policy - 接收策略; param - 接收调度参数
 * 返回: 0 成功, -1 失败
 */
int funsos_sched_getscheduler(uint32_t pid, int *policy, funsos_sched_param_t *param);

/*
 * 设置进程优先级
 * 参数: pid - 进程 ID; priority - 新优先级 (1-99)
 * 返回: 0 成功, -1 失败
 */
int funsos_setpriority(uint32_t pid, int priority);

/*
 * 获取进程优先级
 * 参数: pid - 进程 ID
 * 返回: 优先级, -1 失败
 */
int funsos_getpriority(uint32_t pid);

/*
 * 设置进程 CPU 亲和性
 * 参数: pid - 进程 ID; mask - CPU 掩码
 * 返回: 0 成功, -1 失败
 */
int funsos_setaffinity(uint32_t pid, funsos_cpu_affinity_t *mask);

/*
 * 获取进程 CPU 亲和性
 * 参数: pid - 进程 ID; mask - 接收 CPU 掩码
 * 返回: 0 成功, -1 失败
 */
int funsos_getaffinity(uint32_t pid, funsos_cpu_affinity_t *mask);

/* ---- 进程查询 API ---- */

/*
 * 获取指定进程的信息
 * 参数: pid - 进程 ID; info - 接收进程信息
 * 返回: 0 成功, -1 失败
 */
int funsos_get_process_info(uint32_t pid, funsos_process_info_t *info);

/*
 * 获取系统中所有进程 ID 列表
 * 参数: pid_list - 接收 PID 的数组; count - 输入数组大小, 输出实际数量
 * 返回: 0 成功, -1 失败
 */
int funsos_list_processes(uint32_t *pid_list, uint32_t *count);

#endif /* FUNSOS_PROCESS_H */
