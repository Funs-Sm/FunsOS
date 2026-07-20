#include "debugobjects.h"
#include "klog.h"
#include "string.h"

static struct debug_obj *debug_obj_list;
static struct debug_obj debug_obj_pool[DEBUG_OBJECTS_MAX_POOL];
static uint8_t debug_obj_used[DEBUG_OBJECTS_MAX_POOL];
static uint8_t debug_objects_initialized;
static uint32_t debug_obj_init_count;
static uint32_t debug_obj_activate_count;
static uint32_t debug_obj_deactivate_count;
static uint32_t debug_obj_free_count;
static uint32_t debug_obj_warnings;
static uint32_t active_objects;

int debug_objects_init(void) {
    if (debug_objects_initialized) return 0;

    debug_obj_list = NULL;
    memset(debug_obj_pool, 0, sizeof(debug_obj_pool));
    memset(debug_obj_used, 0, sizeof(debug_obj_used));

    debug_obj_init_count = 0;
    debug_obj_activate_count = 0;
    debug_obj_deactivate_count = 0;
    debug_obj_free_count = 0;
    debug_obj_warnings = 0;
    active_objects = 0;

    debug_objects_initialized = 1;
    klog_info("Debug objects tracking system initialized (max %u objects)", DEBUG_OBJECTS_MAX_POOL);
    return 0;
}

struct debug_obj *debug_object_lookup(void *addr) {
    if (!debug_objects_initialized || !addr) return NULL;

    struct debug_obj *obj = debug_obj_list;
    while (obj) {
        if (obj->addr == addr && obj->magic == DEBUG_OBJECTS_MAGIC) {
            return obj;
        }
        obj = obj->next;
    }
    return NULL;
}

static struct debug_obj *debug_obj_alloc(void) {
    for (uint32_t i = 0; i < DEBUG_OBJECTS_MAX_POOL; i++) {
        if (!debug_obj_used[i]) {
            struct debug_obj *obj = &debug_obj_pool[i];
            memset(obj, 0, sizeof(*obj));
            debug_obj_used[i] = 1;
            return obj;
        }
    }
    return NULL;
}

static void debug_obj_dealloc(struct debug_obj *obj) {
    if (!obj) return;
    uint32_t idx = obj - debug_obj_pool;
    if (idx < DEBUG_OBJECTS_MAX_POOL) {
        debug_obj_used[idx] = 0;
    }
}

void debug_object_init(void *addr, const char *desc) {
    if (!debug_objects_initialized || !addr) return;

    if (debug_object_lookup(addr)) {
        klog_info("debug_objects: warning: re-init object at 0x%x", addr);
        debug_obj_warnings++;
        return;
    }

    struct debug_obj *obj = debug_obj_alloc();
    if (!obj) {
        klog_info("debug_objects: warning: pool full");
        debug_obj_warnings++;
        return;
    }

    obj->addr = addr;
    obj->state = DEBUG_OBJECT_INACTIVE;
    obj->refcount = 1;
    obj->magic = DEBUG_OBJECTS_MAGIC;
    obj->activate_count = 0;
    obj->deactivate_count = 0;

    if (desc) {
        strncpy(obj->desc, desc, DEBUG_OBJECTS_DESC_LEN - 1);
    } else {
        strncpy(obj->desc, "unknown", DEBUG_OBJECTS_DESC_LEN - 1);
    }

    obj->next = debug_obj_list;
    debug_obj_list = obj;

    debug_obj_init_count++;
}

void debug_object_activate(void *addr) {
    if (!debug_objects_initialized || !addr) return;

    struct debug_obj *obj = debug_object_lookup(addr);
    if (!obj) {
        klog_info("debug_objects: warning: activate unknown object at 0x%x", addr);
        debug_obj_warnings++;
        return;
    }

    if (obj->state == DEBUG_OBJECT_DESTROYED) {
        klog_info("debug_objects: warning: activate destroyed object at 0x%x (%s)",
                 addr, obj->desc);
        debug_obj_warnings++;
        return;
    }

    if (obj->state == DEBUG_OBJECT_ACTIVE) {
        klog_info("debug_objects: warning: double activate object at 0x%x (%s)",
                 addr, obj->desc);
        debug_obj_warnings++;
    } else {
        active_objects++;
    }

    obj->state = DEBUG_OBJECT_ACTIVE;
    obj->activate_count++;
    debug_obj_activate_count++;
}

void debug_object_deactivate(void *addr) {
    if (!debug_objects_initialized || !addr) return;

    struct debug_obj *obj = debug_object_lookup(addr);
    if (!obj) {
        klog_info("debug_objects: warning: deactivate unknown object at 0x%x", addr);
        debug_obj_warnings++;
        return;
    }

    if (obj->state == DEBUG_OBJECT_INACTIVE) {
        klog_info("debug_objects: warning: double deactivate object at 0x%x (%s)",
                 addr, obj->desc);
        debug_obj_warnings++;
    } else if (obj->state == DEBUG_OBJECT_ACTIVE) {
        active_objects--;
    }

    obj->state = DEBUG_OBJECT_INACTIVE;
    obj->deactivate_count++;
    debug_obj_deactivate_count++;
}

void debug_object_ref(void *addr) {
    if (!debug_objects_initialized || !addr) return;

    struct debug_obj *obj = debug_object_lookup(addr);
    if (obj) {
        obj->refcount++;
    }
}

void debug_object_unref(void *addr) {
    if (!debug_objects_initialized || !addr) return;

    struct debug_obj *obj = debug_object_lookup(addr);
    if (obj && obj->refcount > 0) {
        obj->refcount--;
    }
}

void debug_object_free(void *addr) {
    if (!debug_objects_initialized || !addr) return;

    struct debug_obj *prev = NULL;
    struct debug_obj *obj = debug_obj_list;

    while (obj) {
        if (obj->addr == addr && obj->magic == DEBUG_OBJECTS_MAGIC) {
            if (obj->state == DEBUG_OBJECT_ACTIVE) {
                klog_info("debug_objects: warning: freeing active object at 0x%x (%s)",
                         addr, obj->desc);
                debug_obj_warnings++;
                active_objects--;
            }

            if (prev) {
                prev->next = obj->next;
            } else {
                debug_obj_list = obj->next;
            }

            obj->state = DEBUG_OBJECT_DESTROYED;
            obj->magic = 0;
            debug_obj_dealloc(obj);
            debug_obj_free_count++;
            return;
        }
        prev = obj;
        obj = obj->next;
    }

    klog_info("debug_objects: warning: free unknown object at 0x%x", addr);
    debug_obj_warnings++;
}

int debug_object_is_active(void *addr) {
    struct debug_obj *obj = debug_object_lookup(addr);
    if (obj && obj->state == DEBUG_OBJECT_ACTIVE) {
        return 1;
    }
    return 0;
}

void debug_objects_print_stats(void) {
    klog_info("=== Debug Objects Tracking System Statistics ===");
    klog_info("Max pool size: %u", DEBUG_OBJECTS_MAX_POOL);
    klog_info("Total init calls: %u", debug_obj_init_count);
    klog_info("Total activate calls: %u", debug_obj_activate_count);
    klog_info("Total deactivate calls: %u", debug_obj_deactivate_count);
    klog_info("Total free calls: %u", debug_obj_free_count);
    klog_info("Warnings detected: %u", debug_obj_warnings);
    klog_info("Currently active objects: %u", active_objects);

    klog_info("Tracked objects:");
    struct debug_obj *obj = debug_obj_list;
    int i = 0;
    while (obj) {
        const char *state_str = "unknown";
        if (obj->state == DEBUG_OBJECT_INACTIVE) state_str = "inactive";
        else if (obj->state == DEBUG_OBJECT_ACTIVE) state_str = "active";
        else if (obj->state == DEBUG_OBJECT_DESTROYED) state_str = "destroyed";

        klog_info("  [%d] addr=0x%x desc=%s state=%s refcount=%u act=%u deact=%u",
                 i++, obj->addr, obj->desc, state_str,
                 obj->refcount, obj->activate_count, obj->deactivate_count);
        obj = obj->next;
    }
}
