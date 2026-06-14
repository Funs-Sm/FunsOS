#ifndef I915_H
#define I915_H

#include "stdint.h"
#include "drm.h"

/* i915 register offsets */
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

/* Display control */
#define I915_DSP_ENABLE     (1 << 31)
#define I915_DSP_GAMMA      (1 << 30)
#define I915_DSP_FORMAT_RGB (0 << 26)

/* Pipe config */
#define I915_PIPE_ENABLE    (1 << 31)
#define I915_PIPE_FORCE     (1 << 25)

/* BLT engine registers */
#define I915_BCS_CTL        0x22000
#define I915_BCS_BUF_INFO   0x22004
#define I915_BCS_BASE       0x22000

/* BLT command definitions */
#define I915_BLT_CMD                0x02
#define I915_BLT_WRITE_RGB          (1 << 20)
#define I915_BLT_SRC_TILED          (1 << 18)
#define I915_BLT_DST_TILED          (1 << 17)
#define I915_BLT_DEPTH_32           (3 << 24)

/* i915 driver private data */
typedef struct {
    uint32_t mmio_base_phys;
    uint32_t gtt_phys;
    uint32_t stolen_base;
    uint32_t stolen_size;
    uint16_t device_id;
    uint32_t is_gen4;       /* Generation 4+ features */
    uint32_t is_gen5;       /* Generation 5+ features */
    uint32_t has_blt_ring;  /* Has BLT ring buffer */
} i915_private_t;

int i915_init(void);

#endif
