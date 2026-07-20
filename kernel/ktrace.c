#include "ktrace.h"
#include "kheap.h"
#include "string.h"
#include "spinlock.h"
#include "sched.h"
#include "klog.h"
#include "stdio.h"
#include "stdarg.h"

/* ============================================================
 * Kernel Tracing Subsystem - 内核跟踪子系统实现
 *
 * 设计要点：
 *   - 使用环形缓冲区（ring buffer）存储事件
 *   - 用 spinlock 保护缓冲区和统计字段
 *   - 当缓冲区满时丢弃最旧事件并计数
 *   - TSC 时间戳由 rdtsc 指令读取
 *   - 缓冲区容量在运行时可调整（resize）
 * ============================================================ */

/* 静态默认缓冲区，避免在 ktrace_init 之前必须 kmalloc */
static ktrace_event_t g_static_buffer[KTRACE_DEFAULT_CAP];

/* 全局状态 */
static struct {
    ktrace_event_t *buffer;        /* 环形缓冲区 */
    uint32_t        capacity;       /* 容量（事件数，必须是 2 的幂） */
    uint32_t        mask;           /* capacity - 1，用于按位与取模 */
    uint32_t        head;           /* 下一个写入位置 */
    uint32_t        tail;           /* 下一个读取位置（最旧事件） */
    uint32_t        count;          /* 当前缓冲区中的事件数 */
    uint32_t        enabled_mask;   /* 启用的类别掩码 */
    uint32_t        min_level;      /* 最低允许级别 */
    spinlock_t      lock;
    int             initialized;
    int             using_static;   /* 是否使用静态缓冲区 */
    ktrace_stats_t  stats;
    uint64_t        boot_tsc;       /* 启动时的 TSC 值 */
} g_ktrace;

/* ------------------------------------------------------------
 * 内部辅助函数
 * ------------------------------------------------------------ */

/* 读取 TSC 时间戳 */
static uint64_t ktrace_rdtsc(void) {
    uint32_t hi, lo;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

/* 计算类别在统计数组中的索引（返回 -1 表示无对应索引） */
static int ktrace_cat_index(uint32_t category) {
    switch (category) {
        case KTRACE_CAT_SCHED:   return 0;
        case KTRACE_CAT_FS:      return 1;
        case KTRACE_CAT_NET:     return 2;
        case KTRACE_CAT_MEM:     return 3;
        case KTRACE_CAT_IRQ:     return 4;
        case KTRACE_CAT_SYSCALL: return 5;
        case KTRACE_CAT_PROC:    return 6;
        case KTRACE_CAT_TIMER:   return 7;
        default:                 return -1;
    }
}

/* 获取当前 PID（安全方式） */
static uint32_t ktrace_current_pid(void) {
    pcb_t *cur = sched_get_current();
    return cur ? cur->pid : 0;
}

/* ------------------------------------------------------------
 * 初始化
 * ------------------------------------------------------------ */

void ktrace_init(void) {
    memset(&g_ktrace, 0, sizeof(g_ktrace));
    g_ktrace.buffer       = g_static_buffer;
    g_ktrace.capacity     = KTRACE_DEFAULT_CAP;
    g_ktrace.mask         = KTRACE_DEFAULT_CAP - 1;
    g_ktrace.head         = 0;
    g_ktrace.tail        = 0;
    g_ktrace.count        = 0;
    g_ktrace.enabled_mask = 0;          /* 默认禁用所有类别 */
    g_ktrace.min_level    = KTRACE_LEVEL_DEBUG;
    g_ktrace.using_static = 1;
    g_ktrace.boot_tsc     = ktrace_rdtsc();
    spinlock_init(&g_ktrace.lock);
    g_ktrace.initialized  = 1;

    klog_info("ktrace: tracing subsystem initialized (capacity=%u)",
              g_ktrace.capacity);
}

/* ------------------------------------------------------------
 * 类别与级别控制
 * ------------------------------------------------------------ */

void ktrace_enable(uint32_t category_mask) {
    spinlock_lock(&g_ktrace.lock);
    g_ktrace.enabled_mask |= category_mask;
    g_ktrace.stats.enabled_mask = g_ktrace.enabled_mask;
    spinlock_unlock(&g_ktrace.lock);
}

void ktrace_disable(uint32_t category_mask) {
    spinlock_lock(&g_ktrace.lock);
    g_ktrace.enabled_mask &= ~category_mask;
    g_ktrace.stats.enabled_mask = g_ktrace.enabled_mask;
    spinlock_unlock(&g_ktrace.lock);
}

void ktrace_set_mask(uint32_t category_mask) {
    spinlock_lock(&g_ktrace.lock);
    g_ktrace.enabled_mask = category_mask;
    g_ktrace.stats.enabled_mask = g_ktrace.enabled_mask;
    spinlock_unlock(&g_ktrace.lock);
}

uint32_t ktrace_get_mask(void) {
    return g_ktrace.enabled_mask;
}

void ktrace_set_level(uint32_t level) {
    spinlock_lock(&g_ktrace.lock);
    g_ktrace.min_level = level;
    g_ktrace.stats.min_level = level;
    spinlock_unlock(&g_ktrace.lock);
}

uint32_t ktrace_get_level(void) {
    return g_ktrace.min_level;
}

/* ------------------------------------------------------------
 * 跟踪事件写入
 * ------------------------------------------------------------ */

/* 内部：写入一个事件到环形缓冲区（调用者需持有锁） */
static void ktrace_write_locked(uint32_t category, uint8_t level,
                                const char *msg) {
    ktrace_event_t *slot;
    uint32_t msg_len;

    /* 缓冲区满 -> 丢弃最旧的事件 */
    if (g_ktrace.count >= g_ktrace.capacity) {
        g_ktrace.tail = (g_ktrace.tail + 1) & g_ktrace.mask;
        g_ktrace.count--;
        g_ktrace.stats.dropped_events++;
    }

    slot = &g_ktrace.buffer[g_ktrace.head];
    slot->timestamp = ktrace_rdtsc();
    slot->pid       = ktrace_current_pid();
    slot->category  = category;
    slot->level     = level;
    slot->cpu       = 0;
    slot->reserved  = 0;

    if (msg) {
        /* 安全截断消息 */
        msg_len = (uint32_t)strlen(msg);
        if (msg_len >= KTRACE_MSG_MAX) msg_len = KTRACE_MSG_MAX - 1;
        memcpy(slot->msg, msg, msg_len);
        slot->msg[msg_len] = '\0';
    } else {
        slot->msg[0] = '\0';
    }

    g_ktrace.head = (g_ktrace.head + 1) & g_ktrace.mask;
    g_ktrace.count++;

    /* 更新统计 */
    g_ktrace.stats.total_events++;
    if (g_ktrace.count > g_ktrace.stats.max_buffered) {
        g_ktrace.stats.max_buffered = g_ktrace.count;
    }
    g_ktrace.stats.events_in_buffer = g_ktrace.count;

    int idx = ktrace_cat_index(category);
    if (idx >= 0) {
        g_ktrace.stats.events_per_cat[idx]++;
    }
}

void ktrace_event(uint32_t category, uint8_t level, const char *msg) {
    if (!g_ktrace.initialized) return;

    /* 快速路径：类别未启用或级别太低则直接返回 */
    if ((category != 0) && !(g_ktrace.enabled_mask & category)) return;
    if (level < g_ktrace.min_level) return;

    spinlock_lock(&g_ktrace.lock);
    ktrace_write_locked(category, level, msg);
    spinlock_unlock(&g_ktrace.lock);
}

void ktrace_eventf_va(uint32_t category, uint8_t level,
                      const char *fmt, va_list args) {
    char buf[KTRACE_MSG_MAX];

    if (!g_ktrace.initialized || !fmt) return;

    /* 在加锁前先格式化（vsnprintf 可能较慢） */
    vsnprintf(buf, KTRACE_MSG_MAX, fmt, args);
    buf[KTRACE_MSG_MAX - 1] = '\0';

    if ((category != 0) && !(g_ktrace.enabled_mask & category)) return;
    if (level < g_ktrace.min_level) return;

    spinlock_lock(&g_ktrace.lock);
    ktrace_write_locked(category, level, buf);
    spinlock_unlock(&g_ktrace.lock);
}

void ktrace_eventf(uint32_t category, uint8_t level,
                   const char *fmt, ...) {
    va_list args;
    char buf[KTRACE_MSG_MAX];

    if (!g_ktrace.initialized || !fmt) return;

    va_start(args, fmt);
    vsnprintf(buf, KTRACE_MSG_MAX, fmt, args);
    va_end(args);
    buf[KTRACE_MSG_MAX - 1] = '\0';

    if ((category != 0) && !(g_ktrace.enabled_mask & category)) return;
    if (level < g_ktrace.min_level) return;

    spinlock_lock(&g_ktrace.lock);
    ktrace_write_locked(category, level, buf);
    spinlock_unlock(&g_ktrace.lock);
}

/* ------------------------------------------------------------
 * 缓冲区操作
 * ------------------------------------------------------------ */

void ktrace_clear(void) {
    spinlock_lock(&g_ktrace.lock);
    g_ktrace.head = 0;
    g_ktrace.tail = 0;
    g_ktrace.count = 0;
    g_ktrace.stats.events_in_buffer = 0;
    spinlock_unlock(&g_ktrace.lock);
}

int ktrace_resize(uint32_t new_capacity) {
    ktrace_event_t *new_buf;
    uint32_t new_mask;
    uint32_t to_copy;
    uint32_t i;

    /* 容量必须是 2 的幂，且至少为 4 */
    if (new_capacity < 4) return -1;
    if ((new_capacity & (new_capacity - 1)) != 0) return -1;

    if (new_capacity == g_ktrace.capacity) return 0;

    new_buf = (ktrace_event_t *)kmalloc(new_capacity * sizeof(ktrace_event_t));
    if (!new_buf) return -1;
    memset(new_buf, 0, new_capacity * sizeof(ktrace_event_t));
    new_mask = new_capacity - 1;

    spinlock_lock(&g_ktrace.lock);

    /* 复制现有事件（最多保留 new_capacity 条，保留最新的） */
    to_copy = g_ktrace.count;
    if (to_copy > new_capacity) to_copy = new_capacity;

    /* 从最旧的 (tail) 开始复制 to_copy 条 */
    for (i = 0; i < to_copy; i++) {
        uint32_t src_idx = (g_ktrace.tail + i) & g_ktrace.mask;
        new_buf[i] = g_ktrace.buffer[src_idx];
    }

    if (!g_ktrace.using_static && g_ktrace.buffer) {
        kfree(g_ktrace.buffer);
    }

    g_ktrace.buffer       = new_buf;
    g_ktrace.capacity     = new_capacity;
    g_ktrace.mask         = new_mask;
    g_ktrace.head         = to_copy & new_mask;
    g_ktrace.tail         = 0;
    g_ktrace.count        = to_copy;
    g_ktrace.using_static = 0;
    g_ktrace.stats.buffer_capacity  = new_capacity;
    g_ktrace.stats.events_in_buffer = to_copy;
    if (to_copy > g_ktrace.stats.max_buffered) {
        g_ktrace.stats.max_buffered = to_copy;
    }

    spinlock_unlock(&g_ktrace.lock);

    klog_info("ktrace: buffer resized to %u events", new_capacity);
    return 0;
}

/* ------------------------------------------------------------
 * 统计
 * ------------------------------------------------------------ */

void ktrace_get_stats(ktrace_stats_t *stats) {
    if (!stats) return;
    spinlock_lock(&g_ktrace.lock);
    *stats = g_ktrace.stats;
    stats->enabled_mask   = g_ktrace.enabled_mask;
    stats->min_level      = g_ktrace.min_level;
    stats->buffer_capacity = g_ktrace.capacity;
    stats->events_in_buffer = g_ktrace.count;
    spinlock_unlock(&g_ktrace.lock);
}

void ktrace_reset_stats(void) {
    spinlock_lock(&g_ktrace.lock);
    memset(&g_ktrace.stats, 0, sizeof(g_ktrace.stats));
    g_ktrace.stats.enabled_mask    = g_ktrace.enabled_mask;
    g_ktrace.stats.min_level       = g_ktrace.min_level;
    g_ktrace.stats.buffer_capacity = g_ktrace.capacity;
    g_ktrace.stats.events_in_buffer = g_ktrace.count;
    spinlock_unlock(&g_ktrace.lock);
}

/* ------------------------------------------------------------
 * 读取与导出
 * ------------------------------------------------------------ */

uint32_t ktrace_read(ktrace_event_t *out, uint32_t max_count) {
    uint32_t copied = 0;
    uint32_t avail;
    uint32_t i;

    if (!out || max_count == 0) return 0;

    spinlock_lock(&g_ktrace.lock);
    avail = g_ktrace.count;
    if (avail > max_count) avail = max_count;

    /* 从最旧（tail）开始拷贝 avail 条 */
    for (i = 0; i < avail; i++) {
        uint32_t src_idx = (g_ktrace.tail + i) & g_ktrace.mask;
        out[i] = g_ktrace.buffer[src_idx];
        copied++;
    }
    spinlock_unlock(&g_ktrace.lock);

    return copied;
}

void ktrace_dump(uint32_t count) {
    ktrace_stats_t stats;
    uint32_t to_dump;
    uint32_t i;
    /* 用于 dump 的临时缓冲区（避免在锁内调用 klog） */
    ktrace_event_t snapshot[KTRACE_DEFAULT_CAP];

    ktrace_get_stats(&stats);

    if (stats.events_in_buffer == 0) {
        klog_info("ktrace: buffer empty");
        return;
    }

    to_dump = stats.events_in_buffer;
    if (count != 0 && count < to_dump) to_dump = count;

    /* 拷贝到栈上后释放锁，再向 klog 输出 */
    if (to_dump > KTRACE_DEFAULT_CAP) to_dump = KTRACE_DEFAULT_CAP;

    spinlock_lock(&g_ktrace.lock);
    for (i = 0; i < to_dump; i++) {
        uint32_t src_idx = (g_ktrace.tail + i) & g_ktrace.mask;
        snapshot[i] = g_ktrace.buffer[src_idx];
    }
    spinlock_unlock(&g_ktrace.lock);

    klog_info("ktrace: dumping %u events (total=%llu, dropped=%llu)",
              to_dump,
              (unsigned long long)stats.total_events,
              (unsigned long long)stats.dropped_events);

    for (i = 0; i < to_dump; i++) {
        ktrace_event_t *e = &snapshot[i];
        klog_info("  [%llu] cat=%s pid=%u lvl=%s: %s",
                  (unsigned long long)e->timestamp,
                  ktrace_cat_name(e->category),
                  e->pid,
                  ktrace_level_name(e->level),
                  e->msg);
    }
}

/* ------------------------------------------------------------
 * 辅助函数
 * ------------------------------------------------------------ */

uint64_t ktrace_now(void) {
    return ktrace_rdtsc();
}

const char *ktrace_cat_name(uint32_t category) {
    switch (category) {
        case KTRACE_CAT_SCHED:   return "sched";
        case KTRACE_CAT_FS:      return "fs";
        case KTRACE_CAT_NET:     return "net";
        case KTRACE_CAT_MEM:     return "mem";
        case KTRACE_CAT_IRQ:     return "irq";
        case KTRACE_CAT_SYSCALL: return "syscall";
        case KTRACE_CAT_PROC:    return "proc";
        case KTRACE_CAT_TIMER:   return "timer";
        case KTRACE_CAT_DEFAULT: return "default";
        case KTRACE_CAT_ALL:     return "all";
        default:                 return "?";
    }
}

const char *ktrace_level_name(uint8_t level) {
    switch (level) {
        case KTRACE_LEVEL_DEBUG: return "DBG";
        case KTRACE_LEVEL_INFO:  return "INF";
        case KTRACE_LEVEL_WARN:  return "WRN";
        case KTRACE_LEVEL_ERROR: return "ERR";
        default:                 return "??";
    }
}
