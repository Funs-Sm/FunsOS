#ifndef VBOX_RNG_H
#define VBOX_RNG_H

#include "stdint.h"

#define VBOX_RNG_MAJOR 1
#define VBOX_RNG_MINOR_RANDOM  8
#define VBOX_RNG_MINOR_URANDOM 9

int      vbox_rng_init(void);
void     vbox_rng_seed(uint64_t seed);
uint32_t vbox_rng_read(uint8_t *buf, uint32_t len);
uint32_t vbox_rng_u32(void);
uint64_t vbox_rng_u64(void);
uint8_t  vbox_rng_byte(void);

#endif
