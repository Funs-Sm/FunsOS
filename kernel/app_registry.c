#include "app_registry.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"
#include "klog.h"
#include "gui_apps.h"

static app_entry_t g_apps[APP_MAX_COUNT];
static int g_app_count = 0;

void app_registry_init(void) {
    for (int i = 0; i < APP_MAX_COUNT; i++) {
        g_apps[i].name = 0;
        g_apps[i].description = 0;
        g_apps[i].launch = 0;
        g_apps[i].builtin = 0;
        g_apps[i].enabled = 0;
    }
    g_app_count = 0;

    klog_info("App registry initialized");

    app_register("settings", "System settings panel", APP_TYPE_SYSTEM, gui_app_settings, 1);
    app_register("filemgr", "File manager", APP_TYPE_SYSTEM, gui_app_filemanager, 1);
    app_register("sysinfo", "System information viewer", APP_TYPE_SYSTEM, gui_app_sysinfo, 1);

    klog_info("Registered %d built-in applications", g_app_count);
}

int app_register(const char *name, const char *desc, app_type_t type, void (*launch)(void), int builtin) {
    if (!name || !launch) return -1;
    if (g_app_count >= APP_MAX_COUNT) return -1;

    for (int i = 0; i < g_app_count; i++) {
        if (g_apps[i].name && strcmp(g_apps[i].name, name) == 0) {
            return -1;
        }
    }

    int idx = g_app_count;
    g_apps[idx].name = name;
    g_apps[idx].description = desc;
    g_apps[idx].type = type;
    g_apps[idx].launch = launch;
    g_apps[idx].builtin = (uint8_t)builtin;
    g_apps[idx].enabled = 1;
    g_app_count++;

    klog_debug("App registered: %s (type=%d, builtin=%d)", name, type, builtin);
    return 0;
}

int app_unregister(const char *name) {
    if (!name) return -1;

    for (int i = 0; i < g_app_count; i++) {
        if (g_apps[i].name && strcmp(g_apps[i].name, name) == 0) {
            for (int j = i; j < g_app_count - 1; j++) {
                g_apps[j] = g_apps[j + 1];
            }
            g_apps[g_app_count - 1].name = 0;
            g_app_count--;
            klog_debug("App unregistered: %s", name);
            return 0;
        }
    }
    return -1;
}

app_entry_t *app_find(const char *name) {
    if (!name) return 0;

    for (int i = 0; i < g_app_count; i++) {
        if (g_apps[i].name && g_apps[i].enabled &&
            strcmp(g_apps[i].name, name) == 0) {
            return &g_apps[i];
        }
    }
    return 0;
}

app_entry_t *app_get_by_index(int index) {
    if (index < 0 || index >= g_app_count) return 0;
    return &g_apps[index];
}

int app_get_count(void) {
    return g_app_count;
}

int app_launch(const char *name) {
    app_entry_t *app = app_find(name);
    if (!app) {
        klog_warn("App not found: %s", name);
        return -1;
    }

    if (!app->launch) {
        klog_err("App has no launch function: %s", name);
        return -1;
    }

    klog_info("Launching app: %s", name);
    app->launch();
    return 0;
}

void app_list_all(void) {
    printf("Installed applications (%d):\n", g_app_count);
    for (int i = 0; i < g_app_count; i++) {
        if (g_apps[i].name && g_apps[i].enabled) {
            printf("  [%d] %-16s %s\n", i, g_apps[i].name,
                    g_apps[i].description ? g_apps[i].description : "");
        }
    }
}
