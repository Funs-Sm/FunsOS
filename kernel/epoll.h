#ifndef EPOLL_H
#define EPOLL_H

#include "stdint.h"
#include "stddef.h"
#include "sync.h"
#include "spinlock.h"

/* ============================================================
 * Epoll (Event Poll) 子系统
 *
 * 提供高效的 I/O 事件多路复用机制。
 * 类似 Linux epoll，支持水平触发和边缘触发。
 * ============================================================ */

/* Epoll 事件类型 */
#define EPOLLIN     0x001    /* 可读 */
#define EPOLLPRI    0x002    /* 紧急数据可读 */
#define EPOLLOUT    0x004    /* 可写 */
#define EPOLLRDNORM 0x040    /* 普通数据可读 */
#define EPOLLRDBAND 0x080    /* 优先级数据可读 */
#define EPOLLWRNORM 0x100    /* 普通数据可写 */
#define EPOLLWRBAND 0x200    /* 优先级数据可写 */
#define EPOLLMSG    0x400    /* 消息可用 */
#define EPOLLERR    0x008    /* 错误 */
#define EPOLLHUP    0x010    /* 挂起 */
#define EPOLLRDHUP  0x2000   /* 对端关闭读半连接 */

/* Epoll 控制操作 */
#define EPOLL_CTL_ADD 1      /* 添加文件描述符 */
#define EPOLL_CTL_DEL 2      /* 删除文件描述符 */
#define EPOLL_CTL_MOD 3      /* 修改文件描述符 */

/* Epoll 标志 */
#define EPOLL_CLOEXEC  02000000  /* 执行时关闭 */
#define EPOLL_NONBLOCK 04000     /* 非阻塞 */

/* 触发模式 */
#define EPOLLET      (1 << 31)   /* 边缘触发 */
#define EPOLLONESHOT (1 << 30)   /* 一次性 */
#define EPOLLWAKEUP  (1 << 29)   /* 唤醒 */
#define EPOLLEXCLUSIVE (1 << 28) /* 独占唤醒 */

/* 最大事件数 */
#define EPOLL_MAX_EVENTS   64
#define EPOLL_MAX_FDS      1024
#define EPOLL_MAX_INSTANCES 64

/* Epoll 事件结构 */
typedef struct epoll_event {
    uint32_t events;    /* Epoll 事件标志 */
    uint64_t data;      /* 用户数据 */
} epoll_event_t;

/* Epoll 文件描述符条目 */
typedef struct epoll_fd_entry {
    int          fd;          /* 文件描述符 */
    uint32_t     events;      /* 感兴趣的事件 */
    uint64_t     data;        /* 用户数据 */
    uint32_t     revents;     /* 已发生的事件 */
    int          active;      /* 是否激活 */
    int          oneshot;     /* 一次性 */
    int          et;          /* 边缘触发 */
    struct epoll_fd_entry *next;
    struct epoll_fd_entry *prev;
} epoll_fd_entry_t;

/* Epoll 实例 */
typedef struct epoll_instance {
    int                id;           /* 实例 ID */
    int                used;         /* 是否使用 */
    int                size;         /* 容量 */
    uint32_t           fd_count;     /* 监听的 fd 数量 */
    epoll_fd_entry_t  *fd_list;      /* fd 链表 */
    epoll_fd_entry_t  *ready_list;   /* 就绪链表 */
    uint32_t           ready_count;  /* 就绪数量 */
    spinlock_t         lock;
    sem_t              wait_sem;     /* 等待信号量 */
    int                closed;       /* 是否已关闭 */
} epoll_instance_t;

/* ============================================================
 * 初始化
 * ============================================================ */
void epoll_init(void);

/* ============================================================
 * 核心 API
 * ============================================================ */

/*
 * epoll_create - 创建 epoll 实例
 * size: 提示内核需要监听的文件描述符数量
 * 返回: epoll 文件描述符, 负数错误码
 */
int epoll_create(int size);

/*
 * epoll_create1 - 创建 epoll 实例 (带标志)
 * flags: EPOLL_CLOEXEC 等
 * 返回: epoll 文件描述符, 负数错误码
 */
int epoll_create1(int flags);

/*
 * epoll_ctl - 控制 epoll 实例
 * epfd: epoll 文件描述符
 * op:   操作类型 (EPOLL_CTL_ADD/DEL/MOD)
 * fd:   目标文件描述符
 * event: 事件配置
 * 返回: 0 成功, 负数错误码
 */
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);

/*
 * epoll_wait - 等待事件
 * epfd: epoll 文件描述符
 * events: 输出事件数组
 * maxevents: 最大事件数
 * timeout: 超时时间(ms), -1 表示无限等待
 * 返回: 就绪事件数量, 负数错误码
 */
int epoll_wait(int epfd, struct epoll_event *events,
               int maxevents, int timeout);

/* ============================================================
 * 事件通知接口 (供其他子系统调用)
 * ============================================================ */

/*
 * epoll_notify - 通知 epoll 有事件发生
 * fd: 发生事件的文件描述符
 * events: 发生的事件
 */
void epoll_notify(int fd, uint32_t events);

/* ============================================================
 * 系统调用接口
 * ============================================================ */
int sys_epoll_create(int size);
int sys_epoll_create1(int flags);
int sys_epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
int sys_epoll_wait(int epfd, struct epoll_event *events,
                   int maxevents, int timeout);

/* ============================================================
 * 调试与统计
 * ============================================================ */
void epoll_dump(int epfd);
int epoll_get_count(void);

#endif /* EPOLL_H */
