#ifndef JPEG_H
#define JPEG_H

#include "stdint.h"

#define JPEG_RGB 0

int jpeg_decode(const uint8_t *data, uint32_t size, uint8_t **out_rgb,
                uint32_t *out_width, uint32_t *out_height);
void jpeg_free(uint8_t *rgb_data);

#endif
