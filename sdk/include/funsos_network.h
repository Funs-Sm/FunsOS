#ifndef FUNSOS_NETWORK_H
#define FUNSOS_NETWORK_H

/*
 * FUNSOS 网络通信 API
 * 提供 TCP/UDP socket 通信功能。
 * 基于 net/socket.h 和 apps/user_syscall.h 的网络系统调用。
 */

#include "stdint.h"

/* ---- 地址族 ---- */
#define FUNSOS_AF_UNSPEC 0    /* 未指定 */
#define FUNSOS_AF_UNIX   1    /* Unix 域 */
#define FUNSOS_AF_INET   2    /* IPv4 */
#define FUNSOS_AF_INET6  10   /* IPv6 */

/* ---- Socket 类型 ---- */
#define FUNSOS_SOCK_STREAM 1  /* TCP 流式 */
#define FUNSOS_SOCK_DGRAM  2  /* UDP 数据报 */
#define FUNSOS_SOCK_RAW    3  /* 原始套接字 */

/* ---- 协议 ---- */
#define FUNSOS_IPPROTO_IP   0   /* IP */
#define FUNSOS_IPPROTO_TCP  6   /* TCP */
#define FUNSOS_IPPROTO_UDP  17  /* UDP */

/* ---- Socket 选项级别 ---- */
#define FUNSOS_SOL_SOCKET       1  /* Socket 级别 */
#define FUNSOS_IPPROTO_TCP_LEVEL 6 /* TCP 级别 */

/* ---- Socket 选项名 ---- */
#define FUNSOS_SO_REUSEADDR  0x0002  /* 地址复用 */
#define FUNSOS_SO_KEEPALIVE  0x0008  /* 保活 */
#define FUNSOS_SO_BROADCAST  0x0020  /* 广播 */
#define FUNSOS_SO_SNDBUF     0x1001  /* 发送缓冲区大小 */
#define FUNSOS_SO_RCVBUF     0x1002  /* 接收缓冲区大小 */
#define FUNSOS_SO_SNDTIMEO   0x1003  /* 发送超时 */
#define FUNSOS_SO_RCVTIMEO   0x1004  /* 接收超时 */
#define FUNSOS_SO_NONBLOCK   0x1010  /* 非阻塞模式 */

/* ---- TCP 选项 ---- */
#define FUNSOS_TCP_NODELAY   0x01  /* 禁用 Nagle 算法 */

/* ---- 发送/接收标志 ---- */
#define FUNSOS_MSG_DONTWAIT  0x40   /* 非阻塞 */
#define FUNSOS_MSG_PEEK      0x02   /* 窥探 */
#define FUNSOS_MSG_NOSIGNAL  0x4000 /* 不产生 SIGPIPE */

/* ---- 关闭方式 ---- */
#define FUNSOS_SHUT_RD   0  /* 关闭读 */
#define FUNSOS_SHUT_WR   1  /* 关闭写 */
#define FUNSOS_SHUT_RDWR 2  /* 关闭读写 */

/* ---- IPv4 地址结构 ---- */
typedef struct {
    uint32_t addr;  /* 网络字节序的 IPv4 地址 */
} funsos_ipv4_t;

/* ---- Socket 地址结构 ---- */
typedef struct {
    uint16_t sin_family;   /* 地址族 (AF_INET) */
    uint16_t sin_port;     /* 端口号（网络字节序） */
    funsos_ipv4_t sin_addr; /* IP 地址 */
    char sin_zero[8];      /* 填充 */
} funsos_sockaddr_in_t;

/*
 * 创建 socket
 * 参数: domain - 地址族; type - socket 类型; protocol - 协议
 * 返回: socket 文件描述符 (>=0), -1 失败
 */
int funsos_socket(int domain, int type, int protocol);

/*
 * 绑定 socket 到地址
 * 参数: fd - socket 描述符; addr - 绑定地址
 * 返回: 0 成功, -1 失败
 */
int funsos_bind(int fd, const funsos_sockaddr_in_t *addr);

/*
 * 连接到远程地址
 * 参数: fd - socket 描述符; addr - 目标地址
 * 返回: 0 成功, -1 失败
 */
int funsos_connect(int fd, const funsos_sockaddr_in_t *addr);

/*
 * 监听连接
 * 参数: fd - socket 描述符; backlog - 等待队列长度
 * 返回: 0 成功, -1 失败
 */
int funsos_listen(int fd, int backlog);

/*
 * 接受连接
 * 参数: fd - socket 描述符; addr - 接收客户端地址（可为 NULL）
 * 返回: 新 socket 描述符, -1 失败
 */
int funsos_accept(int fd, funsos_sockaddr_in_t *addr);

/*
 * 发送数据
 * 参数: fd - socket 描述符; buf - 发送缓冲区; len - 长度; flags - 标志
 * 返回: 实际发送字节数, -1 失败
 */
int funsos_send(int fd, const void *buf, uint32_t len, int flags);

/*
 * 接收数据
 * 参数: fd - socket 描述符; buf - 接收缓冲区; len - 缓冲区大小; flags - 标志
 * 返回: 实际接收字节数, -1 失败
 */
int funsos_recv(int fd, void *buf, uint32_t len, int flags);

/*
 * 发送数据到指定地址（UDP）
 * 参数: fd - socket 描述符; buf - 发送缓冲区; len - 长度;
 *       flags - 标志; addr - 目标地址
 * 返回: 实际发送字节数, -1 失败
 */
int funsos_sendto(int fd, const void *buf, uint32_t len, int flags,
                  const funsos_sockaddr_in_t *addr);

/*
 * 从指定地址接收数据（UDP）
 * 参数: fd - socket 描述符; buf - 接收缓冲区; len - 缓冲区大小;
 *       flags - 标志; addr - 接收来源地址
 * 返回: 实际接收字节数, -1 失败
 */
int funsos_recvfrom(int fd, void *buf, uint32_t len, int flags,
                    funsos_sockaddr_in_t *addr);

/*
 * 关闭 socket 连接
 * 参数: fd - socket 描述符; how - 关闭方式
 * 返回: 0 成功, -1 失败
 */
int funsos_shutdown(int fd, int how);

/*
 * 关闭 socket
 * 参数: fd - socket 描述符
 * 返回: 0 成功, -1 失败
 */
int funsos_closesocket(int fd);

/*
 * 设置 socket 选项
 * 参数: fd - socket 描述符; level - 选项级别; optname - 选项名;
 *       optval - 选项值; optlen - 选项值长度
 * 返回: 0 成功, -1 失败
 */
int funsos_setsockopt(int fd, int level, int optname,
                      const void *optval, uint32_t optlen);

/*
 * 获取 socket 选项
 * 参数: fd - socket 描述符; level - 选项级别; optname - 选项名;
 *       optval - 接收选项值; optlen - 选项值长度指针
 * 返回: 0 成功, -1 失败
 */
int funsos_getsockopt(int fd, int level, int optname,
                      void *optval, uint32_t *optlen);

/*
 * I/O 多路复用 - select
 * 参数: nfds - 最大描述符+1; readfds - 读集合; writefds - 写集合;
 *       exceptfds - 异常集合; timeout_ms - 超时毫秒
 * 返回: 就绪的描述符数, -1 失败
 */
int funsos_select(int nfds, void *readfds, void *writefds,
                  void *exceptfds, uint32_t timeout_ms);

/*
 * I/O 多路复用 - poll
 * 参数: fds - poll 结构数组; nfds - 数组大小; timeout_ms - 超时毫秒
 * 返回: 就绪的描述符数, -1 失败
 */
int funsos_poll(void *fds, uint32_t nfds, int timeout_ms);

/*
 * 将 IP 地址字符串转换为 funsos_ipv4_t
 * 参数: str - IP 地址字符串 (如 "192.168.1.1")
 * 返回: IPv4 地址结构
 */
funsos_ipv4_t funsos_inet_addr(const char *str);

/*
 * 将端口号转换为网络字节序
 * 参数: port - 主机字节序端口号
 * 返回: 网络字节序端口号
 */
uint16_t funsos_htons(uint16_t port);

#endif /* FUNSOS_NETWORK_H */
