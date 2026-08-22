/*
 * kernel/cmd_procinfo.h - Process-information shell commands (PR-7, v0.9)
 *
 *   strace PID         toggle syscall trace on a PID (best-effort)
 *   lsof [PATH]        list open file descriptors (informational)
 *   prlimit [PID]      show rlimit summary
 *   capsh              see cmd_ns.c
 *   sysreport          run a system-health snapshot
 *   kprobe [on|off]    toggle kprobe subsystem
 *   notifier           show notifier-chain stats
 *   kwork              show kernel workqueue stats
 *   dumpstack [N]      see cmd_kdebug.c (alias)
 *   ksym               show kernel symbol table stats
 *   mem                show memory summary (kheap / pages)
 *   dev                show device tree summary
 *   schedpolicy        show scheduler policy table
 *   mempolicy          show memory policy (NUMA-style)
 *   taskset            show CPU-affinity map
 *   chrt               show scheduling-class summary
 *   last               show recent boot / event timestamps
 *   pidof NAME         show PID(s) for a process name
 *   pstree             show process tree (depth-limited)
 */
#ifndef _KERNEL_CMD_PROCINFO_H
#define _KERNEL_CMD_PROCINFO_H

void cmd_strace(const char *args);
void cmd_lsof(const char *args);
void cmd_prlimit(const char *args);
void cmd_sysreport(const char *args);
void cmd_kprobe(const char *args);
void cmd_notifier(const char *args);
void cmd_kwork(const char *args);
void cmd_mem(const char *args);
void cmd_dev(const char *args);
void cmd_schedpolicy(const char *args);
void cmd_mempolicy(const char *args);
void cmd_taskset(const char *args);
void cmd_chrt(const char *args);
void cmd_last(const char *args);
void cmd_pidof(const char *args);
void cmd_pstree(const char *args);
void cmd_dumpstack(const char *args);
void cmd_dcache(const char *args);
void cmd_acpi(const char *args);
void cmd_signal(const char *args);
void cmd_rbtree(const char *args);
void cmd_io(const char *args);
void cmd_eventfd(const char *args);
void cmd_timerfd(const char *args);
void cmd_signalfd(const char *args);
void cmd_evloop(const char *args);
<<<<<<< HEAD
void cmd_xattr(const char *args);
void cmd_path_hash(const char *args);
=======
>>>>>>> 7160e70 (v0.9: add eventfd/timerfd/signalfd + lib/tinyevloop + 4 new cmd_*)

#endif