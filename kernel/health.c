#include "health.h"
#include "klog.h"
#include "string.h"
#include "pmm.h"
#include "vmm.h"
#include "sched.h"

static health_report_t g_report;
static health_thresholds_t g_thresholds;
static int g_initialized = 0;

static health_status_t pct_to_status(uint32_t pct,
                                     uint32_t warn, uint32_t crit) {
    if (pct >= crit) return HEALTH_CRITICAL;
    if (pct >= warn) return HEALTH_WARNING;
    return HEALTH_OK;
}

static void health_check_cpu(health_cpu_t *cpu) {
    extern uint32_t pmm_get_total_pages(void);
    extern uint32_t pmm_get_free_pages(void);

    /* Simplified CPU stats (simulated for demo) */
    cpu->user_pct = 5;
    cpu->sys_pct = 3;
    cpu->idle_pct = 92;
    cpu->iowait_pct = 0;
    cpu->load_avg_1min = 0;
    cpu->load_avg_5min = 0;
    cpu->load_avg_15min = 0;
    cpu->running_procs = 1;
    cpu->total_procs = 4;

    uint32_t used_pct = 100 - cpu->idle_pct;
    cpu->status = pct_to_status(used_pct,
                                 g_thresholds.cpu_warn_pct,
                                 g_thresholds.cpu_crit_pct);
}

static void health_check_memory(health_memory_t *mem) {
    extern uint32_t pmm_get_total_pages(void);
    extern uint32_t pmm_get_free_pages(void);

    uint32_t total_pages = pmm_get_total_pages();
    uint32_t free_pages = pmm_get_free_pages();
    uint32_t used_pages = total_pages > free_pages ? total_pages - free_pages : 0;

    mem->total_kb = (uint64_t)total_pages * 4;
    mem->used_kb = (uint64_t)used_pages * 4;
    mem->free_kb = (uint64_t)free_pages * 4;
    mem->cached_kb = 0;
    mem->buffers_kb = 0;
    mem->swap_total_kb = 0;
    mem->swap_used_kb = 0;
    mem->swap_free_kb = 0;
    mem->used_pct = total_pages > 0 ? (used_pages * 100) / total_pages : 0;

    mem->status = pct_to_status(mem->used_pct,
                                 g_thresholds.mem_warn_pct,
                                 g_thresholds.mem_crit_pct);
}

static void health_check_disk(health_disk_t *disk) {
    strncpy(disk->device, "sda", sizeof(disk->device) - 1);
    disk->total_kb = 1024 * 1024; /* 1 GB */
    disk->used_kb = 256 * 1024;   /* 256 MB */
    disk->free_kb = disk->total_kb - disk->used_kb;
    disk->used_pct = disk->total_kb > 0 ?
                     (disk->used_kb * 100) / disk->total_kb : 0;
    disk->read_bytes = 0;
    disk->write_bytes = 0;
    disk->io_pct = 0;

    disk->status = pct_to_status(disk->used_pct,
                                  g_thresholds.disk_warn_pct,
                                  g_thresholds.disk_crit_pct);
}

static void health_check_network(health_network_t *net) {
    strncpy(net->iface, "eth0", sizeof(net->iface) - 1);
    net->rx_bytes = 0;
    net->tx_bytes = 0;
    net->rx_packets = 0;
    net->tx_packets = 0;
    net->rx_errors = 0;
    net->tx_errors = 0;
    net->speed_mbps = 100;
    net->status = HEALTH_OK;
}

int health_check(void) {
    if (!g_initialized) health_init();

    health_check_cpu(&g_report.cpu);
    health_check_memory(&g_report.memory);
    health_check_disk(&g_report.disk);
    health_check_network(&g_report.network);

    /* Calculate overall status */
    health_status_t overall = HEALTH_OK;
    if (g_report.cpu.status > overall) overall = g_report.cpu.status;
    if (g_report.memory.status > overall) overall = g_report.memory.status;
    if (g_report.disk.status > overall) overall = g_report.disk.status;
    if (g_report.network.status > overall) overall = g_report.network.status;
    g_report.overall = overall;

    g_report.check_count++;

    return 0;
}

void health_init(void) {
    memset(&g_report, 0, sizeof(g_report));
    memset(&g_thresholds, 0, sizeof(g_thresholds));

    /* Default thresholds */
    g_thresholds.cpu_warn_pct = 70;
    g_thresholds.cpu_crit_pct = 90;
    g_thresholds.mem_warn_pct = 80;
    g_thresholds.mem_crit_pct = 95;
    g_thresholds.disk_warn_pct = 80;
    g_thresholds.disk_crit_pct = 95;
    g_thresholds.temp_warn_c = 70;
    g_thresholds.temp_crit_c = 90;

    g_initialized = 1;
    health_check();
    klog_info("health: system health monitor initialized");
}

int health_get_report(health_report_t *report) {
    if (!report) return -22;
    if (!g_initialized) health_init();

    health_check();
    memcpy(report, &g_report, sizeof(g_report));
    return 0;
}

health_status_t health_get_status(void) {
    if (!g_initialized) health_init();
    return g_report.overall;
}

int health_set_thresholds(const health_thresholds_t *th) {
    if (!th) return -22;
    memcpy(&g_thresholds, th, sizeof(g_thresholds));
    return 0;
}

int health_get_thresholds(health_thresholds_t *th) {
    if (!th) return -22;
    memcpy(th, &g_thresholds, sizeof(g_thresholds));
    return 0;
}

const char *health_status_string(health_status_t status) {
    switch (status) {
        case HEALTH_OK:       return "OK";
        case HEALTH_WARNING:  return "WARNING";
        case HEALTH_CRITICAL: return "CRITICAL";
        case HEALTH_DEAD:     return "DEAD";
        default:              return "UNKNOWN";
    }
}
