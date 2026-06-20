#ifndef SOCKET_H
#define SOCKET_H

#include "stdint.h"
#include "net.h"

/* Address families */
#define AF_UNSPEC 0
#define AF_UNIX   1
#define AF_INET   2
#define AF_INET6  10

/* Socket types */
#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define SOCK_RAW    3

/* Protocol families (used in protocol field) */
#define IPPROTO_IP    0
#define IPPROTO_TCP   6
#define IPPROTO_UDP   17

/* setsockopt levels */
#define SOL_SOCKET   1
#define IPPROTO_TCP_LEVEL 6

/* setsockopt option names */
#define SO_REUSEADDR    0x0002
#define SO_KEEPALIVE    0x0008
#define SO_BROADCAST    0x0020
#define SO_SNDBUF       0x1001
#define SO_RCVBUF       0x1002
#define SO_SNDTIMEO     0x1003
#define SO_RCVTIMEO     0x1004
#define SO_ERROR        0x1007
#define SO_TYPE         0x1008
#define SO_NONBLOCK     0x1010
#define SO_NO_CHECK     0x400B
#define SO_LINGER       0x0013
#define SO_REUSEPORT    0x0200
#define SO_RCVLOWAT     0x1005
#define SO_SNDLOWAT     0x1006

/* IP level options */
#define IP_PKTINFO      8

/* TCP level options */
#define TCP_NODELAY     0x01
#define TCP_KEEPIDLE    0x03
#define TCP_KEEPINTVL   0x04
#define TCP_KEEPCNT     0x05
#define TCP_MAXSEG      0x02

/* send/recv flags */
#define MSG_DONTWAIT  0x40
#define MSG_PEEK      0x02
#define MSG_WAITALL   0x100
#define MSG_MORE      0x8000    /* More data to be sent (TCP)         */
#define MSG_CONFIRM   0x800     /* Confirm path is valid (routing)   */
#define MSG_TRUNC     0x20
#define MSG_ERRQUEUE  0x2000
#define MSG_NOSIGNAL  0x4000

/* shutdown how */
#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2

typedef struct {
    uint16_t sin_family;
    uint16_t sin_port;
    ipv4_addr_t sin_addr;
    char sin_zero[8];
} sockaddr_in_t;

typedef struct sockaddr {
    uint16_t sa_family;
    char     sa_data[14];
} sockaddr_t;

typedef struct socket {
    uint32_t domain;
    uint32_t type;
    uint32_t protocol;
    uint8_t  connected;
    int32_t  fd_index;
    uint32_t flags;            /* SO_REUSEADDR etc.            */
    int      err;               /* pending error                */
    uint32_t snd_timeout_ms;
    uint32_t rcv_timeout_ms;
    void    *private_data;
    int32_t (*ops_connect)(struct socket *, const sockaddr_in_t *);
    int32_t (*ops_bind)(struct socket *, const sockaddr_in_t *);
    int32_t (*ops_listen)(struct socket *, int);
    struct socket *(*ops_accept)(struct socket *);
    int32_t (*ops_send)(struct socket *, const void *, uint32_t);
    int32_t (*ops_recv)(struct socket *, void *, uint32_t);
    int32_t (*ops_sendto)(struct socket *, const void *, uint32_t,
                          const sockaddr_in_t *);
    int32_t (*ops_recvfrom)(struct socket *, void *, uint32_t,
                            sockaddr_in_t *);
    int32_t (*ops_close)(struct socket *);
    int32_t (*ops_setsockopt)(struct socket *, int, int, const void *, uint32_t);
    int32_t (*ops_getsockopt)(struct socket *, int, int, void *, uint32_t *);
} socket_t;

/* Socket错误队列 */
#define SOCK_ERRQ_MAX 8
typedef struct sock_err {
    int32_t  errno_val;
    uint8_t  pending;
} sock_err_t;

/* TCP连接池：允许已建立的连接被复用 */
#define CONN_POOL_MAX 64
typedef struct conn_pool_entry {
    ipv4_addr_t remote_ip;
    uint16_t    remote_port;
    uint16_t    local_port;
    int32_t     sock_fd;        /* 关联的socket fd, -1表示空闲 */
    uint32_t    last_used;      /* 最后使用时间(ticks) */
    uint8_t     in_use;
} conn_pool_entry_t;

/* 连接池管理函数 */
void   conn_pool_init(void);
int32_t conn_pool_lookup(ipv4_addr_t ip, uint16_t port);
int32_t conn_pool_register(int32_t fd, ipv4_addr_t ip, uint16_t rport, uint16_t lport);
void   conn_pool_release(int32_t fd);
void   conn_pool_cleanup(uint32_t timeout_ticks); /* 清理超时连接 */

/* Socket错误队列操作函数 */
void sock_errq_enqueue(int fd, int32_t errno_val);
int  sock_errq_dequeue(int fd, int32_t *errno_val);

void socket_init(void);
int32_t sys_socket(int domain, int type, int protocol);
int32_t sys_bind(int fd, const sockaddr_in_t *addr);
int32_t sys_connect(int fd, const sockaddr_in_t *addr);
int32_t sys_listen(int fd, int backlog);
socket_t *sys_accept(int fd, sockaddr_in_t *addr);
int32_t sys_send(int fd, const void *buf, uint32_t len, int flags);
int32_t sys_recv(int fd, void *buf, uint32_t len, int flags);
int32_t sys_sendto(int fd, const void *buf, uint32_t len, int flags,
                   const sockaddr_in_t *addr);
int32_t sys_recvfrom(int fd, void *buf, uint32_t len, int flags,
                     sockaddr_in_t *addr);
int32_t sys_shutdown(int fd, int how);
int32_t sys_closesocket(int fd);
int32_t sys_getsockname(int fd, sockaddr_in_t *addr);
int32_t sys_getpeername(int fd, sockaddr_in_t *addr);
int32_t sys_setsockopt(int fd, int level, int optname,
                       const void *optval, uint32_t optlen);
int32_t sys_getsockopt(int fd, int level, int optname,
                       void *optval, uint32_t *optlen);

/* select / poll helpers */
int32_t sys_select(int nfds, void *readfds, void *writefds, void *exceptfds, uint32_t timeout_ms);
int32_t sys_poll(void *fds, uint32_t nfds, int32_t timeout_ms);

/* Zero-copy sendfile(): copies the contents of an open file
 * (described by the FD) into the socket.  Returns the number of
 * bytes sent, -1 on error.  Only available on STREAM sockets. */
int32_t sys_sendfile(int out_fd, int in_fd, uint64_t *offset, uint32_t count);

#endif
