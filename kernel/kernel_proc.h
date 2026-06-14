#ifndef KERNEL_PROC_H
#define KERNEL_PROC_H

#include "kernel_types.h"
#include "kernel_mem.h"
#include "sync.h"
#include "signal.h"

/* Forward declaration */
struct file_descriptor;
typedef struct file_descriptor file_descriptor_t;

#define MAX_PROCESSES 256
#define MAX_OPEN_FILES 16
#define DEFAULT_TIME_SLICE 10
#define KERNEL_STACK_SIZE 8192
#define USER_STACK_TOP 0xBFFFF000
#define USER_STACK_SIZE 16384

typedef struct file_operations {
    int32_t (*read)(file_descriptor_t *, void *, uint32_t);
    int32_t (*write)(file_descriptor_t *, const void *, uint32_t);
    int32_t (*close)(file_descriptor_t *);
} file_operations_t;

struct file_descriptor {
    int32_t fd;
    uint32_t flags;
    uint32_t ref_count;
    void *private_data;
    struct file_operations *ops;
};

struct pcb_t {
    pid_t pid;
    char name[32];
    uint32_t type;
    uint32_t state;
    regs_t context;
    page_directory_t *page_dir;
    uint32_t kernel_stack;
    uint32_t kernel_esp;     /* saved ESP for context switch */
    uint32_t user_stack;
    uint32_t entry_point;
    int32_t exit_status;
    pid_t parent_pid;
    pcb_t *next;
    pcb_t *prev;
    pcb_t *first_child;
    pcb_t *next_sibling;
    pcb_t *queue_next;
    pcb_t *queue_prev;
    uint32_t time_slice;
    uint32_t priority;
    uint32_t original_priority;
    uint32_t effective_priority;
    uint32_t sched_policy;
    uint32_t queue_level;
    uint32_t ticks_used;
    uint32_t cpu_time;
    uint64_t last_run_time;
    uint64_t wake_time;
    int32_t blocked_reason;
    file_descriptor_t *fd_table[MAX_OPEN_FILES];
    uint32_t fd_count;
    uint32_t signal_pending;
    uint32_t signal_blocked;
    void (*signal_handlers[32])(int);
    uint32_t signal_sa_flags;     /* sigaction 标志 */
    uint32_t signal_sa_mask;      /* sigaction 掩码 */
    uint32_t alarm_ticks;         /* alarm 定时器 (时钟滴答) */
    uint32_t signal_frame_addr;   /* 当前信号帧地址 (用于 sigreturn) */
    uint32_t nice;
    uint8_t *comm;
    uint8_t fpu_saved;
    uint8_t fpu_state[108];
    void *tls_data[128];  /* TLS slots for pthread_key_t */
};

#endif
