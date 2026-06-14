#ifndef CPUFREQ_H
#define CPUFREQ_H

#include "stdint.h"

typedef struct {
    uint32_t current_freq;     /* Current frequency in MHz */
    uint32_t min_freq;         /* Minimum frequency */
    uint32_t max_freq;         /* Maximum frequency */
    uint32_t available_count;  /* Number of available frequencies */
    uint32_t available_freqs[16]; /* Available frequencies */
    char governor[32];         /* Current governor name */
} cpufreq_info_t;

void cpufreq_init(void);
uint32_t cpufreq_get(void);
int cpufreq_set(uint32_t mhz);
cpufreq_info_t *cpufreq_get_info(void);
void cpufreq_set_governor(const char *name);
void cpufreq_governor_performance(void);
void cpufreq_governor_powersave(void);
void cpufreq_governor_ondemand(void);

#endif
