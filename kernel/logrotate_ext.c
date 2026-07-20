/* logrotate_ext.c - 日志轮转扩展实现 */
#include "logrotate_ext.h"
#include "logrotate.h"
#include "evlog.h"
#include "kwork.h"
#include "klog.h"
#include "spinlock.h"
#include "string.h"
#include "timer.h"

#define LOGROTATE_EV_SOURCE "LogRotate"

static struct {
    spinlock_t              lock;
    int                     initialized;
    int                     auto_enabled;
    uint32_t                interval_ms;
    kwork_t                 check_work;
    logrotate_ext_stats_t   stats;
} g_lre;

/* kwork 回调：定期检查所有配置的日志文件 */
static void logrotate_ext_check_work_fn(void *data) {
    (void)data;
    logrotate_ext_check_all();
}

void logrotate_ext_init(void) {
    memset(&g_lre, 0, sizeof(g_lre));
    spinlock_init(&g_lre.lock);
    g_lre.interval_ms = LOGROTATE_EXT_DEFAULT_INTERVAL_MS;

    evlog_register_source(LOGROTATE_EV_SOURCE);
    g_lre.initialized = 1;
    klog_info("logrotate_ext: extension initialized (interval=%u ms)",
              g_lre.interval_ms);
}

void logrotate_ext_shutdown(void) {
    if (!g_lre.initialized) return;
    logrotate_ext_stop_auto();
    g_lre.initialized = 0;
}

int logrotate_ext_start_auto(uint32_t interval_ms) {
    if (interval_ms == 0) interval_ms = LOGROTATE_EXT_DEFAULT_INTERVAL_MS;

    spinlock_lock(&g_lre.lock);
    if (g_lre.auto_enabled) {
        /* 已启用：取消旧的，重新启用 */
        kwork_cancel_work(&g_lre.check_work);
    }

    kwork_init_work(&g_lre.check_work, logrotate_ext_check_work_fn, NULL);
    int rc = kwork_queue_periodic_work(&g_lre.check_work, interval_ms);
    if (rc == 0) {
        g_lre.auto_enabled = 1;
        g_lre.interval_ms = interval_ms;
        g_lre.stats.auto_rotate_enabled = 1;
        g_lre.stats.check_interval_ms = interval_ms;
        spinlock_unlock(&g_lre.lock);

        evlog_info(LOGROTATE_EV_SOURCE, 1,
                   "auto-rotation started (interval=%u ms)", interval_ms);
        return 0;
    }
    spinlock_unlock(&g_lre.lock);
    return -1;
}

int logrotate_ext_stop_auto(void) {
    spinlock_lock(&g_lre.lock);
    if (!g_lre.auto_enabled) {
        spinlock_unlock(&g_lre.lock);
        return 0;
    }
    kwork_cancel_work(&g_lre.check_work);
    g_lre.auto_enabled = 0;
    g_lre.stats.auto_rotate_enabled = 0;
    spinlock_unlock(&g_lre.lock);

    evlog_info(LOGROTATE_EV_SOURCE, 2, "auto-rotation stopped");
    return 0;
}

int logrotate_ext_is_auto_enabled(void) {
    spinlock_lock(&g_lre.lock);
    int e = g_lre.auto_enabled;
    spinlock_unlock(&g_lre.lock);
    return e;
}

int logrotate_ext_check_all(void) {
    spinlock_lock(&g_lre.lock);
    g_lre.stats.total_checks++;
    g_lre.stats.last_check_tick = (uint32_t)timer_get_ticks();
    spinlock_unlock(&g_lre.lock);

    uint32_t n = logrotate_get_config_count();
    int rotated = 0;

    for (uint32_t i = 0; i < n; i++) {
        logrotate_config_t *cfg = logrotate_get_config(i);
        if (!cfg) continue;

        int rc = logrotate_check(cfg->filepath);
        if (rc == 1) {
            /* 文件已轮转 */
            rotated++;
            spinlock_lock(&g_lre.lock);
            g_lre.stats.total_rotations++;
            g_lre.stats.last_rotation_tick = (uint32_t)timer_get_ticks();
            spinlock_unlock(&g_lre.lock);

            evlog_info(LOGROTATE_EV_SOURCE, 3,
                       "rotated log file: %s (was %u bytes, max=%u)",
                       cfg->filepath, cfg->current_size, cfg->max_size);
        } else if (rc < 0) {
            spinlock_lock(&g_lre.lock);
            g_lre.stats.total_failed++;
            spinlock_unlock(&g_lre.lock);

            evlog_warn(LOGROTATE_EV_SOURCE, 4,
                       "rotation failed for %s (rc=%d)", cfg->filepath, rc);
        }
        /* rc == 0: 不需要轮转 */
    }

    return rotated;
}

int logrotate_ext_set_compress(const char *filepath, int enable) {
    if (!filepath) return -1;
    uint32_t n = logrotate_get_config_count();
    for (uint32_t i = 0; i < n; i++) {
        logrotate_config_t *cfg = logrotate_get_config(i);
        if (!cfg) continue;
        if (strcmp(cfg->filepath, filepath) == 0) {
            cfg->compress = enable ? 1 : 0;
            evlog_info(LOGROTATE_EV_SOURCE, 5,
                       "compress %s for %s", enable ? "enabled" : "disabled",
                       filepath);
            return 0;
        }
    }
    return -2;
}

void logrotate_ext_get_stats(logrotate_ext_stats_t *stats) {
    if (!stats) return;
    spinlock_lock(&g_lre.lock);
    *stats = g_lre.stats;
    spinlock_unlock(&g_lre.lock);
}

void logrotate_ext_reset_stats(void) {
    spinlock_lock(&g_lre.lock);
    uint32_t interval = g_lre.stats.check_interval_ms;
    uint32_t auto_on = g_lre.stats.auto_rotate_enabled;
    memset(&g_lre.stats, 0, sizeof(g_lre.stats));
    g_lre.stats.check_interval_ms = interval;
    g_lre.stats.auto_rotate_enabled = auto_on;
    spinlock_unlock(&g_lre.lock);
}
