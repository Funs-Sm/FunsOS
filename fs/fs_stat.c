#include "fs_stat.h"
#include "klog.h"
#include "string.h"
#include "sync.h"

static fs_stats_t g_stats;
static spinlock_t g_lock;
static int g_initialized = 0;

void fs_stat_init(void) {
    memset(&g_stats, 0, sizeof(g_stats));
    spinlock_init(&g_lock);
    g_initialized = 1;
    klog_info("fs_stat: filesystem statistics subsystem initialized");
}

void fs_stat_get(fs_stats_t *stats) {
    if (!stats) return;
    if (!g_initialized) fs_stat_init();

    spinlock_lock(&g_lock);
    memcpy(stats, &g_stats, sizeof(g_stats));
    spinlock_unlock(&g_lock);
}

void fs_stat_reset(void) {
    if (!g_initialized) return;

    spinlock_lock(&g_lock);
    uint64_t saved_uptime = g_stats.uptime_ticks;
    memset(&g_stats, 0, sizeof(g_stats));
    g_stats.uptime_ticks = saved_uptime;
    spinlock_unlock(&g_lock);
}

/* ---- I/O 统计 ---- */

void fs_stat_read(uint32_t bytes, int error) {
    if (!g_initialized) return;

    spinlock_lock(&g_lock);
    g_stats.io.reads++;
    g_stats.ops.reads++;
    if (error == 0) {
        g_stats.io.read_bytes += bytes;
    } else {
        g_stats.io.read_errors++;
    }
    spinlock_unlock(&g_lock);
}

void fs_stat_write(uint32_t bytes, int error) {
    if (!g_initialized) return;

    spinlock_lock(&g_lock);
    g_stats.io.writes++;
    g_stats.ops.writes++;
    if (error == 0) {
        g_stats.io.write_bytes += bytes;
    } else {
        g_stats.io.write_errors++;
    }
    spinlock_unlock(&g_lock);
}

/* ---- 操作计数 ---- */

void fs_stat_open(void) {
    if (!g_initialized) return;

    spinlock_lock(&g_lock);
    g_stats.ops.opens++;
    g_stats.total_files++;
    if (g_stats.total_files > g_stats.max_files) {
        g_stats.max_files = g_stats.total_files;
    }
    spinlock_unlock(&g_lock);
}

void fs_stat_close(void) {
    if (!g_initialized) return;

    spinlock_lock(&g_lock);
    g_stats.ops.closes++;
    if (g_stats.total_files > 0) {
        g_stats.total_files--;
    }
    spinlock_unlock(&g_lock);
}

void fs_stat_create(void) {
    if (!g_initialized) return;

    spinlock_lock(&g_lock);
    g_stats.ops.creates++;
    spinlock_unlock(&g_lock);
}

void fs_stat_delete(void) {
    if (!g_initialized) return;

    spinlock_lock(&g_lock);
    g_stats.ops.deletes++;
    spinlock_unlock(&g_lock);
}

void fs_stat_mkdir(void) {
    if (!g_initialized) return;

    spinlock_lock(&g_lock);
    g_stats.ops.mkdirs++;
    spinlock_unlock(&g_lock);
}

void fs_stat_rename(void) {
    if (!g_initialized) return;

    spinlock_lock(&g_lock);
    g_stats.ops.renames++;
    spinlock_unlock(&g_lock);
}

void fs_stat_lookup(int hit) {
    if (!g_initialized) return;

    spinlock_lock(&g_lock);
    g_stats.ops.lookups++;
    if (hit) {
        g_stats.ops.cache_hits++;
    } else {
        g_stats.ops.cache_misses++;
    }
    spinlock_unlock(&g_lock);
}

void fs_stat_mount(void) {
    if (!g_initialized) return;

    spinlock_lock(&g_lock);
    g_stats.ops.mounts++;
    spinlock_unlock(&g_lock);
}

void fs_stat_umount(void) {
    if (!g_initialized) return;

    spinlock_lock(&g_lock);
    g_stats.ops.umounts++;
    spinlock_unlock(&g_lock);
}

/* ---- 错误统计 ---- */

void fs_stat_error(int errno_val) {
    if (!g_initialized) return;

    spinlock_lock(&g_lock);
    switch (errno_val) {
        case 2:   g_stats.errors.enoent++; break;   /* ENOENT */
        case 1:   g_stats.errors.eacces++; break;   /* EPERM/EACCES */
        case 28:  g_stats.errors.enospc++; break;   /* ENOSPC */
        case 5:   g_stats.errors.eio++; break;      /* EIO */
        case 12:  g_stats.errors.enomem++; break;   /* ENOMEM */
        case 22:  g_stats.errors.einval++; break;   /* EINVAL */
        case 16:  g_stats.errors.ebusy++; break;    /* EBUSY */
        case 20:  g_stats.errors.enotdir++; break;  /* ENOTDIR */
        case 21:  g_stats.errors.eisdir++; break;   /* EISDIR */
        case 17:  g_stats.errors.eexist++; break;   /* EEXIST */
        default:  g_stats.errors.other++; break;
    }
    spinlock_unlock(&g_lock);
}

uint32_t fs_stat_cache_hit_rate(void) {
    if (!g_initialized) return 0;

    spinlock_lock(&g_lock);
    uint64_t total = g_stats.ops.cache_hits + g_stats.ops.cache_misses;
    uint32_t rate = total > 0 ? (uint32_t)((g_stats.ops.cache_hits * 100) / total) : 0;
    spinlock_unlock(&g_lock);
    return rate;
}
