/* video.c - Video mode manager implementation
 * Wraps VBE/VESA mode enumeration and framebuffer operations */

#include "video.h"
#include "kheap.h"
#include "string.h"
#include "klog.h"

/* VBE info block (filled by stage2/bootloader) */
typedef struct {
    uint8_t  signature[4];    /* "VESA" */
    uint16_t version;
    uint32_t oem_string_ptr;
    uint32_t capabilities;
    uint32_t video_mode_ptr;  /* Pointer to mode list */
    uint16_t total_memory;    /* In 64KB blocks */
} vbe_info_t;

/* VBE mode info block */
typedef struct {
    uint16_t mode_attributes;
    uint8_t  win_a_attributes;
    uint8_t  win_b_attributes;
    uint16_t win_granularity;
    uint16_t win_size;
    uint16_t win_a_segment;
    uint16_t win_b_segment;
    uint32_t win_func_ptr;
    uint16_t bytes_per_scanline;
    uint16_t x_resolution;
    uint16_t y_resolution;
    uint8_t  x_char_size;
    uint8_t  y_char_size;
    uint8_t  number_of_planes;
    uint8_t  bits_per_pixel;
    uint8_t  number_of_banks;
    uint8_t  memory_model;
    uint8_t  bank_size;
    uint8_t  number_of_image_pages;
    uint8_t  reserved;
    uint8_t  red_mask_size;
    uint8_t  red_field_position;
    uint8_t  green_mask_size;
    uint8_t  green_field_position;
    uint8_t  blue_mask_size;
    uint8_t  blue_field_position;
    uint8_t  rsvd_mask_size;
    uint8_t  rsvd_field_position;
    uint8_t  direct_color_mode_info;
    uint32_t phys_base_ptr;    /* Linear framebuffer physical address */
    uint32_t off_screen_mem_ptr;
    uint16_t off_screen_mem_size;
} vbe_mode_info_t;

/* VBE info block - bootloader typically places this at physical 0x8000 */
#define VBE_INFO_PHYS_ADDR   0x8000
#define VBE_MODE_INFO_ADDR   0x8200

/* Global adapter state */
static video_adapter_t g_adapter;
static int g_initialized = 0;

/* Helper: determine pixel format from bpp and memory model */
static uint32_t determine_pixel_format(uint8_t bpp, uint8_t memory_model) {
    if (memory_model == 6) return VIDEO_FMT_RGB888;   /* Direct color */
    if (memory_model == 4) return VIDEO_FMT_INDEXED;   /* Packed pixel */
    switch (bpp) {
    case 16: return VIDEO_FMT_RGB565;
    case 24: return VIDEO_FMT_RGB888;
    case 32: return VIDEO_FMT_XRGB8888;
    case 8:  return VIDEO_FMT_INDEXED;
    default: return VIDEO_FMT_RGB888;
    }
}

int video_init(void) {
    if (g_initialized) return 0;

    memset(&g_adapter, 0, sizeof(g_adapter));
    g_adapter.current_mode = NULL;

    /* Try to read VBE info from bootloader-provided data at fixed address */
    vbe_info_t *vbe_info = (vbe_info_t *)(uintptr_t)VBE_INFO_PHYS_ADDR;
    if (vbe_info->signature[0] == 'V' && vbe_info->signature[1] == 'E') {
        g_adapter.vbe_version = vbe_info->version;
        g_adapter.total_memory = (uint32_t)vbe_info->total_memory * 64;

        /* Enumerate VBE modes */
        if (vbe_info->video_mode_ptr) {
            vbe_mode_info_t *vbe_mode = (vbe_mode_info_t *)(uintptr_t)VBE_MODE_INFO_ADDR;
            uint16_t *mode_list = (uint16_t *)(uintptr_t)vbe_info->video_mode_ptr;
            uint32_t count = 0;

            while (count < VIDEO_MAX_MODES) {
                uint16_t mode_id = mode_list[count];
                if (mode_id == 0xFFFF) break;

                /* Read mode info for this mode */
                video_mode_t *vm = &g_adapter.modes[count];
                vm->mode_id = mode_id;
                vm->width = vbe_mode->x_resolution;
                vm->height = vbe_mode->y_resolution;
                vm->bpp = vbe_mode->bits_per_pixel;
                vm->pitch = vbe_mode->bytes_per_scanline;
                vm->framebuffer = (uint32_t *)(uintptr_t)vbe_mode->phys_base_ptr;
                vm->fb_size = vm->pitch * vm->height;
                vm->pixel_format = determine_pixel_format(vm->bpp, vbe_mode->memory_model);

                count++;
            }
            g_adapter.mode_count = count;
        }

        klog_info("Video: VBE v%d.%d, %u KB, %u modes",
                  g_adapter.vbe_version >> 8, g_adapter.vbe_version & 0xFF,
                  g_adapter.total_memory, g_adapter.mode_count);
    } else {
        /* No VBE info - add a default mode */
        g_adapter.modes[0].width = 1024;
        g_adapter.modes[0].height = 768;
        g_adapter.modes[0].bpp = 32;
        g_adapter.modes[0].pitch = 1024 * 4;
        g_adapter.modes[0].pixel_format = VIDEO_FMT_XRGB8888;
        g_adapter.mode_count = 1;

        klog_info("Video: No VBE info, using default 1024x768x32");
    }

    g_initialized = 1;
    return 0;
}

video_adapter_t *video_get_adapter(void) {
    if (!g_initialized) return NULL;
    return &g_adapter;
}

video_mode_t *video_find_mode(uint32_t width, uint32_t height, uint32_t bpp) {
    if (!g_initialized) return NULL;

    for (uint32_t i = 0; i < g_adapter.mode_count; i++) {
        video_mode_t *m = &g_adapter.modes[i];
        if (m->width == width && m->height == height && m->bpp == bpp) {
            return m;
        }
    }

    /* Fallback: match just resolution */
    for (uint32_t i = 0; i < g_adapter.mode_count; i++) {
        video_mode_t *m = &g_adapter.modes[i];
        if (m->width == width && m->height == height) {
            return m;
        }
    }

    return NULL;
}

int video_set_mode(uint32_t width, uint32_t height, uint32_t bpp) {
    if (!g_initialized) return -1;

    video_mode_t *mode = video_find_mode(width, height, bpp);
    if (!mode) {
        klog_warn("Video: mode %ux%ux%u not found", width, height, bpp);
        return -1;
    }

    g_adapter.current_mode = mode;
    klog_info("Video: set mode %ux%ux%u", mode->width, mode->height, mode->bpp);
    return 0;
}

video_mode_t *video_get_current_mode(void) {
    if (!g_initialized) return NULL;
    return g_adapter.current_mode;
}

void video_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const void *data) {
    video_mode_t *mode = video_get_current_mode();
    if (!mode || !mode->framebuffer || !data) return;

    const uint32_t *src = (const uint32_t *)data;
    for (uint32_t row = 0; row < h; row++) {
        uint32_t dy = y + row;
        if (dy >= mode->height) break;
        for (uint32_t col = 0; col < w; col++) {
            uint32_t dx = x + col;
            if (dx >= mode->width) break;
            mode->framebuffer[dy * (mode->pitch / 4) + dx] = src[row * w + col];
        }
    }
}

void video_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    video_mode_t *mode = video_get_current_mode();
    if (!mode || !mode->framebuffer) return;

    for (uint32_t row = 0; row < h; row++) {
        uint32_t dy = y + row;
        if (dy >= mode->height) break;
        for (uint32_t col = 0; col < w; col++) {
            uint32_t dx = x + col;
            if (dx >= mode->width) break;
            mode->framebuffer[dy * (mode->pitch / 4) + dx] = color;
        }
    }
}

void video_scroll_up(uint32_t lines, uint32_t bg_color) {
    video_mode_t *mode = video_get_current_mode();
    if (!mode || !mode->framebuffer) return;

    uint32_t pitch_pixels = mode->pitch / 4;
    if (lines >= mode->height) {
        video_fill_rect(0, 0, mode->width, mode->height, bg_color);
        return;
    }

    /* Move pixels up */
    for (uint32_t y = 0; y < mode->height - lines; y++) {
        uint32_t *dst = &mode->framebuffer[y * pitch_pixels];
        uint32_t *src = &mode->framebuffer[(y + lines) * pitch_pixels];
        for (uint32_t x = 0; x < mode->width; x++) {
            dst[x] = src[x];
        }
    }

    /* Clear bottom */
    video_fill_rect(0, mode->height - lines, mode->width, lines, bg_color);
}

void video_set_pixel(uint32_t x, uint32_t y, uint32_t color) {
    video_mode_t *mode = video_get_current_mode();
    if (!mode || !mode->framebuffer) return;
    if (x >= mode->width || y >= mode->height) return;
    mode->framebuffer[y * (mode->pitch / 4) + x] = color;
}
