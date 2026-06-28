#ifndef I915_H
#define I915_H

#include "stdint.h"
#include "drm.h"

#define I915_GTT_BASE       0x2000
#define I915_GTT_SIZE       0x2000
#define I915_PGTBL_CTL      0x2020
#define I915_PGTBL_CTL2     0x20A0
#define I915_DSPACNTR       0x70180
#define I915_DSPBCNTR       0x71180
#define I915_DSPALINOFF     0x70184
#define I915_DSPASTRIDE     0x70188
#define I915_DSPASIZE       0x70190
#define I915_DSPASURF       0x7019C
#define I915_PIPEACONF      0x70008
#define I915_PIPEASRC       0x6001C
#define I915_HTOTAL_A       0x60000
#define I915_HBLANK_A       0x60004
#define I915_HSYNC_A        0x60008
#define I915_VTOTAL_A       0x6000C
#define I915_VBLANK_A       0x60010
#define I915_VSYNC_A        0x60014
#define I915_ADPA           0x61100
#define I915_SWSCI          0x51000
#define I915_BLC_PWM_CTL    0x61254
#define I915_PIPEBCONF      0x71008
#define I915_PIPECCONF      0x72008

#define I915_DSP_ENABLE     (1 << 31)
#define I915_DSP_GAMMA      (1 << 30)
#define I915_DSP_FORMAT_RGB (0 << 26)
#define I915_PIPE_ENABLE    (1 << 31)
#define I915_PIPE_FORCE     (1 << 25)

#define I915_BCS_CTL        0x22000
#define I915_BCS_BUF_INFO   0x22004
#define I915_BCS_BASE       0x22000
#define I915_BLT_CMD                0x02
#define I915_BLT_WRITE_RGB          (1 << 20)
#define I915_BLT_SRC_TILED          (1 << 18)
#define I915_BLT_DST_TILED          (1 << 17)
#define I915_BLT_DEPTH_32           (3 << 24)

#define I915_GTT_PAGE_SIZE          4096
#define I915_GTT_MAX_ENTRIES        262144
#define I915_GTT_ENTRY_VALID        (1 << 0)
#define I915_GTT_ENTRY_CACHED       (0 << 1)
#define I915_GTT_ENTRY_UNCACHED     (1 << 1)
#define I915_GTT_ENTRY_WC           (2 << 1)

#define I915_GEN_PRE_HISTORIC       0
#define I915_GEN_I915               1
#define I915_GEN_I945               2
#define I915_GEN_G33                3
#define I915_GEN_G4X                4
#define I915_GEN_IRONLAKE           5
#define I915_GEN_SANDYBRIDGE        6
#define I915_GEN_IVYBRIDGE          7
#define I915_GEN_HASWELL            8
#define I915_GEN_BROADWELL          9
#define I915_GEN_SKYLAKE            10
#define I915_GEN_KABYLAKE           11

typedef struct {
    uint32_t *base;
    uint32_t size;
    uint32_t rptr;
    uint32_t wptr;
    uint32_t ready;
} i915_ring_buffer_t;

typedef struct {
    uint32_t mmio_base_phys;
    void *mmio_base_virt;
    uint32_t mmio_size;
    uint32_t gtt_phys;
    uint32_t *gtt_map;
    uint32_t gtt_size;
    uint32_t stolen_base;
    uint32_t stolen_size;
    uint32_t gen;
    uint32_t is_gen4;
    uint32_t is_gen5;
    uint32_t is_gen6;
    uint32_t is_gen7;
    uint32_t is_gen8;
    uint32_t is_gen9;
    uint32_t has_gtt;
    uint32_t has_blt_ring;
    uint32_t has_bsd_ring;
    uint32_t has_blitter;
    i915_ring_buffer_t blt_ring;
    uint32_t engine_clock;
    uint32_t memory_clock;
    uint32_t num_pipes;
    uint32_t pipe_a_enabled;
    uint32_t pipe_b_enabled;
    uint32_t pipe_c_enabled;
    uint32_t vram_size;
    uint8_t pci_bus;
    uint8_t pci_dev;
    uint8_t pci_func;
    uint16_t pci_device_id;
    uint16_t pci_vendor_id;
    char gpu_name[48];
} i915_private_t;

int i915_init(void);
void i915_get_info(char *buf, int bufsize);
void i915_fill_rect(uint32_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void i915_blit(uint32_t *dst, uint32_t *src, uint32_t w, uint32_t h, uint32_t dst_pitch, uint32_t src_pitch);
void i915_shutdown(void);

#endif
