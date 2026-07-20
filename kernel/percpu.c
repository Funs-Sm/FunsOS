#include "percpu.h"
#include "klog.h"
#include "string.h"

static struct percpu_area *percpu_list;
static uint8_t percpu_initialized;
static uint32_t percpu_alloc_count;
static uint32_t percpu_free_count;
static uint32_t total_percpu_memory;
static uint32_t percpu_num_cpus;
static uint32_t current_cpu_id;

static struct percpu_area percpu_area_pool[PERCPU_MAX_ALLOCS];
static uint32_t percpu_area_used[PERCPU_MAX_ALLOCS];

static uint8_t percpu_base_memory[PERCPU_AREA_SIZE];
static uint32_t percpu_base_offset;

static uint32_t percpu_align_up(uint32_t val, uint32_t align) {
    if (align == 0) align = 64;
    return (val + align - 1) & ~(align - 1);
}

int percpu_init(void) {
    if (percpu_initialized) return 0;

    percpu_list = NULL;
    memset(percpu_area_pool, 0, sizeof(percpu_area_pool));
    memset(percpu_area_used, 0, sizeof(percpu_area_used));
    memset(percpu_base_memory, 0, PERCPU_AREA_SIZE);

    percpu_alloc_count = 0;
    percpu_free_count = 0;
    total_percpu_memory = 0;
    percpu_base_offset = 0;
    percpu_num_cpus = PERCPU_MAX_CPUS;
    current_cpu_id = 0;

    percpu_initialized = 1;
    klog_info("Per-CPU allocator initialized (%u CPUs, %u KB area)", percpu_num_cpus, PERCPU_AREA_SIZE / 1024);
    return 0;
}

uint32_t percpu_get_cpu_id(void) {
    return current_cpu_id;
}

void percpu_set_cpu_id(uint32_t cpu) {
    if (cpu < percpu_num_cpus) {
        current_cpu_id = cpu;
    }
}

void *percpu_alloc(uint32_t size, uint32_t align, const char *name) {
    if (!percpu_initialized || size == 0) return NULL;

    struct percpu_area *area = NULL;
    for (uint32_t i = 0; i < PERCPU_MAX_ALLOCS; i++) {
        if (!percpu_area_used[i]) {
            area = &percpu_area_pool[i];
            percpu_area_used[i] = 1;
            break;
        }
    }

    if (!area) return NULL;

    memset(area, 0, sizeof(*area));

    uint32_t aligned_size = percpu_align_up(size, align);
    area->size = aligned_size;
    area->align = align ? align : 64;
    area->in_use = 1;

    if (name) {
        strncpy(area->name, name, PERCPU_NAME_LEN - 1);
    } else {
        strncpy(area->name, "percpu", PERCPU_NAME_LEN - 1);
    }

    for (uint32_t cpu = 0; cpu < percpu_num_cpus; cpu++) {
        area->cpu_offset[cpu] = percpu_base_offset;
        area->cpu_ptr[cpu] = &percpu_base_memory[percpu_base_offset];
        percpu_base_offset += aligned_size;
        memset(area->cpu_ptr[cpu], 0, aligned_size);
    }

    area->next = percpu_list;
    percpu_list = area;

    percpu_alloc_count++;
    total_percpu_memory += aligned_size * percpu_num_cpus;

    return area;
}

void percpu_free(void *ptr) {
    if (!percpu_initialized || !ptr) return;

    struct percpu_area *prev = NULL;
    struct percpu_area *area = percpu_list;

    while (area) {
        if ((void *)area == ptr) {
            if (prev) {
                prev->next = area->next;
            } else {
                percpu_list = area->next;
            }

            uint32_t idx = area - percpu_area_pool;
            if (idx < PERCPU_MAX_ALLOCS) {
                percpu_area_used[idx] = 0;
            }

            memset(area, 0, sizeof(*area));
            percpu_free_count++;
            return;
        }
        prev = area;
        area = area->next;
    }
}

void *percpu_ptr(void *base, uint32_t cpu) {
    if (!percpu_initialized || !base || cpu >= percpu_num_cpus) return NULL;

    struct percpu_area *area = (struct percpu_area *)base;
    return area->cpu_ptr[cpu];
}

void percpu_print_stats(void) {
    klog_info("=== Per-CPU Allocator Statistics ===");
    klog_info("Number of CPUs: %u", percpu_num_cpus);
    klog_info("Current CPU: %u", current_cpu_id);
    klog_info("Total allocations: %u", percpu_alloc_count);
    klog_info("Total frees: %u", percpu_free_count);
    klog_info("Total per-CPU memory: %u bytes (%u KB)", total_percpu_memory, total_percpu_memory / 1024);
    klog_info("Base memory used: %u / %u bytes", percpu_base_offset, PERCPU_AREA_SIZE);

    klog_info("Active per-CPU areas:");
    struct percpu_area *area = percpu_list;
    int i = 0;
    while (area) {
        klog_info("  [%d] name=%s size=%u align=%u",
                 i++, area->name, area->size, area->align);
        klog_info("       offsets: cpu0=0x%x cpu1=0x%x cpu2=0x%x cpu3=0x%x",
                 area->cpu_offset[0], area->cpu_offset[1],
                 area->cpu_offset[2], area->cpu_offset[3]);
        area = area->next;
    }
}
