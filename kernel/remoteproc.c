#include "remoteproc.h"
#include "stdint.h"
#include "string.h"
#include "klog.h"

struct remoteproc_global {
    uint8_t     initialized;
    rproc_t     rprocs[REMOTEPROC_MAX];
    uint32_t    rproc_count;
    uint32_t    running_count;
    uint32_t    next_id;
    uint64_t    total_boots;
    uint64_t    total_shutdowns;
    uint64_t    total_crashes;
    uint64_t    total_gets;
    uint64_t    total_puts;
};

static struct remoteproc_global rp_data;

static rproc_t *rp_alloc(const char *name, const char *firmware) {
    for (uint32_t i = 0; i < REMOTEPROC_MAX; i++) {
        if (!rp_data.rprocs[i].used) {
            rproc_t *r = &rp_data.rprocs[i];
            memset(r, 0, sizeof(*r));
            r->id = rp_data.next_id++;
            r->used = 1;
            r->state = RPROC_OFFLINE;
            r->refcnt = 1;
            r->mem_base = 0xD0000000 + (i * 0x00100000);
            r->mem_size = 0x00100000;
            r->boot_addr = r->mem_base;
            r->load_addr = r->mem_base + 0x1000;
            strncpy(r->name, name, REMOTEPROC_NAME_MAX - 1);
            if (firmware) {
                strncpy(r->firmware, firmware, REMOTEPROC_FW_MAX - 1);
            } else {
                strncpy(r->firmware, "rproc/default-fw.bin", REMOTEPROC_FW_MAX - 1);
            }
            rp_data.rproc_count++;
            return r;
        }
    }
    return NULL;
}

int remoteproc_init(void) {
    if (rp_data.initialized) return 0;
    memset(&rp_data, 0, sizeof(rp_data));

    /* 内置模拟协处理器 */
    struct builtin_rproc {
        const char *name;
        const char *fw;
        rproc_state_t state;
    };
    static struct builtin_rproc builtins[] = {
        { "ipu-core0",    "rproc/ipu_fw.bin",    RPROC_RUNNING },
        { "ipu-core1",    "rproc/ipu_fw.bin",    RPROC_RUNNING },
        { "dsp",          "rproc/dsp_fw.bin",    RPROC_OFFLINE },
        { "gpu-pm",       "rproc/gpu_pm.bin",    RPROC_RUNNING },
        { "sensor-hub",   "rproc/sensor_hub.bin", RPROC_RUNNING },
        { "modem-r5",     "rproc/modem_fw.bin",   RPROC_OFFLINE },
        { "wifi-wcn",     "rproc/wlan_fw.bin",    RPROC_RUNNING },
        { NULL, NULL, RPROC_OFFLINE }
    };

    for (int i = 0; builtins[i].name; i++) {
        rproc_t *r = rp_alloc(builtins[i].name, builtins[i].fw);
        if (r) {
            r->state = builtins[i].state;
            if (builtins[i].state == RPROC_RUNNING) {
                r->boot_count = 1;
                rp_data.running_count++;
                rp_data.total_boots++;
            }
        }
    }

    rp_data.initialized = 1;
    klog_info("remoteproc: remote processor framework initialized (%u processors, %u running)",
              rp_data.rproc_count, rp_data.running_count);
    return 0;
}

rproc_t *rproc_find_by_name(const char *name) {
    if (!name || !rp_data.initialized) return NULL;
    for (uint32_t i = 0; i < REMOTEPROC_MAX; i++) {
        if (rp_data.rprocs[i].used &&
            strcmp(rp_data.rprocs[i].name, name) == 0) {
            return &rp_data.rprocs[i];
        }
    }
    return NULL;
}

rproc_t *rproc_get_by_id(int id) {
    if (!rp_data.initialized) return NULL;
    for (uint32_t i = 0; i < REMOTEPROC_MAX; i++) {
        if (rp_data.rprocs[i].used && rp_data.rprocs[i].id == id) {
            return &rp_data.rprocs[i];
        }
    }
    return NULL;
}

void rproc_get(rproc_t *rproc) {
    if (!rproc) return;
    rproc->refcnt++;
    rp_data.total_gets++;
}

void rproc_put(rproc_t *rproc) {
    if (!rproc) return;
    if (rproc->refcnt > 0) rproc->refcnt--;
    rp_data.total_puts++;
}

rproc_t *rproc_alloc(const char *name, const char *firmware) {
    if (!name || !rp_data.initialized) return NULL;
    if (rproc_find_by_name(name)) return NULL;
    return rp_alloc(name, firmware);
}

int rproc_add(rproc_t *rproc) {
    if (!rproc || !rp_data.initialized) return -22;
    /* 已经通过 rproc_alloc 添加 */
    klog_info("remoteproc: added '%s' (firmware: %s)",
              rproc->name, rproc->firmware);
    return 0;
}

int rproc_boot(rproc_t *rproc) {
    if (!rproc || !rp_data.initialized) return -22;
    if (rproc->state == RPROC_RUNNING) return -16;

    rproc->state = RPROC_RUNNING;
    rproc->boot_count++;
    rproc->last_boot = rp_data.total_boots++;
    if (rproc->start) {
        rproc->start(rproc);
    }
    rp_data.running_count++;

    klog_info("remoteproc: booting '%s' (firmware: %s, bootaddr=0x%x)",
              rproc->name, rproc->firmware, rproc->boot_addr);
    return 0;
}

int rproc_shutdown(rproc_t *rproc) {
    if (!rproc || !rp_data.initialized) return -22;
    if (rproc->state != RPROC_RUNNING) return -19;

    if (rproc->stop) {
        rproc->stop(rproc);
    }
    rproc->state = RPROC_OFFLINE;
    rproc->uptime++;
    if (rp_data.running_count > 0) rp_data.running_count--;
    rp_data.total_shutdowns++;

    klog_info("remoteproc: shutting down '%s'", rproc->name);
    return 0;
}

int rproc_attach(rproc_t *rproc) {
    if (!rproc || !rp_data.initialized) return -22;
    rproc->state = RPROC_ATTACHED;
    rp_data.running_count++;
    klog_info("remoteproc: attached to '%s'", rproc->name);
    return 0;
}

int rproc_detach(rproc_t *rproc) {
    if (!rproc || !rp_data.initialized) return -22;
    rproc->state = RPROC_OFFLINE;
    if (rp_data.running_count > 0) rp_data.running_count--;
    klog_info("remoteproc: detached from '%s'", rproc->name);
    return 0;
}

void remoteproc_print_stats(void) {
    if (!rp_data.initialized) {
        klog_info("remoteproc: not initialized");
        return;
    }

    /* 模拟一些活动 */
    static int sim_tick = 0;
    sim_tick++;
    for (uint32_t i = 0; i < REMOTEPROC_MAX; i++) {
        if (rp_data.rprocs[i].used && rp_data.rprocs[i].state == RPROC_RUNNING) {
            rp_data.rprocs[i].uptime++;
            /* 偶发崩溃模拟 */
            if (sim_tick > 100 && (i + sim_tick) % 97 == 0) {
                rp_data.rprocs[i].state = RPROC_CRASHED;
                rp_data.rprocs[i].crash_count++;
                rp_data.rprocs[i].last_crash = sim_tick;
                rp_data.total_crashes++;
                if (rp_data.running_count > 0) rp_data.running_count--;
            }
        }
    }

    const char *state_names[] = {
        "OFFLINE", "SUSPENDED", "RUNNING", "CRASHED", "DELETED", "ATTACHED"
    };

    klog_info("=== Remoteproc (Remote Processor) Statistics ===");
    klog_info("Initialized: yes");
    klog_info("Total remote processors: %u/%d", rp_data.rproc_count, REMOTEPROC_MAX);
    klog_info("Running processors: %u", rp_data.running_count);
    klog_info("Total boots: %llu", (unsigned long long)rp_data.total_boots);
    klog_info("Total shutdowns: %llu", (unsigned long long)rp_data.total_shutdowns);
    klog_info("Total crashes: %llu", (unsigned long long)rp_data.total_crashes);
    klog_info("Total rproc_get() calls: %llu", (unsigned long long)rp_data.total_gets);
    klog_info("Total rproc_put() calls: %llu", (unsigned long long)rp_data.total_puts);
    klog_info("");

    klog_info("Remote processors:");
    for (uint32_t i = 0; i < REMOTEPROC_MAX; i++) {
        rproc_t *r = &rp_data.rprocs[i];
        if (r->used) {
            int state_idx = (int)r->state;
            if (state_idx < 0 || state_idx > 5) state_idx = 0;
            const char *fw_basename = strrchr(r->firmware, '/');
            fw_basename = fw_basename ? fw_basename + 1 : r->firmware;
            klog_info("  [%2d] %-16s %-10s fw=%-22s boots=%u crashes=%u uptime=%llu refs=%u",
                      r->id, r->name, state_names[state_idx], fw_basename,
                      r->boot_count, r->crash_count,
                      (unsigned long long)r->uptime, r->refcnt);
        }
    }
}
