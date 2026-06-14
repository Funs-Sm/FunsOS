#ifndef PNG_H
#define PNG_H

#include "stdint.h"

#define PNG_RGB 0

int png_decode(const uint8_t *data, uint32_t size, uint8_t **out_rgb,
               uint32_t *out_width, uint32_t *out_height);
void png_free(uint8_t *rgb_data);

#endif
