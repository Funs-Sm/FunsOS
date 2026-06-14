#ifndef AMD_GPU_H
#define AMD_GPU_H

#include "stdint.h"
#include "pci.h"

/* AMD GPU PCI IDs */
#define AMD_VENDOR_ID 0x1002

/* Register offsets */
#define AMD_MM_INDEX      0x0000
#define AMD_MM_DATA       0x0004
#define AMD_BIOS_ROM      0x30C0

/* Display controller registers */
#define AMD_CRTC_CONTROL  0x6800
#define AMD_CRTC_BLANK    0x6804

typedef struct {
    uint8_t  pci_bus;
    uint8_t  pci_dev;
    uint8_t  pci_func;
    uint32_t mmio_base;
    uint32_t vram_size;
    uint16_t device_id;
    uint32_t *fb_mapped;       /* Mapped framebuffer */
    uint32_t fb_phys;          /* Physical framebuffer address */
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t enabled;
} amd_gpu_t;

int amd_gpu_init(void);
int amd_gpu_set_mode(uint32_t width, uint32_t height);
uint32_t *amd_gpu_get_framebuffer(void);
uint32_t amd_gpu_get_vram_size(void);
void amd_gpu_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void amd_gpu_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint32_t *data);

#endif
