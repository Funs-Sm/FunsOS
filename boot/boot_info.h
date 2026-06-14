#ifndef BOOT_INFO_H
#define BOOT_INFO_H

#include "stdint.h"

#define BOOT_INFO_MAGIC 0xB007F1E0

typedef struct {
    uint32_t base_low;
    uint32_t base_high;
    uint32_t length_low;
    uint32_t length_high;
    uint32_t type;
    uint32_t acpi_attrs;
} __attribute__((packed)) mmap_entry_t;

typedef struct {
    uint32_t magic;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t mmap_addr;
    uint32_t mmap_length;
    uint32_t vbe_mode;
    uint32_t vbe_fb_addr;
    uint32_t vbe_fb_width;
    uint32_t vbe_fb_height;
    uint32_t vbe_fb_bpp;
    uint32_t vbe_fb_pitch;
} __attribute__((packed)) boot_info_t;

#endif
