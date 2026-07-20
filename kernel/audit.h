#ifndef AUDIT_H
#define AUDIT_H

#include "stdint.h"

#define AUDIT_MAX_BUFFER      256
#define AUDIT_CONTEXT_MAX     64
#define AUDIT_RING_SIZE       512

#define AUDIT_DISABLED        0
#define AUDIT_ENABLED         1
#define AUDIT_LOCKED          2

#define AUDIT_RECORD_MAX      256

#define AUDIT_SYSCALL         1
#define AUDIT_PATH            2
#define AUDIT_IPC             3
#define AUDIT_SOCKETCALL      4
#define AUDIT_CONFIG_CHANGE   5
#define AUDIT_LOGIN           6
#define AUDIT_OPEN            7
#define AUDIT_EXECVE          8

struct audit_buffer {
    char buf[AUDIT_RECORD_MAX];
    uint32_t len;
    uint32_t type;
    uint32_t used;
};

struct audit_context {
    uint32_t pid;
    uint32_t uid;
    uint32_t syscall;
    uint64_t timestamp;
    uint32_t arch;
    int return_code;
    char comm[32];
    char path[64];
    uint32_t used;
};

struct audit_entry {
    uint32_t type;
    uint32_t pid;
    uint32_t uid;
    uint64_t timestamp;
    char message[128];
    uint32_t used;
};

int audit_init(void);
void audit_set_enabled(uint32_t state);
uint32_t audit_get_enabled(void);
void audit_set_rate_limit(uint32_t limit);
uint32_t audit_get_rate_limit(void);
struct audit_buffer *audit_log_start(uint32_t type, uint32_t pid);
int audit_log_format(struct audit_buffer *ab, const char *fmt, ...);
void audit_log_end(struct audit_buffer *ab);
int audit_log_syscall(uint32_t pid, uint32_t uid, uint32_t syscall, const char *comm, int ret);
int audit_log_path(const char *path, uint32_t pid, uint32_t uid);
void audit_print_stats(void);

#endif
