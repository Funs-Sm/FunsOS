#ifndef AMDGPU_H
#define AMDGPU_H
#include "stdint.h"

/* AMD GPU 寄存器定义 */
#define AMDGPU_MMIO_BASE    0x0000
#define AMDGPU_FB_BASE      0x10000000

/* 图形引擎寄存器 */
#define AMDGPU_GRPH_ENABLE         0x6800
#define AMDGPU_GRPH_CONTROL        0x6801
#define AMDGPU_GRPH_SWAP_CNTL      0x6802
#define AMDGPU_GRPH_PRIMARY_SURFACE_ADDRESS  0x6804
#define AMDGPU_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH  0x6805
#define AMDGPU_GRPH_PITCH          0x6808
#define AMDGPU_GRPH_X_START        0x680C
#define AMDGPU_GRPH_Y_START        0x680D
#define AMDGPU_GRPH_X_END          0x680E
#define AMDGPU_GRPH_Y_END          0x680F
#define AMDGPU_GRPH_UPDATE         0x6810

/* 显示控制器 */
#define AMDGPU_DC_CRTC_CONTROL     0x6E00
#define AMDGPU_DC_CRTC_STATUS      0x6E01
#define AMDGPU_DC_TIMING_H_TOTAL   0x6E02
#define AMDGPU_DC_TIMING_V_TOTAL   0x6E03
#define AMDGPU_DC_TIMING_H_SYNC    0x6E04
#define AMDGPU_DC_TIMING_V_SYNC    0x6E05

/* 电源管理 */
#define AMDGPU_PM_CNTL             0x6F00
#define AMDGPU_PM_STATUS           0x6F01

/* AMD GPU 设备结构 */
typedef struct amdgpu_device {
    uint32_t mmio_base;
    uint32_t fb_base;
    uint32_t fb_size;
    uint32_t *framebuffer;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t engine_clock;
    uint32_t memory_clock;
    uint32_t initialized;
    uint32_t pci_bus;
    uint32_t pci_slot;
    uint32_t pci_func;
} amdgpu_device_t;

/* AMD GPU 驱动 API */
int amdgpu_init(void);
int amdgpu_probe(void);
void amdgpu_shutdown(void);
int amdgpu_get_framebuffer(amdgpu_device_t **dev);
void amdgpu_fill_rect(uint32_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void amdgpu_blit(uint32_t *dst, uint32_t *src, uint32_t w, uint32_t h, uint32_t dst_pitch, uint32_t src_pitch);
void amdgpu_set_mode(uint32_t width, uint32_t height, uint32_t bpp);
void amdgpu_power_save(void);
void amdgpu_power_full(void);
void amdgpu_get_info(char *buf, int bufsize);

#endif