#ifndef FUNSOS_MEMORY_H
#define FUNSOS_MEMORY_H

/*
 * FUNSOS 内存管理 API
 * 提供动态内存分配和内存映射功能。
 * 基于 kernel/sys_api.h 和 apps/user_syscall.h 的内存管理调用。
 */

#include "stdint.h"

/* ---- 内存保护标志 ---- */
#define FUNSOS_PROT_NONE   0x00  /* 不可访问 */
#define FUNSOS_PROT_READ   0x01  /* 可读 */
#define FUNSOS_PROT_WRITE  0x02  /* 可写 */
#define FUNSOS_PROT_EXEC   0x04  /* 可执行 */

/* ---- 内存映射标志 ---- */
#define FUNSOS_MAP_SHARED  0x01  /* 共享映射 */
#define FUNSOS_MAP_PRIVATE 0x02  /* 私有映射 */
#define FUNSOS_MAP_FIXED   0x10  /* 固定地址映射 */
#define FUNSOS_MAP_ANONYMOUS 0x20 /* 匿名映射 */

/*
 * 分配内存
 * 参数: size - 分配字节数
 * 返回: 分配的内存指针, NULL 失败
 */
void *funsos_alloc(uint32_t size);

/*
 * 释放内存
 * 参数: ptr - 要释放的内存指针
 */
void funsos_free(void *ptr);

/*
 * 内存映射
 * 参数: addr - 建议的映射地址（NULL 由系统选择）;
 *       len - 映射长度; prot - 保护标志; flags - 映射标志
 * 返回: 映射的内存地址, (void*)-1 失败
 */
void *funsos_mmap(void *addr, uint32_t len, int prot, int flags);

/*
 * 取消内存映射
 * 参数: addr - 映射地址; len - 映射长度
 * 返回: 0 成功, -1 失败
 */
int funsos_munmap(void *addr, uint32_t len);

#endif /* FUNSOS_MEMORY_H */
