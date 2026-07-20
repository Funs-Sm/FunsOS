#include "oom_killer.h"
#include "process.h"
#include "kernel_proc.h"
#include "pmm.h"
#include "kheap.h"
#include "klog.h"
#include "string.h"
#include "timer.h"
#include "stdio.h"
#include "sched.h"
#include "signal.h"

static struct {
    uint8_t initialized;
    uint8_t oom_triggered;
    uint64_t kills;
    uint64_t last_kill_time;
    uint32_t panic_on_oom;
} oom_state;

void oom_init(void) {
    if (oom_state.initialized) return;
    memset(&oom_state, 0, sizeof(oom_state));
    oom_state.panic_on_oom = 0;
    oom_state.initialized = 1;
    klog_info("OOM killer initialized");
}

int32_t oom_evaluate_task(pcb_t *proc) {
    if (!proc) return -10000;
    if (proc->pid == 0 || proc->pid == 1) return -10000;
    if (proc->state == PROCESS_ZOMBIE) return -10000;

    int32_t points = 100;
    if (proc->state == PROCESS_RUNNING) points += 20;
    if (proc->nice > 0) points += proc->nice * 2;
    points += proc->cpu_time / 100;
    if (points < 1) points = 1;
    return points;
}

void oom_kill_process(uint32_t pid, const char *message) {
    pcb_t *proc = process_get_pcb((pid_t)pid);
    if (!proc) {
        klog_warn("OOM: Cannot find PID %u to kill", pid);
        return;
    }

    klog_emerg("OOM: Killed process %u (%s) - %s",
               pid, proc->name, message ? message : "Out of memory");

    oom_state.kills++;
    oom_state.last_kill_time = timer_get_ticks();
    oom_state.oom_triggered = 0;

    kill((pid_t)pid, 9);
}

int oom_kill_best_task(oom_trigger_t trigger) {
    (void)trigger;
    int32_t worst_score = -1;
    pcb_t *worst_proc = NULL;

    for (uint32_t i = 2; i < MAX_PROCESSES; i++) {
        pcb_t *proc = process_get_pcb((pid_t)i);
        if (!proc) continue;
        int32_t score = oom_evaluate_task(proc);
        if (score > worst_score) {
            worst_score = score;
            worst_proc = proc;
        }
    }

    if (worst_proc) {
        oom_kill_process((uint32_t)worst_proc->pid, "OOM victim selected");
        return 0;
    }

    klog_emerg("OOM: No killable processes found!");
    return -1;
}

void oom_out_of_memory(oom_trigger_t trigger, uint32_t order) {
    (void)order;
    if (oom_state.oom_triggered) return;
    oom_state.oom_triggered = 1;

    klog_emerg("============================================");
    klog_emerg("OUT OF MEMORY!");
    klog_emerg("Trigger: %s, order: %u",
               trigger == OOM_TRIGGER_PAGE_ALLOC ? "page alloc" :
               trigger == OOM_TRIGGER_KHEAP_ALLOC ? "kheap alloc" : "manual",
               order);
    klog_emerg("Free pages: %u / %u", pmm_get_free_pages(), pmm_get_total_pages());
    klog_emerg("============================================");

    if (oom_state.panic_on_oom) {
        klog_emerg("OOM: panic_on_oom set");
        for (volatile int i = 0; i < 10000000; i++) { asm volatile("pause"); }
        oom_state.oom_triggered = 0;
        return;
    }

    oom_kill_best_task(trigger);
    for (volatile int i = 0; i < 1000000; i++) { asm volatile("pause"); }
}

int oom_set_adj(uint32_t pid, int32_t adj) {
    (void)pid; (void)adj;
    return 0;
}

int32_t oom_get_adj(uint32_t pid) {
    (void)pid;
    return 0;
}

void oom_update_score(pcb_t *proc) {
    if (!proc) return;
    oom_evaluate_task(proc);
}

void oom_print_score_table(void) {
    klog_info("=== OOM Score Table ===");
    klog_info("  PID  SCORE  NAME");
    for (uint32_t i = 0; i < MAX_PROCESSES; i++) {
        pcb_t *proc = process_get_pcb((pid_t)i);
        if (!proc) continue;
        int32_t score = oom_evaluate_task(proc);
        klog_info("  %-4u %-6d %s", (uint32_t)proc->pid, score, proc->name);
    }
    klog_info("Free pages: %u", pmm_get_free_pages());
    klog_info("Total kills: %lu", (unsigned long)oom_state.kills);
}

uint8_t oom_is_triggered(void) {
    return oom_state.oom_triggered;
}

uint64_t oom_kill_count(void) {
    return oom_state.kills;
}
