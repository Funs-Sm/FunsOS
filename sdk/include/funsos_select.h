#ifndef FUNSOS_SELECT_H
#define FUNSOS_SELECT_H

/*
 * FUNSOS I/O 多路复用 API
 * 提供 select、poll、epoll 等 I/O 多路复用功能。
 * 基于 kernel/epoll.c 和 net/socket.c 的系统调用封装。
 */

#include "stdint.h"

/* ---- fd_set 操作 ---- */

#define FUNSOS_FD_SETSIZE  1024   /* 文件描述符最大数量 */

typedef struct {
    uint32_t fds_bits[FUNSOS_FD_SETSIZE / 32];   /* 文件描述符位掩码 */
} funsos_fd_set;

/* 操作 fd_set 的宏（函数实现）
void funsos_FD_ZERO(funsos_fd_set *set);
void funsos_FD_SET(int fd, funsos_fd_set *set);
void funsos_FD_CLR(int fd, funsos_fd_set *set);
int  funsos_FD_ISSET(int fd, funsos_fd_set *set);

/* ---- timeval 结构 ---- */
typedef struct {
    uint32_t tv_sec;      /* 秒 */
    uint32_t tv_usec;     /* 微秒 */
} funsos_timeval_t;

/* ---- select ---- */

/*
 * select I/O 多路复用
 * 参数: nfds - 最大文件描述符+1
 *       readfds - 可读集合 (NULL=不关心)
 *       writefds - 可写集合 (NULL=不关心)
 *       exceptfds - 异常集合 (NULL=不关心)
 *       timeout - 超时时间 (NULL=无限等待)
 * 返回: 就绪的文件描述符数量, 0 超时, -1 失败
 */
int funsos_select(int nfds, funsos_fd_set *readfds,
                  funsos_fd_set *writefds,
                  funsos_fd_set *exceptfds,
                  funsos_timeval_t *timeout);

/* ---- poll ---- */

/* poll 事件类型 */
#define FUNSOS_POLLIN       0x0001   /* 有数据可读 */
#define FUNSOS_POLLPRI      0x0002   /* 有紧急数据可读 */
#define FUNSOS_POLLOUT      0x0004   /* 现在写操作不会阻塞 */
#define FUNSOS_POLLRDNORM   0x0040   /* 普通数据可读 */
#define FUNSOS_POLLWRNORM   0x0100   /* 普通数据可写 */
#define FUNSOS_POLLRDBAND   0x0080   /* 优先级数据可读 */
#define FUNSOS_POLLWRBAND   0x0200   /* 优先级数据可写 */
#define FUNSOS_POLLMSG      0x0400   /* 有消息可读 */
#define FUNSOS_POLLERR      0x0008   /* 错误（输出） */
#define FUNSOS_POLLHUP      0x0010   /* 挂起（输出） */
#define FUNSOS_POLLNVAL     0x0020   /* 无效请求（输出） */

/* pollfd 结构 */
typedef struct {
    int      fd;        /* 文件描述符 */
    uint16_t events;    /* 请求的事件 */
    uint16_t revents;   /* 返回的事件 */
} funsos_pollfd_t;

/*
 * poll I/O 多路复用
 * 参数: fds - pollfd 数组; nfds - 数组大小; timeout - 超时时间(毫秒, -1=无限)
 * 返回: 就绪的文件描述符数量, 0 超时, -1 失败
 */
int funsos_poll(funsos_pollfd_t *fds, uint32_t nfds, int timeout);

/* ---- epoll ---- */

/* epoll 事件类型 */
#define FUNSOS_EPOLLIN        0x00000001   /* 可读 */
#define FUNSOS_EPOLLPRI       0x00000002   /* 紧急可读 */
#define FUNSOS_EPOLLOUT       0x00000004   /* 可写 */
#define FUNSOS_EPOLLRDNORM    0x00000040   /* 普通可读 */
#define FUNSOS_EPOLLWRNORM    0x00000100   /* 普通可写 */
#define FUNSOS_EPOLLMSG       0x00000400   /* 消息可读 */
#define FUNSOS_EPOLLERR       0x00000008   /* 错误 */
#define FUNSOS_EPOLLHUP       0x00000010   /* 挂起 */
#define FUNSOS_EPOLLET        0x80000000   /* 边缘触发 */
#define FUNSOS_EPOLLEXCLUSIVE  0x10000000   /* 独占唤醒模式 */

/* epoll 数据结构（联合体） */
typedef union {
    void    *ptr;      /* 指针数据 */
    int      fd;       /* 文件描述符 */
    uint32_t u32;     /* 32位整数 */
    uint64_t u64;     /* 64位整数 */
} funsos_epoll_data_t;

/* epoll 事件结构 */
typedef struct {
    uint32_t           events;    /* 事件 */
    funsos_epoll_data_t data;      /* 用户数据 */
} funsos_epoll_event_t;

/* epoll 操作 */
#define FUNSOS_EPOLL_CTL_ADD  1   /* 添加 */
#define FUNSOS_EPOLL_CTL_DEL  2   /* 删除 */
#define FUNSOS_EPOLL_CTL_MOD  3   /* 修改 */

/*
 * 创建 epoll 实例
 * 参数: size - 预计监控的描述符数量（提示用）
 * 返回: epoll 文件描述符, -1 失败
 */
int funsos_epoll_create(int size);

/*
 * 创建 epoll 实例（带标志）
 * 参数: flags - 标志位
 * 返回: epoll 文件描述符, -1 失败
 */
int funsos_epoll_create1(int flags);

/*
 * 控制 epoll 实例
 * 参数: epfd - epoll 文件描述符
 *       op - 操作 (EPOLL_CTL_ADD/DEL/MOD)
 *       fd - 目标文件描述符
 *       event - 事件配置 (DEL时可为NULL)
 * 返回: 0 成功, -1 失败
 */
int funsos_epoll_ctl(int epfd, int op, int fd, 
                    funsos_epoll_event_t *event);

/*
 * 等待 epoll 事件
 * 参数: epfd - epoll 文件描述符
 *       events - 接收事件的数组
 *       maxevents - 最大事件数
 *       timeout - 超时时间(毫秒, -1=无限)
 * 返回: 就绪的事件数量, 0 超时, -1 失败
 */
int funsos_epoll_wait(int epfd, funsos_epoll_event_t *events,
                      int maxevents, int timeout);

#endif /* FUNSOS_SELECT_H */
