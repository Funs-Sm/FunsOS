#ifndef PIT_H
#define PIT_H

#include "stdint.h"

#define PIT_CHANNEL0 0x40
#define PIT_CHANNEL1 0x41
#define PIT_CHANNEL2 0x42
#define PIT_COMMAND  0x43

#define PIT_BASE_FREQ 1193182

#define PIT_MODE_SQUARE_WAVE 3
#define PIT_MODE_RATE_GEN    2

#define PIT_ACCESS_LATCH 0
#define PIT_ACCESS_LOW   1
#define PIT_ACCESS_HIGH  2
#define PIT_ACCESS_BOTH  3

void pit_init(uint32_t freq);
void pit_set_frequency(uint32_t freq);
uint16_t pit_read_count(void);
void pit_wait_ticks(uint32_t ticks);
void pit_beep(uint32_t freq, uint32_t duration_ms);
void pit_silence(void);

#endif
