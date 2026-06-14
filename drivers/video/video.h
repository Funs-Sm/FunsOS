#ifndef VIDEO_H
#define VIDEO_H

#include "stdint.h"

#define VIDEO_MAX_MODES 32

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;
    uint32_t *framebuffer;
} video_mode_t;

void video_init(void);
int32_t video_set_mode(uint32_t width, uint32_t height, uint32_t bpp);
video_mode_t *video_get_mode(void);
void video_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const void *data);

#endif
