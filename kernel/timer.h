#ifndef TIMER_H
#define TIMER_H

#include "kernel_types.h"

void init_timer(void);
void timer_set_frequency(uint32_t freq);
uint32_t timer_get_ticks(void);
void timer_sleep(uint32_t ms);

uint32_t sys_timer_create(uint32_t interval_ms, void (*callback)(void), void *arg);
void sys_timer_stop(uint32_t timer_id);
int sys_timer_destroy(uint32_t timer_id);

#endif
