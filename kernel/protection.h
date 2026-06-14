#ifndef PROTECTION_H
#define PROTECTION_H

#include "stdint.h"

/* 保护系统初始化 */
void protection_init(void);

/* 堆溢出检测 */
int heap_check_integrity(void);

/* 内核栈溢出检测 */
int stack_check_guard(void);

/* 空指针解引用保护 */
int null_ptr_check(void *ptr);

/* 双重释放检测 */
int double_free_check(void *ptr);

/* 使用后释放检测 */
int use_after_free_check(void *ptr);

/* 整数溢出检测 */
int int_overflow_check(uint64_t a, uint64_t b, int is_add);

/* 保护状态报告 */
void protection_report(void);

/* ---- 堆块魔数标记 ---- */
#define HEAP_MAGIC_ALLOC  0xA110CA7E  /* 已分配块的魔数 */
#define HEAP_MAGIC_FREE   0xF2EEB10C  /* 已释放块的魔数 */

/* ---- 栈保护 Canary 值 ---- */
#define STACK_CANARY_VALUE 0xDEADBEEF

/* ---- 保护统计 ---- */
typedef struct {
    uint32_t heap_corruptions;     /* 堆溢出次数 */
    uint32_t stack_overflows;      /* 栈溢出次数 */
    uint32_t null_ptr_derefs;      /* 空指针解引用次数 */
    uint32_t double_frees;         /* 双重释放次数 */
    uint32_t use_after_frees;      /* 使用后释放次数 */
    uint32_t int_overflows;        /* 整数溢出次数 */
} protection_stats_t;

/* 获取保护统计 */
const protection_stats_t *protection_get_stats(void);

/* 注册/注销已释放的指针（供 kfree/kmalloc 调用） */
void protection_register_free(void *ptr, uint32_t size);
void protection_unregister_free(void *ptr);

/* 注册/注销栈 canary */
void protection_register_stack_canary(uint32_t *canary_addr);
void protection_unregister_stack_canary(uint32_t *canary_addr);

#endif
