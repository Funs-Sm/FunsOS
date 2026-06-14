#include "vmstate.h"
#include "acpi_sleep.h"
#include "timer.h"
#include "string.h"
#include "stdio.h"
#include "kheap.h"

static vm_state_t *saved_state = 0;  /* 保存在kmalloc区域 */
static int state_valid = 0;

void vmstate_init(void) {
    saved_state = (vm_state_t *)kmalloc(sizeof(vm_state_t));
    if (saved_state) {
        memset(saved_state, 0, sizeof(vm_state_t));
        state_valid = 0;
    }
}

/* 计算校验和: 对结构体前 N-2 个 uint32 做旋转累加 (跳过 magic 和 checksum 字段) */
static uint32_t compute_checksum(vm_state_t *s) {
    uint32_t *p = (uint32_t *)s;
    uint32_t sum = 0;
    for (uint32_t i = 0; i < sizeof(vm_state_t) / 4 - 2; i++) {
        sum += p[i];
        sum = (sum << 1) | (sum >> 31);  /* rotate left */
    }
    return sum;
}

int vmstate_save(void) {
    if (!saved_state) return -1;

    /*
     * 保存内核级状态。
     * 注意: S3 睡眠由 ACPI 固件保存/恢复完整 CPU 寄存器上下文，
     *       所以我们不需要在内联汇编中逐个保存通用寄存器。
     *       这里只保存操作系统层面的关键状态，供唤醒后恢复使用。
     */

    /* 通过栈变量间接获取部分寄存器值 */
    uint32_t stack_ptr;
    asm volatile("movl %%esp, %0" : "=r"(stack_ptr));
    saved_state->esp = stack_ptr;

    /* eip/eflags 在运行中无法直接获取，S3 恢复时由固件处理 */
    saved_state->eip = 0;
    saved_state->eflags = 0;

    /* 段寄存器 */
    uint16_t ss_val, cs_val, ds_val;
    asm volatile("movw %%ss, %0" : "=r"(ss_val));
    asm volatile("movw %%cs, %0" : "=r"(cs_val));
    asm volatile("movw %%ds, %0" : "=r"(ds_val));
    saved_state->ss = ss_val;
    saved_state->cs = cs_val;
    saved_state->ds = ds_val;

    /* 其他通用寄存器设为0（S3固件负责恢复） */
    saved_state->eax = 0;
    saved_state->ebx = 0;
    saved_state->ecx = 0;
    saved_state->edx = 0;
    saved_state->esi = 0;
    saved_state->edi = 0;
    saved_state->ebp = 0;
    saved_state->es = 0;
    saved_state->fs = 0;
    saved_state->gs = 0;

    /* 保存定时器状态 */
    saved_state->saved_ticks = timer_get_ticks();
    saved_state->saved_uptime = timer_get_ticks() / 100;  /* 约 秒 */

    /* 光标位置: fb_console 的 cursor_x/cursor_y 是 static 的 */
    saved_state->saved_cursor_x = 0;
    saved_state->saved_cursor_y = 0;

    /* 写入魔数和校验值 */
    saved_state->magic = 0x564D5356;  /* "VMSV" */
    saved_state->checksum = compute_checksum(saved_state);
    state_valid = 1;

    return 0;
}

int vmstate_restore(void) {
    if (!saved_state || !state_valid) return -1;

    /* 验证校验和 */
    uint32_t ck = compute_checksum(saved_state);
    if (ck != saved_state->checksum) {
        return -2;  /* 校验失败 */
    }

    if (saved_state->magic != 0x564D5356) {
        return -3;  /* 魔数不匹配 */
    }

    /* 状态已验证，标记为已消耗 */
    state_valid = 0;
    return 0;
}

void vmstate_suspend(void) {
    /* 先保存状态 */
    if (vmstate_save() != 0) {
        printf("[vmstate] Failed to save state - no memory\n");
        return;
    }

    printf("[vmstate] Suspending VM... saving state\n");

    /* 刷缓存 */
    asm volatile("wbinvd");

    /* 禁中断 */
    asm volatile("cli");

    /* 调用 ACPI S3 睡眠 */
    acpi_enter_sleep(3);

    /* 如果从 S3 返回（被唤醒）*/
    asm volatile("sti");

    /* 恢复状态 */
    if (vmstate_restore() == 0) {
        printf("[vmstate] VM Resumed (ticks=%u)\n", saved_state->saved_ticks);
    } else {
        printf("[vmstate] Warning: state restore failed\n");
    }

    /* 重新初始化定时器 (S3 唤醒后 PIT 需要重新配置) */
    init_timer();
}

const char *vmstate_status(void) {
    if (state_valid && saved_state && saved_state->magic == 0x564D5356)
        return "saved";
    return "none";
}
