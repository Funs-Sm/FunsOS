#include "cpufreq.h"
#include "klog.h"
#include "string.h"

struct cpufreq_global cpufreq_data;

static uint32_t dummy_driver_get(uint32_t cpu) {
    if (cpu >= CPUFREQ_MAX_CPUS) return 0;
    return cpufreq_data.policies[cpu].cur_freq;
}

static int dummy_driver_target(struct cpufreq_policy *policy, uint32_t target_freq) {
    if (!policy) return -1;
    policy->cur_freq = target_freq;
    return 0;
}

static int dummy_driver_init(struct cpufreq_driver *driver) {
    (void)driver;
    return 0;
}

static int gov_performance_init(struct cpufreq_policy *policy) {
    if (!policy) return -1;
    policy->cur_freq = policy->max;
    return 0;
}

static void gov_performance_exit(struct cpufreq_policy *policy) { (void)policy; }
static void gov_performance_limits(struct cpufreq_policy *policy) {
    if (policy) policy->cur_freq = policy->max;
}

static int gov_powersave_init(struct cpufreq_policy *policy) {
    if (!policy) return -1;
    policy->cur_freq = policy->min;
    return 0;
}

static void gov_powersave_exit(struct cpufreq_policy *policy) { (void)policy; }
static void gov_powersave_limits(struct cpufreq_policy *policy) {
    if (policy) policy->cur_freq = policy->min;
}

static int gov_userspace_init(struct cpufreq_policy *policy) { (void)policy; return 0; }
static void gov_userspace_exit(struct cpufreq_policy *policy) { (void)policy; }
static void gov_userspace_limits(struct cpufreq_policy *policy) { (void)policy; }

static int gov_ondemand_init(struct cpufreq_policy *policy) {
    if (!policy) return -1;
    policy->cur_freq = (policy->min + policy->max) / 2;
    return 0;
}
static void gov_ondemand_exit(struct cpufreq_policy *policy) { (void)policy; }
static void gov_ondemand_limits(struct cpufreq_policy *policy) { (void)policy; }

static int gov_conservative_init(struct cpufreq_policy *policy) {
    if (!policy) return -1;
    policy->cur_freq = policy->min + (policy->max - policy->min) / 4;
    return 0;
}
static void gov_conservative_exit(struct cpufreq_policy *policy) { (void)policy; }
static void gov_conservative_limits(struct cpufreq_policy *policy) { (void)policy; }

static int gov_schedutil_init(struct cpufreq_policy *policy) {
    if (!policy) return -1;
    policy->cur_freq = policy->max * 80 / 100;
    return 0;
}
static void gov_schedutil_exit(struct cpufreq_policy *policy) { (void)policy; }
static void gov_schedutil_limits(struct cpufreq_policy *policy) { (void)policy; }

static struct cpufreq_governor builtin_governors[CPUFREQ_GOV_MAX] = {
    { "performance", CPUFREQ_GOV_PERFORMANCE, gov_performance_init, gov_performance_exit, gov_performance_limits, 1000, NULL },
    { "powersave",   CPUFREQ_GOV_POWERSAVE,   gov_powersave_init,   gov_powersave_exit,   gov_powersave_limits,   1000, NULL },
    { "userspace",   CPUFREQ_GOV_USERSPACE,   gov_userspace_init,   gov_userspace_exit,   gov_userspace_limits,   1000, NULL },
    { "ondemand",    CPUFREQ_GOV_ONDEMAND,    gov_ondemand_init,    gov_ondemand_exit,    gov_ondemand_limits,    10,   NULL },
    { "conservative",CPUFREQ_GOV_CONSERVATIVE,gov_conservative_init,gov_conservative_exit,gov_conservative_limits,20,   NULL },
    { "schedutil",   CPUFREQ_GOV_SCHEDUTIL,   gov_schedutil_init,   gov_schedutil_exit,   gov_schedutil_limits,   1,    NULL },
};

static struct cpufreq_governor *find_governor(const char *name) {
    struct cpufreq_governor *g = cpufreq_data.governors;
    while (g) {
        if (strcmp(g->name, name) == 0) return g;
        g = g->next;
    }
    for (uint32_t i = 0; i < CPUFREQ_GOV_MAX; i++) {
        if (strcmp(builtin_governors[i].name, name) == 0) return &builtin_governors[i];
    }
    return NULL;
}

static int setup_policy(uint32_t cpu) {
    struct cpufreq_policy *p = &cpufreq_data.policies[cpu];
    struct cpufreq_stats *s = &cpufreq_data.stats[cpu];
    memset(p, 0, sizeof(*p));
    memset(s, 0, sizeof(*s));

    p->cpu = cpu;
    p->min_freq = 800000;
    p->max_freq = 4000000;
    p->min = 800000;
    p->max = 4000000;
    p->cur_freq = 2000000;

    uint32_t freqs[] = {800000, 1200000, 1600000, 2000000, 2400000, 2800000, 3200000, 3600000, 4000000};
    uint32_t volts[] = {800, 850, 900, 950, 1000, 1050, 1100, 1150, 1200};
    p->n_freqs = sizeof(freqs)/sizeof(freqs[0]);
    if (p->n_freqs > CPUFREQ_MAX_FREQS) p->n_freqs = CPUFREQ_MAX_FREQS;
    for (uint32_t i = 0; i < p->n_freqs; i++) {
        p->freq_table[i].frequency = freqs[i];
        p->freq_table[i].voltage = volts[i];
    }

    p->governor = find_governor(CPUFREQ_DEFAULT_GOVERNOR);
    if (p->governor && p->governor->init) p->governor->init(p);
    p->policy_ready = 1;
    return 0;
}

int cpufreq_init(void) {
    if (cpufreq_data.initialized) return 0;
    memset(&cpufreq_data, 0, sizeof(cpufreq_data));

    for (uint32_t i = 0; i < CPUFREQ_GOV_MAX; i++) {
        builtin_governors[i].next = cpufreq_data.governors;
        cpufreq_data.governors = &builtin_governors[i];
        cpufreq_data.governor_count++;
    }

    memset(&cpufreq_data.driver, 0, sizeof(cpufreq_data.driver));
    strncpy(cpufreq_data.driver.name, "dummy_cpufreq", CPUFREQ_NAME_LEN);
    cpufreq_data.driver.init = dummy_driver_init;
    cpufreq_data.driver.target = dummy_driver_target;
    cpufreq_data.driver.get = dummy_driver_get;
    cpufreq_data.driver.initialized = 1;
    cpufreq_data.n_cpus = 1;
    cpufreq_data.boost_enabled = 0;
    cpufreq_data.transition_latency_ns = 10000;

    for (uint32_t cpu = 0; cpu < cpufreq_data.n_cpus; cpu++) {
        setup_policy(cpu);
    }

    cpufreq_data.initialized = 1;
    return 0;
}

int cpufreq_register_driver(struct cpufreq_driver *driver) {
    if (!driver || cpufreq_data.driver.initialized) return -1;
    memcpy(&cpufreq_data.driver, driver, sizeof(*driver));
    cpufreq_data.driver.initialized = 1;
    if (driver->init) driver->init(&cpufreq_data.driver);
    return 0;
}

int cpufreq_unregister_driver(struct cpufreq_driver *driver) {
    (void)driver;
    memset(&cpufreq_data.driver, 0, sizeof(cpufreq_data.driver));
    return 0;
}

int cpufreq_register_governor(struct cpufreq_governor *gov) {
    if (!gov) return -1;
    gov->next = cpufreq_data.governors;
    cpufreq_data.governors = gov;
    cpufreq_data.governor_count++;
    return 0;
}

int cpufreq_unregister_governor(struct cpufreq_governor *gov) {
    if (!gov) return -1;
    struct cpufreq_governor **p = &cpufreq_data.governors;
    while (*p) {
        if (*p == gov) { *p = gov->next; cpufreq_data.governor_count--; return 0; }
        p = &(*p)->next;
    }
    return -1;
}

struct cpufreq_governor *cpufreq_find_governor(const char *name) {
    return find_governor(name);
}

int cpufreq_set_policy(uint32_t cpu, uint32_t min_freq, uint32_t max_freq) {
    if (cpu >= cpufreq_data.n_cpus || !cpufreq_data.initialized) return -1;
    struct cpufreq_policy *p = &cpufreq_data.policies[cpu];
    if (min_freq < 800000) min_freq = 800000;
    if (max_freq > 4000000) max_freq = 4000000;
    if (min_freq > max_freq) return -1;
    p->min = min_freq;
    p->max = max_freq;
    if (p->cur_freq < min_freq) p->cur_freq = min_freq;
    if (p->cur_freq > max_freq) p->cur_freq = max_freq;
    if (p->governor && p->governor->limits) p->governor->limits(p);
    return 0;
}

int cpufreq_set_governor(uint32_t cpu, const char *gov_name) {
    if (cpu >= cpufreq_data.n_cpus || !gov_name || !cpufreq_data.initialized) return -1;
    struct cpufreq_policy *p = &cpufreq_data.policies[cpu];
    struct cpufreq_governor *gov = find_governor(gov_name);
    if (!gov) return -1;
    if (p->governor && p->governor->exit) p->governor->exit(p);
    p->governor = gov;
    if (gov->init) gov->init(p);
    return 0;
}

int cpufreq_set_frequency(uint32_t cpu, uint32_t freq) {
    if (cpu >= cpufreq_data.n_cpus || !cpufreq_data.initialized) return -1;
    struct cpufreq_policy *p = &cpufreq_data.policies[cpu];
    if (p->governor && p->governor->type != CPUFREQ_GOV_USERSPACE) return -1;
    if (freq < p->min) freq = p->min;
    if (freq > p->max) freq = p->max;
    if (cpufreq_data.driver.target) cpufreq_data.driver.target(p, freq);
    cpufreq_data.stats[cpu].total_trans++;
    return 0;
}

uint32_t cpufreq_get_frequency(uint32_t cpu) {
    if (cpu >= cpufreq_data.n_cpus) return 0;
    if (cpufreq_data.driver.get) return cpufreq_data.driver.get(cpu);
    return cpufreq_data.policies[cpu].cur_freq;
}

void cpufreq_set_boost(uint8_t enable) {
    cpufreq_data.boost_enabled = enable ? 1 : 0;
}

void cpufreq_update_load(uint32_t cpu, uint8_t load) {
    if (cpu >= cpufreq_data.n_cpus || !cpufreq_data.initialized) return;
    struct cpufreq_policy *p = &cpufreq_data.policies[cpu];
    if (!p->governor) return;

    switch (p->governor->type) {
    case CPUFREQ_GOV_ONDEMAND:
        if (load > 80) p->cur_freq = p->max;
        else if (load < 20) p->cur_freq = p->min;
        else p->cur_freq = p->min + (p->max - p->min) * load / 100;
        break;
    case CPUFREQ_GOV_CONSERVATIVE:
        if (load > 80) p->cur_freq += (p->max - p->min) / 10;
        else if (load < 20) p->cur_freq -= (p->max - p->min) / 10;
        if (p->cur_freq > p->max) p->cur_freq = p->max;
        if (p->cur_freq < p->min) p->cur_freq = p->min;
        break;
    case CPUFREQ_GOV_SCHEDUTIL:
        p->cur_freq = p->min + (p->max - p->min) * load / 100;
        break;
    default: break;
    }
    p->last_load = load;
}

void cpufreq_tick(void) {
    static uint8_t sim_load = 50;
    static int dir = 1;
    for (uint32_t cpu = 0; cpu < cpufreq_data.n_cpus; cpu++) {
        cpufreq_update_load(cpu, sim_load);
    }
    sim_load = (uint8_t)(sim_load + dir * 5);
    if (sim_load >= 95) dir = -1;
    if (sim_load <= 10) dir = 1;
}

void cpufreq_print_governors(void) {
    klog_info("=== CPUFreq Governors ===");
    struct cpufreq_governor *g = cpufreq_data.governors;
    uint32_t idx = 0;
    while (g && idx < cpufreq_data.governor_count) {
        const char *desc = "";
        switch (g->type) {
        case CPUFREQ_GOV_PERFORMANCE: desc = "Run at max frequency"; break;
        case CPUFREQ_GOV_POWERSAVE: desc = "Run at min frequency"; break;
        case CPUFREQ_GOV_USERSPACE: desc = "User-set frequency"; break;
        case CPUFREQ_GOV_ONDEMAND: desc = "On-demand scaling"; break;
        case CPUFREQ_GOV_CONSERVATIVE: desc = "Conservative scaling"; break;
        case CPUFREQ_GOV_SCHEDUTIL: desc = "Scheduler-driven"; break;
        default: break;
        }
        klog_info("  %s: %s (sample %u ms)", g->name, desc, g->min_sampling_rate_ms);
        g = g->next;
        idx++;
    }
}

void cpufreq_print_stats(void) {
    klog_info("=== CPUFreq Statistics ===");
    klog_info("CPUFreq Driver: %s", cpufreq_data.driver.name);
    klog_info("Boost: %s", cpufreq_data.boost_enabled ? "enabled" : "disabled");
    klog_info("Transition latency: %u ns", cpufreq_data.transition_latency_ns);
    klog_info("Number of CPUs: %u", cpufreq_data.n_cpus);
    klog_info("Governors registered: %u", cpufreq_data.governor_count);
    klog_info("");
    for (uint32_t cpu = 0; cpu < cpufreq_data.n_cpus; cpu++) {
        struct cpufreq_policy *p = &cpufreq_data.policies[cpu];
        struct cpufreq_stats *s = &cpufreq_data.stats[cpu];
        klog_info("CPU %u:", cpu);
        klog_info("  Current: %u MHz", p->cur_freq / 1000);
        klog_info("  Policy:  %u - %u MHz", p->min / 1000, p->max / 1000);
        klog_info("  Governor: %s", p->governor ? p->governor->name : "none");
        klog_info("  Available frequencies: ");
        for (uint32_t i = 0; i < p->n_freqs; i++) {
            klog_info("    %u MHz", p->freq_table[i].frequency / 1000);
        }
        klog_info("  Total transitions: %u", (uint32_t)(s->total_trans & 0xFFFFFFFF));
    }
    cpufreq_print_governors();
}

static cpufreq_info_t compat_info;
cpufreq_info_t *cpufreq_get_info(void) {
    if (!cpufreq_data.initialized || cpufreq_data.n_cpus == 0) return NULL;
    struct cpufreq_policy *p = &cpufreq_data.policies[0];
    memset(&compat_info, 0, sizeof(compat_info));
    compat_info.current_freq = p->cur_freq / 1000;
    compat_info.min_freq = p->min / 1000;
    compat_info.max_freq = p->max / 1000;
    if (p->governor) {
        strncpy(compat_info.governor, p->governor->name, CPUFREQ_NAME_LEN - 1);
        compat_info.governor[CPUFREQ_NAME_LEN - 1] = '\0';
    } else {
        compat_info.governor[0] = '\0';
    }
    compat_info.available_count = p->n_freqs;
    if (compat_info.available_count > CPUFREQ_MAX_FREQS)
        compat_info.available_count = CPUFREQ_MAX_FREQS;
    for (uint32_t i = 0; i < compat_info.available_count; i++) {
        compat_info.available_freqs[i] = p->freq_table[i].frequency / 1000;
    }
    return &compat_info;
}

int cpufreq_set(uint32_t mhz) {
    if (!cpufreq_data.initialized || cpufreq_data.n_cpus == 0) return -1;
    struct cpufreq_policy *p = &cpufreq_data.policies[0];
    uint32_t freq = mhz * 1000;
    if (freq < p->min) freq = p->min;
    if (freq > p->max) freq = p->max;
    if (cpufreq_data.driver.target) cpufreq_data.driver.target(p, freq);
    cpufreq_data.stats[0].total_trans++;
    return 0;
}
