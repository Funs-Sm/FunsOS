#ifndef KRNG_H
#define KRNG_H

#include "stdint.h"

void krng_init(void);
uint64_t krng_next(void);
uint32_t krng_next32(void);
uint32_t krng_range(uint32_t min, uint32_t max);
void krng_seed(uint64_t seed);

#endif
