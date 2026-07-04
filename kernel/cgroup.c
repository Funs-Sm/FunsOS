#include "cgroup.h"
#include "kheap.h"
#include "spinlock.h"
#include "sched.h"
#include "string.h"
#include "klog.h"

static cgroup_t cgroups[CGROUP_MAX_GROUPS];
static int cgroup_next_id = 0;
static spinlock_t cgroup_lock;

void cgroup_init(void) {
    for (int i = 0; i < CGROUP_MAX_GROUPS; i++) {
        cgroups[i].id = -1;
        cgroups[i].used = 0;
        cgroups[i].state = CGROUP_STATE_INACTIVE;
    }
    spinlock_init(&cgroup_lock);
    cgroup_next_id = 0;

    cgroup_create("root", 0xFF, -1);
}

static cgroup_t *cgroup_find_by_id(int id) {
    for (int i = 0; i < CGROUP_MAX_GROUPS; i++) {
        if (cgroups[i].used && cgroups[i].id == id) {
            return &cgroups[i];
        }
    }
    return NULL;
}

cgroup_t *cgroup_find_by_name(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < CGROUP_MAX_GROUPS; i++) {
        if (cgroups[i].used && strcmp(cgroups[i].name, name) == 0) {
            return &cgroups[i];
        }
    }
    return NULL;
}

int cgroup_create(const char *name, uint32_t subsys_mask, int parent_id) {
    if (!name || !*name) return -22;
    if (strlen(name) >= CGROUP_NAME_MAX) return -36;

    spinlock_lock(&cgroup_lock);

    if (cgroup_find_by_name(name)) {
        spinlock_unlock(&cgroup_lock);
        return -17;
    }

    int idx = -1;
    for (int i = 0; i < CGROUP_MAX_GROUPS; i++) {
        if (!cgroups[i].used) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        spinlock_unlock(&cgroup_lock);
        return -28;
    }

    memset(&cgroups[idx], 0, sizeof(cgroup_t));
    cgroups[idx].id = cgroup_next_id++;
    cgroups[idx].used = 1;
    cgroups[idx].state = CGROUP_STATE_ACTIVE;
    strncpy(cgroups[idx].name, name, CGROUP_NAME_MAX - 1);
    cgroups[idx].subsys_mask = subsys_mask;
    cgroups[idx].parent_id = parent_id;
    cgroups[idx].proc_count = 0;

    /* 默认值 */
    cgroups[idx].cpu.shares = 1024;
    cgroups[idx].cpu.quota_us = (uint32_t)-1;
    cgroups[idx].cpu.period_us = 100000;
    cgroups[idx].mem.limit_in_bytes = (uint64_t)-1;
    cgroups[idx].mem.soft_limit = (uint64_t)-1;
    cgroups[idx].cpuset.cpumask = 0x1;
    cgroups[idx].freezer.state = 0;

    int id = cgroups[idx].id;
    spinlock_unlock(&cgroup_lock);

    klog_info("cgroup: created '%s' (id=%d)", name, id);
    return id;
}

int cgroup_delete(int id) {
    spinlock_lock(&cgroup_lock);

    cgroup_t *cg = cgroup_find_by_id(id);
    if (!cg) {
        spinlock_unlock(&cgroup_lock);
        return -22;
    }

    if (cg->proc_count > 0) {
        spinlock_unlock(&cgroup_lock);
        return -16;
    }

    /* 检查是否有子 cgroup */
    for (int i = 0; i < CGROUP_MAX_GROUPS; i++) {
        if (cgroups[i].used && cgroups[i].parent_id == id) {
            spinlock_unlock(&cgroup_lock);
            return -39;
        }
    }

    cg->state = CGROUP_STATE_DELETING;
    cg->used = 0;
    cg->id = -1;

    spinlock_unlock(&cgroup_lock);
    return 0;
}

int cgroup_attach(int id, pid_t pid) {
    spinlock_lock(&cgroup_lock);

    cgroup_t *cg = cgroup_find_by_id(id);
    if (!cg) {
        spinlock_unlock(&cgroup_lock);
        return -22;
    }

    if (cg->state != CGROUP_STATE_ACTIVE) {
        spinlock_unlock(&cgroup_lock);
        return -16;
    }

    if (cg->proc_count >= CGROUP_MAX_PROCS) {
        spinlock_unlock(&cgroup_lock);
        return -28;
    }

    /* 检查进程是否已在 cgroup 中 */
    for (uint32_t i = 0; i < cg->proc_count; i++) {
        if (cg->procs[i] == pid) {
            spinlock_unlock(&cgroup_lock);
            return 0;
        }
    }

    cg->procs[cg->proc_count++] = pid;
    spinlock_unlock(&cgroup_lock);
    return 0;
}

int cgroup_detach(int id, pid_t pid) {
    spinlock_lock(&cgroup_lock);

    cgroup_t *cg = cgroup_find_by_id(id);
    if (!cg) {
        spinlock_unlock(&cgroup_lock);
        return -22;
    }

    for (uint32_t i = 0; i < cg->proc_count; i++) {
        if (cg->procs[i] == pid) {
            /* 用最后一个元素替换 */
            cg->procs[i] = cg->procs[cg->proc_count - 1];
            cg->proc_count--;
            spinlock_unlock(&cgroup_lock);
            return 0;
        }
    }

    spinlock_unlock(&cgroup_lock);
    return -3;
}

int cgroup_set_cpu_shares(int id, uint32_t shares) {
    if (shares == 0 || shares > 262144) return -22;

    spinlock_lock(&cgroup_lock);
    cgroup_t *cg = cgroup_find_by_id(id);
    if (!cg) {
        spinlock_unlock(&cgroup_lock);
        return -22;
    }
    cg->cpu.shares = shares;
    spinlock_unlock(&cgroup_lock);
    return 0;
}

int cgroup_set_cpu_quota(int id, uint32_t quota_us) {
    spinlock_lock(&cgroup_lock);
    cgroup_t *cg = cgroup_find_by_id(id);
    if (!cg) {
        spinlock_unlock(&cgroup_lock);
        return -22;
    }
    cg->cpu.quota_us = quota_us;
    spinlock_unlock(&cgroup_lock);
    return 0;
}

int cgroup_set_mem_limit(int id, uint64_t limit_bytes) {
    spinlock_lock(&cgroup_lock);
    cgroup_t *cg = cgroup_find_by_id(id);
    if (!cg) {
        spinlock_unlock(&cgroup_lock);
        return -22;
    }
    cg->mem.limit_in_bytes = limit_bytes;
    spinlock_unlock(&cgroup_lock);
    return 0;
}

int cgroup_set_io_read_bps(int id, uint64_t bps) {
    spinlock_lock(&cgroup_lock);
    cgroup_t *cg = cgroup_find_by_id(id);
    if (!cg) {
        spinlock_unlock(&cgroup_lock);
        return -22;
    }
    cg->io.read_bps = bps;
    spinlock_unlock(&cgroup_lock);
    return 0;
}

int cgroup_set_freeze(int id, int freeze) {
    spinlock_lock(&cgroup_lock);
    cgroup_t *cg = cgroup_find_by_id(id);
    if (!cg) {
        spinlock_unlock(&cgroup_lock);
        return -22;
    }

    cg->freezer.state = freeze ? 2 : 0;

    if (freeze) {
        cg->state = CGROUP_STATE_FROZEN;
        cg->freezer.frozen_count = cg->proc_count;
    } else {
        cg->state = CGROUP_STATE_ACTIVE;
        cg->freezer.frozen_count = 0;
    }

    spinlock_unlock(&cgroup_lock);
    return 0;
}

int cgroup_set_cpumask(int id, uint32_t cpumask) {
    if (cpumask == 0) return -22;

    spinlock_lock(&cgroup_lock);
    cgroup_t *cg = cgroup_find_by_id(id);
    if (!cg) {
        spinlock_unlock(&cgroup_lock);
        return -22;
    }
    cg->cpuset.cpumask = cpumask;
    spinlock_unlock(&cgroup_lock);
    return 0;
}

cgroup_t *cgroup_get(int id) {
    return cgroup_find_by_id(id);
}

int cgroup_get_proc_cgroup(pid_t pid) {
    spinlock_lock(&cgroup_lock);
    for (int i = 0; i < CGROUP_MAX_GROUPS; i++) {
        if (cgroups[i].used) {
            for (uint32_t j = 0; j < cgroups[i].proc_count; j++) {
                if (cgroups[i].procs[j] == pid) {
                    spinlock_unlock(&cgroup_lock);
                    return cgroups[i].id;
                }
            }
        }
    }
    spinlock_unlock(&cgroup_lock);
    return -1;
}

void cgroup_dump(int id) {
    spinlock_lock(&cgroup_lock);
    cgroup_t *cg = cgroup_find_by_id(id);
    if (cg) {
        klog_info("cgroup '%s' (id=%d)", cg->name, cg->id);
        klog_info("  procs: %u", cg->proc_count);
        klog_info("  cpu.shares: %u", cg->cpu.shares);
        klog_info("  mem.limit: %llu", (unsigned long long)cg->mem.limit_in_bytes);
    }
    spinlock_unlock(&cgroup_lock);
}

int sys_cgroup_create(const char *name, uint32_t subsys_mask, int parent_id) {
    return cgroup_create(name, subsys_mask, parent_id);
}

int sys_cgroup_delete(int id) {
    return cgroup_delete(id);
}

int sys_cgroup_attach(int id, pid_t pid) {
    return cgroup_attach(id, pid);
}

int sys_cgroup_detach(int id, pid_t pid) {
    return cgroup_detach(id, pid);
}
