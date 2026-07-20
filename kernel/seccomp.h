#ifndef SECCOMP_H
#define SECCOMP_H

#include "stdint.h"

#define SECCOMP_MODE_DISABLED  0
#define SECCOMP_MODE_STRICT    1
#define SECCOMP_MODE_FILTER    2

#define SECCOMP_MAX_FILTERS    32
#define SECCOMP_MAX_PROG_LEN   64
#define SECCOMP_MAX_PROCS      64

#define BPF_RET_ALLOW  0x7FFF0000
#define BPF_RET_KILL   0x00000000
#define BPF_RET_ERRNO  0x00050000
#define BPF_LD         0x00
#define BPF_JEQ        0x10
#define BPF_JMP        0x05
#define BPF_RET        0x06

struct seccomp_data {
    int nr;
    uint32_t arch;
    uint64_t instruction_pointer;
    uint64_t args[6];
};

struct seccomp_filter {
    uint32_t prog[SECCOMP_MAX_PROG_LEN];
    uint32_t prog_len;
    uint32_t used;
    uint64_t count;
};

struct seccomp_proc {
    uint32_t pid;
    uint32_t mode;
    struct seccomp_filter *filter;
    uint32_t used;
    uint64_t allowed_syscalls;
    uint64_t filtered_syscalls;
};

int seccomp_init(void);
int seccomp_set_mode_strict(uint32_t pid);
int seccomp_set_mode_filter(uint32_t pid);
int seccomp_attach_filter(uint32_t pid, const uint32_t *prog, uint32_t len);
int seccomp_check_syscall(uint32_t pid, int syscall_nr);
void seccomp_print_stats(void);

#endif
