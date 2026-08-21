/* funsos_logger.c - 日志系统子模块实现
 *
 * 提供应用级独立环形缓冲 + 桥接内核 klog
 */

#include "funsos.h"
#include "funsos_logger.h"
#include "klog.h"
#include "timer.h"
#include "string.h"
#include "kheap.h"
#include "stdarg.h"
#include "stdio.h"

/* ---- 应用级环形缓冲 ---- */

static funsos_log_entry_t g_ring[FUNSOS_LOG_RING_SIZE];
static uint32_t g_ring_head = 0;     /* 下一个写入位置 */
static uint32_t g_ring_count = 0;    /* 当前条目数 */
static uint32_t g_min_level = FUNSOS_LOG_INFO;
static uint32_t g_category_mask = 0xFFFFFFFFu;  /* 所有分类启用 */

/* ---- 基础日志 API ---- */

void funsos_log_write(uint32_t level, uint32_t category,
                     const char *source, const char *fmt, ...) {
    if (level > g_min_level) return;
    if (category < 32 && !(g_category_mask & (1u << category))) return;

    funsos_log_entry_t *e = &g_ring[g_ring_head];
    e->timestamp = timer_get_ticks();
    e->level = level;
    e->category = category;
    e->pid = 0;  /* 单进程内核, PID 暂为 0 */

    if (source) {
        strncpy(e->source, source, sizeof(e->source) - 1);
        e->source[sizeof(e->source) - 1] = '\0';
    } else {
        e->source[0] = '\0';
    }

    va_list args;
    va_start(args, fmt);
    /* 简化: 仅截取前 N-1 字节 */
    vsnprintf(e->message, sizeof(e->message), fmt, args);
    va_end(args);
    e->message[sizeof(e->message) - 1] = '\0';

    g_ring_head = (g_ring_head + 1) % FUNSOS_LOG_RING_SIZE;
    if (g_ring_count < FUNSOS_LOG_RING_SIZE) g_ring_count++;

    /* 同步写入内核日志以便 dmesg 查询 */
    klog_write(level, "[%s] %s", e->source, e->message);
}

/* ---- 日志查询 ---- */

uint32_t funsos_log_count(void) {
    return g_ring_count;
}

int funsos_log_get(uint32_t index, funsos_log_entry_t *entry) {
    if (!entry) return -1;
    if (index >= g_ring_count) return -1;

    /* index=0 为最旧条目 */
    uint32_t start;
    if (g_ring_count < FUNSOS_LOG_RING_SIZE) {
        start = 0;
    } else {
        start = g_ring_head;  /* head 即为最旧 */
    }
    uint32_t pos = (start + index) % FUNSOS_LOG_RING_SIZE;
    *entry = g_ring[pos];
    return 0;
}

uint32_t funsos_log_query(const funsos_log_filter_t *filter,
                         funsos_log_entry_t *entries,
                         uint32_t max_count,
                         uint32_t start_index) {
    if (!entries || max_count == 0) return 0;

    uint32_t out = 0;
    for (uint32_t i = start_index; i < g_ring_count && out < max_count; i++) {
        funsos_log_entry_t e;
        if (funsos_log_get(i, &e) != 0) break;

        /* 应用过滤器 */
        if (filter) {
            if (e.level > filter->min_level) continue;
            if (e.category < 32 &&
                !(filter->category_mask & (1u << e.category))) continue;
            if (filter->source_filter[0] != '\0' &&
                strcmp(e.source, filter->source_filter) != 0) continue;
        }

        entries[out++] = e;
    }
    return out;
}

uint32_t funsos_log_dump(char *buf, uint32_t max_len,
                        const funsos_log_filter_t *filter) {
    if (!buf || max_len == 0) return 0;
    uint32_t pos = 0;

    for (uint32_t i = 0; i < g_ring_count; i++) {
        funsos_log_entry_t e;
        if (funsos_log_get(i, &e) != 0) break;

        if (filter) {
            if (e.level > filter->min_level) continue;
            if (e.category < 32 &&
                !(filter->category_mask & (1u << e.category))) continue;
            if (filter->source_filter[0] != '\0' &&
                strcmp(e.source, filter->source_filter) != 0) continue;
        }

        /* 格式: [时间戳] 级别 来源: 消息 */
        static const char *level_names[] = {
            "EMERG", "ALERT", "CRIT", "ERR",
            "WARN", "NOTICE", "INFO", "DEBUG", "TRACE"
        };
        const char *lvl = (e.level <= 8) ? level_names[e.level] : "?";
        const char *cat_names[] = {
            "kernel", "driver", "fs", "net", "user", "security", "systemd"
        };
        const char *cat = (e.category < 7) ? cat_names[e.category] : "other";

        /* 计算所需长度 */
        char line[FUNSOS_LOG_MAX_MSG + 80];
        int n = snprintf(line, sizeof(line), "[%u] %s %s %s: %s\n",
                         e.timestamp, cat, lvl, e.source, e.message);
        if (n < 0) continue;
        if (pos + (uint32_t)n + 1 > max_len) break;

        for (int k = 0; k < n && pos < max_len - 1; k++) {
            buf[pos++] = line[k];
        }
    }
    buf[pos] = '\0';
    return pos;
}

/* ---- 内核日志 (dmesg) ---- */

uint32_t funsos_log_kernel_count(void) {
    return klog_get_line_count();
}

uint32_t funsos_log_kernel_read(char *buf, uint32_t max_len) {
    if (!buf || max_len == 0) return 0;
    return klog_read(buf, max_len);
}

void funsos_log_kernel_clear(void) {
    klog_clear();
}

/* ---- 配置 ---- */

void funsos_log_set_level(uint32_t min_level) {
    g_min_level = min_level;
}

uint32_t funsos_log_get_level(void) {
    return g_min_level;
}

void funsos_log_clear(void) {
    g_ring_head = 0;
    g_ring_count = 0;
}

void funsos_log_set_category_enabled(uint32_t category, int enable) {
    if (category >= 32) return;
    if (enable) {
        g_category_mask |= (1u << category);
    } else {
        g_category_mask &= ~(1u << category);
    }
}

int funsos_log_is_category_enabled(uint32_t category) {
    if (category >= 32) return 0;
    return (g_category_mask & (1u << category)) ? 1 : 0;
}
