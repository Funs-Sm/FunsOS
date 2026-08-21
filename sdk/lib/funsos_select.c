/*
 * FUNSOS I/O 多路复用 API 实现
 * ==============================
 * 封装内核 select/poll/epoll 系统调用，提供 I/O 多路复用功能。
 *
 */

#include "funsos.h"
#include "funsos_select.h"
#include "stddef.h"
#include "string.h"

/* ---- 系统调用号 ---- */
#define SYS_SELECT    30
#define SYS_POLL      31
#define SYS_EPOLL_CREATE  340
#define SYS_EPOLL_CTL     341
#define SYS_EPOLL_WAIT    342

/* 系统调用包装 */
static inline int syscall0(int num) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num)
        : "memory"
    );
    return ret;
}

static inline int syscall1(int num, int a1) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1)
        : "memory"
    );
    return ret;
}

static inline int syscall2(int num, int a1, int a2) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2)
        : "memory"
    );
    return ret;
}

static inline int syscall3(int num, int a1, int a2, int a3) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3)
        : "memory"
    );
    return ret;
}

static inline int syscall4(int num, int a1, int a2, int a3, int a4) {
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4)
        : "memory"
    );
    return ret;
}

static inline int syscall5(int num, int a1, int a2, int a3, int a4, int a5) {
    int ret;
    __asm__ volatile (
        "push %%ebp\n"
        "mov %7, %%ebp\n"
        "int $0x80\n"
        "pop %%ebp\n"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4), "D"(a5), "m"(a5)
        : "memory"
    );
    return ret;
}

/* ---- fd_set 操作 ---- */

/*
 * 清空 fd_set
 */
void funsos_FD_ZERO(funsos_fd_set *set)
{
    if (set == NULL) return;
    for (int i = 0; i < FUNSOS_FD_SETSIZE / 32; i++) {
        set->fds_bits[i] = 0;
    }
}

/*
 * 设置 fd
 */
void funsos_FD_SET(int fd, funsos_fd_set *set)
{
    if (set == NULL || fd < 0 || fd >= FUNSOS_FD_SETSIZE) return;
    set->fds_bits[fd / 32] |= (1U << (fd % 32));
}

/*
 * 清除 fd
 */
void funsos_FD_CLR(int fd, funsos_fd_set *set)
{
    if (set == NULL || fd < 0 || fd >= FUNSOS_FD_SETSIZE) return;
    set->fds_bits[fd / 32] &= ~(1U << (fd % 32));
}

/*
 * 检查 fd 是否设置
 */
int funsos_FD_ISSET(int fd, funsos_fd_set *set)
{
    if (set == NULL || fd < 0 || fd >= FUNSOS_FD_SETSIZE) return 0;
    return (set->fds_bits[fd / 32] & (1U << (fd % 32))) ? 1 : 0;
}

/* ---- select ---- */

/*
 * select I/O 多路复用
 */
int funsos_select(int nfds, funsos_fd_set *readfds,
                  funsos_fd_set *writefds,
                  funsos_fd_set *exceptfds,
                  funsos_timeval_t *timeout)
{
    return syscall5(SYS_SELECT, nfds, (int)readfds, (int)writefds,
                    (int)exceptfds, (int)timeout);
}

/* ---- poll ---- */

/*
 * poll I/O 多路复用
 */
int funsos_poll(funsos_pollfd_t *fds, uint32_t nfds, int timeout)
{
    return syscall3(SYS_POLL, (int)fds, (int)nfds, timeout);
}

/* ---- epoll ---- */

/*
 * 创建 epoll 实例
 */
int funsos_epoll_create(int size)
{
    return syscall1(SYS_EPOLL_CREATE, size);
}

/*
 * 创建 epoll 实例（带标志）
 */
int funsos_epoll_create1(int flags)
{
    (void)flags;
    return syscall1(SYS_EPOLL_CREATE, 10);
}

/*
 * 控制 epoll 实例
 */
int funsos_epoll_ctl(int epfd, int op, int fd,
                    funsos_epoll_event_t *event)
{
    return syscall4(SYS_EPOLL_CTL, epfd, op, fd, (int)event);
}

/*
 * 等待 epoll 事件
 */
int funsos_epoll_wait(int epfd, funsos_epoll_event_t *events,
                      int maxevents, int timeout)
{
    return syscall4(SYS_EPOLL_WAIT, epfd, (int)events, maxevents, timeout);
}
