#include "syscall.h"
#include "syscall_impl.h"
#include "process.h"
#include "idt.h"
#include "sched.h"
#include "thread.h"
#include "kheap.h"
#include "signal.h"
#include "../lib/string.h"

#define ENOSYS 38
#define MAX_SYSCALL 256

enum {
    SYS_EXIT    = 1,
    SYS_FORK    = 2,
    SYS_READ    = 3,
    SYS_WRITE   = 4,
    SYS_OPEN    = 5,
    SYS_CLOSE   = 6,
    SYS_WAITPID = 7,
    SYS_GETPID  = 8,
    SYS_EXEC    = 9,
    SYS_SLEEP   = 10,
    SYS_YIELD   = 11,
    SYS_PIPE    = 12,
    SYS_SIGNAL  = 13,
    SYS_KILL    = 14,
    SYS_MMAP    = 15,
    SYS_MUNMAP  = 16,
    SYS_IOCTL   = 17,
    SYS_READDIR = 18,
    SYS_CHDIR   = 19,
    SYS_GETCWD  = 20,
    SYS_SOCKET  = 21,
    SYS_BIND    = 22,
    SYS_CONNECT = 23,
    SYS_LISTEN  = 24,
    SYS_ACCEPT  = 25,
    SYS_SEND    = 26,
    SYS_RECV    = 27,
    SYS_SHUTDOWN = 28,
    SYS_CLOSESOCK = 29,
    SYS_SELECT  = 30,
    SYS_POLL    = 31,
    SYS_SENDTO  = 32,
    SYS_RECVFROM = 33,
    SYS_GETSOCKNAME = 34,
    SYS_GETPEERNAME = 35,
    SYS_SETSOCKOPT  = 36,
    SYS_GETSOCKOPT  = 37,
    SYS_SENDFILE    = 38,
    SYS_LSEEK       = 39,
    SYS_GETPPID     = 40,
    SYS_NANOSLEEP   = 41,
    SYS_MOUNT       = 42,
    SYS_EXECVE      = 43,
    SYS_SIGACTION   = 44,
    SYS_SIGPROCMASK = 45,
    SYS_ALARM       = 46,
    SYS_PAUSE       = 47,
    SYS_SIGRETURN   = 48,
    SYS_IOCTL_IMPL  = 49,
    SYS_READDIR_IMPL= 50,
    SYS_CHDIR_IMPL  = 51,
    SYS_GETCWD_IMPL = 52,
    SYS_INVALIDATE  = 103,
    SYS_GET_CONTEXT = 108,
    SYS_SET_TIMER   = 122,
    SYS_CANCEL_TIMER= 123,
    SYS_AUDIO_INIT  = 130,
    SYS_AUDIO_PLAY  = 131,
    SYS_AUDIO_STOP  = 132,
    SYS_AUDIO_SET_VOL=133,
    SYS_AUDIO_GET_VOL=134,
    SYS_AUDIO_PLAY_WAV=135,
    SYS_GET_VERSION = 141,
    SYS_GET_MEM_INFO= 142,
    SYS_GET_TIME    = 144,
    SYS_3D_INIT     = 150,
    SYS_3D_RENDER   = 151,
    SYS_3D_CLEAR_DEPTH=152,
    SYS_APP_INIT    = 160,
    SYS_APP_CLEANUP = 161,
    SYS_MESSAGE_BOX = 170,
    SYS_SET_CURSOR  = 171,
    SYS_REGISTER_HOTKEY=180,
    SYS_UNREGISTER_HOTKEY=181,
    SYS_CREATE_TIMER_EXT=190,
    SYS_RESET_TIMER = 191,
    SYS_GET_TIMER_REMAIN=192,
    SYS_CLIPBOARD_SET=200,
    SYS_CLIPBOARD_GET=201,
    SYS_CLIPBOARD_CLEAR=202,
    SYS_CLIPBOARD_QUERY=203,
    SYS_INSTALL_FILTER=210,
    SYS_REMOVE_FILTER=211,
    SYS_BYPASS_FILTERS=212,
    SYS_WINDOW_STATE=220,
    SYS_WINDOW_EX   = 221,
    SYS_FOCUS_WINDOW=222,
    SYS_RAISE_WINDOW=223,
    SYS_GET_WIN_RECT=224
};

extern volatile int need_resched;

static syscall_func_t syscall_table[256];

void syscall_handler(regs_t *regs) {
    pcb_t *current = sched_get_current();
    if (current) {
        current->kernel_stack = regs->esp_kernel;
    }

    uint32_t num = regs->eax;

    if (num >= MAX_SYSCALL || !syscall_table[num]) {
        regs->eax = (uint32_t)(-ENOSYS);
        return;
    }

    uint32_t arg1 = regs->ebx;
    uint32_t arg2 = regs->ecx;
    uint32_t arg3 = regs->edx;
    uint32_t arg4 = regs->esi;
    uint32_t arg5 = regs->edi;

    int32_t ret = syscall_table[num](arg1, arg2, arg3, arg4, arg5);
    regs->eax = (uint32_t)ret;

    if (current) {
        if (signal_check(current)) {
            /* signal_deliver is called inside signal_check for pending signals */
        }
    }

    if (need_resched) {
        need_resched = 0;
        schedule();
    }
}

void init_syscall(void) {
    memset(syscall_table, 0, sizeof(syscall_table));

    idt_set_gate(0x80, (uint32_t)interrupt_entry_table[0x80], 0x08, 0xEE);

    init_syscall_impl();
}

int32_t syscall_register(uint32_t num, syscall_func_t func) {
    if (num >= 256 || !func) {
        return -1;
    }
    syscall_table[num] = func;
    return 0;
}
