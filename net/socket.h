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
