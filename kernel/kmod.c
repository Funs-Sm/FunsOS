#include "kmod.h"
#include "stdint.h"
#include "string.h"
#include "klog.h"

struct kmod_global {
    uint8_t   initialized;
    kmod_t    modules[KMOD_MAX_MODULES];
    uint32_t  mod_count;
    uint32_t  live_count;
    uint32_t  next_id;
    uint64_t  total_loads;
    uint64_t  total_unloads;
    uint64_t  total_gets;
    uint64_t  total_puts;
};

static struct kmod_global km_data;

static kmod_t *km_alloc(const char *name) {
    for (uint32_t i = 0; i < KMOD_MAX_MODULES; i++) {
        if (!km_data.modules[i].used) {
            kmod_t *m = &km_data.modules[i];
            memset(m, 0, sizeof(*m));
            m->id = km_data.next_id++;
            m->used = 1;
            m->state = KMOD_STATE_LOADING;
            m->refcnt = 1;
            m->size = 4096;
            strncpy(m->name, name, KMOD_NAME_MAX - 1);
            strncpy(m->version, "1.0.0", sizeof(m->version) - 1);
            km_data.mod_count++;
            return m;
        }
    }
    return NULL;
}

int kmod_init(void) {
    if (km_data.initialized) return 0;
    memset(&km_data, 0, sizeof(km_data));

    /* 注册内置模块 */
    struct builtin_mod {
        const char *name;
        const char *version;
    };
    static struct builtin_mod builtins[] = {
        { "ext2",     "1.0.0" },
        { "ext3",     "1.0.0" },
        { "ext4",     "1.0.0" },
        { "fat32",    "1.2.0" },
        { "ntfs",     "0.5.0" },
        { "tmpfs",    "1.0.0" },
        { "ramfs",    "1.0.0" },
        { "procfs",   "1.0.0" },
        { "sysfs",    "1.0.0" },
        { "devtmpfs", "1.0.0" },
        { "e1000",    "3.2.0" },
        { "e1000e",   "3.8.0" },
        { "rtl8139",  "1.0.0" },
        { "ahci",     "3.0.0" },
        { "usbhid",   "1.1.0" },
        { "uhci_hcd", "1.0.0" },
        { "ehci_hcd", "1.0.0" },
        { "xhci_hcd", "1.0.0" },
        { "sd_mod",   "1.0.0" },
        { "sr_mod",   "1.0.0" },
        { "psmouse",  "1.0.0" },
        { "serio",    "1.0.0" },
        { NULL, NULL }
    };

    for (int i = 0; builtins[i].name; i++) {
        kmod_t *m = km_alloc(builtins[i].name);
        if (m) {
            strncpy(m->version, builtins[i].version, sizeof(m->version) - 1);
            m->state = KMOD_STATE_LIVE;
            m->size = 8192 + (i * 512);
            km_data.live_count++;
        }
    }

    km_data.initialized = 1;
    klog_info("kmod: kernel module manager initialized (%u builtin modules, %u live)",
              km_data.mod_count, km_data.live_count);
    return 0;
}

kmod_t *find_module(const char *name) {
    if (!name || !km_data.initialized) return NULL;
    for (uint32_t i = 0; i < KMOD_MAX_MODULES; i++) {
        if (km_data.modules[i].used &&
            strcmp(km_data.modules[i].name, name) == 0) {
            return &km_data.modules[i];
        }
    }
    return NULL;
}

void module_get(kmod_t *mod) {
    if (!mod) return;
    mod->refcnt++;
    mod->last_used = km_data.total_gets++;
    km_data.total_gets++;
}

void module_put(kmod_t *mod) {
    if (!mod) return;
    if (mod->refcnt > 0) mod->refcnt--;
    km_data.total_puts++;
}

kmod_t *init_module(const char *name, kmod_init_func_t init, kmod_exit_func_t exit) {
    if (!name || !*name || !km_data.initialized) return NULL;

    if (find_module(name)) return NULL;

    kmod_t *m = km_alloc(name);
    if (!m) return NULL;

    m->init = init;
    m->exit = exit;
    m->state = KMOD_STATE_COMING;

    if (init) {
        int ret = init();
        if (ret != 0) {
            m->used = 0;
            km_data.mod_count--;
            klog_info("kmod: init failed for '%s' (ret=%d)", name, ret);
            return NULL;
        }
    }

    m->state = KMOD_STATE_LIVE;
    km_data.live_count++;
    km_data.total_loads++;

    klog_info("kmod: module '%s' loaded (id=%d, refcnt=%u)",
              name, m->id, m->refcnt);
    return m;
}

int delete_module(const char *name) {
    if (!name || !km_data.initialized) return -22;

    kmod_t *m = find_module(name);
    if (!m) return -3;

    if (m->state != KMOD_STATE_LIVE) return -16;
    if (m->refcnt > 1) {
        klog_info("kmod: module '%s' is in use (refcnt=%u)", name, m->refcnt);
        return -16;
    }

    m->state = KMOD_STATE_GOING;
    if (m->exit) m->exit();
    m->state = KMOD_STATE_UNLOADING;

    m->used = 0;
    km_data.mod_count--;
    if (km_data.live_count > 0) km_data.live_count--;
    km_data.total_unloads++;

    klog_info("kmod: module '%s' unloaded", name);
    return 0;
}

int kmod_register_builtin(const char *name, kmod_init_func_t init,
                          kmod_exit_func_t exit, const char *version) {
    if (!name) return -22;
    kmod_t *m = find_module(name);
    if (m) return -17;

    m = km_alloc(name);
    if (!m) return -28;

    m->init = init;
    m->exit = exit;
    if (version) strncpy(m->version, version, sizeof(m->version) - 1);
    m->state = KMOD_STATE_LIVE;
    km_data.live_count++;
    return 0;
}

int kmod_load_builtin(const char *name) {
    kmod_t *m = find_module(name);
    if (!m) return -3;
    if (m->state != KMOD_STATE_LIVE) {
        if (m->init) {
            int ret = m->init();
            if (ret == 0) {
                m->state = KMOD_STATE_LIVE;
                km_data.live_count++;
            }
            return ret;
        }
        m->state = KMOD_STATE_LIVE;
        km_data.live_count++;
    }
    return 0;
}

uint32_t kmod_get_count(void) {
    return km_data.mod_count;
}

uint32_t kmod_get_live_count(void) {
    return km_data.live_count;
}

void kmod_print_stats(void) {
    if (!km_data.initialized) {
        klog_info("kmod: not initialized");
        return;
    }

    klog_info("=== Kmod (Kernel Module Manager) Statistics ===");
    klog_info("Initialized: yes");
    klog_info("Total modules registered: %u/%d", km_data.mod_count, KMOD_MAX_MODULES);
    klog_info("Live (active) modules: %u", km_data.live_count);
    klog_info("Total module loads: %llu", (unsigned long long)km_data.total_loads);
    klog_info("Total module unloads: %llu", (unsigned long long)km_data.total_unloads);
    klog_info("Total module_get() calls: %llu", (unsigned long long)km_data.total_gets);
    klog_info("Total module_put() calls: %llu", (unsigned long long)km_data.total_puts);
    klog_info("");

    const char *state_names[] = { "LIVE", "LOADING", "UNLOADING", "COMING", "GOING" };
    klog_info("Loaded modules:");
    int shown = 0;
    for (uint32_t i = 0; i < KMOD_MAX_MODULES && shown < 24; i++) {
        kmod_t *m = &km_data.modules[i];
        if (m->used) {
            int state_idx = (int)m->state;
            if (state_idx < 0 || state_idx > 4) state_idx = 0;
            klog_info("  [%2d] %-16s v%-8s %-9s refcnt=%u size=%u syms=%u",
                      m->id, m->name, m->version, state_names[state_idx],
                      m->refcnt, m->size, m->sym_count);
            shown++;
        }
    }

    if (km_data.mod_count > 24) {
        klog_info("  ... and %u more", km_data.mod_count - 24);
    }
}
