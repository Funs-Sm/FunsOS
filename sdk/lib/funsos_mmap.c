/*
 * FUNSOS 内存映射 API 实现
 * ========================
 * 封装内核内存管理系统调用，提供内存映射、保护、同步等功能。
 *
 */

#include "funsos.h"
#include "funsos_mmap.h"
#include "stddef.h"
#include "string.h"

/* ---- 系统调用号 ---- */
#define SYS_MMAP      15
#define SYS_MUNMAP    16
#define SYS_MPROTECT  330
#define SYS_MSYNC     331
#define SYS_MREMAP    332
#define SYS_MLOCK     333
#define SYS_MUNLOCK   334
#define SYS_MLOCKALL  335
#define SYS_MUNLOCKALL 336
#define SYS_MADVISE   337
#define SYS_SHM_OPEN  338
#define SYS_SHM_UNLINK 339

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

/*
 * 创建内存映射
 */
void *funsos_mmap(void *addr, uint32_t length, int prot, int flags,
                  int fd, uint32_t offset)
{
    return (void *)syscall5(SYS_MMAP, (int)addr, (int)length, prot, flags, fd);
}

/*
 * 取消内存映射
 */
int funsos_munmap(void *addr, uint32_t length)
{
    return syscall2(SYS_MUNMAP, (int)addr, (int)length);
}

/*
 * 改变内存保护
 */
int funsos_mprotect(void *addr, uint32_t length, int prot)
{
    return syscall3(SYS_MPROTECT, (int)addr, (int)length, prot);
}

/*
 * 同步内存映射到文件
 */
int funsos_msync(void *addr, uint32_t length, int flags)
{
    return syscall3(SYS_MSYNC, (int)addr, (int)length, flags);
}

/*
 * 内存映射调整大小
 */
void *funsos_mremap(void *old_address, uint32_t old_size,
                    uint32_t new_size, int flags)
{
    (void)flags;
    /* 简化实现：重新分配并复制 */
    void *new_addr = funsos_mmap(NULL, new_size, FUNSOS_PROT_READ | FUNSOS_PROT_WRITE,
                                 FUNSOS_MAP_PRIVATE | FUNSOS_MAP_ANONYMOUS, -1, 0);
    if (new_addr == FUNSOS_MAP_FAILED) return FUNSOS_MAP_FAILED;

    if (old_address != NULL && old_size > 0) {
        uint32_t copy_size = old_size < new_size ? old_size : new_size;
        uint8_t *src = (uint8_t *)old_address;
        uint8_t *dst = (uint8_t *)new_addr;
        for (uint32_t i = 0; i < copy_size; i++) {
            dst[i] = src[i];
        }
    }

    funsos_munmap(old_address, old_size);
    return new_addr;
}

/*
 * 锁定内存
 */
int funsos_mlock(const void *addr, uint32_t length)
{
    return syscall2(SYS_MLOCK, (int)addr, (int)length);
}

/*
 * 解锁内存
 */
int funsos_munlock(const void *addr, uint32_t length)
{
    return syscall2(SYS_MUNLOCK, (int)addr, (int)length);
}

/*
 * 锁定全部内存
 */
int funsos_mlockall(int flags)
{
    return syscall1(SYS_MLOCKALL, flags);
}

/*
 * 解锁全部内存
 */
int funsos_munlockall(void)
{
    return syscall0(SYS_MUNLOCKALL);
}

/*
 * 内存使用建议
 */
int funsos_madvise(void *addr, uint32_t length, int advice)
{
    (void)addr; (void)length; (void)advice;
    return 0;
}

/* ---- 共享内存 ---- */

/*
 * 打开/创建共享内存对象
 */
int funsos_shm_open(const char *name, int oflag, uint32_t mode)
{
    (void)name; (void)oflag; (void)mode;
    return -1;
}

/*
 * 删除共享内存对象
 */
int funsos_shm_unlink(const char *name)
{
    (void)name;
    return -1;
}
