#ifndef DEBUGOBJECTS_H
#define DEBUGOBJECTS_H

#include "stdint.h"

#define DEBUG_OBJECTS_MAX_POOL    256
#define DEBUG_OBJECTS_DESC_LEN    64
#define DEBUG_OBJECTS_MAGIC       0x44424A4F

enum debug_obj_state {
    DEBUG_OBJECT_INACTIVE = 0,
    DEBUG_OBJECT_ACTIVE,
    DEBUG_OBJECT_DESTROYED
};

struct debug_obj {
    struct debug_obj *next;
    void *addr;
    char desc[DEBUG_OBJECTS_DESC_LEN];
    uint32_t state;
    uint32_t refcount;
    uint32_t magic;
    uint32_t activate_count;
    uint32_t deactivate_count;
};

int debug_objects_init(void);

void debug_object_init(void *addr, const char *desc);
void debug_object_activate(void *addr);
void debug_object_deactivate(void *addr);
void debug_object_free(void *addr);
void debug_object_ref(void *addr);
void debug_object_unref(void *addr);

struct debug_obj *debug_object_lookup(void *addr);
int debug_object_is_active(void *addr);

void debug_objects_print_stats(void);

#endif
