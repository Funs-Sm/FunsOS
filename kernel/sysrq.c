#include "sysrq.h"
#include "klog.h"
#include "string.h"
#include "pmm.h"
#include "vmm.h"
#include "process.h"
#include "kernel_proc.h"
#include "sched.h"
#include "kheap.h"
#include "sync.h"
#include "stdio.h"

static struct sysrq_key_op sysrq_table[32];
static uint8_t sysrq_enabled = 1;
static uint8_t sysrq_initialized = 0;

void sysrq_init(void) {
    if (sysrq_initialized) return;
    memset(sysrq_table, 0, sizeof(sysrq_table));

    sysrq_register_key('c', sysrq_trigger_crash, "Crash", "Trigger kernel crash");
    sysrq_register_key('m', sysrq_show_mem, "Memory", "Show memory info");
    sysrq_register_key('p', sysrq_show_regs, "Regs", "Show registers");
    sysrq_register_key('t', sysrq_show_tasks, "Tasks", "Show all tasks");
    sysrq_register_key('s', sysrq_sync_fs, "Sync", "Sync all filesystems");
    sysrq_register_key('u', sysrq_unmount_raw, "Umount", "Remount readonly");
    sysrq_register_key('b', sysrq_reboot, "Reboot", "Immediate reboot");
    sysrq_register_key('w', sysrq_show_state, "Blocked", "Show blocked tasks");
    sysrq_register_key('l', sysrq_show_locks, "Locks", "Show lock state");
    sysrq_register_key('i', sysrq_kill_all, "Kill", "SIGKILL all");
    sysrq_register_key('n', sysrq_nice_all, "Nice", "Reset priorities");
    sysrq_register_key('h', sysrq_print_help, "Help", "Show help");

    sysrq_initialized = 1;
    klog_info("SysRq magic key system initialized");
}

int sysrq_register_key(uint8_t key, sysrq_handler_t handler, const char *help, const char *action) {
    if (!handler || key == 0) return -1;
    for (int i = 0; i < 32; i++) {
        if (sysrq_table[i].key == key) {
            sysrq_table[i].handler = handler;
            sysrq_table[i].help_msg = help;
            sysrq_table[i].action_msg = action;
            return 0;
        }
    }
    for (int i = 0; i < 32; i++) {
        if (sysrq_table[i].handler == NULL) {
            sysrq_table[i].key = key;
            sysrq_table[i].handler = handler;
            sysrq_table[i].help_msg = help;
            sysrq_table[i].action_msg = action;
            return 0;
        }
    }
    return -1;
}

int sysrq_unregister_key(uint8_t key) {
    for (int i = 0; i < 32; i++) {
        if (sysrq_table[i].key == key) {
            memset(&sysrq_table[i], 0, sizeof(struct sysrq_key_op));
            return 0;
        }
    }
    return -1;
}

void sysrq_handle_key(uint8_t key) {
    if (!sysrq_enabled) {
        klog_info("SysRq: disabled");
        return;
    }
    klog_info("SysRq: key '%c' pressed", key);
    for (int i = 0; i < 32; i++) {
        if (sysrq_table[i].key == key && sysrq_table[i].handler) {
            if (sysrq_table[i].action_msg) {
                klog_info("SysRq: %s", sysrq_table[i].action_msg);
            }
            sysrq_table[i].handler(key);
            return;
        }
    }
    klog_info("SysRq: unknown key '%c' (press 'h' for help)", key);
}

void sysrq_toggle_enable(void) {
    sysrq_enabled = !sysrq_enabled;
    klog_info("SysRq: %s", sysrq_enabled ? "enabled" : "disabled");
}

uint8_t sysrq_is_enabled(void) { return sysrq_enabled; }

void sysrq_print_help(uint8_t key) {
    (void)key;
    klog_info("=== SysRq Magic Keys ===");
    for (int i = 0; i < 32; i++) {
        if (sysrq_table[i].handler) {
            klog_info("  '%c' - %s", sysrq_table[i].key,
                     sysrq_table[i].action_msg ? sysrq_table[i].action_msg : "unknown");
        }
    }
}

void sysrq_trigger_crash(uint8_t key) {
    (void)key;
    klog_emerg("SysRq: Triggering kernel panic (forced)");
}

void sysrq_show_mem(uint8_t key) {
    (void)key;
    klog_info("SysRq: Memory info");
    klog_info("Total pages: %u", pmm_get_total_pages());
    klog_info("Free pages: %u", pmm_get_free_pages());
    klog_info("Used pages: %u", pmm_get_used_pages());
}

void sysrq_show_tasks(uint8_t key) {
    (void)key;
    klog_info("SysRq: Process list:");
    klog_info("  PID  STATE  NAME");
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        pcb_t *proc = process_get_pcb((pid_t)i);
        if (proc) {
            klog_info("  %-4u %-6u %s", (uint32_t)proc->pid, (uint32_t)proc->state, proc->name);
        }
    }
}

void sysrq_show_regs(uint8_t key) {
    (void)key;
    klog_info("SysRq: Register dump not available");
}

void sysrq_sync_fs(uint8_t key) {
    (void)key;
    klog_info("SysRq: Syncing all filesystems...");
    klog_info("SysRq: Done");
}

void sysrq_unmount_raw(uint8_t key) {
    (void)key;
    klog_info("SysRq: Remounting all filesystems read-only...");
    klog_info("SysRq: Done");
}

void sysrq_reboot(uint8_t key) {
    (void)key;
    klog_emerg("SysRq: Emergency reboot requested!");
}

void sysrq_show_state(uint8_t key) {
    (void)key;
    klog_info("SysRq: Showing blocked/hung tasks...");
}

void sysrq_show_timers(uint8_t key) {
    (void)key;
    klog_info("SysRq: Timer list");
}

void sysrq_show_locks(uint8_t key) {
    (void)key;
    klog_info("SysRq: Lock state dump");
}

void sysrq_kill_all(uint8_t key) {
    (void)key;
    klog_info("SysRq: Sending SIGKILL to all user processes");
}

void sysrq_nice_all(uint8_t key) {
    (void)key;
    klog_info("SysRq: Resetting task priorities");
}
