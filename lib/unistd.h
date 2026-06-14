#ifndef UNISTD_H
#define UNISTD_H

#include "stdint.h"
#include "stddef.h"

/* POSIX 类型定义 */
typedef int32_t pid_t;
typedef int32_t uid_t;
typedef int32_t gid_t;
typedef int32_t off_t;
typedef uint32_t mode_t;
typedef uint32_t useconds_t;

/* POSIX 系统调用号 */
#define SYS_EXIT    1
#define SYS_READ    3
#define SYS_WRITE   4
#define SYS_OPEN    5
#define SYS_CLOSE   6
#define SYS_WAITPID 7
#define SYS_CREAT   8
#define SYS_LINK    9
#define SYS_UNLINK  10
#define SYS_EXECVE  11
#define SYS_CHDIR   12
#define SYS_GETPID  20
#define SYS_GETUID  24
#define SYS_GETEUID 25
#define SYS_GETGID  26
#define SYS_GETEGID 27
#define SYS_KILL    37
#define SYS_MKDIR   39
#define SYS_RMDIR   40
#define SYS_DUP     41
#define SYS_DUP2    63
#define SYS_GETCWD  79
#define SYS_MMAP    90
#define SYS_MUNMAP  91
#define SYS_IOCTL   54
#define SYS_FORK    2
#define SYS_PIPE    42
#define SYS_MOUNT   21
#define SYS_UMOUNT  22
#define SYS_REBOOT  88
#define SYS_SYSINFO 116

/* 内联系统调用宏 */
static inline int syscall0(int nr) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(nr));
    return ret;
}
static inline int syscall1(int nr, int a) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(nr), "b"(a));
    return ret;
}
static inline int syscall2(int nr, int a, int b) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(nr), "b"(a), "c"(b));
    return ret;
}
static inline int syscall3(int nr, int a, int b, int c) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(nr), "b"(a), "c"(b), "d"(c));
    return ret;
}

/* POSIX 函数声明 */
pid_t fork(void);
int execve(const char *path, char *const argv[], char *const envp[]);
void _exit(int status);
pid_t getpid(void);
pid_t getppid(void);
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int setuid(uid_t uid);
int setgid(gid_t gid);

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int open(const char *path, int flags, ...);
int close(int fd);
int dup(int oldfd);
int dup2(int oldfd, int newfd);

int pipe(int pipefd[2]);
int chdir(const char *path);
char *getcwd(char *buf, size_t size);
int mkdir(const char *path, mode_t mode);
int rmdir(const char *path);
int unlink(const char *path);
int link(const char *oldpath, const char *newpath);
int creat(const char *path, mode_t mode);

off_t lseek(int fd, off_t offset, int whence);

int mount(const char *source, const char *target, const char *fs_type, unsigned long flags);
int umount(const char *target);

unsigned int sleep(unsigned int seconds);
unsigned int alarm(unsigned int seconds);
int usleep(useconds_t usec);

int isatty(int fd);

/* POSIX 路径常量 */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define O_RDONLY  0
#define O_WRONLY  1
#define O_RDWR    2
#define O_CREAT   0x40
#define O_TRUNC   0x200
#define O_APPEND  0x400

#endif
