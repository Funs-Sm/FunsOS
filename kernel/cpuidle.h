#ifndef CPUIDLE_H
#define CPUIDLE_H

#include "stdint.h"

#define CPUIDLE_NAME_LEN 16
#define CPUIDLE_MAX_STATES 8
#define CPUIDLE_MAX_CPUS 8
#define CPUIDLE_MAX_DEVICES 8

#define CPUIDLE_STATE_MAX 8

struct cpuidle_state {
    char name[CPUIDLE_NAME_LEN];
    char desc[64];
    uint32_t exit_latency;
    uint32_t target_residency;
    uint32_t power_usage;
    uint8_t disabled;
    uint64_t usage;
    uint64_t time;
};

struct cpuidle_driver;
struct cpuidle_device;

typedef int (*cpuidle_enter_t)(struct cpuidle_device *dev, struct cpuidle_driver *drv, int index);

struct cpuidle_driver {
    char name[CPUIDLE_NAME_LEN];
    uint8_t initialized;
    struct cpuidle_state states[CPUIDLE_MAX_STATES];
    uint32_t state_count;
    uint32_t governor;
};

struct cpuidle_device {
    uint32_t cpu;
    uint8_t registered;
    uint8_t enabled;
    uint32_t current_state;
    uint64_t last_residency;
    uint64_t usage_count;
    struct cpuidle_driver *driver;
};

#define CPUIDLE_GOV_LADDER 0
#define CPUIDLE_GOV_MENU   1

int cpuidle_init(void);
int cpuidle_register_driver(struct cpuidle_driver *drv);
int cpuidle_register_device(struct cpuidle_device *dev);
int cpuidle_enter_state(struct cpuidle_device *dev, int index);
int cpuidle_select(struct cpuidle_device *dev);
void cpuidle_print_stats(void);

#endif
