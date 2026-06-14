#include "signal.h"
#include "process.h"
#include "sched.h"
#include "kheap.h"
#include "timer.h"
#include "stddef.h"
#include "string.h"

/* 信号帧结构 - 保存在用户栈上用于 sigreturn */
typedef struct {
    uint32_t return_addr;    /* sa_restorer 或 sigreturn 入口 */
    uint32_t sig;            /* 信号编号 */
    uint32_t old_eip;        /* 原始 EIP */
    uint32_t old_eflags;     /* 原始 EFLAGS */
    uint32_t old_eax;        /* 原始 EAX */
    uint32_t old_ecx;        /* 原始 ECX */
    uint32_t old_edx;        /* 原始 EDX */
    uint32_t sa_flags;       /* sigaction 标志 */
    uint32_t sa_mask;        /* 信号掩码 */
} signal_frame_t;

/* sigreturn 系统调用号 */
#define SYS_SIGRETURN 100

void signal_init(void) {
    /* 全局信号子系统初始化 - 目前无需额外操作 */
}

void signal_init_proc(pcb_t *proc) {
    for (int i = 0; i < NSIG; i++) {
        proc->signal_handlers[i] = SIG_DFL;
    }
    proc->signal_pending = 0;
    proc->signal_blocked = 0;
    proc->signal_sa_flags = 0;     /* 新增: sigaction 标志 */
    proc->signal_sa_mask = 0;      /* 新增: sigaction 掩码 */
    proc->alarm_ticks = 0;         /* 新增: alarm 定时器 */
}

int signal_send(pid_t pid, int sig) {
    if (sig < 1 || sig >= NSIG) return -1;

    pcb_t *target = process_get_pcb(pid);
    if (target == NULL) {
        return -1;
    }

    /* SIGKILL 和 SIGSTOP 不能被忽略或阻塞 */
    target->signal_pending |= (1 << (sig - 1));

    if (target->state == PROCESS_BLOCKED) {
        sched_unblock(target);
    }

    return 0;
}

int signal_check(pcb_t *proc) {
    for (int sig = 1; sig <= 31; sig++) {
        uint32_t bit = (1 << (sig - 1));
        if ((proc->signal_pending & bit) && !(proc->signal_blocked & bit)) {
            signal_deliver(proc, sig);
        }
    }
    return 0;
}

void signal_deliver(pcb_t *proc, int sig) {
    void (*handler)(int) = proc->signal_handlers[sig - 1];

    proc->signal_pending &= ~(1 << (sig - 1));

    if (handler == SIG_DFL) {
        /* 默认信号处理 */
        switch (sig) {
        case SIGKILL:
            proc->state = PROCESS_ZOMBIE;
            break;
        case SIGSTOP:
        case SIGTSTP:
        case SIGTTIN:
        case SIGTTOU:
            proc->state = PROCESS_BLOCKED;
            break;
        case SIGCHLD:
        case SIGURG:
        case SIGWINCH:
        case SIGIO:
            /* 这些信号默认忽略 */
            break;
        case SIGCONT:
            if (proc->state == PROCESS_BLOCKED) {
                sched_unblock(proc);
            }
            break;
        default:
            /* 其他信号默认终止进程 */
            proc->state = PROCESS_ZOMBIE;
            break;
        }
        return;
    }

    if (handler == SIG_IGN) {
        return;
    }

    /* 在用户栈上构建信号帧用于 sigreturn */
    signal_frame_t frame;
    frame.sig = (uint32_t)sig;
    frame.old_eip = proc->context.eip;
    frame.old_eflags = proc->context.eflags;
    frame.old_eax = proc->context.eax;
    frame.old_ecx = proc->context.ecx;
    frame.old_edx = proc->context.edx;
    frame.sa_flags = proc->signal_sa_flags;
    frame.sa_mask = proc->signal_sa_mask;
    frame.return_addr = 0; /* 由 sa_restorer 设置 */

    /* 将信号帧压入用户栈 */
    proc->context.useresp -= sizeof(signal_frame_t);
    uint32_t frame_addr = proc->context.useresp;

    /* 复制信号帧到用户栈 (使用内核虚拟地址映射) */
    memcpy((void *)(uintptr_t)frame_addr, &frame, sizeof(signal_frame_t));

    /* 在信号帧之前压入信号编号和返回地址 */
    proc->context.useresp -= 4;
    *(uint32_t *)(uintptr_t)(proc->context.useresp) = (uint32_t)sig;

    /* 设置 sigreturn 入口作为返回地址 */
    /* 使用 int 0x80 触发 SYS_SIGRETURN 系统调用 */
    proc->context.useresp -= 4;
    *(uint32_t *)(uintptr_t)(proc->context.useresp) = frame_addr;

    /* 保存信号帧地址到进程上下文用于 sigreturn */
    proc->signal_frame_addr = frame_addr;

    /* 在信号处理期间阻塞当前信号 */
    proc->signal_blocked |= (1 << (sig - 1));

    /* 跳转到信号处理函数 */
    proc->context.eip = (uint32_t)handler;
}

int signal_register(pcb_t *proc, int sig, void (*handler)(int)) {
    if (sig < 1 || sig >= NSIG) return -1;
    /* SIGKILL 和 SIGSTOP 不能被捕获 */
    if (sig == SIGKILL || sig == SIGSTOP) return -1;
    proc->signal_handlers[sig - 1] = handler;
    return 0;
}

void signal_block(pcb_t *proc, int sig) {
    if (sig < 1 || sig >= NSIG) return;
    /* SIGKILL 和 SIGSTOP 不能被阻塞 */
    if (sig == SIGKILL || sig == SIGSTOP) return;
    proc->signal_blocked |= (1 << (sig - 1));
}

void signal_unblock(pcb_t *proc, int sig) {
    if (sig < 1 || sig >= NSIG) return;
    proc->signal_blocked &= ~(1 << (sig - 1));
}

/* ---- 新增: sigaction 系统调用支持 ---- */

int signal_sigaction(pcb_t *proc, int sig, const struct sigaction *act, struct sigaction *oact) {
    if (sig < 1 || sig >= NSIG) return -1;
    /* SIGKILL 和 SIGSTOP 不能被捕获 */
    if (sig == SIGKILL || sig == SIGSTOP) return -1;

    /* 保存旧的处理方式 */
    if (oact) {
        oact->sa_handler = proc->signal_handlers[sig - 1];
        oact->sa_mask = proc->signal_sa_mask;
        oact->sa_flags = proc->signal_sa_flags;
        oact->sa_restorer = NULL;
    }

    /* 设置新的处理方式 */
    if (act) {
        proc->signal_handlers[sig - 1] = act->sa_handler;
        proc->signal_sa_flags = act->sa_flags;
        proc->signal_sa_mask = act->sa_mask;
    }

    return 0;
}

/* ---- 新增: sigprocmask 系统调用支持 ---- */

int signal_sigprocmask(pcb_t *proc, int how, const sigset_t *set, sigset_t *oset) {
    if (!set && !oset) return -1;

    /* 保存旧的信号掩码 */
    if (oset) {
        *oset = proc->signal_blocked;
    }

    if (set) {
        sigset_t new_mask = *set;
        /* SIGKILL 和 SIGSTOP 不能被阻塞 */
        new_mask &= ~(1 << (SIGKILL - 1));
        new_mask &= ~(1 << (SIGSTOP - 1));

        switch (how) {
        case SIG_BLOCK:
            proc->signal_blocked |= new_mask;
            break;
        case SIG_UNBLOCK:
            proc->signal_blocked &= ~new_mask;
            break;
        case SIG_SETMASK:
            proc->signal_blocked = new_mask;
            break;
        default:
            return -1;
        }
    }

    return 0;
}

/* ---- 新增: alarm 系统调用支持 ---- */

int signal_alarm(pcb_t *proc, unsigned int seconds) {
    /* 返回上一个 alarm 的剩余秒数 */
    int remaining = 0;
    if (proc->alarm_ticks > 0) {
        remaining = (int)(proc->alarm_ticks / 100); /* 假设 100Hz 时钟 */
    }

    if (seconds == 0) {
        proc->alarm_ticks = 0;
    } else {
        /* 将秒转换为时钟滴答 (假设 100Hz) */
        proc->alarm_ticks = (uint32_t)(seconds * 100);
    }

    return remaining;
}

/* ---- 新增: pause 系统调用支持 ---- */

int signal_pause(pcb_t *proc) {
    /* 阻塞进程直到收到信号 */
    proc->state = PROCESS_BLOCKED;
    proc->blocked_reason = 2; /* blocked for pause */
    schedule();
    return -1; /* 总是返回 -1 (EINTR) */
}

/* ---- 新增: sigreturn 机制 ---- */

void signal_sigreturn(pcb_t *proc) {
    /* 从信号帧恢复上下文 */
    if (proc->signal_frame_addr == 0) return;

    signal_frame_t *frame = (signal_frame_t *)(uintptr_t)proc->signal_frame_addr;

    /* 恢复寄存器 */
    proc->context.eip = frame->old_eip;
    proc->context.eflags = frame->old_eflags;
    proc->context.eax = frame->old_eax;
    proc->context.ecx = frame->old_ecx;
    proc->context.edx = frame->old_edx;

    /* 恢复信号掩码 */
    proc->signal_blocked = frame->sa_mask;

    /* 恢复用户栈指针 (跳过信号帧) */
    proc->context.useresp = proc->signal_frame_addr + sizeof(signal_frame_t);

    /* 清除信号帧地址 */
    proc->signal_frame_addr = 0;
}

/* ---- 新增: 检查并投递信号 (含 sigreturn 恢复) ---- */

int signal_check_and_deliver(pcb_t *proc) {
    /* 检查 alarm 定时器 */
    if (proc->alarm_ticks > 0) {
        proc->alarm_ticks--;
        if (proc->alarm_ticks == 0) {
            /* alarm 超时，发送 SIGALRM */
            signal_send(proc->pid, SIGALRM);
        }
    }

    /* 检查并投递待处理信号 */
    for (int sig = 1; sig <= 31; sig++) {
        uint32_t bit = (1 << (sig - 1));
        if ((proc->signal_pending & bit) && !(proc->signal_blocked & bit)) {
            signal_deliver(proc, sig);
            return 1; /* 有信号被投递 */
        }
    }
    return 0; /* 没有信号被投递 */
}
