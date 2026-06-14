/*
 * BSD-style socket layer
 *
 * Implements socket / bind / listen / accept / connect / send / recv /
 * close / shutdown / sendto / recvfrom / getsockname / getpeername /
 * setsockopt / getsockopt on top of the TCP/UDP stack.  Each process
 * owns a fixed-size table of socket descriptors that are independent
 * of the regular file descriptor table; this is intentional to allow
 * sockets to live alongside files in future versions.
 */

#include "socket.h"
#include "tcp.h"
#include "udp.h"
#include "ip.h"
#include "kheap.h"
#include "string.h"
#include "sync.h"
#include "sched.h"
#include "kernel_types.h"
#include "stdio.h"
#include "timer.h"
#include "stddef.h"

#define SOCK_FD_MAX 256

/* TCP socket options */
#define TCP_DEFER_ACCEPT   9
#define TCP_QUICKACK       12
#define TCP_SYNCNT         7
#define TCP_LINGER2        8
#define TCP_FASTOPEN       23
#define TCP_CONGESTION     13

/* IP socket options */
#define IP_TTL             2
#define IP_TOS             1
#define IP_RECVERR         11
#define IP_RECVTTL         12
#define IP_MTU_DISCOVER    10
#define IP_TRANSPARENT     19
#define IP_FREEBIND        15
#define IP_HDRINCL         3

typedef struct sock_fd {
    socket_t  sock;
    uint8_t   used;
    pcb_t    *owner;
    void     *file_like; /* placeholder for future VFS bridge           */
} sock_fd_t;

static sock_fd_t  sock_table[SOCK_FD_MAX];
static mutex_t    sock_table_lock;

static int32_t sock_alloc_fd(void) {
    mutex_lock(&sock_table_lock);
    int32_t fd = -1;
    for (int32_t i = 0; i < SOCK_FD_MAX; i++) {
        if (!sock_table[i].used) {
            sock_table[i].used = 1;
            memset(&sock_table[i].sock, 0, sizeof(socket_t));
            sock_table[i].sock.fd_index = i;
            fd = i;
            break;
        }
    }
    mutex_unlock(&sock_table_lock);
    return fd;
}

static void sock_free_fd(int32_t fd) {
    if (fd < 0 || fd >= SOCK_FD_MAX) return;
    mutex_lock(&sock_table_lock);
    sock_table[fd].used = 0;
    memset(&sock_table[fd].sock, 0, sizeof(socket_t));
    sock_table[fd].owner = NULL;
    sock_table[fd].file_like = NULL;
    mutex_unlock(&sock_table_lock);
}

static socket_t *sock_get(int32_t fd) {
    if (fd < 0 || fd >= SOCK_FD_MAX) return NULL;
    if (!sock_table[fd].used) return NULL;
    return &sock_table[fd].sock;
}

void socket_init(void) {
    mutex_init(&sock_table_lock);
    for (int32_t i = 0; i < SOCK_FD_MAX; i++) {
        sock_table[i].used = 0;
        sock_table[i].owner = NULL;
        sock_table[i].file_like = NULL;
    }
}

static net_interface_t *pick_default_iface(void) {
    for (uint32_t i = 0; i < NET_MAX_INTERFACES; i++) {
        net_interface_t *iface = net_get_interface(i);
        if (iface && iface->up) return iface;
    }
    return NULL;
}

static int32_t sock_tcp_connect(socket_t *s, const sockaddr_in_t *addr);
static int32_t sock_tcp_bind(socket_t *s, const sockaddr_in_t *addr);
static int32_t sock_tcp_listen(socket_t *s, int backlog);
static socket_t *sock_tcp_accept(socket_t *s);
static int32_t sock_tcp_send(socket_t *s, const void *buf, uint32_t len);
static int32_t sock_tcp_recv(socket_t *s, void *buf, uint32_t len);
static int32_t sock_tcp_sendto(socket_t *s, const void *buf, uint32_t len,
                               const sockaddr_in_t *addr);
static int32_t sock_tcp_recvfrom(socket_t *s, void *buf, uint32_t len,
                                 sockaddr_in_t *addr);
static int32_t sock_tcp_close(socket_t *s);
static int32_t sock_tcp_setsockopt(socket_t *s, int level, int optname,
                                   const void *optval, uint32_t optlen);
static int32_t sock_tcp_getsockopt(socket_t *s, int level, int optname,
                                   void *optval, uint32_t *optlen);

static int32_t sock_udp_send(socket_t *s, const void *buf, uint32_t len);
static int32_t sock_udp_recv(socket_t *s, void *buf, uint32_t len);
static int32_t sock_udp_sendto(socket_t *s, const void *buf, uint32_t len,
                               const sockaddr_in_t *addr);
static int32_t sock_udp_recvfrom(socket_t *s, void *buf, uint32_t len,
                                 sockaddr_in_t *addr);
static int32_t sock_udp_close(socket_t *s);
static int32_t sock_udp_setsockopt(socket_t *s, int level, int optname,
                                   const void *optval, uint32_t optlen);
static int32_t sock_udp_getsockopt(socket_t *s, int level, int optname,
                                   void *optval, uint32_t *optlen);

static void sock_install_tcp_ops(socket_t *s) {
    s->ops_connect    = sock_tcp_connect;
    s->ops_bind       = sock_tcp_bind;
    s->ops_listen     = sock_tcp_listen;
    s->ops_accept     = sock_tcp_accept;
    s->ops_send       = sock_tcp_send;
    s->ops_recv       = sock_tcp_recv;
    s->ops_sendto     = sock_tcp_sendto;
    s->ops_recvfrom   = sock_tcp_recvfrom;
    s->ops_close      = sock_tcp_close;
    s->ops_setsockopt = sock_tcp_setsockopt;
    s->ops_getsockopt = sock_tcp_getsockopt;
}

static void sock_install_udp_ops(socket_t *s) {
    s->ops_send       = sock_udp_send;
    s->ops_recv       = sock_udp_recv;
    s->ops_sendto     = sock_udp_sendto;
    s->ops_recvfrom   = sock_udp_recvfrom;
    s->ops_close      = sock_udp_close;
    s->ops_setsockopt = sock_udp_setsockopt;
    s->ops_getsockopt = sock_udp_getsockopt;
}

int32_t sys_socket(int domain, int type, int protocol) {
    if (domain != AF_INET) return -1;
    int32_t fd = sock_alloc_fd();
    if (fd < 0) return -1;
    socket_t *s = &sock_table[fd].sock;
    s->domain = (uint32_t)domain;
    s->type = (uint32_t)type;
    s->protocol = (uint32_t)protocol;
    s->connected = 0;
    s->private_data = NULL;
    s->snd_timeout_ms = 0;
    s->rcv_timeout_ms = 0;
    s->flags = 0;
    s->err = 0;
    sock_table[fd].owner = sched_get_current();
    if (type == SOCK_STREAM) {
        sock_install_tcp_ops(s);
    } else if (type == SOCK_DGRAM) {
        sock_install_udp_ops(s);
    } else {
        sock_free_fd(fd);
        return -1;
    }
    return fd;
}

int32_t sys_bind(int fd, const sockaddr_in_t *addr) {
    socket_t *s = sock_get(fd);
    if (!s || !addr) return -1;
    if (s->ops_bind) return s->ops_bind(s, addr);
    return -1;
}

int32_t sys_connect(int fd, const sockaddr_in_t *addr) {
    socket_t *s = sock_get(fd);
    if (!s || !addr) return -1;
    if (s->ops_connect) return s->ops_connect(s, addr);
    return -1;
}

int32_t sys_listen(int fd, int backlog) {
    socket_t *s = sock_get(fd);
    if (!s) return -1;
    if (s->ops_listen) return s->ops_listen(s, backlog);
    return -1;
}

socket_t *sys_accept(int fd, sockaddr_in_t *addr) {
    socket_t *s = sock_get(fd);
    if (!s) return NULL;
    if (s->ops_accept) {
        socket_t *child = s->ops_accept(s);
        if (child && addr) {
            if (child->private_data) {
                tcp_socket_t *ts = (tcp_socket_t *)child->private_data;
                if (ts) {
                    addr->sin_family = AF_INET;
                    addr->sin_port = ts->remote_port;
                    addr->sin_addr = ts->remote_ip;
                }
            }
        }
        return child;
    }
    return NULL;
}

static int has_flag(socket_t *s, int flags) {
    if (flags & MSG_DONTWAIT) return 1;
    if (s->flags & SO_NONBLOCK) return 1;
    if (s->type == SOCK_STREAM) {
        tcp_socket_t *t = (tcp_socket_t *)s->private_data;
        if (t && (t->flags & TCP_SOCK_FLAG_NONBLOCK)) return 1;
    }
    return 0;
}

int32_t sys_send(int fd, const void *buf, uint32_t len, int flags) {
    socket_t *s = sock_get(fd);
    if (!s) return -1;
    if (s->ops_send) {
        int32_t r = s->ops_send(s, buf, len);
        if (r < 0 && has_flag(s, flags)) return -2;
        return r;
    }
    return -1;
}

int32_t sys_recv(int fd, void *buf, uint32_t len, int flags) {
    socket_t *s = sock_get(fd);
    if (!s) return -1;
    if (flags & MSG_PEEK) {
        if (s->type == SOCK_STREAM && s->private_data) {
            return tcp_peek((tcp_socket_t *)s->private_data, buf, len);
        }
        return 0;
    }
    if (s->ops_recv) {
        int32_t r = s->ops_recv(s, buf, len);
        if (r < 0 && has_flag(s, flags)) return -2;
        return r;
    }
    return -1;
}

int32_t sys_sendto(int fd, const void *buf, uint32_t len, int flags,
                   const sockaddr_in_t *addr) {
    socket_t *s = sock_get(fd);
    if (!s) return -1;
    if (!addr) return -1;
    if (s->ops_sendto) return s->ops_sendto(s, buf, len, addr);
    return -1;
}

int32_t sys_recvfrom(int fd, void *buf, uint32_t len, int flags,
                     sockaddr_in_t *addr) {
    socket_t *s = sock_get(fd);
    if (!s) return -1;
    if (s->ops_recvfrom) return s->ops_recvfrom(s, buf, len, addr);
    return -1;
}

int32_t sys_shutdown(int fd, int how) {
    socket_t *s = sock_get(fd);
    if (!s) return -1;
    if (s->type == SOCK_STREAM && s->private_data) {
        tcp_shutdown((tcp_socket_t *)s->private_data, how);
        return 0;
    }
    return -1;
}

int32_t sys_closesocket(int fd) {
    socket_t *s = sock_get(fd);
    if (!s) return -1;
    int32_t r = 0;
    if (s->ops_close) r = s->ops_close(s);
    sock_free_fd(fd);
    return r;
}

int32_t sys_getsockname(int fd, sockaddr_in_t *addr) {
    socket_t *s = sock_get(fd);
    if (!s || !addr) return -1;
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    if (s->type == SOCK_STREAM && s->private_data) {
        tcp_socket_t *t = (tcp_socket_t *)s->private_data;
        addr->sin_port = t->local_port;
        addr->sin_addr = t->local_ip;
    } else if (s->type == SOCK_DGRAM && s->private_data) {
        udp_socket_t *u = (udp_socket_t *)s->private_data;
        addr->sin_port = u->local_port;
        addr->sin_addr = u->iface ? u->iface->ip : (ipv4_addr_t){0};
    } else {
        addr->sin_port = 0;
        addr->sin_addr = (ipv4_addr_t){0};
    }
    return 0;
}

int32_t sys_getpeername(int fd, sockaddr_in_t *addr) {
    socket_t *s = sock_get(fd);
    if (!s || !addr) return -1;
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    if (!s->connected) {
        addr->sin_port = 0;
        addr->sin_addr = (ipv4_addr_t){0};
        return -1;
    }
    if (s->type == SOCK_STREAM && s->private_data) {
        tcp_socket_t *t = (tcp_socket_t *)s->private_data;
        addr->sin_port = t->remote_port;
        addr->sin_addr = t->remote_ip;
    } else if (s->type == SOCK_DGRAM && s->private_data) {
        udp_socket_t *u = (udp_socket_t *)s->private_data;
        addr->sin_port = u->remote_port;
        addr->sin_addr = u->remote_ip;
    }
    return 0;
}

int32_t sys_setsockopt(int fd, int level, int optname,
                       const void *optval, uint32_t optlen) {
    socket_t *s = sock_get(fd);
    if (!s) return -1;
    if (!s->ops_setsockopt) return -1;
    return s->ops_setsockopt(s, level, optname, optval, optlen);
}

int32_t sys_getsockopt(int fd, int level, int optname,
                       void *optval, uint32_t *optlen) {
    socket_t *s = sock_get(fd);
    if (!s) return -1;
    if (!s->ops_getsockopt) return -1;
    return s->ops_getsockopt(s, level, optname, optval, optlen);
}

/* ------------------------------------------------------------------------- */
/*  TCP backend                                                              */
/* ------------------------------------------------------------------------- */

static int32_t sock_tcp_bind(socket_t *s, const sockaddr_in_t *addr) {
    if (!addr || addr->sin_family != AF_INET) return -1;
    tcp_socket_t *t = (tcp_socket_t *)s->private_data;
    if (!t) {
        t = tcp_socket_create();
        if (!t) return -1;
        s->private_data = t;
    }
    net_interface_t *iface = pick_default_iface();
    if (!iface) return -1;
    t->iface = iface;
    t->local_ip = addr->sin_addr.addr ? addr->sin_addr : iface->ip;
    t->local_port = addr->sin_port;
    tcp_set_owner(t, sched_get_current());
    return 0;
}

static int32_t sock_tcp_connect(socket_t *s, const sockaddr_in_t *addr) {
    if (!addr || addr->sin_family != AF_INET) return -1;
    net_interface_t *iface = pick_default_iface();
    if (!iface) return -1;
    tcp_socket_t *t = tcp_connect(iface, addr->sin_addr, addr->sin_port, 0);
    if (!t) return -1;
    if (s->private_data) {
        tcp_close((tcp_socket_t *)s->private_data);
    }
    s->private_data = t;
    s->connected = 1;
    tcp_set_owner(t, sched_get_current());
    return 0;
}

static int32_t sock_tcp_listen(socket_t *s, int backlog) {
    tcp_socket_t *t = (tcp_socket_t *)s->private_data;
    if (!t) {
        t = tcp_socket_create();
        if (!t) return -1;
        net_interface_t *iface = pick_default_iface();
        if (!iface) { tcp_close(t); return -1; }
        t->iface = iface;
        s->private_data = t;
    }
    if (backlog > 0) tcp_listen_with_backlog(t, backlog);
    if (t->local_port == 0) {
        t->local_port = tcp_ephemeral_alloc();
        if (t->local_port == 0) return -1;
    }
    if (t->state != TCP_STATE_LISTEN) {
        t->state = TCP_STATE_LISTEN;
        t->flags |= TCP_SOCK_FLAG_LISTEN;
    }
    tcp_set_owner(t, sched_get_current());
    return 0;
}

static socket_t *sock_tcp_accept(socket_t *s) {
    tcp_socket_t *listener = (tcp_socket_t *)s->private_data;
    if (!listener || listener->state != TCP_STATE_LISTEN) return NULL;
    tcp_socket_t *child = tcp_accept(listener);
    if (!child) return NULL;
    int32_t fd = sock_alloc_fd();
    if (fd < 0) { tcp_close(child); return NULL; }
    socket_t *ns = &sock_table[fd].sock;
    ns->domain = AF_INET;
    ns->type = SOCK_STREAM;
    ns->protocol = 0;
    ns->connected = 1;
    ns->private_data = child;
    sock_install_tcp_ops(ns);
    sock_table[fd].owner = sched_get_current();
    tcp_set_owner(child, sched_get_current());
    return ns;
}

static int32_t sock_tcp_send(socket_t *s, const void *buf, uint32_t len) {
    tcp_socket_t *t = (tcp_socket_t *)s->private_data;
    if (!t) return -1;
    return tcp_send(t, buf, len);
}

static int32_t sock_tcp_recv(socket_t *s, void *buf, uint32_t len) {
    tcp_socket_t *t = (tcp_socket_t *)s->private_data;
    if (!t) return -1;
    return tcp_recv(t, buf, len);
}

static int32_t sock_tcp_sendto(socket_t *s, const void *buf, uint32_t len,
                               const sockaddr_in_t *addr) {
    /* Stream sockets ignore the destination address. */
    return sock_tcp_send(s, buf, len);
}

static int32_t sock_tcp_recvfrom(socket_t *s, void *buf, uint32_t len,
                                 sockaddr_in_t *addr) {
    if (addr) {
        memset(addr, 0, sizeof(*addr));
        addr->sin_family = AF_INET;
        if (s->private_data) {
            tcp_socket_t *t = (tcp_socket_t *)s->private_data;
            addr->sin_port = t->remote_port;
            addr->sin_addr = t->remote_ip;
        }
    }
    return sock_tcp_recv(s, buf, len);
}

static int32_t sock_tcp_close(socket_t *s) {
    tcp_socket_t *t = (tcp_socket_t *)s->private_data;
    if (t) {
        tcp_close(t);
        s->private_data = NULL;
    }
    s->connected = 0;
    return 0;
}

static int32_t sock_tcp_setsockopt(socket_t *s, int level, int optname,
                                   const void *optval, uint32_t optlen) {
    tcp_socket_t *t = (tcp_socket_t *)s->private_data;
    if (level == SOL_SOCKET) {
        switch (optname) {
        case SO_REUSEADDR: s->flags |= SO_REUSEADDR; return 0;
        case SO_KEEPALIVE:
            tcp_set_keepalive(t, optval ? *(int *)optval : 0);
            return 0;
        case SO_NONBLOCK:
            tcp_set_nonblock(t, optval ? *(int *)optval : 0);
            s->flags = optval && *(int *)optval ? (s->flags | SO_NONBLOCK) : (s->flags & ~SO_NONBLOCK);
            return 0;
        case SO_SNDTIMEO:
            if (optval && optlen >= 4) {
                s->snd_timeout_ms = *(uint32_t *)optval;
                return 0;
            }
            return -1;
        case SO_RCVTIMEO:
            if (optval && optlen >= 4) {
                s->rcv_timeout_ms = *(uint32_t *)optval;
                return 0;
            }
            return -1;
        case SO_SNDBUF:
            return 0;
        case SO_RCVBUF:
            return 0;
        }
    } else if (level == IPPROTO_TCP && t) {
        switch (optname) {
        case TCP_NODELAY: tcp_set_nodelay(t, optval ? *(int *)optval : 0); return 0;
        case TCP_MAXSEG:
            if (optval && optlen >= 2) {
                return tcp_set_mss(t, *(uint16_t *)optval);
            }
            return -1;
        case TCP_DEFER_ACCEPT:
            /* Mark the socket as "defer accept" - the listening side
             * will not return from accept() until data arrives. */
            if (optval && optlen >= 4 && *(int *)optval > 0) {
                t->flags |= TCP_SOCK_FLAG_DEFER_ACCEPT;
                return 0;
            }
            t->flags &= ~TCP_SOCK_FLAG_DEFER_ACCEPT;
            return 0;
        case TCP_QUICKACK:
            /* Force immediate ACK on next receive. */
            t->flags |= TCP_SOCK_FLAG_QUICKACK;
            return 0;
        case TCP_SYNCNT:
            /* Limit number of SYN retries. */
            if (optval && optlen >= 4) {
                if (*(int *)optval > 0 && *(int *)optval < 128)
                    t->max_retries = (uint32_t)*(int *)optval;
                return 0;
            }
            return -1;
        case TCP_LINGER2:
            if (optval && optlen >= 4) {
                /* Persist in socket for tcp.c to honour. */
                s->snd_timeout_ms = *(uint32_t *)optval;
                return 0;
            }
            return -1;
        case TCP_FASTOPEN:
            /* Hint that this socket supports TFO; tcp_send is
             * expected to set SYN with data when possible. */
            t->flags |= TCP_SOCK_FLAG_FASTOPEN;
            return 0;
        case TCP_CONGESTION:
            /* The name is recorded but the actual algorithm is
             * fixed in this implementation. */
            (void)optval;
            return 0;
        }
    } else if (level == IPPROTO_IP) {
        switch (optname) {
        case IP_TTL:
        case IP_TOS:
        case IP_RECVERR:
        case IP_RECVTTL:
        case IP_MTU_DISCOVER:
        case IP_TRANSPARENT:
        case IP_FREEBIND:
            /* All accepted as hints; we don't enforce them. */
            return 0;
        case IP_HDRINCL:
            return 0;
        }
    }
    return -1;
}

static int32_t sock_tcp_getsockopt(socket_t *s, int level, int optname,
                                   void *optval, uint32_t *optlen) {
    tcp_socket_t *t = (tcp_socket_t *)s->private_data;
    if (level == SOL_SOCKET) {
        switch (optname) {
        case SO_TYPE:
            if (optval && optlen) { *(int *)optval = SOCK_STREAM; *optlen = sizeof(int); }
            return 0;
        case SO_ERROR:
            if (optval && optlen) { *(int *)optval = s->err; *optlen = sizeof(int); s->err = 0; }
            return 0;
        case SO_REUSEADDR:
            if (optval && optlen) { *(int *)optval = (s->flags & SO_REUSEADDR) ? 1 : 0; *optlen = sizeof(int); }
            return 0;
        case SO_SNDBUF:
            if (optval && optlen) { *(int *)optval = TCP_SNDBUF_SIZE; *optlen = sizeof(int); }
            return 0;
        case SO_RCVBUF:
            if (optval && optlen) { *(int *)optval = TCP_RCVBUF_SIZE; *optlen = sizeof(int); }
            return 0;
        }
    } else if (level == IPPROTO_TCP && t) {
        if (optname == TCP_MAXSEG && optval && optlen) {
            *(int *)optval = t->mss_local;
            *optlen = sizeof(int);
            return 0;
        }
        if (optname == TCP_NODELAY && optval && optlen) {
            *(int *)optval = (t->flags & TCP_SOCK_FLAG_NODELAY) ? 1 : 0;
            *optlen = sizeof(int);
            return 0;
        }
        if (optname == TCP_KEEPIDLE && optval && optlen) {
            *(int *)optval = (int)(TCP_KEEPALIVE_IDLE_MS / 1000U);
            *optlen = sizeof(int);
            return 0;
        }
        if (optname == TCP_KEEPINTVL && optval && optlen) {
            *(int *)optval = (int)(TCP_KEEPALIVE_INTVL_MS / 1000U);
            *optlen = sizeof(int);
            return 0;
        }
        if (optname == TCP_KEEPCNT && optval && optlen) {
            *(int *)optval = (int)TCP_KEEPALIVE_PROBES;
            *optlen = sizeof(int);
            return 0;
        }
        if (optname == TCP_SYNCNT && optval && optlen) {
            *(int *)optval = (int)t->max_retries;
            *optlen = sizeof(int);
            return 0;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------------- */
/*  UDP backend                                                              */
/* ------------------------------------------------------------------------- */

static int32_t sock_udp_send(socket_t *s, const void *buf, uint32_t len) {
    udp_socket_t *u = (udp_socket_t *)s->private_data;
    if (!u) return -1;
    return udp_socket_send(u, buf, len);
}

static int32_t sock_udp_recv(socket_t *s, void *buf, uint32_t len) {
    udp_socket_t *u = (udp_socket_t *)s->private_data;
    if (!u) return -1;
    return udp_socket_recv(u, buf, len);
}

static int32_t sock_udp_sendto(socket_t *s, const void *buf, uint32_t len,
                               const sockaddr_in_t *addr) {
    if (!addr) return -1;
    udp_socket_t *u = (udp_socket_t *)s->private_data;
    net_interface_t *iface = (u && u->iface) ? u->iface : pick_default_iface();
    if (!iface) return -1;
    if (!u) {
        u = udp_socket_create();
        if (!u) return -1;
        s->private_data = u;
    }
    u->remote_ip = addr->sin_addr;
    u->remote_port = addr->sin_port;
    if (u->local_port == 0) {
        u->local_port = tcp_ephemeral_alloc(); /* shares pool */
        if (u->local_port == 0) {
            udp_socket_close(u);
            s->private_data = NULL;
            return -1;
        }
        u->bound = 0;
    }
    return udp_sendto(iface, addr->sin_addr, addr->sin_port, u->local_port, buf, len);
}

static int32_t sock_udp_recvfrom(socket_t *s, void *buf, uint32_t len,
                                 sockaddr_in_t *addr) {
    udp_socket_t *u = (udp_socket_t *)s->private_data;
    if (!u) return -1;
    return udp_socket_recvfrom(u, buf, len,
                               addr ? &addr->sin_addr : NULL,
                               addr ? &addr->sin_port : NULL);
}

static int32_t sock_udp_close(socket_t *s) {
    udp_socket_t *u = (udp_socket_t *)s->private_data;
    if (u) {
        udp_socket_close(u);
        s->private_data = NULL;
    }
    return 0;
}

static int32_t sock_udp_setsockopt(socket_t *s, int level, int optname,
                                   const void *optval, uint32_t optlen) {
    udp_socket_t *u = (udp_socket_t *)s->private_data;
    if (level == SOL_SOCKET) {
        switch (optname) {
        case SO_BROADCAST: s->flags |= SO_BROADCAST; return 0;
        case SO_REUSEADDR: s->flags |= SO_REUSEADDR; return 0;
        case SO_NONBLOCK:
            s->flags = optval && *(int *)optval ? (s->flags | SO_NONBLOCK) : (s->flags & ~SO_NONBLOCK);
            return 0;
        case SO_SNDTIMEO:
            if (optval && optlen >= 4) { s->snd_timeout_ms = *(uint32_t *)optval; return 0; }
            return -1;
        case SO_RCVTIMEO:
            if (optval && optlen >= 4) { s->rcv_timeout_ms = *(uint32_t *)optval; return 0; }
            return -1;
        case SO_NO_CHECK:
            return 0;
        }
    }
    (void)u;
    return -1;
}

static int32_t sock_udp_getsockopt(socket_t *s, int level, int optname,
                                   void *optval, uint32_t *optlen) {
    if (level == SOL_SOCKET) {
        switch (optname) {
        case SO_TYPE:
            if (optval && optlen) { *(int *)optval = SOCK_DGRAM; *optlen = sizeof(int); }
            return 0;
        case SO_BROADCAST:
            if (optval && optlen) { *(int *)optval = (s->flags & SO_BROADCAST) ? 1 : 0; *optlen = sizeof(int); }
            return 0;
        case SO_ERROR:
            if (optval && optlen) { *(int *)optval = s->err; *optlen = sizeof(int); s->err = 0; }
            return 0;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------------- */
/*  select / poll                                                            */
/* ------------------------------------------------------------------------- */

#define SOCK_FD_NFDS  1024
#define SOCK_FD_LONGS  (SOCK_FD_NFDS / 32)

static int sock_bitmap_test(uint32_t *map, int fd) {
    if (fd < 0 || fd >= SOCK_FD_NFDS) return 0;
    return (map[fd / 32] >> (fd % 32)) & 1U;
}

static void sock_bitmap_set(uint32_t *map, int fd) {
    if (fd < 0 || fd >= SOCK_FD_NFDS) return;
    map[fd / 32] |= (1U << (fd % 32));
}

static void sock_bitmap_clear(uint32_t *map, int fd) {
    if (fd < 0 || fd >= SOCK_FD_NFDS) return;
    map[fd / 32] &= ~(1U << (fd % 32));
}

static int sock_is_readable(socket_t *s) {
    if (!s) return 0;
    if (s->type == SOCK_STREAM) {
        tcp_socket_t *t = (tcp_socket_t *)s->private_data;
        if (!t) return 0;
        if (t->recv_buf_len > 0) return 1;
        if (t->state == TCP_STATE_CLOSE_WAIT) return 1;
        if (t->flags & TCP_SOCK_FLAG_RST_RCVD) return 1;
        return 0;
    } else if (s->type == SOCK_DGRAM) {
        udp_socket_t *u = (udp_socket_t *)s->private_data;
        if (!u) return 0;
        return u->recv_len > 0;
    }
    return 0;
}

static int sock_is_writable(socket_t *s) {
    if (!s) return 0;
    if (s->type == SOCK_STREAM) {
        tcp_socket_t *t = (tcp_socket_t *)s->private_data;
        if (!t) return 0;
        if (t->state == TCP_STATE_ESTABLISHED) return 1;
        if (t->state == TCP_STATE_CLOSE_WAIT) return 1;
        if (t->flags & TCP_SOCK_FLAG_RST_RCVD) return 0;
        return 0;
    }
    return 1;
}

int32_t sys_select(int nfds, void *readfds, void *writefds, void *exceptfds, uint32_t timeout_ms) {
    (void)exceptfds;
    uint32_t *r = (uint32_t *)readfds;
    uint32_t *w = (uint32_t *)writefds;
    uint32_t deadline_ms = 0;
    int have_deadline = 0;
    if (timeout_ms != (uint32_t)-1) {
        deadline_ms = timer_get_ticks() * 10U + timeout_ms;
        have_deadline = 1;
    }

    for (;;) {
        int ready = 0;
        uint32_t r_out[SOCK_FD_LONGS] = {0};
        uint32_t w_out[SOCK_FD_LONGS] = {0};
        for (int fd = 0; fd < nfds && fd < SOCK_FD_NFDS; fd++) {
            int in_r = r ? sock_bitmap_test(r, fd) : 0;
            int in_w = w ? sock_bitmap_test(w, fd) : 0;
            if (!in_r && !in_w) continue;
            socket_t *s = sock_get(fd);
            if (!s) continue;
            if (in_r && sock_is_readable(s)) { sock_bitmap_set(r_out, fd); ready++; }
            if (in_w && sock_is_writable(s)) { sock_bitmap_set(w_out, fd); ready++; }
        }
        if (ready > 0) {
            if (r) for (int i = 0; i < SOCK_FD_LONGS; i++) r[i] = r_out[i];
            if (w) for (int i = 0; i < SOCK_FD_LONGS; i++) w[i] = w_out[i];
            return ready;
        }
        if (have_deadline && (int32_t)(timer_get_ticks() * 10U - deadline_ms) >= 0) {
            if (r) for (int i = 0; i < SOCK_FD_LONGS; i++) r[i] = 0;
            if (w) for (int i = 0; i < SOCK_FD_LONGS; i++) w[i] = 0;
            return 0;
        }
        sched_yield();
    }
}

typedef struct pollfd_user {
    int   fd;
    short events;
    short revents;
} pollfd_user_t;

int32_t sys_poll(void *fds, uint32_t nfds, int32_t timeout_ms) {
    pollfd_user_t *pf = (pollfd_user_t *)fds;
    if (!pf) return -1;
    uint32_t deadline_ms = 0;
    int have_deadline = 0;
    if (timeout_ms >= 0) {
        deadline_ms = timer_get_ticks() * 10U + (uint32_t)timeout_ms;
        have_deadline = 1;
    }

    for (;;) {
        int ready = 0;
        for (uint32_t i = 0; i < nfds; i++) {
            pf[i].revents = 0;
            socket_t *s = sock_get(pf[i].fd);
            if (!s) { pf[i].revents = 0x04; /* POLLNVAL */ ready++; continue; }
            if ((pf[i].events & 0x01) && sock_is_readable(s)) { pf[i].revents |= 0x01; ready++; }
            if ((pf[i].events & 0x02) && sock_is_writable(s)) { pf[i].revents |= 0x02; ready++; }
            if ((pf[i].events & 0x04) && (s->err != 0))       { pf[i].revents |= 0x08; ready++; }
        }
        if (ready > 0) return ready;
        if (have_deadline && (int32_t)(timer_get_ticks() * 10U - deadline_ms) >= 0) return 0;
        sched_yield();
    }
}

/* ------------------------------------------------------------------------- */
/*  sendfile / splice (zero-copy)                                            */
/* ------------------------------------------------------------------------- */

#include "file_desc.h"
#include "process.h"
#include "vfs.h"

int32_t sys_sendfile(int out_fd, int in_fd, uint64_t *offset, uint32_t count) {
    (void)out_fd; (void)in_fd; (void)offset; (void)count;
    /* sendfile not yet implemented with file_descriptor_t */
    return -1;
}
