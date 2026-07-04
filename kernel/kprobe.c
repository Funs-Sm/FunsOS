#include "kprobe.h"
#include "kheap.h"
#include "spinlock.h"
#include "string.h"
#include "klog.h"
#include "ksym.h"

static kprobe_t kprobes[KPROBE_MAX_PROBES];
static int kprobe_next_id = 0;
static spinlock_t kprobe_lock;

void kprobe_init(void) {
    for (int i = 0; i < KPROBE_MAX_PROBES; i++) {
        kprobes[i].id = -1;
        kprobes[i].used = 0;
        kprobes[i].state = KPROBE_STATE_INACTIVE;
    }
    spinlock_init(&kprobe_lock);
    kprobe_next_id = 0;
    klog_info("kprobe: initialized (max %d probes)", KPROBE_MAX_PROBES);
}

static kprobe_t *kprobe_find_by_id(int id) {
    for (int i = 0; i < KPROBE_MAX_PROBES; i++) {
        if (kprobes[i].used && kprobes[i].id == id) {
            return &kprobes[i];
        }
    }
    return NULL;
}

int kprobe_find_by_addr(uint32_t addr) {
    for (int i = 0; i < KPROBE_MAX_PROBES; i++) {
        if (kprobes[i].used && kprobes[i].addr == addr &&
            kprobes[i].state == KPROBE_STATE_ACTIVE) {
            return kprobes[i].id;
        }
    }
    return -1;
}

static uint32_t resolve_symbol(const char *symbol) {
    if (!symbol) return 0;
    extern uint32_t ksym_lookup(const char *name);
    return ksym_lookup(symbol);
}

int kprobe_register(const char *symbol, kprobe_type_t type,
                    kprobe_handler_t pre_handler,
                    kprobe_handler_t post_handler) {
    if (!symbol || !*symbol) return -22;

    uint32_t addr = resolve_symbol(symbol);
    if (addr == 0) return -2;

    return kprobe_register_addr(addr, type, pre_handler, post_handler);
}

int kprobe_register_addr(uint32_t addr, kprobe_type_t type,
                         kprobe_handler_t pre_handler,
                         kprobe_handler_t post_handler) {
    if (addr == 0) return -22;

    spinlock_lock(&kprobe_lock);

    /* 检查是否已存在 */
    for (int i = 0; i < KPROBE_MAX_PROBES; i++) {
        if (kprobes[i].used && kprobes[i].addr == addr &&
            kprobes[i].type == type) {
            spinlock_unlock(&kprobe_lock);
            return -17;
        }
    }

    /* 找空闲槽位 */
    int idx = -1;
    for (int i = 0; i < KPROBE_MAX_PROBES; i++) {
        if (!kprobes[i].used) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        spinlock_unlock(&kprobe_lock);
        return -28;
    }

    memset(&kprobes[idx], 0, sizeof(kprobe_t));
    kprobes[idx].id = kprobe_next_id++;
    kprobes[idx].used = 1;
    kprobes[idx].type = type;
    kprobes[idx].state = KPROBE_STATE_INACTIVE;
    kprobes[idx].addr = addr;
    kprobes[idx].offset = 0;
    kprobes[idx].pre_handler = pre_handler;
    kprobes[idx].post_handler = post_handler;
    kprobes[idx].hit_count = 0;

    int id = kprobes[idx].id;
    spinlock_unlock(&kprobe_lock);

    klog_info("kprobe: registered probe id=%d addr=0x%x", id, addr);
    return id;
}

int kprobe_unregister(int id) {
    spinlock_lock(&kprobe_lock);

    kprobe_t *kp = kprobe_find_by_id(id);
    if (!kp) {
        spinlock_unlock(&kprobe_lock);
        return -22;
    }

    if (kp->state == KPROBE_STATE_ACTIVE) {
        spinlock_unlock(&kprobe_lock);
        return -16;
    }

    kp->used = 0;
    kp->id = -1;
    kp->state = KPROBE_STATE_INACTIVE;

    spinlock_unlock(&kprobe_lock);
    return 0;
}

int kprobe_enable(int id) {
    spinlock_lock(&kprobe_lock);

    kprobe_t *kp = kprobe_find_by_id(id);
    if (!kp) {
        spinlock_unlock(&kprobe_lock);
        return -22;
    }

    if (kp->state == KPROBE_STATE_ACTIVE) {
        spinlock_unlock(&kprobe_lock);
        return 0;
    }

    /* 保存原始指令 (模拟, 实际架构需要 int3 断点) */
    kp->orig_insn = *(uint32_t *)kp->addr;
    kp->state = KPROBE_STATE_ACTIVE;

    spinlock_unlock(&kprobe_lock);

    klog_info("kprobe: enabled probe id=%d", id);
    return 0;
}

int kprobe_disable(int id) {
    spinlock_lock(&kprobe_lock);

    kprobe_t *kp = kprobe_find_by_id(id);
    if (!kp) {
        spinlock_unlock(&kprobe_lock);
        return -22;
    }

    if (kp->state != KPROBE_STATE_ACTIVE) {
        spinlock_unlock(&kprobe_lock);
        return 0;
    }

    /* 恢复原始指令 */
    *(uint32_t *)kp->addr = kp->orig_insn;
    kp->state = KPROBE_STATE_DISABLED;

    spinlock_unlock(&kprobe_lock);

    klog_info("kprobe: disabled probe id=%d", id);
    return 0;
}

kprobe_t *kprobe_get(int id) {
    return kprobe_find_by_id(id);
}

uint64_t kprobe_get_hit_count(int id) {
    spinlock_lock(&kprobe_lock);
    kprobe_t *kp = kprobe_find_by_id(id);
    uint64_t count = kp ? kp->hit_count : 0;
    spinlock_unlock(&kprobe_lock);
    return count;
}

void kprobe_dump(int id) {
    spinlock_lock(&kprobe_lock);
    kprobe_t *kp = kprobe_find_by_id(id);
    if (kp) {
        klog_info("kprobe[%d]: addr=0x%x type=%d state=%d hits=%llu",
                  kp->id, kp->addr, kp->type, kp->state,
                  (unsigned long long)kp->hit_count);
    }
    spinlock_unlock(&kprobe_lock);
}

void kprobe_dump_all(void) {
    spinlock_lock(&kprobe_lock);
    klog_info("=== kprobe list ===");
    for (int i = 0; i < KPROBE_MAX_PROBES; i++) {
        if (kprobes[i].used) {
            klog_info("  [%d] addr=0x%x type=%d state=%d hits=%llu",
                      kprobes[i].id, kprobes[i].addr,
                      kprobes[i].type, kprobes[i].state,
                      (unsigned long long)kprobes[i].hit_count);
        }
    }
    spinlock_unlock(&kprobe_lock);
}

int kprobe_get_count(void) {
    int count = 0;
    spinlock_lock(&kprobe_lock);
    for (int i = 0; i < KPROBE_MAX_PROBES; i++) {
        if (kprobes[i].used) count++;
    }
    spinlock_unlock(&kprobe_lock);
    return count;
}

void kprobe_get_stats(kprobe_stats_t *stats) {
    if (!stats) return;

    memset(stats, 0, sizeof(*stats));
    spinlock_lock(&kprobe_lock);
    for (int i = 0; i < KPROBE_MAX_PROBES; i++) {
        if (kprobes[i].used) {
            stats->total_probes++;
            if (kprobes[i].state == KPROBE_STATE_ACTIVE) {
                stats->active_probes++;
            }
            stats->total_hits += kprobes[i].hit_count;
        }
    }
    spinlock_unlock(&kprobe_lock);
}
