#include "cpufreq.h"
#include "string.h"
#include "timer.h"

/* Intel MSR definitions */
#define IA32_PERF_STATUS   0x198
#define IA32_PERF_CTL      0x199
#define IA32_MISC_ENABLE   0x1A0

/* CPUID feature flags */
#define CPUID_EST_BIT      7  /* Enhanced SpeedStep Technology bit in ECX */

/* Governor types */
#define GOV_PERFORMANCE  0
#define GOV_POWERSAVE    1
#define GOV_ONDEMAND     2

static cpufreq_info_t cpufreq_info;
static int cpufreq_available = 0;
static int current_governor = GOV_PERFORMANCE;

/* On-demand governor state */
static uint32_t ondemand_idle_ticks = 0;
static uint32_t ondemand_total_ticks = 0;
static uint32_t ondemand_last_check = 0;
#define ONDEMAND_CHECK_INTERVAL  100  /* Check every 100 ticks (1 second) */
#define ONDEMAND_UP_THRESHOLD    80   /* Scale up if CPU usage > 80% */
#define ONDEMAND_DOWN_THRESHOLD  20   /* Scale down if CPU usage < 20% */

/* MSR read/write using inline assembly */
static inline uint64_t msr_read(uint32_t msr) {
    uint32_t low, high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

static inline void msr_write(uint32_t msr, uint64_t val) {
    asm volatile("wrmsr" :: "a"((uint32_t)val), "d"((uint32_t)(val >> 32)), "c"(msr));
}

/* Check if CPU supports Enhanced SpeedStep */
static int cpufreq_check_est(void) {
    uint32_t eax, ebx, ecx, edx;

    /* CPUID with EAX=1 to get feature flags */
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));

    /* EST is bit 7 of ECX for some CPUs, but for older ones it's in EDX.
       For Intel, check ECX bit 7 (SpeedStep) or use model-specific detection */
    /* A simpler approach: try to read IA32_PERF_STATUS and check if it's valid */
    uint64_t perf_status = msr_read(IA32_PERF_STATUS);
    uint32_t current_ratio = (uint32_t)(perf_status >> 8) & 0xFF;

    /* If we can read a non-zero ratio, SpeedStep is likely available */
    if (current_ratio != 0) {
        return 1;
    }

    return 0;
}

/* Detect available P-states from MSR */
static void cpufreq_detect_pstates(void) {
    /* Read the current performance status */
    uint64_t perf_status = msr_read(IA32_PERF_STATUS);

    /* The bus ratio is in bits [15:8] of the lower 32 bits */
    uint32_t current_ratio = (uint32_t)(perf_status >> 8) & 0xFF;

    /* Assume a typical FSB of 100 MHz for modern Intel CPUs */
    /* The actual FSB varies, but 100 MHz is common for Core-series */
    uint32_t fsb = 100;

    cpufreq_info.current_freq = current_ratio * fsb;
    cpufreq_info.max_freq = current_ratio * fsb;
    cpufreq_info.min_freq = (current_ratio / 2) * fsb;  /* Estimate min as half */

    /* Try to detect available P-states by reading different values
       from IA32_PERF_STATUS. On many Intel CPUs, the available ratios
       can be found by iterating. For simplicity, we generate a set. */
    cpufreq_info.available_count = 0;
    uint32_t min_ratio = current_ratio / 2;
    if (min_ratio < 6) min_ratio = 6;  /* Minimum ratio is usually 6 */

    for (uint32_t r = min_ratio; r <= current_ratio; r++) {
        if (cpufreq_info.available_count >= 16) break;
        cpufreq_info.available_freqs[cpufreq_info.available_count++] = r * fsb;
    }

    /* If no P-states found, just add current */
    if (cpufreq_info.available_count == 0) {
        cpufreq_info.available_freqs[0] = cpufreq_info.current_freq;
        cpufreq_info.available_count = 1;
    }
}

void cpufreq_init(void) {
    memset(&cpufreq_info, 0, sizeof(cpufreq_info_t));

    /* Check for Intel Speed Step support */
    if (!cpufreq_check_est()) {
        cpufreq_available = 0;
        strcpy(cpufreq_info.governor, "none");
        return;
    }

    cpufreq_available = 1;

    /* Detect available P-states */
    cpufreq_detect_pstates();

    /* Default to performance governor */
    cpufreq_governor_performance();
}

uint32_t cpufreq_get(void) {
    if (!cpufreq_available) return 0;

    /* Read current frequency from IA32_PERF_STATUS */
    uint64_t perf_status = msr_read(IA32_PERF_STATUS);
    uint32_t ratio = (uint32_t)(perf_status >> 8) & 0xFF;
    uint32_t fsb = 100;  /* Assume 100 MHz FSB */

    cpufreq_info.current_freq = ratio * fsb;
    return cpufreq_info.current_freq;
}

int cpufreq_set(uint32_t mhz) {
    if (!cpufreq_available) return -1;

    /* Validate the requested frequency is in available list */
    int found = 0;
    for (uint32_t i = 0; i < cpufreq_info.available_count; i++) {
        if (cpufreq_info.available_freqs[i] == mhz) {
            found = 1;
            break;
        }
    }
    if (!found) return -1;

    /* Calculate the target ratio */
    uint32_t fsb = 100;
    uint32_t target_ratio = mhz / fsb;
    if (target_ratio == 0) return -1;

    /* Write to IA32_PERF_CTL to set the new frequency */
    uint64_t perf_ctl = ((uint64_t)target_ratio << 8);
    msr_write(IA32_PERF_CTL, perf_ctl);

    /* Update current frequency */
    cpufreq_info.current_freq = mhz;
    return 0;
}

cpufreq_info_t *cpufreq_get_info(void) {
    return &cpufreq_info;
}

void cpufreq_set_governor(const char *name) {
    if (!name) return;

    if (strcmp(name, "performance") == 0) {
        cpufreq_governor_performance();
    } else if (strcmp(name, "powersave") == 0) {
        cpufreq_governor_powersave();
    } else if (strcmp(name, "ondemand") == 0) {
        cpufreq_governor_ondemand();
    }
}

void cpufreq_governor_performance(void) {
    current_governor = GOV_PERFORMANCE;
    strcpy(cpufreq_info.governor, "performance");

    if (cpufreq_available) {
        cpufreq_set(cpufreq_info.max_freq);
    }
}

void cpufreq_governor_powersave(void) {
    current_governor = GOV_POWERSAVE;
    strcpy(cpufreq_info.governor, "powersave");

    if (cpufreq_available) {
        cpufreq_set(cpufreq_info.min_freq);
    }
}

void cpufreq_governor_ondemand(void) {
    current_governor = GOV_ONDEMAND;
    strcpy(cpufreq_info.governor, "ondemand");
    ondemand_idle_ticks = 0;
    ondemand_total_ticks = 0;
    ondemand_last_check = timer_get_ticks();
}

/* Called periodically from timer interrupt for ondemand governor */
void cpufreq_ondemand_tick(void) {
    if (!cpufreq_available || current_governor != GOV_ONDEMAND) return;

    ondemand_total_ticks++;
    /* We count idle ticks by checking if the current CPU is in idle loop.
       For simplicity, we use a heuristic based on timer_get_ticks(). */

    uint32_t now = timer_get_ticks();
    if (now - ondemand_last_check >= ONDEMAND_CHECK_INTERVAL) {
        /* Calculate CPU usage as percentage */
        uint32_t usage_pct = 0;
        if (ondemand_total_ticks > 0) {
            /* Simple heuristic: assume idle if we're in HLT often */
            usage_pct = 100 - (ondemand_idle_ticks * 100 / ondemand_total_ticks);
        }

        if (usage_pct > ONDEMAND_UP_THRESHOLD) {
            /* High load: scale to max */
            cpufreq_set(cpufreq_info.max_freq);
        } else if (usage_pct < ONDEMAND_DOWN_THRESHOLD) {
            /* Low load: scale to min */
            cpufreq_set(cpufreq_info.min_freq);
        } else {
            /* Medium load: find appropriate frequency */
            uint32_t target = cpufreq_info.min_freq +
                (cpufreq_info.max_freq - cpufreq_info.min_freq) * usage_pct / 100;
            /* Find nearest available frequency */
            uint32_t best = cpufreq_info.available_freqs[0];
            for (uint32_t i = 1; i < cpufreq_info.available_count; i++) {
                if (cpufreq_info.available_freqs[i] <= target) {
                    best = cpufreq_info.available_freqs[i];
                }
            }
            cpufreq_set(best);
        }

        ondemand_idle_ticks = 0;
        ondemand_total_ticks = 0;
        ondemand_last_check = now;
    }
}

/* Mark a tick as idle (called from idle loop) */
void cpufreq_ondemand_idle(void) {
    if (current_governor == GOV_ONDEMAND) {
        ondemand_idle_ticks++;
    }
}
