/* signal.c - Unix风格信号处理 */

#include "signal.h"
#include "process.h"
#include "sched.h"
#include "kheap.h"
#include "timer.h"
#include "stddef.h"
#include "string.h"
#include "stdio.h"

/* ============================================================
 * 信号帧结构 - 保存在用户栈上用于 sigreturn
 * ============================================================ */
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

/* ============================================================
 * 全局信号子系统初始化
 * ============================================================ */

void signal_init(void) {
    /* 全局信号子系统初始化 - 目前无需额外操作 */
}

/* 初始化进程的信号处理结构 */
void signal_init_proc(pcb_t *proc) {
    for (int i = 0; i < NSIG; i++) {
        proc->signal_handlers[i] = SIG_DFL;
        proc->signal_actions[i].sa_handler = SIG_DFL;
        proc->signal_actions[i].sa_mask = 0;
        proc->signal_actions[i].sa_flags = 0;
    }
    proc->signal_pending = 0;
    proc->signal_blocked = 0;
    proc->signal_sa_flags = 0;
    proc->signal_sa_mask = 0;
    proc->alarm_ticks = 0;
    proc->signal_frame_addr = 0;

    /* 初始化待处理信号位图（用于实时信号） */
    for (int i = 0; i < (NSIG + 31) / 32; i++) {
        proc->sig_pending_bits[i] = 0;
    }
}

/* ============================================================
 * 发送信号函数
 * ============================================================ */

/* 向指定进程发送信号 (kill系统调用) */
int kill(pid_t pid, int sig) {
    if (sig < 1 || sig >= NSIG) return -1;

    pcb_t *target = process_get_pcb(pid);
    if (target == NULL) {
        return -1;
    }

    /* SIGKILL 和 SIGSTOP 不能被忽略或阻塞，但可以被发送 */
    target->signal_pending |= (1 << (sig - 1));

    /* 如果目标进程处于阻塞状态，唤醒它以处理信号 */
    if (target->state == PROCESS_BLOCKED && sig != SIGCONT) {
        sched_unblock(target);
    }

    /* 特殊处理SIGCONT：恢复被停止的进程 */
    if (sig == SIGCONT && target->state == PROCESS_BLOCKED) {
        sched_unblock(target);
    }

    return 0;
}

/* 向指定线程发送信号 (tkill系统调用) */
int tkill(pid_t tid, int sig) {
    if (sig < 1 || sig >= NSIG) return -1;

    /* 在单线程模型下，tid等同于pid */
    return kill(tid, sig);
}

/* 向指定线程组中的特定线程发送信号 (tgkill系统调用) */
int tgkill(pid_t tgid, pid_t tid, int sig) {
    if (sig < 1 || sig >= NSIG) return -1;

    /* 在单线程模型下简化处理 */
    (void)tgid;
    return kill(tid, sig);
}

/* 发送带值的实时信号 (sigqueue系统调用) */
int sigqueue(pid_t pid, int sig, int value) {
    if (sig < 1 || sig >= NSIG) return -1;

    pcb_t *target = process_get_pcb(pid);
    if (target == NULL) {
        return -1;
    }

    /* 设置待处理信号位 */
    target->signal_pending |= (1 << (sig - 1));

    /* 存储实时信号的值（使用简单数组存储） */
    if (target->sig_queue_count < MAX_SIG_QUEUE_SIZE) {
        target->sig_queue[target->sig_queue_count].signo = sig;
        target->sig_queue[target->sig_queue_count].value = value;
        target->sig_queue_count++;
    }

    /* 唤醒目标进程 */
    if (target->state == PROCESS_BLOCKED) {
        sched_unblock(target);
    }

    return 0;
}

/* ============================================================
 * 信号处理注册函数
 * ============================================================ */

/* 注册信号处理动作 (sigaction系统调用) */
int sigaction(int signum, const sigaction_t *act, sigaction_t *oldact) {
    if (signum < 1 || signum >= NSIG) return -1;
    /* SIGKILL 和 SIGSTOP 不能被捕获或忽略 */
    if (signum == SIGKILL || signum == SIGSTOP) return -1;

    pcb_t *current = sched_get_current();
    if (!current) return -1;

    /* 保存旧的处理方式 */
    if (oldact) {
        oldact->sa_handler = current->signal_actions[signum - 1].sa_handler;
        oldact->sa_mask = current->signal_actions[signum - 1].sa_mask;
        oldact->sa_flags = current->signal_actions[signum - 1].sa_flags;
    }

    /* 设置新的处理方式 */
    if (act) {
        current->signal_actions[signum - 1].sa_handler = act->sa_handler;
        current->signal_actions[signum - 1].sa_mask = act->sa_mask;
        current->signal_actions[signum - 1].sa_flags = act->sa_flags;
        current->signal_handlers[signum - 1] = act->sa_handler;
    }

    return 0;
}

/* 简化的信号注册函数 (signal系统调用) */
sighandler_t signal(int signum, sighandler_t handler) {
    if (signum < 1 || signum >= NSIG) return SIG_ERR;
    /* SIGKILL 和 SIGSTOP 不能被捕获 */
    if (signum == SIGKILL || signum == SIGSTOP) return SIG_ERR;

    pcb_t *current = sched_get_current();
    if (!current) return SIG_ERR;

    sighandler_t old_handler = current->signal_handlers[signum - 1];
    current->signal_handlers[signum - 1] = handler;
    current->signal_actions[signum - 1].sa_handler = handler;

    return old_handler;
}

/* 设置信号屏蔽码 (sigprocmask系统调用) */
int sigprocmask(int how, const uint32_t *set, uint32_t *oldset) {
    pcb_t *current = sched_get_current();
    if (!current) return -1;

    /* 保存旧的信号掩码 */
    if (oldset) {
        *oldset = current->signal_blocked;
    }

    if (set) {
        uint32_t new_mask = *set;
        /* SIGKILL 和 SIGSTOP 不能被阻塞 */
        new_mask &= ~(1 << (SIGKILL - 1));
        new_mask &= ~(1 << (SIGSTOP - 1));

        switch (how) {
        case SIG_BLOCK:
            current->signal_blocked |= new_mask;
            break;
        case SIG_UNBLOCK:
            current->signal_blocked &= ~new_mask;
            break;
        case SIG_SETMASK:
            current->signal_blocked = new_mask;
            break;
        default:
            return -1;
        }
    }

    return 0;
}

/* 获取待处理信号集合 (sigpending系统调用) */
int sigpending(sig_pending_t *set) {
    if (!set) return -1;

    pcb_t *current = sched_get_current();
    if (!current) return -1;

    /* 将待处理信号位图复制到用户空间 */
    for (int i = 0; i < (NSIG + 31) / 32; i++) {
        set->bits[i] = current->sig_pending_bits[i];
    }
    /* 同时包含传统的pending字段 */
    set->bits[0] |= current->signal_pending;

    return 0;
}

/* 临时替换信号掩码并等待信号 (sigsuspend系统调用) */
int sigsuspend(const uint32_t *mask) {
    pcb_t *current = sched_get_current();
    if (!current) return -1;

    /* 临时设置新的信号掩码 */
    uint32_t old_mask = current->signal_blocked;
    if (mask) {
        current->signal_blocked = *mask & ~((1 << (SIGKILL - 1)) | (1 << (SIGSTOP - 1)));
    }

    /* 阻塞当前进程直到收到信号 */
    current->state = PROCESS_BLOCKED;
    current->blocked_reason = BLOCK_REASON_SLEEP;
    schedule();

    /* 恢复原始信号掩码（实际上会被信号处理程序修改） */
    current->signal_blocked = old_mask;

    return -1; /* 总是返回 -1 (EINTR) */
}

/* ============================================================
 * 信号检查和投递
 * ============================================================ */

/* 检查并投递待处理信号（在sched_tick中调用） */
void signal_check_deliver(pcb_t *proc) {
    if (!proc) return;

    /* 检查alarm定时器 */
    if (proc->alarm_ticks > 0) {
        proc->alarm_ticks--;
        if (proc->alarm_ticks == 0) {
            /* alarm超时，发送SIGALRM */
            kill(proc->pid, SIGALRM);
        }
    }

    /* 检查并投递标准信号 */
    for (int sig = 1; sig <= 31; sig++) {
        uint32_t bit = (1 << (sig - 1));
        if ((proc->signal_pending & bit) && !(proc->signal_blocked & bit)) {
            signal_deliver(proc, sig);
            return; /* 每次只投递一个信号 */
        }
    }
}

/* 检查是否有指定的待处理信号 */
int signal_has_pending(pcb_t *proc, int signum) {
    if (!proc || signum < 1 || signum >= NSIG) return 0;

    uint32_t bit = (1 << (signum - 1));
    return (proc->signal_pending & bit) != 0;
}

/* 投递信号到进程 */
void signal_deliver(pcb_t *proc, int sig) {
    void (*handler)(int) = proc->signal_handlers[sig - 1];

    /* 清除待处理标志 */
    proc->signal_pending &= ~(1 << (sig - 1));

    /* 处理默认动作 */
    if (handler == SIG_DFL) {
        signal_handle_default(proc, sig);
        return;
    }

    /* 忽略信号 */
    if (handler == SIG_IGN) {
        return;
    }

    /* 用户自定义处理函数 - 在用户栈上构建信号帧用于sigreturn */
    signal_frame_t frame;
    frame.sig = (uint32_t)sig;
    frame.old_eip = proc->context.eip;
    frame.old_eflags = proc->context.eflags;
    frame.old_eax = proc->context.eax;
    frame.old_ecx = proc->context.ecx;
    frame.old_edx = proc->context.edx;
    frame.sa_flags = proc->signal_sa_flags;
    frame.sa_mask = proc->signal_sa_mask;
    frame.return_addr = 0; /* 由sa_restorer设置 */

    /* 将信号帧压入用户栈 */
    proc->context.useresp -= sizeof(signal_frame_t);
    uint32_t frame_addr = proc->context.useresp;

    /* 复制信号帧到用户栈 */
    memcpy((void *)(uintptr_t)frame_addr, &frame, sizeof(signal_frame_t));

    /* 在信号帧之前压入信号编号和返回地址 */
    proc->context.useresp -= 4;
    *(uint32_t *)(uintptr_t)(proc->context.useresp) = (uint32_t)sig;

    /* 设置sigreturn入口作为返回地址 */
    proc->context.useresp -= 4;
    *(uint32_t *)(uintptr_t)(proc->context.useresp) = frame_addr;

    /* 保存信号帧地址到进程上下文用于sigreturn */
    proc->signal_frame_addr = frame_addr;

    /* 在信号处理期间阻塞当前信号 */
    proc->signal_blocked |= (1 << (sig - 1));

    /* 跳转到信号处理函数 */
    proc->context.eip = (uint32_t)handler;
}

/* 默认信号动作处理 */
void signal_handle_default(pcb_t *proc, int sig) {
    switch (sig) {
    case SIGKILL:
        /* 强制终止进程 */
        proc->state = PROCESS_ZOMBIE;
        break;

    case SIGSTOP:
    case SIGTSTP:
    case SIGTTIN:
    case SIGTTOU:
        /* 停止进程执行 */
        proc->state = PROCESS_BLOCKED;
        proc->blocked_reason = BLOCK_REASON_WAIT;
        break;

    case SIGCHLD:
    case SIGURG:
    case SIGWINCH:
    case SIGIO:
        /* 这些信号默认忽略 */
        break;

    case SIGCONT:
        /* 继续执行被停止的进程 */
        if (proc->state == PROCESS_BLOCKED) {
            sched_unblock(proc);
        }
        break;

    default:
        /* 其他信号默认终止进程 */
        proc->state = PROCESS_ZOMBIE;
        break;
    }
}

/* ============================================================
 * 兼容旧接口的包装函数
 * ============================================================ */

/* 发送信号（兼容旧接口） */
int signal_send(pid_t pid, int sig) {
    return kill(pid, sig);
}

/* 检查信号（兼容旧接口） */
int signal_check(pcb_t *proc) {
    signal_check_deliver(proc);
    return 0;
}

/* 注册信号处理器（兼容旧接口） */
int signal_register(pcb_t *proc, int sig, void (*handler)(int)) {
    if (sig < 1 || sig >= NSIG) return -1;
    if (sig == SIGKILL || sig == SIGSTOP) return -1;

    proc->signal_handlers[sig - 1] = handler;
    proc->signal_actions[sig - 1].sa_handler = handler;
    return 0;
}

/* 阻塞信号（兼容旧接口） */
void signal_block(pcb_t *proc, int sig) {
    if (sig < 1 || sig >= NSIG) return;
    if (sig == SIGKILL || sig == SIGSTOP) return;
    proc->signal_blocked |= (1 << (sig - 1));
}

/* 解除信号阻塞（兼容旧接口） */
void signal_unblock(pcb_t *proc, int sig) {
    if (sig < 1 || sig >= NSIG) return;
    proc->signal_blocked &= ~(1 << (sig - 1));
}

/* sigaction系统调用支持（兼容旧接口） */
int signal_sigaction(pcb_t *proc, int sig, const struct sigaction *act, struct sigaction *oact) {
    if (sig < 1 || sig >= NSIG) return -1;
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

/* sigprocmask系统调用支持（兼容旧接口） */
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

/* alarm系统调用支持 */
int signal_alarm(pcb_t *proc, unsigned int seconds) {
    /* 返回上一个alarm的剩余秒数 */
    int remaining = 0;
    if (proc->alarm_ticks > 0) {
        remaining = (int)(proc->alarm_ticks / 100); /* 假设100Hz时钟 */
    }

    if (seconds == 0) {
        proc->alarm_ticks = 0;
    } else {
        /* 将秒转换为时钟滴答（假设100Hz） */
        proc->alarm_ticks = (uint32_t)(seconds * 100);
    }

    return remaining;
}

/* pause系统调用支持 */
int signal_pause(pcb_t *proc) {
    /* 阻塞进程直到收到信号 */
    proc->state = PROCESS_BLOCKED;
    proc->blocked_reason = BLOCK_REASON_SLEEP;
    schedule();
    return -1; /* 总是返回-1 (EINTR) */
}

/* sigreturn机制 */
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

    /* 恢复用户栈指针（跳过信号帧） */
    proc->context.useresp = proc->signal_frame_addr + sizeof(signal_frame_t);

    /* 清除信号帧地址 */
    proc->signal_frame_addr = 0;
}

/* 检查并投递信号（含sigreturn恢复） */
int signal_check_and_deliver(pcb_t *proc) {
    signal_check_deliver(proc);

    /* 检查是否有信号被投递 */
    for (int sig = 1; sig <= 31; sig++) {
        uint32_t bit = (1 << (sig - 1));
        if ((proc->signal_pending & bit) && !(proc->signal_blocked & bit)) {
            return 1; /* 有信号待处理 */
        }
    }
    return 0; /* 没有信号被投递 */
}
