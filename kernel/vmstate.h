#ifndef VMSTATE_H
#define VMSTATE_H

#include "stdint.h"

/* VM State Save/Restore for QEMU/KVM pause/resume */

/* 保存的状态大小 (最大 64KB 用于寄存器+关键内存) */
#define VMSTATE_SAVE_SIZE  65536

/* 保存的结构 */
typedef struct {
    /* CPU 寄存器上下文 */
    uint32_t eax, ebx, ecx, edx, esi, edi, ebp;
    uint32_t eip, eflags;
    uint32_t esp, ss, cs, ds, es, fs, gs;

    /* 标志位 */
    uint32_t saved_ticks;        /* 定时器 tick 计数 */
    uint32_t saved_uptime;       /* 系统运行时间 */
    uint16_t saved_cursor_x, saved_cursor_y;  /* 光标位置 */

    /* 校验值 */
    uint32_t magic;              /* 0x564D5356 = "VMSV" */
    uint32_t checksum;           /* 简单校验 */
} vm_state_t;

void vmstate_init(void);
int  vmstate_save(void);   /* 保存状态到固定内存区域 */
int  vmstate_restore(void); /* 从固定内存区域恢复状态 */
void vmstate_suspend(void); /* 保存 + ACPI S3 入睡 */
const char *vmstate_status(void); /* 查询状态 */

#endif
