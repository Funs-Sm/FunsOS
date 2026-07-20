#ifndef CPUFREQ_H
#define CPUFREQ_H

#include "stdint.h"

#define CPUFREQ_NAME_LEN 16
#define CPUFREQ_MAX_FREQS 32
#define CPUFREQ_MAX_GOVERNORS 8
#define CPUFREQ_MAX_CPUS 8

#define CPUFREQ_DEFAULT_GOVERNOR "ondemand"

typedef enum {
    CPUFREQ_GOV_PERFORMANCE = 0,
    CPUFREQ_GOV_POWERSAVE = 1,
    CPUFREQ_GOV_USERSPACE = 2,
    CPUFREQ_GOV_ONDEMAND = 3,
    CPUFREQ_GOV_CONSERVATIVE = 4,
    CPUFREQ_GOV_SCHEDUTIL = 5,
    CPUFREQ_GOV_MAX
} cpufreq_gov_type_t;

struct cpufreq_policy;
struct cpufreq_governor;
struct cpufreq_driver;

typedef int (*cpufreq_driver_init_t)(struct cpufreq_driver *driver);
typedef int (*cpufreq_setpolicy_t)(struct cpufreq_policy *policy);
typedef int (*cpufreq_target_t)(struct cpufreq_policy *policy, uint32_t target_freq);
typedef uint32_t (*cpufreq_get_t)(uint32_t cpu);
typedef int (*cpufreq_governor_init_t)(struct cpufreq_policy *policy);
typedef void (*cpufreq_governor_exit_t)(struct cpufreq_policy *policy);
typedef void (*cpufreq_governor_limits_t)(struct cpufreq_policy *policy);

struct cpufreq_freq_table {
    uint32_t frequency;
    uint32_t voltage;
};

struct cpufreq_policy {
    uint32_t cpu;
    uint32_t min_freq;
    uint32_t max_freq;
    uint32_t cur_freq;
    uint32_t min;
    uint32_t max;
    struct cpufreq_freq_table freq_table[CPUFREQ_MAX_FREQS];
    uint32_t n_freqs;
    struct cpufreq_governor *governor;
    void *governor_data;
    uint64_t last_load;
    uint8_t policy_ready;
};

struct cpufreq_governor {
    char name[CPUFREQ_NAME_LEN];
    cpufreq_gov_type_t type;
    cpufreq_governor_init_t init;
    cpufreq_governor_exit_t exit;
    cpufreq_governor_limits_t limits;
    uint32_t min_sampling_rate_ms;
    struct cpufreq_governor *next;
};

struct cpufreq_driver {
    char name[CPUFREQ_NAME_LEN];
    uint8_t initialized;
    cpufreq_driver_init_t init;
    cpufreq_setpolicy_t setpolicy;
    cpufreq_target_t target;
    cpufreq_get_t get;
    uint32_t flags;
};

struct cpufreq_stats {
    uint64_t total_trans;
    uint64_t last_update;
    uint32_t cur_freq_idx;
    uint64_t time_in_state[CPUFREQ_MAX_FREQS];
};

struct cpufreq_global {
    uint8_t initialized;
    struct cpufreq_driver driver;
    struct cpufreq_policy policies[CPUFREQ_MAX_CPUS];
    struct cpufreq_stats stats[CPUFREQ_MAX_CPUS];
    uint32_t n_cpus;
    struct cpufreq_governor *governors;
    uint32_t governor_count;
    uint8_t boost_enabled;
    uint32_t transition_latency_ns;
};

int cpufreq_init(void);

int cpufreq_register_driver(struct cpufreq_driver *driver);
int cpufreq_unregister_driver(struct cpufreq_driver *driver);

int cpufreq_register_governor(struct cpufreq_governor *gov);
int cpufreq_unregister_governor(struct cpufreq_governor *gov);
struct cpufreq_governor *cpufreq_find_governor(const char *name);

int cpufreq_set_policy(uint32_t cpu, uint32_t min_freq, uint32_t max_freq);
int cpufreq_set_governor(uint32_t cpu, const char *gov_name);
int cpufreq_set_frequency(uint32_t cpu, uint32_t freq);
uint32_t cpufreq_get_frequency(uint32_t cpu);
void cpufreq_set_boost(uint8_t enable);

void cpufreq_update_load(uint32_t cpu, uint8_t load);
void cpufreq_tick(void);

void cpufreq_print_stats(void);
void cpufreq_print_governors(void);

typedef struct {
    uint32_t current_freq;
    uint32_t min_freq;
    uint32_t max_freq;
    char governor[CPUFREQ_NAME_LEN];
    uint32_t available_count;
    uint32_t available_freqs[CPUFREQ_MAX_FREQS];
} cpufreq_info_t;

cpufreq_info_t *cpufreq_get_info(void);
int cpufreq_set(uint32_t mhz);

extern struct cpufreq_global cpufreq_data;

#endif
