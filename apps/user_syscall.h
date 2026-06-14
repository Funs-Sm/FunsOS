#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

#include <stdint.h>
#include <stddef.h>

#define SYS_EXIT      1
#define SYS_FORK      2
#define SYS_READ      3
#define SYS_WRITE     4
#define SYS_OPEN      5
#define SYS_CLOSE     6
#define SYS_WAITPID   7
#define SYS_GETPID    8
#define SYS_EXEC      9
#define SYS_SLEEP     10
#define SYS_YIELD     11
#define SYS_PIPE      12
#define SYS_SIGNAL    13
#define SYS_KILL      14
#define SYS_MMAP      15
#define SYS_MUNMAP    16
#define SYS_IOCTL     17
#define SYS_READDIR   18
#define SYS_CHDIR     19
#define SYS_GETCWD    20
#define SYS_SOCKET        21
#define SYS_BIND          22
#define SYS_CONNECT       23
#define SYS_LISTEN        24
#define SYS_ACCEPT        25
#define SYS_SEND          26
#define SYS_RECV          27
#define SYS_SHUTDOWN      28
#define SYS_CLOSESOCK     29
#define SYS_SELECT        30
#define SYS_POLL          31
#define SYS_SENDTO        32
#define SYS_RECVFROM      33
#define SYS_GETSOCKNAME   34
#define SYS_GETPEERNAME   35
#define SYS_SETSOCKOPT    36
#define SYS_GETSOCKOPT    37
#define SYS_SENDFILE      38
#define SYS_GET_TICKS     140
#define SYS_MOUNT         42
#define SYS_EXECVE        43

#define O_RDONLY      0
#define O_WRONLY      1
#define O_RDWR        2
#define O_CREAT       0x100
#define O_TRUNC       0x200
#define O_APPEND      0x400
#define O_DIRECTORY   0x10000

#define SEEK_SET      0
#define SEEK_CUR      1
#define SEEK_END      2

static inline int syscall0(int num)
{
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num)
        : "memory"
    );
    return ret;
}

static inline int syscall1(int num, int a1)
{
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1)
        : "memory"
    );
    return ret;
}

static inline int syscall2(int num, int a1, int a2)
{
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2)
        : "memory"
    );
    return ret;
}

static inline int syscall3(int num, int a1, int a2, int a3)
{
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3)
        : "memory"
    );
    return ret;
}

static inline int syscall4(int num, int a1, int a2, int a3, int a4)
{
    int ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "b"(a1), "c"(a2), "d"(a3), "S"(a4)
        : "memory"
    );
    return ret;
}

static inline int syscall5(int num, int a1, int a2, int a3, int a4, int a5)
{
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

static inline void sys_exit(int status)
{
    syscall1(SYS_EXIT, status);
    __builtin_unreachable();
}

static inline int sys_fork(void)
{
    return syscall0(SYS_FORK);
}

static inline int sys_read(int fd, void *buf, size_t count)
{
    return syscall3(SYS_READ, fd, (int)buf, (int)count);
}

static inline int sys_write(int fd, const void *buf, size_t count)
{
    return syscall3(SYS_WRITE, fd, (int)buf, (int)count);
}

static inline int sys_open(const char *path, int flags)
{
    return syscall2(SYS_OPEN, (int)path, flags);
}

static inline int sys_close(int fd)
{
    return syscall1(SYS_CLOSE, fd);
}

static inline int sys_waitpid(int pid, int *status)
{
    return syscall2(SYS_WAITPID, pid, (int)status);
}

static inline int sys_getpid(void)
{
    return syscall0(SYS_GETPID);
}

static inline int sys_exec(const char *path, char *argv[])
{
    return syscall2(SYS_EXEC, (int)path, (int)argv);
}

static inline int sys_execve(const char *path, char *argv[], char *envp[])
{
    return syscall3(SYS_EXECVE, (int)path, (int)argv, (int)envp);
}

static inline int sys_mount(const char *path, const char *fs_type, void *data)
{
    return syscall3(SYS_MOUNT, (int)path, (int)fs_type, (int)data);
}

static inline int sys_sleep(unsigned int sec)
{
    return syscall1(SYS_SLEEP, (int)sec);
}

static inline int sys_yield(void)
{
    return syscall0(SYS_YIELD);
}

static inline int sys_pipe(int fd[2])
{
    return syscall1(SYS_PIPE, (int)fd);
}

static inline int sys_signal(int sig, void (*handler)(int))
{
    return syscall2(SYS_SIGNAL, sig, (int)handler);
}

static inline int sys_kill(int pid, int sig)
{
    return syscall2(SYS_KILL, pid, sig);
}

static inline int sys_mmap(void *addr, size_t len, int prot, int flags)
{
    return syscall4(SYS_MMAP, (int)addr, (int)len, prot, flags);
}

static inline int sys_munmap(void *addr, size_t len)
{
    return syscall2(SYS_MUNMAP, (int)addr, (int)len);
}

static inline int sys_ioctl(int fd, int cmd, void *arg)
{
    return syscall3(SYS_IOCTL, fd, cmd, (int)arg);
}

static inline int sys_readdir(int fd, void *buf, size_t count)
{
    return syscall3(SYS_READDIR, fd, (int)buf, (int)count);
}

static inline int sys_chdir(const char *path)
{
    return syscall1(SYS_CHDIR, (int)path);
}

static inline int sys_getcwd(char *buf, size_t size)
{
    return syscall2(SYS_GETCWD, (int)buf, (int)size);
}

static inline int sys_socket(int domain, int type, int protocol)
{
    return syscall3(SYS_SOCKET, domain, type, protocol);
}

static inline int sys_bind(int fd, const void *addr)
{
    return syscall2(SYS_BIND, fd, (int)addr);
}

static inline int sys_connect(int fd, const void *addr)
{
    return syscall2(SYS_CONNECT, fd, (int)addr);
}

static inline int sys_listen(int fd, int backlog)
{
    return syscall2(SYS_LISTEN, fd, backlog);
}

static inline int sys_accept(int fd, void *addr)
{
    return syscall2(SYS_ACCEPT, fd, (int)addr);
}

static inline int sys_send(int fd, const void *buf, size_t len, int flags)
{
    return syscall4(SYS_SEND, fd, (int)buf, (int)len, flags);
}

static inline int sys_recv(int fd, void *buf, size_t len, int flags)
{
    return syscall4(SYS_RECV, fd, (int)buf, (int)len, flags);
}

static inline int sys_shutdown(int fd, int how)
{
    return syscall2(SYS_SHUTDOWN, fd, how);
}

static inline int sys_closesocket(int fd)
{
    return syscall1(SYS_CLOSESOCK, fd);
}

static inline int sys_select(int nfds, void *rfds, void *wfds, void *efds, unsigned int timeout)
{
    return syscall5(SYS_SELECT, nfds, (int)rfds, (int)wfds, (int)efds, (int)timeout);
}

static inline int sys_poll(void *fds, unsigned int nfds, int timeout)
{
    return syscall3(SYS_POLL, (int)fds, (int)nfds, timeout);
}

static inline int sys_sendto(int fd, const void *buf, size_t len, int flags, const void *addr)
{
    return syscall5(SYS_SENDTO, fd, (int)buf, (int)len, flags, (int)addr);
}

static inline int sys_recvfrom(int fd, void *buf, size_t len, int flags, void *addr)
{
    return syscall5(SYS_RECVFROM, fd, (int)buf, (int)len, flags, (int)addr);
}

static inline int sys_getsockname(int fd, void *addr)
{
    return syscall2(SYS_GETSOCKNAME, fd, (int)addr);
}

static inline int sys_getpeername(int fd, void *addr)
{
    return syscall2(SYS_GETPEERNAME, fd, (int)addr);
}

static inline int sys_setsockopt(int fd, int level, int optname, const void *optval, unsigned int optlen)
{
    return syscall5(SYS_SETSOCKOPT, fd, level, optname, (int)optval, (int)optlen);
}

static inline int sys_getsockopt(int fd, int level, int optname, void *optval, unsigned int *optlen)
{
    return syscall5(SYS_GETSOCKOPT, fd, level, optname, (int)optval, (int)optlen);
}

static inline int sys_sendfile(int out_fd, int in_fd, unsigned long long *offset, unsigned int count)
{
    return syscall4(SYS_SENDFILE, out_fd, in_fd, (int)offset, (int)count);
}

static inline uint32_t sys_get_ticks(void)
{
    return (uint32_t)syscall0(SYS_GET_TICKS);
}

#endif
