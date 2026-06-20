#ifndef VIDEO_MODE_MGR_H
#define VIDEO_MODE_MGR_H

#include "stdint.h"

/* Video mode manager - enumerates and manages display modes.
 * Wraps VBE/VESA functionality for mode listing and switching. */

#define VIDEO_MAX_MODES  32

/* Pixel format */
#define VIDEO_FMT_RGB565    0   /* 16-bit RGB */
#define VIDEO_FMT_RGB888    1   /* 24-bit RGB */
#define VIDEO_FMT_XRGB8888  2   /* 32-bit XRGB */
#define VIDEO_FMT_INDEXED   3   /* 8-bit palette */

typedef struct video_mode {
    uint32_t mode_id;         /* VBE mode number */
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t pitch;           /* Bytes per scanline */
    uint32_t pixel_format;    /* VIDEO_FMT_* */
    uint32_t *framebuffer;    /* Linear framebuffer address */
    uint32_t fb_size;         /* Framebuffer size in bytes */
} video_mode_t;

/* Video adapter info */
typedef struct video_adapter {
    char     vendor[64];
    char     name[64];
    uint32_t vbe_version;
    uint32_t total_memory;    /* Video memory in KB */
    uint32_t mode_count;
    video_mode_t modes[VIDEO_MAX_MODES];
    video_mode_t *current_mode;
} video_adapter_t;

/* Initialize video mode manager (probes VBE) */
int video_init(void);

/* Get the adapter info (modes, memory, etc.) */
video_adapter_t *video_get_adapter(void);

/* Find a mode matching the requested resolution */
video_mode_t *video_find_mode(uint32_t width, uint32_t height, uint32_t bpp);

/* Set the current video mode */
int video_set_mode(uint32_t width, uint32_t height, uint32_t bpp);

/* Get the current video mode */
video_mode_t *video_get_current_mode(void);

/* Blit a rectangular region to the framebuffer */
void video_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const void *data);

/* Fill a rectangular region with a color */
void video_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);

/* Scroll the screen up by 'lines' pixels */
void video_scroll_up(uint32_t lines, uint32_t bg_color);

/* Set a single pixel */
void video_set_pixel(uint32_t x, uint32_t y, uint32_t color);

#endif /* VIDEO_MODE_MGR_H */
