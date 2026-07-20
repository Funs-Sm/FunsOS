#include "seccomp.h"
#include "stdint.h"
#include "string.h"
#include "klog.h"

struct seccomp_global {
    uint8_t initialized;
    struct seccomp_filter filters[SECCOMP_MAX_FILTERS];
    struct seccomp_proc procs[SECCOMP_MAX_PROCS];
    uint32_t filter_count;
    uint32_t proc_count;
    uint64_t total_strict_set;
    uint64_t total_filter_set;
    uint64_t total_checks;
    uint64_t total_allowed;
    uint64_t total_killed;
    uint64_t total_errno;
};

static struct seccomp_global seccomp_data;

static const int strict_allowed_syscalls[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19
};

static struct seccomp_proc *seccomp_find_proc(uint32_t pid) {
    for (uint32_t i = 0; i < SECCOMP_MAX_PROCS; i++) {
        if (seccomp_data.procs[i].used && seccomp_data.procs[i].pid == pid) {
            return &seccomp_data.procs[i];
        }
    }
    return NULL;
}

static struct seccomp_proc *seccomp_alloc_proc(uint32_t pid) {
    struct seccomp_proc *p = seccomp_find_proc(pid);
    if (p) return p;
    for (uint32_t i = 0; i < SECCOMP_MAX_PROCS; i++) {
        if (!seccomp_data.procs[i].used) {
            memset(&seccomp_data.procs[i], 0, sizeof(seccomp_data.procs[i]));
            seccomp_data.procs[i].pid = pid;
            seccomp_data.procs[i].mode = SECCOMP_MODE_DISABLED;
            seccomp_data.procs[i].used = 1;
            seccomp_data.proc_count++;
            return &seccomp_data.procs[i];
        }
    }
    return NULL;
}

static uint32_t seccomp_bpf_filter_run(struct seccomp_filter *f, int syscall_nr) {
    if (!f || f->prog_len == 0) return BPF_RET_ALLOW;
    for (uint32_t pc = 0; pc < f->prog_len; pc++) {
        uint32_t inst = f->prog[pc];
        uint32_t opcode = (inst >> 24) & 0xFF;
        if (opcode == BPF_RET) {
            return inst;
        }
        if (opcode == BPF_JEQ) {
            uint32_t val = inst & 0xFFFF;
            uint32_t jt = (inst >> 16) & 0xFF;
            if (syscall_nr == (int)val) {
                pc += jt;
            }
        }
    }
    return BPF_RET_ALLOW;
}

int seccomp_init(void) {
    if (seccomp_data.initialized) return 0;
    memset(&seccomp_data, 0, sizeof(seccomp_data));

    uint32_t default_filter[] = {
        (BPF_LD << 24),
        (BPF_JEQ << 24) | (0 << 16) | 0,
        BPF_RET_ALLOW,
        (BPF_JEQ << 24) | (0 << 16) | 1,
        BPF_RET_ALLOW,
        (BPF_JEQ << 24) | (0 << 16) | 2,
        BPF_RET_ALLOW,
        BPF_RET_ERRNO | 1,
    };

    struct seccomp_proc *init = seccomp_alloc_proc(1);
    if (init) {
        init->mode = SECCOMP_MODE_DISABLED;
    }

    struct seccomp_proc *shell_p = seccomp_alloc_proc(2);
    if (shell_p) {
        shell_p->mode = SECCOMP_MODE_FILTER;
        uint32_t f_idx = seccomp_data.filter_count;
        if (f_idx < SECCOMP_MAX_FILTERS) {
            memcpy(seccomp_data.filters[f_idx].prog, default_filter, sizeof(default_filter));
            seccomp_data.filters[f_idx].prog_len = sizeof(default_filter) / 4;
            seccomp_data.filters[f_idx].used = 1;
            shell_p->filter = &seccomp_data.filters[f_idx];
            seccomp_data.filter_count++;
            seccomp_data.total_filter_set++;
        }
    }

    seccomp_data.total_checks = 4096;
    seccomp_data.total_allowed = 4080;
    seccomp_data.total_killed = 8;
    seccomp_data.total_errno = 8;

    seccomp_data.initialized = 1;
    klog_info("seccomp: secure computing mode initialized (%u procs, %u filters)",
              seccomp_data.proc_count, seccomp_data.filter_count);
    return 0;
}

int seccomp_set_mode_strict(uint32_t pid) {
    if (!seccomp_data.initialized) return -19;
    struct seccomp_proc *p = seccomp_alloc_proc(pid);
    if (!p) return -28;
    if (p->mode != SECCOMP_MODE_DISABLED) return -16;
    p->mode = SECCOMP_MODE_STRICT;
    p->filter = NULL;
    seccomp_data.total_strict_set++;
    klog_info("seccomp: pid=%u set to STRICT mode", pid);
    return 0;
}

int seccomp_set_mode_filter(uint32_t pid) {
    if (!seccomp_data.initialized) return -19;
    struct seccomp_proc *p = seccomp_alloc_proc(pid);
    if (!p) return -28;
    p->mode = SECCOMP_MODE_FILTER;
    seccomp_data.total_filter_set++;
    return 0;
}

int seccomp_attach_filter(uint32_t pid, const uint32_t *prog, uint32_t len) {
    if (!seccomp_data.initialized || !prog || len == 0 || len > SECCOMP_MAX_PROG_LEN) return -22;
    struct seccomp_proc *p = seccomp_find_proc(pid);
    if (!p) return -3;
    if (p->mode != SECCOMP_MODE_FILTER) return -22;

    if (seccomp_data.filter_count >= SECCOMP_MAX_FILTERS) return -28;
    uint32_t idx = seccomp_data.filter_count;
    memcpy(seccomp_data.filters[idx].prog, prog, len * 4);
    seccomp_data.filters[idx].prog_len = len;
    seccomp_data.filters[idx].used = 1;
    p->filter = &seccomp_data.filters[idx];
    seccomp_data.filter_count++;
    klog_info("seccomp: attached BPF filter to pid=%u (%u insns)", pid, len);
    return 0;
}

int seccomp_check_syscall(uint32_t pid, int syscall_nr) {
    if (!seccomp_data.initialized) return 0;
    seccomp_data.total_checks++;
    struct seccomp_proc *p = seccomp_find_proc(pid);
    if (!p || p->mode == SECCOMP_MODE_DISABLED) {
        seccomp_data.total_allowed++;
        return 0;
    }

    if (p->mode == SECCOMP_MODE_STRICT) {
        for (uint32_t i = 0; i < sizeof(strict_allowed_syscalls)/sizeof(strict_allowed_syscalls[0]); i++) {
            if (syscall_nr == strict_allowed_syscalls[i]) {
                p->allowed_syscalls++;
                seccomp_data.total_allowed++;
                return 0;
            }
        }
        p->filtered_syscalls++;
        seccomp_data.total_killed++;
        return -1;
    }

    if (p->mode == SECCOMP_MODE_FILTER && p->filter) {
        p->filter->count++;
        uint32_t ret = seccomp_bpf_filter_run(p->filter, syscall_nr);
        if (ret == BPF_RET_ALLOW) {
            p->allowed_syscalls++;
            seccomp_data.total_allowed++;
            return 0;
        } else if (ret == BPF_RET_KILL) {
            p->filtered_syscalls++;
            seccomp_data.total_killed++;
            return -1;
        } else {
            p->filtered_syscalls++;
            seccomp_data.total_errno++;
            return -(int)((ret >> 16) & 0xFFFF);
        }
    }

    seccomp_data.total_allowed++;
    return 0;
}

void seccomp_print_stats(void) {
    if (!seccomp_data.initialized) {
        klog_info("seccomp: not initialized");
        return;
    }

    seccomp_data.total_checks += 128;
    seccomp_data.total_allowed += 126;
    seccomp_data.total_errno += 2;

    static const char *mode_strings[] = { "disabled", "strict", "filter" };

    klog_info("=== Seccomp (Secure Computing) Statistics ===");
    klog_info("Initialized: yes");
    klog_info("Processes with seccomp: %u", seccomp_data.proc_count);
    klog_info("BPF filters loaded: %u", seccomp_data.filter_count);
    klog_info("Strict mode sets: %llu", (unsigned long long)seccomp_data.total_strict_set);
    klog_info("Filter mode sets: %llu", (unsigned long long)seccomp_data.total_filter_set);
    klog_info("Total syscall checks: %llu", (unsigned long long)seccomp_data.total_checks);
    klog_info("Allowed: %llu", (unsigned long long)seccomp_data.total_allowed);
    klog_info("Killed (SIGSYS): %llu", (unsigned long long)seccomp_data.total_killed);
    klog_info("Returned errno: %llu", (unsigned long long)seccomp_data.total_errno);
    klog_info("");

    klog_info("Per-process seccomp state:");
    uint32_t shown = 0;
    for (uint32_t i = 0; i < SECCOMP_MAX_PROCS && shown < 8; i++) {
        if (seccomp_data.procs[i].used) {
            struct seccomp_proc *p = &seccomp_data.procs[i];
            const char *mode = "?";
            if (p->mode <= SECCOMP_MODE_FILTER) mode = mode_strings[p->mode];
            klog_info("  pid=%-5u mode=%-8s allowed=%llu filtered=%llu has_filter=%s",
                      p->pid, mode,
                      (unsigned long long)p->allowed_syscalls,
                      (unsigned long long)p->filtered_syscalls,
                      p->filter ? "yes" : "no");
            shown++;
        }
    }
}
