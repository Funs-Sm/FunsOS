#ifndef STACKTRACE_H
#define STACKTRACE_H

#include "stdint.h"
#include "kernel_types.h"

/* ============================================================
 * Stack Trace 堆栈跟踪子系统
 *
 * 提供内核栈回溯能力，用于调试和错误分析。
 * 支持帧指针回溯和符号解析。
 * ============================================================ */

#define STACKTRACE_MAX_DEPTH  64
#define STACKTRACE_SYMBOL_MAX 128

/* 堆栈帧结构 */
typedef struct stack_frame {
    struct stack_frame *next;
    uint32_t return_addr;
} stack_frame_t;

/* 堆栈跟踪条目 */
typedef struct {
    uint32_t address;                /* 返回地址 */
    char     symbol[STACKTRACE_SYMBOL_MAX]; /* 符号名 */
    uint32_t offset;                 /* 相对于符号的偏移 */
} stacktrace_entry_t;

/* 堆栈跟踪结果 */
typedef struct {
    int    depth;                    /* 回溯深度 */
    stacktrace_entry_t entries[STACKTRACE_MAX_DEPTH];
} stacktrace_t;

/* ============================================================
 * 核心 API
 * ============================================================ */

/*
 * dump_stack - 打印当前内核栈回溯
 * 直接输出到内核日志
 */
void dump_stack(void);

/*
 * stacktrace_save - 保存当前堆栈跟踪
 * trace: 输出跟踪结果
 * max_depth: 最大深度
 * 返回: 实际深度, 负数错误码
 */
int stacktrace_save(stacktrace_t *trace, int max_depth);

/*
 * stacktrace_save_regs - 从寄存器上下文保存堆栈跟踪
 * regs: 寄存器上下文
 * trace: 输出跟踪结果
 * max_depth: 最大深度
 * 返回: 实际深度, 负数错误码
 */
int stacktrace_save_regs(regs_t *regs, stacktrace_t *trace, int max_depth);

/*
 * stacktrace_print - 打印堆栈跟踪
 * trace: 跟踪结果
 */
void stacktrace_print(stacktrace_t *trace);

/*
 * stacktrace_print_from - 从指定地址打印回溯
 * fp: 帧指针
 */
void stacktrace_print_from(uint32_t fp);

/* ============================================================
 * 符号解析
 * ============================================================ */

/*
 * stacktrace_resolve_symbol - 解析地址对应的符号
 * addr: 地址
 * name: 输出符号名缓冲区
 * name_size: 缓冲区大小
 * offset: 输出相对于符号的偏移
 * 返回: 0 成功, 负数错误码
 */
int stacktrace_resolve_symbol(uint32_t addr, char *name,
                              int name_size, uint32_t *offset);

#endif /* STACKTRACE_H */
