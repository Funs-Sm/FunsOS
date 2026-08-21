#ifndef FUNSOS_MMAP_H
#define FUNSOS_MMAP_H

/*
 * FUNSOS 内存映射 API
 * 提供内存映射、内存保护、内存同步等功能。
 * 基于 kernel/mmap.h 和 kernel/vmm.h 的系统调用封装。
 */

#include "stdint.h"

/* ---- 内存保护标志 ---- */
#define FUNSOS_PROT_NONE      0x00   /* 页不可访问 */
#define FUNSOS_PROT_READ      0x01   /* 页可读 */
#define FUNSOS_PROT_WRITE     0x02   /* 页可写 */
#define FUNSOS_PROT_EXEC      0x04   /* 页可执行 */
#define FUNSOS_PROT_READ_WRITE (FUNSOS_PROT_READ | FUNSOS_PROT_WRITE)
#define FUNSOS_PROT_ALL       (FUNSOS_PROT_READ | FUNSOS_PROT_WRITE | FUNSOS_PROT_EXEC)

/* ---- 映射标志 ---- */
#define FUNSOS_MAP_SHARED     0x0001   /* 共享映射 */
#define FUNSOS_MAP_PRIVATE    0x0002   /* 私有映射（写时复制） */
#define FUNSOS_MAP_FIXED      0x0010   /* 固定地址映射 */
#define FUNSOS_MAP_ANONYMOUS  0x0020   /* 匿名映射（不关联文件） */
#define FUNSOS_MAP_GROWSDOWN  0x0100   /* 栈式向下增长 */
#define FUNSOS_MAP_LOCKED     0x0200   /* 锁定内存页 */
#define FUNSOS_MAP_NORESERVE 0x0400   /* 不预留交换空间 */
#define FUNSOS_MAP_POPULATE   0x0800   /* 预分配页表 */
#define FUNSOS_MAP_NONBLOCK  0x1000   /* 非阻塞映射 */
#define FUNSOS_MAP_STACK      0x2000   /* 栈映射 */

/* ---- 映射失败返回值 ---- */
#define FUNSOS_MAP_FAILED    ((void *)-1)

/* ---- msync 标志 ---- */
#define FUNSOS_MS_ASYNC        0x01   /* 异步同步 */
#define FUNSOS_MS_SYNC        0x02   /* 同步同步 */
#define FUNSOS_MS_INVALIDATE  0x04   /* 使缓存失效 */

/* ---- mlock 相关 ---- */
#define FUNSOS_MCL_CURRENT   0x01   /* 锁定当前页 */
#define FUNSOS_MCL_FUTURE    0x02   /* 锁定将来的页 */

/* ---- madvise 建议 ---- */
#define FUNSOS_MADV_NORMAL     0   /* 默认行为 */
#define FUNSOS_MADV_RANDOM     1   /* 随机访问预期 */
#define FUNSOS_MADV_SEQUENTIAL 2   /* 顺序访问预期 */
#define FUNSOS_MADV_WILLNEED   3   /* 预计很快会访问 */
#define FUNSOS_MADV_DONTNEED   4   /* 预计不会访问 */
#define FUNSOS_MADV_FREE       5   /* 释放页 */
#define FUNSOS_MADV_DONTFORK   6   /* 不被子进程继承 */
#define FUNSOS_MADV_DOFORK     7   /* 被子进程继承 */
#define FUNSOS_MADV_HUGEPAGE    8   /* 使用大页 */
#define FUNSOS_MADV_NOHUGEPAGE  9   /* 不使用大页 */

/*
 * 创建内存映射
 * 参数: addr - 建议的映射地址 (NULL=内核选择); length - 映射长度
 *       prot - 保护标志; flags - 映射标志
 *       fd - 文件描述符 (MAP_ANONYMOUS 时为 -1); offset - 文件偏移
 * 返回: 映射后的地址, MAP_FAILED 失败
 */
void *funsos_mmap(void *addr, uint32_t length, int prot, int flags, 
                  int fd, uint32_t offset);

/*
 * 取消内存映射
 * 参数: addr - 映射起始地址; length - 映射长度
 * 返回: 0 成功, -1 失败
 */
int funsos_munmap(void *addr, uint32_t length);

/*
 * 改变内存保护
 * 参数: addr - 起始地址; length - 长度; prot - 新的保护标志
 * 返回: 0 成功, -1 失败
 */
int funsos_mprotect(void *addr, uint32_t length, int prot);

/*
 * 同步内存映射到文件
 * 参数: addr - 起始地址; length - 长度; flags - 同步标志
 * 返回: 0 成功, -1 失败
 */
int funsos_msync(void *addr, uint32_t length, int flags);

/*
 * 内存映射调整大小
 * 参数: old_address - 旧地址; old_size - 旧大小
 *       new_size - 新大小; flags - 标志
 * 返回: 新的映射地址, MAP_FAILED 失败
 */
void *funsos_mremap(void *old_address, uint32_t old_size,
                    uint32_t new_size, int flags);

/*
 * 锁定内存
 * 参数: addr - 起始地址; length - 长度
 * 返回: 0 成功, -1 失败
 */
int funsos_mlock(const void *addr, uint32_t length);

/*
 * 解锁内存
 * 参数: addr - 起始地址; length - 长度
 * 返回: 0 成功, -1 失败
 */
int funsos_munlock(const void *addr, uint32_t length);

/*
 * 锁定全部内存
 * 参数: flags - 标志 (MCL_CURRENT / MCL_FUTURE)
 * 返回: 0 成功, -1 失败
 */
int funsos_mlockall(int flags);

/*
 * 解锁全部内存
 * 返回: 0 成功, -1 失败
 */
int funsos_munlockall(void);

/*
 * 内存使用建议
 * 参数: addr - 起始地址; length - 长度; advice - 建议
 * 返回: 0 成功, -1 失败
 */
int funsos_madvise(void *addr, uint32_t length, int advice);

/* ---- 共享内存 ---- */

/*
 * 打开/创建共享内存对象
 * 参数: name - 共享内存名称; oflag - 打开标志; mode - 权限模式
 * 返回: 文件描述符, -1 失败
 */
int funsos_shm_open(const char *name, int oflag, uint32_t mode);

/*
 * 删除共享内存对象
 * 参数: name - 共享内存名称
 * 返回: 0 成功, -1 失败
 */
int funsos_shm_unlink(const char *name);

/*
 * POSIX 信号量（共享内存同步用）
 *
 * 注意：完整信号量API在 funsos_thread.h 中
 */

#endif /* FUNSOS_MMAP_H */
