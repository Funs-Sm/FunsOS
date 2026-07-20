#include "tracepoint.h"
#include "stdint.h"
#include "string.h"
#include "klog.h"

struct tracepoint_global {
    uint8_t      initialized;
    tracepoint_t tracepoints[TRACEPOINT_MAX];
    uint32_t     tp_count;
    uint32_t     next_id;
    uint64_t     total_regs;
    uint64_t     total_unregs;
    uint64_t     total_calls;
    uint64_t     sync_count;
    uint8_t      unregister_pending;
};

static struct tracepoint_global tp_data;

static tracepoint_t *tp_alloc(const char *name) {
    for (uint32_t i = 0; i < TRACEPOINT_MAX; i++) {
        if (!tp_data.tracepoints[i].used) {
            tracepoint_t *tp = &tp_data.tracepoints[i];
            memset(tp, 0, sizeof(*tp));
            tp->used = 1;
            tp->id = tp_data.next_id++;
            tp->state = 1;
            tp->probe_count = 0;
            strncpy(tp->name, name, TRACEPOINT_NAME_MAX - 1);
            tp_data.tp_count++;
            return tp;
        }
    }
    return NULL;
}

tracepoint_t *tracepoint_find(const char *name) {
    if (!name || !tp_data.initialized) return NULL;
    for (uint32_t i = 0; i < TRACEPOINT_MAX; i++) {
        if (tp_data.tracepoints[i].used &&
            strcmp(tp_data.tracepoints[i].name, name) == 0) {
            return &tp_data.tracepoints[i];
        }
    }
    return NULL;
}

static tracepoint_t *tp_get_or_create(const char *name) {
    tracepoint_t *tp = tracepoint_find(name);
    if (tp) return tp;
    return tp_alloc(name);
}

int tracepoint_init(void) {
    if (tp_data.initialized) return 0;
    memset(&tp_data, 0, sizeof(tp_data));

    /* 预创建一些常用 tracepoints */
    static const char *builtin_tps[] = {
        "sched_switch", "sched_wakeup", "sched_process_fork",
        "sched_process_exec", "sched_process_exit",
        "irq_handler_entry", "irq_handler_exit",
        "kmem_alloc", "kmem_free",
        "netif_rx", "netif_tx",
        "block_rq_issue", "block_rq_complete",
        "sys_enter", "sys_exit",
        NULL
    };

    for (int i = 0; builtin_tps[i]; i++) {
        tp_alloc(builtin_tps[i]);
    }

    tp_data.initialized = 1;
    klog_info("tracepoint: static tracepoint subsystem initialized (%u tracepoints)",
              tp_data.tp_count);
    return 0;
}

int tracepoint_probe_register(const char *name, tracepoint_func_t probe, void *data) {
    if (!name || !probe || !tp_data.initialized) return -22;

    tracepoint_t *tp = tp_get_or_create(name);
    if (!tp) return -28;

    for (uint32_t i = 0; i < TRACEPOINT_MAX_PROBES; i++) {
        if (tp->probes[i].used && tp->probes[i].func == probe &&
            tp->probes[i].data == data) {
            return -17;
        }
    }

    int idx = -1;
    for (uint32_t i = 0; i < TRACEPOINT_MAX_PROBES; i++) {
        if (!tp->probes[i].used) { idx = i; break; }
    }
    if (idx < 0) return -28;

    tp->probes[idx].used = 1;
    tp->probes[idx].func = probe;
    tp->probes[idx].data = data;
    tp->probes[idx].hit_count = 0;
    tp->probe_count++;
    tp->reg_count++;
    tp_data.total_regs++;

    klog_info("tracepoint: registered probe for '%s' (%u probes)", name, tp->probe_count);
    return 0;
}

int tracepoint_probe_unregister(const char *name, tracepoint_func_t probe, void *data) {
    if (!name || !probe || !tp_data.initialized) return -22;

    tracepoint_t *tp = tracepoint_find(name);
    if (!tp) return -3;

    for (uint32_t i = 0; i < TRACEPOINT_MAX_PROBES; i++) {
        if (tp->probes[i].used && tp->probes[i].func == probe &&
            tp->probes[i].data == data) {
            tp->probes[i].used = 0;
            tp->probes[i].func = NULL;
            tp->probes[i].data = NULL;
            tp->probe_count--;
            tp->unreg_count++;
            tp_data.total_unregs++;
            tp_data.unregister_pending = 1;
            klog_info("tracepoint: unregistered probe from '%s' (%u probes)",
                      name, tp->probe_count);
            return 0;
        }
    }

    return -3;
}

void tracepoint_synchronize_unregister(void) {
    if (!tp_data.initialized) return;
    tp_data.sync_count++;
    tp_data.unregister_pending = 0;
    klog_info("tracepoint: synchronize_unregister completed (sync=%llu)",
              (unsigned long long)tp_data.sync_count);
}

int tracepoint_enable(const char *name) {
    tracepoint_t *tp = tracepoint_find(name);
    if (!tp) return -3;
    tp->state = 1;
    return 0;
}

int tracepoint_disable(const char *name) {
    tracepoint_t *tp = tracepoint_find(name);
    if (!tp) return -3;
    tp->state = 0;
    return 0;
}

void tracepoint_call(tracepoint_t *tp, unsigned long a1, unsigned long a2,
                     unsigned long a3, unsigned long a4) {
    if (!tp || !tp->used || !tp->state) return;

    tp->call_count++;
    tp_data.total_calls++;

    for (uint32_t i = 0; i < TRACEPOINT_MAX_PROBES; i++) {
        if (tp->probes[i].used && tp->probes[i].func) {
            tp->probes[i].hit_count++;
            tp->probes[i].func(tp->probes[i].data, a1, a2, a3, a4);
        }
    }
}

void tracepoint_print_stats(void) {
    if (!tp_data.initialized) {
        klog_info("tracepoint: not initialized");
        return;
    }

    klog_info("=== Tracepoint Subsystem Statistics ===");
    klog_info("Initialized: yes");
    klog_info("Total tracepoints: %u/%d", tp_data.tp_count, TRACEPOINT_MAX);
    klog_info("Total probe registrations: %llu", (unsigned long long)tp_data.total_regs);
    klog_info("Total probe unregistrations: %llu", (unsigned long long)tp_data.total_unregs);
    klog_info("Total tracepoint calls: %llu", (unsigned long long)tp_data.total_calls);
    klog_info("Synchronize_unregister calls: %llu", (unsigned long long)tp_data.sync_count);
    klog_info("Unregister pending: %s", tp_data.unregister_pending ? "yes" : "no");
    klog_info("");

    uint32_t active_tps = 0, total_probes = 0;
    for (uint32_t i = 0; i < TRACEPOINT_MAX; i++) {
        if (tp_data.tracepoints[i].used) {
            active_tps++;
            total_probes += tp_data.tracepoints[i].probe_count;
        }
    }
    klog_info("Active tracepoints: %u", active_tps);
    klog_info("Attached probes total: %u", total_probes);
    klog_info("");

    klog_info("Tracepoints with probes:");
    int shown = 0;
    for (uint32_t i = 0; i < TRACEPOINT_MAX && shown < 16; i++) {
        tracepoint_t *tp = &tp_data.tracepoints[i];
        if (tp->used && tp->probe_count > 0) {
            klog_info("  [%-2d] %-24s probes=%u calls=%llu state=%s",
                      tp->id, tp->name, tp->probe_count,
                      (unsigned long long)tp->call_count,
                      tp->state ? "enabled" : "disabled");
            for (uint32_t j = 0; j < TRACEPOINT_MAX_PROBES; j++) {
                if (tp->probes[j].used) {
                    klog_info("         probe @0x%x data=%p hits=%llu",
                              (uint32_t)(uintptr_t)tp->probes[j].func,
                              tp->probes[j].data,
                              (unsigned long long)tp->probes[j].hit_count);
                }
            }
            shown++;
        }
    }

    klog_info("");
    klog_info("Built-in tracepoints (first 8, no probes):");
    shown = 0;
    for (uint32_t i = 0; i < TRACEPOINT_MAX && shown < 8; i++) {
        tracepoint_t *tp = &tp_data.tracepoints[i];
        if (tp->used && tp->probe_count == 0) {
            klog_info("  [%-2d] %-24s calls=%llu", tp->id, tp->name,
                      (unsigned long long)tp->call_count);
            shown++;
        }
    }
}
