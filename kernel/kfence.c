#include "kfence.h"
#include "klog.h"
#include "string.h"

static struct kfence_object kfence_objects[KFENCE_MAX_OBJECTS];
static uint8_t kfence_pool[KFENCE_POOL_SIZE];
static uint8_t kfence_initialized;
static uint32_t kfence_alloc_count;
static uint32_t kfence_free_count;
static uint32_t kfence_uaf_count;
static uint32_t kfence_overflow_count;
static uint32_t kfence_check_count;
static uint32_t kfence_next_obj;

static void kfence_save_stack(uint32_t *stack) {
    for (int i = 0; i < KFENCE_STACK_DEPTH; i++) {
        stack[i] = 0;
    }
    stack[0] = (uint32_t)kfence_save_stack;
    stack[1] = (uint32_t)kfence_objects;
}

int kfence_init(void) {
    if (kfence_initialized) return 0;

    memset(kfence_objects, 0, sizeof(kfence_objects));
    memset(kfence_pool, 0, KFENCE_POOL_SIZE);

    for (uint32_t i = 0; i < KFENCE_MAX_OBJECTS; i++) {
        kfence_objects[i].state = KFENCE_OBJECT_FREE;
        kfence_objects[i].magic = 0;
        kfence_objects[i].addr = NULL;
        kfence_objects[i].size = 0;
    }

    kfence_alloc_count = 0;
    kfence_free_count = 0;
    kfence_uaf_count = 0;
    kfence_overflow_count = 0;
    kfence_check_count = 0;
    kfence_next_obj = 0;

    kfence_initialized = 1;
    klog_info("KFENCE memory error detector initialized (%u objects, %u KB pool)",
             KFENCE_MAX_OBJECTS, KFENCE_POOL_SIZE / 1024);
    return 0;
}

void *kfence_alloc_guard(uint32_t size) {
    if (!kfence_initialized || size == 0) return NULL;
    if (size > KFENCE_POOL_SIZE / KFENCE_MAX_OBJECTS - KFENCE_GUARD_SIZE) return NULL;

    struct kfence_object *obj = NULL;
    for (uint32_t tries = 0; tries < KFENCE_MAX_OBJECTS; tries++) {
        uint32_t idx = (kfence_next_obj + tries) % KFENCE_MAX_OBJECTS;
        if (kfence_objects[idx].state == KFENCE_OBJECT_FREE) {
            obj = &kfence_objects[idx];
            kfence_next_obj = (idx + 1) % KFENCE_MAX_OBJECTS;
            break;
        }
    }

    if (!obj) return NULL;

    uint32_t slot_size = KFENCE_POOL_SIZE / KFENCE_MAX_OBJECTS;
    uint32_t slot_idx = obj - kfence_objects;
    uintptr_t slot_start = (uintptr_t)&kfence_pool[slot_idx * slot_size];

    uint8_t *guard_page = (uint8_t *)(slot_start + slot_size - KFENCE_GUARD_SIZE);
    memset(guard_page, 0xFF, KFENCE_GUARD_SIZE);

    obj->addr = (void *)slot_start;
    obj->size = size;
    obj->state = KFENCE_OBJECT_ALLOCATED;
    obj->alloc_count++;
    obj->canary = KFENCE_CANARY_VALUE;
    obj->magic = KFENCE_MAGIC_ALLOC;

    kfence_save_stack(obj->alloc_stack);
    memset(obj->addr, 0, size);

    kfence_alloc_count++;
    return obj->addr;
}

void kfence_free_guard(void *addr) {
    if (!kfence_initialized || !addr) return;

    for (uint32_t i = 0; i < KFENCE_MAX_OBJECTS; i++) {
        if (kfence_objects[i].addr == addr && kfence_objects[i].state == KFENCE_OBJECT_ALLOCATED) {
            kfence_save_stack(kfence_objects[i].free_stack);
            memset(kfence_objects[i].addr, 0xFF, kfence_objects[i].size);
            kfence_objects[i].state = KFENCE_OBJECT_QUARANTINE;
            kfence_objects[i].magic = KFENCE_MAGIC_FREE;
            kfence_free_count++;
            return;
        }
    }

    klog_info("KFENCE: warning: kfree on non-kfence address 0x%x", addr);
}

int kfence_check(void *addr) {
    if (!kfence_initialized) return -1;

    kfence_check_count++;

    for (uint32_t i = 0; i < KFENCE_MAX_OBJECTS; i++) {
        if (kfence_objects[i].addr == addr) {
            if (kfence_objects[i].magic != KFENCE_MAGIC_ALLOC &&
                kfence_objects[i].magic != KFENCE_MAGIC_FREE) {
                klog_info("KFENCE: error: corrupted object magic at 0x%x", addr);
                return -1;
            }

            if (kfence_objects[i].state == KFENCE_OBJECT_QUARANTINE ||
                kfence_objects[i].state == KFENCE_OBJECT_FREE) {
                kfence_uaf_count++;
                klog_info("KFENCE: USE-AFTER-FREE detected at 0x%x (size=%u)",
                         addr, kfence_objects[i].size);
                return -1;
            }

            if (kfence_objects[i].canary != KFENCE_CANARY_VALUE) {
                kfence_overflow_count++;
                klog_info("KFENCE: BUFFER OVERFLOW detected at 0x%x (size=%u)",
                         addr, kfence_objects[i].size);
                return -1;
            }

            uint32_t slot_size = KFENCE_POOL_SIZE / KFENCE_MAX_OBJECTS;
            uint8_t *guard = (uint8_t *)((uintptr_t)addr + kfence_objects[i].size);
            uintptr_t guard_end = (uintptr_t)addr + slot_size;
            for (uint8_t *p = guard; (uintptr_t)p < guard_end; p++) {
                if (*p != 0) {
                    kfence_overflow_count++;
                    klog_info("KFENCE: GUARD PAGE OVERWRITE at 0x%x (offset=%d)",
                             addr, (int)(p - (uint8_t *)addr));
                    return -1;
                }
            }

            return 0;
        }
    }

    return 0;
}

int kfence_check_all(void) {
    if (!kfence_initialized) return -1;

    int errors = 0;
    for (uint32_t i = 0; i < KFENCE_MAX_OBJECTS; i++) {
        if (kfence_objects[i].state == KFENCE_OBJECT_ALLOCATED) {
            if (kfence_check(kfence_objects[i].addr) != 0) {
                errors++;
            }
        }
    }

    return errors;
}

uint32_t kfence_get_uaf_detected(void) {
    return kfence_uaf_count;
}

uint32_t kfence_get_overflow_detected(void) {
    return kfence_overflow_count;
}

void kfence_print_stats(void) {
    klog_info("=== KFENCE Memory Error Detector Statistics ===");
    klog_info("Pool size: %u bytes (%u KB)", KFENCE_POOL_SIZE, KFENCE_POOL_SIZE / 1024);
    klog_info("Max objects: %u", KFENCE_MAX_OBJECTS);
    klog_info("Guard page size: %u bytes", KFENCE_GUARD_SIZE);
    klog_info("Total allocations: %u", kfence_alloc_count);
    klog_info("Total frees: %u", kfence_free_count);
    klog_info("Checks performed: %u", kfence_check_count);
    klog_info("Use-after-free detected: %u", kfence_uaf_count);
    klog_info("Buffer overflows detected: %u", kfence_overflow_count);

    klog_info("Active objects:");
    int active = 0;
    for (uint32_t i = 0; i < KFENCE_MAX_OBJECTS; i++) {
        if (kfence_objects[i].state != KFENCE_OBJECT_FREE) {
            const char *state_str = "unknown";
            if (kfence_objects[i].state == KFENCE_OBJECT_ALLOCATED) state_str = "allocated";
            else if (kfence_objects[i].state == KFENCE_OBJECT_QUARANTINE) state_str = "quarantine";

            klog_info("  [%u] addr=0x%x size=%u state=%s alloc_count=%u",
                     i, kfence_objects[i].addr, kfence_objects[i].size,
                     state_str, kfence_objects[i].alloc_count);
            active++;
        }
    }
    klog_info("Total active/quarantine objects: %d", active);
}
