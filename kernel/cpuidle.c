#include "cpuidle.h"
#include "klog.h"
#include "string.h"

struct cpuidle_global {
    uint8_t initialized;
    struct cpuidle_driver driver;
    struct cpuidle_device devices[CPUIDLE_MAX_CPUS];
    uint32_t n_devices;
    uint64_t total_entries;
    uint64_t total_residency;
    uint32_t current_governor;
};

static struct cpuidle_global cpuidle_data;

static void cpuidle_init_driver(struct cpuidle_driver *drv) {
    memset(drv, 0, sizeof(*drv));
    strncpy(drv->name, "dummy_cpuidle", CPUIDLE_NAME_LEN);
    drv->state_count = 4;
    drv->governor = CPUIDLE_GOV_MENU;

    strncpy(drv->states[0].name, "C1", CPUIDLE_NAME_LEN);
    strncpy(drv->states[0].desc, "Halt", 63);
    drv->states[0].exit_latency = 1;
    drv->states[0].target_residency = 1;
    drv->states[0].power_usage = 1000;

    strncpy(drv->states[1].name, "C3", CPUIDLE_NAME_LEN);
    strncpy(drv->states[1].desc, "Sleep", 63);
    drv->states[1].exit_latency = 15;
    drv->states[1].target_residency = 30;
    drv->states[1].power_usage = 500;

    strncpy(drv->states[2].name, "C6", CPUIDLE_NAME_LEN);
    strncpy(drv->states[2].desc, "Deep Sleep", 63);
    drv->states[2].exit_latency = 50;
    drv->states[2].target_residency = 200;
    drv->states[2].power_usage = 200;

    strncpy(drv->states[3].name, "C7", CPUIDLE_NAME_LEN);
    strncpy(drv->states[3].desc, "Deeper Sleep", 63);
    drv->states[3].exit_latency = 150;
    drv->states[3].target_residency = 500;
    drv->states[3].power_usage = 50;

    drv->initialized = 1;
}

static int cpuidle_enter_ladder(struct cpuidle_device *dev) {
    (void)dev;
    static int promotion_threshold = 10;
    static int demotion_threshold = 2;
    static int last_state = 0;

    if (dev->last_residency > promotion_threshold) {
        if (last_state < (int)dev->driver->state_count - 1) {
            last_state++;
        }
    } else if (dev->last_residency < demotion_threshold) {
        if (last_state > 0) {
            last_state--;
        }
    }
    return last_state;
}

static int cpuidle_enter_menu(struct cpuidle_device *dev) {
    (void)dev;
    uint32_t predicted_us = 0;
    static uint32_t next_timer_us = 1000;
    static uint32_t history[12] = {100, 200, 500, 1000, 500, 300, 800, 1200, 600, 400, 900, 700};
    static int history_idx = 0;

    predicted_us = next_timer_us;
    for (int i = 0; i < 12; i++) {
        predicted_us = (predicted_us + history[(history_idx + i) % 12]) / 2;
    }

    int best_idx = 0;
    uint32_t best_power = 0xFFFFFFFF;

    for (uint32_t i = 0; i < dev->driver->state_count; i++) {
        struct cpuidle_state *s = &dev->driver->states[i];
        if (s->disabled) continue;
        if (s->target_residency > predicted_us) continue;
        if (s->power_usage < best_power) {
            best_power = s->power_usage;
            best_idx = (int)i;
        }
    }

    history[history_idx] = predicted_us;
    history_idx = (history_idx + 1) % 12;
    return best_idx;
}

int cpuidle_init(void) {
    if (cpuidle_data.initialized) return 0;
    memset(&cpuidle_data, 0, sizeof(cpuidle_data));

    cpuidle_init_driver(&cpuidle_data.driver);
    cpuidle_data.current_governor = cpuidle_data.driver.governor;
    cpuidle_data.n_devices = 1;

    for (uint32_t cpu = 0; cpu < cpuidle_data.n_devices; cpu++) {
        struct cpuidle_device *dev = &cpuidle_data.devices[cpu];
        memset(dev, 0, sizeof(*dev));
        dev->cpu = cpu;
        dev->enabled = 1;
        dev->registered = 1;
        dev->current_state = -1;
        dev->driver = &cpuidle_data.driver;
    }

    cpuidle_data.initialized = 1;
    klog_info("CPUIdle: initialized with %u states, governor=%s",
              cpuidle_data.driver.state_count,
              cpuidle_data.current_governor == CPUIDLE_GOV_LADDER ? "ladder" : "menu");
    return 0;
}

int cpuidle_register_driver(struct cpuidle_driver *drv) {
    if (!drv || cpuidle_data.driver.initialized) return -1;
    memcpy(&cpuidle_data.driver, drv, sizeof(*drv));
    cpuidle_data.driver.initialized = 1;
    return 0;
}

int cpuidle_register_device(struct cpuidle_device *dev) {
    if (!dev || cpuidle_data.n_devices >= CPUIDLE_MAX_DEVICES) return -1;
    memcpy(&cpuidle_data.devices[cpuidle_data.n_devices], dev, sizeof(*dev));
    cpuidle_data.n_devices++;
    return 0;
}

int cpuidle_select(struct cpuidle_device *dev) {
    if (!dev || !dev->enabled) return 0;
    if (cpuidle_data.current_governor == CPUIDLE_GOV_LADDER) {
        return cpuidle_enter_ladder(dev);
    } else {
        return cpuidle_enter_menu(dev);
    }
}

int cpuidle_enter_state(struct cpuidle_device *dev, int index) {
    if (!dev || !dev->driver || index < 0 || (uint32_t)index >= dev->driver->state_count) return -1;
    struct cpuidle_state *state = &dev->driver->states[index];
    if (state->disabled) return -1;

    dev->current_state = index;
    state->usage++;
    dev->usage_count++;
    cpuidle_data.total_entries++;

    static uint64_t simulated_time = 0;
    uint64_t residency = state->target_residency + (simulated_time % 100);
    state->time += residency;
    dev->last_residency = residency;
    cpuidle_data.total_residency += residency;
    simulated_time += residency + state->exit_latency;

    dev->current_state = -1;
    return index;
}

void cpuidle_idle_call(void) {
    for (uint32_t cpu = 0; cpu < cpuidle_data.n_devices; cpu++) {
        struct cpuidle_device *dev = &cpuidle_data.devices[cpu];
        if (!dev->enabled || !dev->driver) continue;
        int state = cpuidle_select(dev);
        cpuidle_enter_state(dev, state);
    }
}

void cpuidle_print_stats(void) {
    klog_info("=== CPUIdle Statistics ===");
    klog_info("Driver: %s", cpuidle_data.driver.name);
    klog_info("Governor: %s", cpuidle_data.current_governor == CPUIDLE_GOV_LADDER ? "ladder" : "menu");
    klog_info("Total C-state entries: %llu", (unsigned long long)cpuidle_data.total_entries);
    klog_info("Total residency: %llu us", (unsigned long long)cpuidle_data.total_residency);
    klog_info("Devices: %u", cpuidle_data.n_devices);
    klog_info("");

    klog_info("C-states:");
    for (uint32_t i = 0; i < cpuidle_data.driver.state_count; i++) {
        struct cpuidle_state *s = &cpuidle_data.driver.states[i];
        klog_info("  %s: %s", s->name, s->desc);
        klog_info("    exit_latency: %u us, target_residency: %u us",
                  s->exit_latency, s->target_residency);
        klog_info("    power: %u mW, usage: %llu, time: %llu us",
                  s->power_usage, (unsigned long long)s->usage, (unsigned long long)s->time);
        klog_info("    status: %s", s->disabled ? "disabled" : "enabled");
    }
    klog_info("");

    for (uint32_t cpu = 0; cpu < cpuidle_data.n_devices; cpu++) {
        struct cpuidle_device *dev = &cpuidle_data.devices[cpu];
        klog_info("CPU%u:", cpu);
        klog_info("  enabled: %s, registered: %s",
                  dev->enabled ? "yes" : "no", dev->registered ? "yes" : "no");
        klog_info("  last residency: %llu us", (unsigned long long)dev->last_residency);
        klog_info("  total entries: %llu", (unsigned long long)dev->usage_count);
    }
}
