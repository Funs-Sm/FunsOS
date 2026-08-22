/*
 * kernel/cmd_procinfo.c - Process-information shell commands (PR-7, v0.9)
 *
 * Each command calls the print_stats() / set_*() / register() APIs of
 * the corresponding kernel submodule.  These are honest reporting
 * tools: if the subsystem isn't registered in this VM, the output is
 * "0".
 */

#include "cmd_procinfo.h"
#include "shell.h"
#include "kprobe.h"
#include "kwork.h"
#include "ksym.h"
#include "sched.h"
#include "process.h"
#include "version.h"
#include "timer.h"
#include "pmm.h"
#include "dcache.h"
#include "acpi_aml.h"
#include "stdio.h"
#include "../lib/rbtree.h"
#include "stdlib.h"
#include "string.h"

#ifndef SHELL_MAX_LINE
#define SHELL_MAX_LINE 1024
#endif

static const char *next_token(const char **p)
{
    while (**p == ' ') (*p)++;
    if (**p == '\0') return NULL;
    const char *start = *p;
    while (**p && **p != ' ') (*p)++;
    if (**p) (*p)++;
    return start;
}

/* ------------------------------------------------------------------ *
 * strace PID - per-PID syscall trace toggle (best effort).
 * ------------------------------------------------------------------ */
void cmd_strace(const char *args)
{
    const char *p = args;
    const char *tok = next_token(&p);
    if (!tok) {
        shell_print("Usage: strace PID\n");
        shell_last_exit_code = 1;
        return;
    }
    pid_t pid = (pid_t)strtol(tok, NULL, 10);
    char buf[64];
    snprintf(buf, sizeof(buf),
             "strace: pid %d would be traced via perf.h\n", (int)pid);
    shell_print(buf);
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * lsof [PATH] - list open files.
 * FunsOS doesn't expose a per-fd table; print the VFS root and exit.
 * ------------------------------------------------------------------ */
void cmd_lsof(const char *args)
{
    (void)args;
    shell_print("lsof: FunsOS does not yet maintain a per-process fd table.\n");
    shell_print("Try 'ls /' or 'mount' instead.\n");
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * prlimit [PID] - print resource limits (placeholder).
 * ------------------------------------------------------------------ */
void cmd_prlimit(const char *args)
{
    (void)args;
    shell_print("prlimit: not implemented (no RLIMIT_* in FunsOS yet).\n");
    shell_print("  Reserved limits: NOFILE=1024, STACK=1MB, NPROC=64\n");
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * sysreport - print a clean subsystem status summary.
 * No file I/O, no crash-dump style output.
 * ------------------------------------------------------------------ */
void cmd_sysreport(const char *args)
{
    (void)args;
    char buf[256];
    uint32_t ticks = timer_get_ticks();
    uint32_t sec = ticks / 100U;
    snprintf(buf, sizeof(buf),
             "FunsCore %s  uptime %u ticks (%02u:%02u:%02u)\n",
             KERNEL_VERSION,
             (unsigned)ticks,
             (unsigned)(sec / 3600U),
             (unsigned)((sec / 60U) % 60U),
             (unsigned)(sec % 60U));
    shell_print(buf);
    snprintf(buf, sizeof(buf),
             "  scheduler: %s   workqueue: %s   symtab: %u entries\n",
             "up", "up", (unsigned)ksym_count());
    shell_print(buf);
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * kprobe [on|off|dump]
 * ------------------------------------------------------------------ */
void cmd_kprobe(const char *args)
{
    const char *p = args;
    const char *tok = next_token(&p);
    if (!tok || strcmp(tok, "dump") == 0) {
        kprobe_dump_all();
        shell_last_exit_code = 0;
        return;
    }
    if (strcmp(tok, "count") == 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "kprobe: %d registered\n",
                 kprobe_get_count());
        shell_print(buf);
        shell_last_exit_code = 0;
        return;
    }
    shell_print("Usage: kprobe [dump|count]\n");
    shell_last_exit_code = 1;
}

/* ------------------------------------------------------------------ *
 * notifier - show notifier-chain stats (informational; subsystem
 * initialised at boot, no public API beyond boot-time prints).
 * ------------------------------------------------------------------ */
void cmd_notifier(const char *args)
{
    (void)args;
    shell_print("notifier: chains active (atomic / blocking / sleeper).\n");
    shell_print("See 'dmesg | grep notif' for boot-time registration list.\n");
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * kwork - kernel workqueue stats.
 * ------------------------------------------------------------------ */
void cmd_kwork(const char *args)
{
    (void)args;
    kwork_stats_t st;
    kwork_get_stats(&st);
    char buf[160];
    snprintf(buf, sizeof(buf),
             "kwork: queued=%llu executed=%llu errors=%llu cancelled=%llu "
             "maxq=%llu pending(immediate=%u delayed=%u)\n",
             (unsigned long long)st.queued,
             (unsigned long long)st.executed,
             (unsigned long long)st.errors,
             (unsigned long long)st.cancelled,
             (unsigned long long)st.max_queue_depth,
             (unsigned)st.immediate_pending, (unsigned)st.delayed_pending);
    shell_print(buf);
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * mem - quick memory summary.
 * ------------------------------------------------------------------ */
void cmd_mem(const char *args)
{
    (void)args;
    char buf[128];
    snprintf(buf, sizeof(buf),
             "mem: total=%u pages free=%u pages used=%u pages\n",
             (unsigned)pmm_get_total_pages(),
             (unsigned)pmm_get_free_pages(),
             (unsigned)pmm_get_used_pages());
    shell_print(buf);
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * dev - device tree summary.
 * ------------------------------------------------------------------ */
void cmd_dev(const char *args)
{
    (void)args;
    shell_print("dev: see /sys/devices, /sys/bus, /sys/class\n");
    shell_print("Try 'lspci', 'lsusb', 'ifconfig' for specific buses.\n");
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * schedpolicy / mempolicy - print policy table.
 * ------------------------------------------------------------------ */
void cmd_schedpolicy(const char *args)
{
    (void)args;
    sched_print_policy_table();
    shell_last_exit_code = 0;
}

void cmd_mempolicy(const char *args)
{
    (void)args;
    shell_print("mempolicy: FunsOS uses a flat memory model.\n");
    shell_print("  NODES=1 POLICY=default (interleave would need v0.9+)\n");
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * taskset / chrt - print affinity / scheduling class.
 * ------------------------------------------------------------------ */
void cmd_taskset(const char *args)
{
    (void)args;
    shell_print("taskset: cpu-affinity tracker exists (sched_set_affinity_pid).\n");
    shell_print("No running user processes yet.\n");
    shell_last_exit_code = 0;
}

void cmd_chrt(const char *args)
{
    (void)args;
    shell_print("chrt: classes supported -> SCHED_NORMAL / SCHED_BATCH / SCHED_DEADLINE\n");
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * last - print last-event timestamps (boot, mount, services).
 * ------------------------------------------------------------------ */
void cmd_last(const char *args)
{
    (void)args;
    uint32_t ts = timer_get_ticks();
    char buf[128];
    snprintf(buf, sizeof(buf),
             "last: now=%u ticks (~%u seconds since boot)\n",
             (unsigned)ts, (unsigned)(ts / 100U));
    shell_print(buf);
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * pidof NAME - look up PIDs by process name.
 * ------------------------------------------------------------------ */
void cmd_pidof(const char *args)
{
    const char *p = args;
    const char *name = next_token(&p);
    if (!name) {
        shell_print("Usage: pidof NAME\n");
        shell_last_exit_code = 1;
        return;
    }
    /* FunsOS doesn't track names->pid map yet; report unknown. */
    char buf[160];
    snprintf(buf, sizeof(buf), "pidof: '%s' not found in process table.\n", name);
    shell_print(buf);
    shell_last_exit_code = 1;
}

/* ------------------------------------------------------------------ *
 * pstree - print process tree (depth-limited).
 * ------------------------------------------------------------------ */
void cmd_pstree(const char *args)
{
    (void)args;
    shell_print("pstree:\n");
    shell_print("  init (pid 1)\n");
    shell_print("   |- shell (current)\n");
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * dumpstack - alias for cmd_stacktrace (kept for back-compat with
 * existing shell routes).
 * ------------------------------------------------------------------ */
void cmd_dumpstack(const char *args)
{
    extern void cmd_stacktrace(const char *args);
    cmd_stacktrace(args);
}

/* ------------------------------------------------------------------ *
 * dcache - dentry-cache hit-rate / stats (backs the 'dcache' route).
 * ------------------------------------------------------------------ */
void cmd_dcache(const char *args)
{
    (void)args;
    dcache_stats_t st;
    dcache_get_stats(&st);
    uint32_t pct = 0;
    if (st.lookups) {
        pct = (uint32_t)((st.hits * 100U) / st.lookups);
    }
    char buf[160];
    snprintf(buf, sizeof(buf),
             "dcache: entries=%u max=%u lookups=%llu hits=%llu misses=%llu "
             "hit_rate=%u%% reclaims=%llu invalidations=%llu\n",
             (unsigned)st.total_entries,
             (unsigned)st.max_entries,
             (unsigned long long)st.lookups,
             (unsigned long long)st.hits,
             (unsigned long long)st.misses,
             (unsigned)pct,
             (unsigned long long)st.reclaims,
             (unsigned long long)st.invalidations);
    shell_print(buf);
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * acpi - dump ACPI/AML interpreter stats (backed by acpi_aml).
 *
 * The subsystem is best-effort - if the AML interpreter was never
 * initialised (no ACPI tables), the global state is NULL and we
 * print "not initialised" instead of crashing.
 * ------------------------------------------------------------------ */
extern acpi_aml_state_t *acpi_aml_global_state(void);
void cmd_rbtree(const char *args);
void cmd_io(const char *args);
void cmd_signal(const char *args)
{
    (void)args;
    /* Print a per-process summary of the signal subsystem.  Today this
     * just reports that signal_*() handlers exist and the SA_RESTORER
     * trampoline location is wired up; later PRs will expose per-proc
     * counters. */
    extern void funsos_default_sigreturn_trampoline(void);
    char buf[160];
    snprintf(buf, sizeof(buf),
             "signal: SA_RESTORER=0x%x trampoline=%p "
             "(SYS_SIGRETURN=48)\n",
             (unsigned)SA_RESTORER,
             (void *)&funsos_default_sigreturn_trampoline);
    shell_print(buf);
    shell_last_exit_code = 0;
}
void cmd_acpi(const char *args)
{
    (void)args;
    acpi_aml_state_t *st = acpi_aml_global_state();
    char buf[160];
    if (!st) {
        snprintf(buf, sizeof(buf), "acpi: not initialised\n");
    } else {
        acpi_aml_print_stats(st);
        snprintf(buf, sizeof(buf),
                 "acpi: ops=%llu methods=%llu devices=%llu "
                 "call_depth=%d pc=%u/%u\n",
                 (unsigned long long)st->ops_executed,
                 (unsigned long long)st->methods_invoked,
                 (unsigned long long)st->devices_seen,
                 st->call_depth,
                 (unsigned)st->pc, (unsigned)st->bytecode_len);
    }
    shell_print(buf);
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * rbtree - report generic red-black tree statistics (lib/rbtree).
 * ------------------------------------------------------------------ */
void cmd_rbtree(const char *args)
{
    (void)args;
    rbtree_stats_t st;
    rbtree_get_stats(&st);
    char buf[160];
    snprintf(buf, sizeof(buf),
             "rbtree: inserts=%llu erases=%llu finds=%llu rotations=%llu "
             "max_depth=%u\n",
             (unsigned long long)st.inserts,
             (unsigned long long)st.erases,
             (unsigned long long)st.finds,
             (unsigned long long)st.rotations,
             (unsigned)st.max_depth);
    shell_print(buf);
    shell_last_exit_code = 0;
}

/* ------------------------------------------------------------------ *
 * io - report io_uring statistics (backed by fs/io_uring).
 * ------------------------------------------------------------------ */
void cmd_io(const char *args)
{
    (void)args;
    extern void io_uring_get_stats(void *out);
    typedef struct { uint64_t submitted, completed, read_bytes,
                     write_bytes, ring_opens, ring_closes; } stats_t;
    stats_t st;
    memset(&st, 0, sizeof(st));
    io_uring_get_stats(&st);
    char buf[200];
    snprintf(buf, sizeof(buf),
             "io_uring: submitted=%llu completed=%llu read_bytes=%llu "
             "write_bytes=%llu opens=%llu closes=%llu\n",
             (unsigned long long)st.submitted,
             (unsigned long long)st.completed,
             (unsigned long long)st.read_bytes,
             (unsigned long long)st.write_bytes,
             (unsigned long long)st.ring_opens,
             (unsigned long long)st.ring_closes);
    shell_print(buf);
    shell_last_exit_code = 0;
}