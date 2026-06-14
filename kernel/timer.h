#ifndef TIMER_H
#define TIMER_H

#include "kernel_types.h"

void init_timer(void);
void timer_set_frequency(uint32_t freq);
uint32_t timer_get_ticks(void);
void timer_sleep(uint32_t ms);

#endif
