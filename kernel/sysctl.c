#include "sysctl.h"
#include "kheap.h"
#include "string.h"
#include "klog.h"
#include "stdio.h"
#include "stdlib.h"
#include "pmm.h"
#include "sched.h"
#include "version.h"

static struct sysctl_entry sysctl_table[SYSCTL_MAX_ENTRIES];
static uint32_t sysctl_count;
static uint8_t sysctl_initialized = 0;

static uint32_t sysctl_kernel_panic_on_oops = 0;
static uint32_t sysctl_kernel_panic = 0;
static uint32_t sysctl_vm_swappiness = 60;
static uint32_t sysctl_vm_overcommit = 1;
static uint32_t sysctl_sched_migration_cost = 500000;
static uint32_t sysctl_klog_level = 4;
static char sysctl_kernel_hostname[64] = "funos";
static char sysctl_kernel_domainname[64] = "(none)";

void sysctl_init(void) {
    if (sysctl_initialized) return;
    memset(sysctl_table, 0, sizeof(sysctl_table));
    sysctl_count = 0;

    sysctl_register("kernel.hostname", SYSCTL_STRING, sysctl_kernel_hostname,
                   sizeof(sysctl_kernel_hostname), 0, 0, "System hostname");
    sysctl_register("kernel.domainname", SYSCTL_STRING, sysctl_kernel_domainname,
                   sizeof(sysctl_kernel_domainname), 0, 0, "Domain name");
    sysctl_register("kernel.panic", SYSCTL_UINT, &sysctl_kernel_panic,
                   sizeof(uint32_t), 0, 3600, "Kernel panic timeout (seconds)");
    sysctl_register("kernel.panic_on_oops", SYSCTL_BOOL, &sysctl_kernel_panic_on_oops,
                   sizeof(uint32_t), 0, 1, "Panic on kernel BUG/oops");
    sysctl_register("vm.swappiness", SYSCTL_UINT, &sysctl_vm_swappiness,
                   sizeof(uint32_t), 0, 100, "Page replacement aggressiveness");
    sysctl_register("vm.overcommit_memory", SYSCTL_UINT, &sysctl_vm_overcommit,
                   sizeof(uint32_t), 0, 2, "Memory overcommit policy");
    sysctl_register("sched.migration_cost_ns", SYSCTL_UINT, &sysctl_sched_migration_cost,
                   sizeof(uint32_t), 0, 1000000000, "Task migration cost (ns)");
    sysctl_register("kernel.printk", SYSCTL_UINT, &sysctl_klog_level,
                   sizeof(uint32_t), 0, 7, "Kernel log level");

    sysctl_initialized = 1;
    klog_info("Sysctl interface initialized (%u default entries)", sysctl_count);
}

struct sysctl_entry *sysctl_find(const char *name) {
    if (!name) return NULL;
    for (uint32_t i = 0; i < sysctl_count; i++) {
        if (strcmp(sysctl_table[i].name, name) == 0) {
            return &sysctl_table[i];
        }
    }
    return NULL;
}

int sysctl_register(const char *name, sysctl_type_t type, void *data, uint32_t size,
                    uint32_t min_val, uint32_t max_val, const char *description) {
    if (!name || sysctl_count >= SYSCTL_MAX_ENTRIES) return -1;
    if (sysctl_find(name)) return -1;

    struct sysctl_entry *ent = &sysctl_table[sysctl_count];
    memset(ent, 0, sizeof(*ent));
    strncpy(ent->name, name, SYSCTL_MAX_NAME - 1);
    ent->type = type;
    ent->data = data;
    ent->data_size = size;
    ent->read = NULL;
    ent->write = NULL;
    ent->min_val = min_val;
    ent->max_val = max_val;
    ent->description = description;
    sysctl_count++;
    return 0;
}

int sysctl_register_handler(const char *name, sysctl_read_func_t read,
                            sysctl_write_func_t write, const char *description) {
    if (!name || sysctl_count >= SYSCTL_MAX_ENTRIES) return -1;
    if (sysctl_find(name)) return -1;

    struct sysctl_entry *ent = &sysctl_table[sysctl_count];
    memset(ent, 0, sizeof(*ent));
    strncpy(ent->name, name, SYSCTL_MAX_NAME - 1);
    ent->type = SYSCTL_STRING;
    ent->data = NULL;
    ent->data_size = 0;
    ent->read = read;
    ent->write = write;
    ent->description = description;
    sysctl_count++;
    return 0;
}

int sysctl_set(const char *name, const char *value) {
    struct sysctl_entry *ent = sysctl_find(name);
    if (!ent) return -1;

    if (ent->write) {
        return ent->write(value);
    }

    if (!ent->data) return -1;

    switch (ent->type) {
        case SYSCTL_INT:
        case SYSCTL_UINT:
        case SYSCTL_BOOL: {
            int val = atoi(value);
            if (ent->min_val != ent->max_val) {
                if ((uint32_t)val < ent->min_val || (uint32_t)val > ent->max_val) {
                    return -1;
                }
            }
            if (ent->data_size == 4) {
                *(uint32_t *)ent->data = (uint32_t)val;
            }
            return 0;
        }
        case SYSCTL_STRING: {
            strncpy((char *)ent->data, value, ent->data_size - 1);
            ((char *)ent->data)[ent->data_size - 1] = '\0';
            return 0;
        }
        default:
            return -1;
    }
}

int sysctl_get(const char *name, char *buf, uint32_t buf_size) {
    struct sysctl_entry *ent = sysctl_find(name);
    if (!ent || !buf || buf_size == 0) return -1;

    if (ent->read) {
        return ent->read(buf, buf_size);
    }

    if (!ent->data) return -1;

    switch (ent->type) {
        case SYSCTL_INT:
            snprintf(buf, buf_size, "%d", *(int *)ent->data);
            return 0;
        case SYSCTL_UINT:
        case SYSCTL_BOOL:
            snprintf(buf, buf_size, "%u", *(uint32_t *)ent->data);
            return 0;
        case SYSCTL_STRING:
            strncpy(buf, (char *)ent->data, buf_size - 1);
            buf[buf_size - 1] = '\0';
            return 0;
        case SYSCTL_LONG:
            snprintf(buf, buf_size, "%ld", *(long *)ent->data);
            return 0;
        default:
            return -1;
    }
}

int sysctl_set_int(const char *name, int value) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    return sysctl_set(name, buf);
}

int sysctl_get_int(const char *name, int *value) {
    char buf[32];
    if (sysctl_get(name, buf, sizeof(buf)) != 0) return -1;
    *value = atoi(buf);
    return 0;
}

void sysctl_dump_all(void) {
    char val[SYSCTL_MAX_STR];
    klog_info("=== Sysctl Settings ===");
    for (uint32_t i = 0; i < sysctl_count; i++) {
        struct sysctl_entry *ent = &sysctl_table[i];
        if (sysctl_get(ent->name, val, sizeof(val)) == 0) {
            klog_info("  %s = %s", ent->name, val);
        }
    }
}

int sysctl_unregister(const char *name) {
    for (uint32_t i = 0; i < sysctl_count; i++) {
        if (strcmp(sysctl_table[i].name, name) == 0) {
            for (uint32_t j = i; j < sysctl_count - 1; j++) {
                sysctl_table[j] = sysctl_table[j + 1];
            }
            memset(&sysctl_table[sysctl_count - 1], 0, sizeof(struct sysctl_entry));
            sysctl_count--;
            return 0;
        }
    }
    return -1;
}
