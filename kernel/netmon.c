/* netmon.c - 网络监视子系统实现
 *
 * 使用 net_get_interface_count/net_get_interface 采样接口统计，
 * 基于 kwork 周期性触发，变化写入 evlog。
 */
#include "netmon.h"
#include "kheap.h"
#include "string.h"
#include "stdlib.h"
#include "stdio.h"
#include "spinlock.h"
#include "klog.h"
#include "fundb.h"
#include "evlog.h"
#include "kwork.h"
#include "timer.h"
#include "../net/net.h"

/* ============================================================
 * 内部状态
 * ============================================================ */

typedef struct {
    spinlock_t          lock;
    fundb_handle_t      db;
    uint8_t             initialized;
    netmon_stats_t      stats;
    netmon_sample_t     history[NETMON_HISTORY_SIZE];
    uint32_t            history_head;
    uint32_t            history_count;
    netmon_if_snapshot_t last_snapshot[NETMON_MAX_INTERFACES];
    uint32_t            last_count;
    kwork_t             sample_work;
    uint32_t            sample_interval;
} netmon_globals_t;

static netmon_globals_t g_netmon;

/* ============================================================
 * 辅助：IP 格式化
 * ============================================================ */

static void netmon_ip_str(uint32_t ip, char *buf, uint32_t size) {
    snprintf(buf, size, "%u.%u.%u.%u",
             (ip & 0xFF), (ip >> 8) & 0xFF,
             (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
}

/* ============================================================
 * FunDB 持久化
 * ============================================================ */

#define NETMON_TABLE_HISTORY "netmon_history"

static void netmon_db_init_table(void) {
    if (!g_netmon.db) return;
    fundb_column_t cols[9];
    memset(cols, 0, sizeof(cols));

    strcpy(cols[0].name, "sample_tick");
    cols[0].type = FUNDB_TYPE_INT;
    cols[0].size = 4;

    strcpy(cols[1].name, "ifname");
    cols[1].type = FUNDB_TYPE_TEXT;
    cols[1].size = NETMON_IFNAME_LEN;

    strcpy(cols[2].name, "ip");
    cols[2].type = FUNDB_TYPE_INT;
    cols[2].size = 4;

    strcpy(cols[3].name, "up");
    cols[3].type = FUNDB_TYPE_INT;
    cols[3].size = 4;

    strcpy(cols[4].name, "tx_packets");
    cols[4].type = FUNDB_TYPE_INT;
    cols[4].size = 4;

    strcpy(cols[5].name, "rx_packets");
    cols[5].type = FUNDB_TYPE_INT;
    cols[5].size = 4;

    strcpy(cols[6].name, "tx_bytes");
    cols[6].type = FUNDB_TYPE_INT;
    cols[6].size = 4;

    strcpy(cols[7].name, "tx_errors");
    cols[7].type = FUNDB_TYPE_INT;
    cols[7].size = 4;

    strcpy(cols[8].name, "rx_errors");
    cols[8].type = FUNDB_TYPE_INT;
    cols[8].size = 4;

    if (!fundb_table_exists(g_netmon.db, NETMON_TABLE_HISTORY)) {
        fundb_create_table(g_netmon.db, NETMON_TABLE_HISTORY, cols, 9);
    }
}

static void netmon_db_save_sample(const netmon_sample_t *s) {
    if (!g_netmon.db || !s) return;
    for (uint32_t i = 0; i < s->interface_count; i++) {
        const netmon_if_snapshot_t *ifc = &s->interfaces[i];
        fundb_row_t row;
        void *vals[9];
        uint32_t sizes[9];
        uint32_t types[9];

        uint32_t tick = (uint32_t)s->sample_tick;
        uint32_t ip = ifc->ip;
        uint32_t up = ifc->up;
        uint32_t txp = ifc->tx_packets;
        uint32_t rxp = ifc->rx_packets;
        uint32_t txb = ifc->tx_bytes;
        uint32_t txe = ifc->tx_errors;
        uint32_t rxe = ifc->rx_errors;

        vals[0] = &tick; sizes[0] = 4; types[0] = FUNDB_TYPE_INT;
        vals[1] = (void *)ifc->name; sizes[1] = (uint32_t)strlen(ifc->name) + 1; types[1] = FUNDB_TYPE_TEXT;
        vals[2] = &ip; sizes[2] = 4; types[2] = FUNDB_TYPE_INT;
        vals[3] = &up; sizes[3] = 4; types[3] = FUNDB_TYPE_INT;
        vals[4] = &txp; sizes[4] = 4; types[4] = FUNDB_TYPE_INT;
        vals[5] = &rxp; sizes[5] = 4; types[5] = FUNDB_TYPE_INT;
        vals[6] = &txb; sizes[6] = 4; types[6] = FUNDB_TYPE_INT;
        vals[7] = &txe; sizes[7] = 4; types[7] = FUNDB_TYPE_INT;
        vals[8] = &rxe; sizes[8] = 4; types[8] = FUNDB_TYPE_INT;

        row.values = vals;
        row.sizes = sizes;
        row.types = types;
        fundb_insert(g_netmon.db, NETMON_TABLE_HISTORY, &row);
    }
}

/* ============================================================
 * 采样
 * ============================================================ */

static void netmon_take_snapshot(netmon_sample_t *s) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->sample_tick = (uint64_t)timer_get_ticks();

    uint32_t if_count = net_get_interface_count();
    if (if_count > NETMON_MAX_INTERFACES) if_count = NETMON_MAX_INTERFACES;

    for (uint32_t i = 0; i < if_count; i++) {
        net_interface_t *iface = net_get_interface(i);
        if (!iface) continue;
        netmon_if_snapshot_t *snap = &s->interfaces[s->interface_count];
        strncpy(snap->name, iface->name, NETMON_IFNAME_LEN - 1);
        snap->ip = iface->ip.addr;
        snap->mask = iface->mask.addr;
        snap->gateway = iface->gateway.addr;
        snap->up = iface->up;
        snap->flags = (uint8_t)(iface->flags & 0xFF);
        snap->tx_packets = iface->tx_packets;
        snap->rx_packets = iface->rx_packets;
        snap->tx_bytes = iface->tx_bytes;
        snap->rx_bytes = iface->rx_bytes;
        snap->tx_errors = iface->tx_errors;
        snap->rx_errors = iface->rx_errors;
        snap->sample_tick = s->sample_tick;
        s->interface_count++;
    }
}

/* 对比新旧快照，检测变化并写入 evlog */
static void netmon_detect_changes(const netmon_if_snapshot_t *prev,
                                  uint32_t prev_count,
                                  const netmon_if_snapshot_t *curr,
                                  uint32_t curr_count) {
    /* 检测新增接口和状态变化 */
    for (uint32_t i = 0; i < curr_count; i++) {
        const netmon_if_snapshot_t *c = &curr[i];
        const netmon_if_snapshot_t *p = NULL;
        for (uint32_t j = 0; j < prev_count; j++) {
            if (strcmp(prev[j].name, c->name) == 0) {
                p = &prev[j];
                break;
            }
        }

        if (!p) {
            /* 新接口 */
            char ipbuf[32];
            netmon_ip_str(c->ip, ipbuf, sizeof(ipbuf));
            evlog_info("NetMon", 1, "interface '%s' appeared (ip=%s up=%u)",
                       c->name, ipbuf, c->up);
            spinlock_lock(&g_netmon.lock);
            g_netmon.stats.interfaces_seen++;
            g_netmon.stats.state_changes++;
            spinlock_unlock(&g_netmon.lock);
            continue;
        }

        /* 检测 up/down 变化 */
        if (p->up != c->up) {
            evlog_warn("NetMon", 2, "interface '%s' state %s->%s",
                       c->name, p->up ? "UP" : "DOWN",
                       c->up ? "UP" : "DOWN");
            spinlock_lock(&g_netmon.lock);
            g_netmon.stats.state_changes++;
            spinlock_unlock(&g_netmon.lock);
        }

        /* 检测 IP 变化 */
        if (p->ip != c->ip) {
            char oldip[32], newip[32];
            netmon_ip_str(p->ip, oldip, sizeof(oldip));
            netmon_ip_str(c->ip, newip, sizeof(newip));
            evlog_info("NetMon", 3, "interface '%s' IP %s->%s",
                       c->name, oldip, newip);
        }

        /* 检测错误激增 */
        uint32_t tx_err_delta = (c->tx_errors > p->tx_errors) ?
                                (c->tx_errors - p->tx_errors) : 0;
        uint32_t rx_err_delta = (c->rx_errors > p->rx_errors) ?
                                (c->rx_errors - p->rx_errors) : 0;
        if (tx_err_delta >= NETMON_ERROR_THRESHOLD ||
            rx_err_delta >= NETMON_ERROR_THRESHOLD) {
            evlog_warn("NetMon", 4,
                       "interface '%s' error spike: tx +%u, rx +%u",
                       c->name, tx_err_delta, rx_err_delta);
            spinlock_lock(&g_netmon.lock);
            g_netmon.stats.error_alerts++;
            spinlock_unlock(&g_netmon.lock);
        }
    }

    /* 检测消失的接口 */
    for (uint32_t i = 0; i < prev_count; i++) {
        const netmon_if_snapshot_t *p = &prev[i];
        int found = 0;
        for (uint32_t j = 0; j < curr_count; j++) {
            if (strcmp(curr[j].name, p->name) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            evlog_warn("NetMon", 5, "interface '%s' disappeared", p->name);
            spinlock_lock(&g_netmon.lock);
            g_netmon.stats.state_changes++;
            spinlock_unlock(&g_netmon.lock);
        }
    }
}

void netmon_sample(void) {
    if (!g_netmon.initialized) return;

    netmon_sample_t s;
    netmon_take_snapshot(&s);

    /* 对比上次快照 */
    spinlock_lock(&g_netmon.lock);
    netmon_if_snapshot_t prev[NETMON_MAX_INTERFACES];
    uint32_t prev_count = g_netmon.last_count;
    memcpy(prev, g_netmon.last_snapshot, sizeof(prev));

    /* 保存为最新快照 */
    memcpy(g_netmon.last_snapshot, s.interfaces, sizeof(s.interfaces));
    g_netmon.last_count = s.interface_count;

    /* 存入历史 */
    uint32_t idx = g_netmon.history_head;
    g_netmon.history[idx] = s;
    g_netmon.history_head = (g_netmon.history_head + 1) % NETMON_HISTORY_SIZE;
    if (g_netmon.history_count < NETMON_HISTORY_SIZE) {
        g_netmon.history_count++;
    }

    g_netmon.stats.total_samples++;
    g_netmon.stats.last_sample_tick = s.sample_tick;
    spinlock_unlock(&g_netmon.lock);

    /* 检测变化（第一次采样 prev_count=0 时只记录新接口） */
    netmon_detect_changes(prev, prev_count, s.interfaces, s.interface_count);

    /* 持久化到 FunDB */
    netmon_db_save_sample(&s);
}

/* kwork 周期回调 */
static void netmon_sample_work_fn(void *data) {
    (void)data;
    netmon_sample();
}

/* ============================================================
 * 公共 API
 * ============================================================ */

void netmon_init(void) {
    memset(&g_netmon, 0, sizeof(g_netmon));
    spinlock_init(&g_netmon.lock);
    g_netmon.sample_interval = NETMON_SAMPLE_INTERVAL;

    g_netmon.db = fundb_open(NETMON_DB_PATH);
    if (g_netmon.db) {
        netmon_db_init_table();
    } else {
        klog_warn("netmon: failed to open FunDB at %s", NETMON_DB_PATH);
    }

    g_netmon.initialized = 1;

    /* 立即采样一次建立基线 */
    netmon_sample();

    /* 启动周期性 kwork */
    kwork_init_work(&g_netmon.sample_work, netmon_sample_work_fn, NULL);
    kwork_queue_periodic_work(&g_netmon.sample_work, g_netmon.sample_interval);

    klog_info("netmon: initialized, sampling every %ums",
              g_netmon.sample_interval);
    evlog_info("NetMon", 0, "network monitor started (interval=%ums)",
               g_netmon.sample_interval);
}

void netmon_shutdown(void) {
    spinlock_lock(&g_netmon.lock);
    g_netmon.initialized = 0;
    kwork_cancel_work(&g_netmon.sample_work);
    spinlock_unlock(&g_netmon.lock);
}

uint32_t netmon_get_latest(netmon_if_snapshot_t *out, uint32_t max_count) {
    if (!out) return 0;
    spinlock_lock(&g_netmon.lock);
    uint32_t n = g_netmon.last_count;
    if (n > max_count) n = max_count;
    for (uint32_t i = 0; i < n; i++) {
        out[i] = g_netmon.last_snapshot[i];
    }
    spinlock_unlock(&g_netmon.lock);
    return n;
}

uint32_t netmon_get_history(netmon_sample_t *out, uint32_t max_count) {
    if (!out || max_count == 0) return 0;
    spinlock_lock(&g_netmon.lock);

    uint32_t count = g_netmon.history_count;
    if (count > max_count) count = max_count;

    /* 从最新到最旧 */
    for (uint32_t i = 0; i < count; i++) {
        int idx = (int)g_netmon.history_head - 1 - (int)i;
        while (idx < 0) idx += NETMON_HISTORY_SIZE;
        out[i] = g_netmon.history[idx];
    }

    spinlock_unlock(&g_netmon.lock);
    return count;
}

void netmon_get_stats(netmon_stats_t *stats) {
    if (!stats) return;
    spinlock_lock(&g_netmon.lock);
    *stats = g_netmon.stats;
    spinlock_unlock(&g_netmon.lock);
}

void netmon_reset_stats(void) {
    spinlock_lock(&g_netmon.lock);
    uint32_t interval = g_netmon.stats.sample_interval_ms;
    memset(&g_netmon.stats, 0, sizeof(g_netmon.stats));
    g_netmon.stats.sample_interval_ms = interval;
    spinlock_unlock(&g_netmon.lock);
}

void netmon_clear_history(void) {
    spinlock_lock(&g_netmon.lock);
    memset(g_netmon.history, 0, sizeof(g_netmon.history));
    g_netmon.history_head = 0;
    g_netmon.history_count = 0;
    spinlock_unlock(&g_netmon.lock);
}

int netmon_set_interval(uint32_t interval_ms) {
    if (interval_ms < 1000 || interval_ms > 60000) return -1;
    spinlock_lock(&g_netmon.lock);
    /* 取消旧 kwork */
    kwork_cancel_work(&g_netmon.sample_work);
    g_netmon.sample_interval = interval_ms;
    g_netmon.stats.sample_interval_ms = interval_ms;
    spinlock_unlock(&g_netmon.lock);

    /* 重新排队周期任务 */
    kwork_init_work(&g_netmon.sample_work, netmon_sample_work_fn, NULL);
    kwork_queue_periodic_work(&g_netmon.sample_work, interval_ms);

    evlog_info("NetMon", 6, "sample interval changed to %ums", interval_ms);
    return 0;
}
