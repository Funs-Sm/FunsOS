#ifndef VESA_H
#define VESA_H

#include "stdint.h"

typedef struct __attribute__((packed)) {
    uint16_t mode_attributes;
    uint8_t  win_a_attrs;
    uint8_t  win_b_attrs;
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
    uint8_t  reserved_mask_size;
    uint8_t  reserved_field_position;
    uint8_t  direct_color_mode_info;
    uint32_t phys_base_ptr;
    uint32_t off_screen_memory_offset;
    uint16_t off_screen_memory_size;
    uint8_t  reserved2[206];
} VBE_MODE_INFO;

typedef struct __attribute__((packed)) {
    uint8_t  signature[4];
    uint16_t version;
    uint32_t oem_string_ptr;
    uint8_t  capabilities[4];
    uint32_t video_mode_ptr;
    uint16_t total_memory;
    uint16_t oem_software_rev;
    uint32_t oem_vendor_ptr;
    uint32_t oem_product_ptr;
    uint32_t oem_revision_ptr;
    uint8_t  reserved[222];
} VBE_CONTROLLER_INFO;

typedef struct {
    uint32_t fb_addr;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_bpp;
    uint32_t fb_pitch;
    uint32_t fb_size;
} vesa_info_t;

typedef struct {
    uint16_t mode;
    uint16_t width;
    uint16_t height;
    uint8_t  bpp;
    uint8_t  _padding;
    uint32_t framebuffer;
    uint32_t pitch;
} vbe_mode_info_t;

void vesa_init(vesa_info_t *info);
void vesa_set_mode(uint16_t mode);
void vesa_get_mode_info(uint16_t mode, VBE_MODE_INFO *info);
void vesa_put_pixel(uint32_t x, uint32_t y, uint32_t color);
void vesa_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void vesa_draw_bitmap(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const void *data);

int vbe_set_mode(uint32_t width, uint32_t height, uint32_t bpp);
int is_vbe_mode(void);
vbe_mode_info_t *vbe_get_current_mode(void);
uint32_t *vbe_get_framebuffer(void);
void vbe_init_from_multiboot(uint32_t vbe_mode, uint32_t fb_addr,
                             uint32_t fb_width, uint32_t fb_height,
                             uint32_t fb_bpp, uint32_t fb_pitch);

#endif
