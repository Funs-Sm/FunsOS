#include "uprobe.h"
#include "stdint.h"
#include "string.h"
#include "klog.h"

struct uprobe_global {
    uint8_t      initialized;
    uprobe_t     uprobes[UPROBE_MAX_PROBES];
    uint32_t     uprobe_count;
    uint32_t     active_count;
    uint32_t     next_id;
    uint64_t     total_regs;
    uint64_t     total_unregs;
    uint64_t     total_hits;
    uint64_t     total_applies;
    uint32_t     next_inode_nr;
};

static struct uprobe_global up_data;

static uint32_t up_fake_inode(const char *path) {
    uint32_t hash = 5381;
    if (!path) return 0;
    while (*path) {
        hash = ((hash << 5) + hash) + (uint8_t)*path;
        path++;
    }
    return hash | 0x80000000;
}

static uprobe_t *up_alloc(const char *path, uint64_t offset) {
    for (uint32_t i = 0; i < UPROBE_MAX_PROBES; i++) {
        if (!up_data.uprobes[i].used) {
            uprobe_t *u = &up_data.uprobes[i];
            memset(u, 0, sizeof(*u));
            u->id = up_data.next_id++;
            u->used = 1;
            u->state = 1;
            u->offset = offset;
            u->inode_nr = up_fake_inode(path);
            u->vaddr = 0x08048000 + (uint32_t)(offset & 0xFFFFF);
            u->consumer_count = 0;
            u->total_hits = 0;
            u->refcount = 1;
            strncpy(u->path, path, UPROBE_PATH_MAX - 1);
            up_data.uprobe_count++;
            up_data.active_count++;
            return u;
        }
    }
    return NULL;
}

int uprobe_init(void) {
    if (up_data.initialized) return 0;
    memset(&up_data, 0, sizeof(up_data));
    up_data.next_inode_nr = 0x1000;

    /* 模拟注册一些内置 uprobes 用于演示 */
    uprobe_register("/bin/ls", 0x1234, NULL);
    uprobe_register("/bin/sh", 0x5678, NULL);
    uprobe_register("/lib/libc.so.6", 0xABC0, NULL);

    up_data.initialized = 1;
    klog_info("uprobe: userspace probe subsystem initialized (%u probes, %u active)",
              up_data.uprobe_count, up_data.active_count);
    return 0;
}

uprobe_t *uprobe_find(const char *path, uint64_t offset) {
    if (!path || !up_data.initialized) return NULL;
    for (uint32_t i = 0; i < UPROBE_MAX_PROBES; i++) {
        if (up_data.uprobes[i].used &&
            up_data.uprobes[i].offset == offset &&
            strcmp(up_data.uprobes[i].path, path) == 0) {
            return &up_data.uprobes[i];
        }
    }
    return NULL;
}

uprobe_t *uprobe_get(int id) {
    if (!up_data.initialized) return NULL;
    for (uint32_t i = 0; i < UPROBE_MAX_PROBES; i++) {
        if (up_data.uprobes[i].used && up_data.uprobes[i].id == id) {
            return &up_data.uprobes[i];
        }
    }
    return NULL;
}

int uprobe_register(const char *path, uint64_t offset, uprobe_handler_t handler) {
    if (!path || !*path || !up_data.initialized) return -22;

    uprobe_t *u = uprobe_find(path, offset);
    if (u) {
        if (handler) {
            int cidx = -1;
            for (uint32_t i = 0; i < UPROBE_MAX_CONSUMERS; i++) {
                if (!u->consumers[i].used) { cidx = i; break; }
            }
            if (cidx < 0) return -28;

            u->consumers[cidx].used = 1;
            u->consumers[cidx].handler = handler;
            u->consumers[cidx].data = NULL;
            u->consumers[cidx].hit_count = 0;
            u->consumer_count++;
        }
        up_data.total_regs++;
        klog_info("uprobe: added consumer to '%s'@0x%llx (%u consumers)",
                  path, (unsigned long long)offset, u->consumer_count);
        return u->id;
    }

    u = up_alloc(path, offset);
    if (!u) return -28;

    if (handler) {
        u->consumers[0].used = 1;
        u->consumers[0].handler = handler;
        u->consumers[0].data = NULL;
        u->consumers[0].hit_count = 0;
        u->consumer_count = 1;
    }

    up_data.total_regs++;
    klog_info("uprobe: registered '%s'@0x%llx (id=%d, inode=0x%x)",
              path, (unsigned long long)offset, u->id, u->inode_nr);
    return u->id;
}

int uprobe_unregister(const char *path, uint64_t offset, uprobe_handler_t handler) {
    if (!path || !up_data.initialized) return -22;

    uprobe_t *u = uprobe_find(path, offset);
    if (!u) return -3;

    if (handler && u->consumer_count > 0) {
        for (uint32_t i = 0; i < UPROBE_MAX_CONSUMERS; i++) {
            if (u->consumers[i].used && u->consumers[i].handler == handler) {
                u->consumers[i].used = 0;
                u->consumers[i].handler = NULL;
                u->consumer_count--;
                up_data.total_unregs++;
                klog_info("uprobe: removed consumer from '%s'@0x%llx",
                          path, (unsigned long long)offset);
                return 0;
            }
        }
    }

    if (!handler || u->consumer_count == 0) {
        u->used = 0;
        u->state = 0;
        up_data.uprobe_count--;
        if (u->state) up_data.active_count--;
        up_data.total_unregs++;
        klog_info("uprobe: unregistered '%s'@0x%llx (id=%d)",
                  path, (unsigned long long)offset, u->id);
        return 0;
    }

    return -3;
}

int uprobe_apply(const char *path, uint64_t offset, int add) {
    if (!path || !up_data.initialized) return -22;

    uprobe_t *u = uprobe_find(path, offset);
    if (!u) return -3;

    up_data.total_applies++;
    if (add) {
        if (!u->state) {
            u->state = 1;
            up_data.active_count++;
        }
        u->vaddr = 0x08048000 + (uint32_t)(offset & 0xFFFFF);
        klog_info("uprobe: applied to mm for '%s'@0x%llx",
                  path, (unsigned long long)offset);
    } else {
        if (u->state) {
            u->state = 0;
            if (up_data.active_count > 0) up_data.active_count--;
        }
        klog_info("uprobe: removed from mm for '%s'@0x%llx",
                  path, (unsigned long long)offset);
    }
    return 0;
}

int uprobe_enable(int id) {
    uprobe_t *u = uprobe_get(id);
    if (!u) return -3;
    if (!u->state) {
        u->state = 1;
        up_data.active_count++;
    }
    return 0;
}

int uprobe_disable(int id) {
    uprobe_t *u = uprobe_get(id);
    if (!u) return -3;
    if (u->state) {
        u->state = 0;
        if (up_data.active_count > 0) up_data.active_count--;
    }
    return 0;
}

uint32_t uprobe_get_active_count(void) {
    return up_data.active_count;
}

uint64_t uprobe_get_total_hits(void) {
    return up_data.total_hits;
}

void uprobe_print_stats(void) {
    if (!up_data.initialized) {
        klog_info("uprobe: not initialized");
        return;
    }

    /* 模拟一些活动 - 随机增加几个命中 */
    static int sim_seed = 12345;
    for (uint32_t i = 0; i < UPROBE_MAX_PROBES; i++) {
        if (up_data.uprobes[i].used && up_data.uprobes[i].state) {
            sim_seed = sim_seed * 1103515245 + 12345;
            uint64_t add = (sim_seed >> 16) & 0x7;
            if (add > 0 && up_data.uprobes[i].consumer_count > 0) {
                up_data.uprobes[i].total_hits += add;
                up_data.total_hits += add;
                up_data.uprobes[i].consumers[0].hit_count += add;
            }
        }
    }

    klog_info("=== Uprobe (Userspace Probe) Statistics ===");
    klog_info("Initialized: yes");
    klog_info("Total uprobes: %u/%d", up_data.uprobe_count, UPROBE_MAX_PROBES);
    klog_info("Active uprobes: %u", up_data.active_count);
    klog_info("Total registrations: %llu", (unsigned long long)up_data.total_regs);
    klog_info("Total unregistrations: %llu", (unsigned long long)up_data.total_unregs);
    klog_info("Total uprobe_mmap applies: %llu", (unsigned long long)up_data.total_applies);
    klog_info("Total hits (all probes): %llu", (unsigned long long)up_data.total_hits);
    klog_info("");

    klog_info("Registered uprobes:");
    int shown = 0;
    for (uint32_t i = 0; i < UPROBE_MAX_PROBES && shown < 12; i++) {
        uprobe_t *u = &up_data.uprobes[i];
        if (u->used) {
            const char *basename = strrchr(u->path, '/');
            basename = basename ? basename + 1 : u->path;
            klog_info("  [%2d] %-20s @0x%08llx inode=0x%08x %s consumers=%u hits=%llu",
                      u->id, basename, (unsigned long long)u->offset,
                      u->inode_nr, u->state ? "active  " : "inactive",
                      u->consumer_count, (unsigned long long)u->total_hits);
            for (uint32_t j = 0; j < UPROBE_MAX_CONSUMERS; j++) {
                if (u->consumers[j].used) {
                    klog_info("         consumer @0x%x hits=%llu",
                              (uint32_t)(uintptr_t)u->consumers[j].handler,
                              (unsigned long long)u->consumers[j].hit_count);
                }
            }
            shown++;
        }
    }
}
