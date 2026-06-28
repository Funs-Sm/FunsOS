#include "i915.h"
#include "drm.h"
#include "pci.h"
#include "vmm.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"
#include "io.h"
#include "vesa.h"

/* Global i915 state */
static i915_private_t i915_priv;
static drm_driver_t i915_drm_driver;
static uint32_t i915_fb_width = 0;
static uint32_t i915_fb_height = 0;
static uint32_t i915_fb_pitch = 0;
static uint32_t i915_fb_bpp = 32;
static uint32_t *i915_fb_addr = (void *)0;
static uint32_t i915_initialized = 0;

/* Known Intel GPU devices */
static const struct {
    uint16_t device_id;
    const char name[48];
    uint32_t gen;
} i915_known_devices[] = {
    /* Gen1: i8xx */
    { 0x2582, "855GM",              I915_GEN_I915 },
    { 0x2592, "855GM",              I915_GEN_I915 },
    { 0x3582, "865G",               I915_GEN_I915 },
    
    /* Gen2: i915/i945 */
    { 0x2562, "915G",               I915_GEN_I915 },
    { 0x2572, "915GM",              I915_GEN_I915 },
    { 0x2772, "945G",               I915_GEN_I945 },
    { 0x27A2, "945GM",              I915_GEN_I945 },
    { 0x27AE, "945GME",             I915_GEN_I945 },
    { 0x2972, "i945G/GZ",           I915_GEN_I945 },
    { 0x2982, "Q965/Q963",          I915_GEN_I945 },
    { 0x2992, "Q965/Q963",          I915_GEN_I945 },
    { 0x29A2, "G965",               I915_GEN_I945 },
    { 0x29B2, "G965",               I915_GEN_I945 },
    
    /* Gen3: G33/G31/Pineview */
    { 0x29C2, "G33/G31",            I915_GEN_G33 },
    { 0x29D2, "Q33/Q35/G31",        I915_GEN_G33 },
    { 0x29E2, "Q35",                I915_GEN_G33 },
    { 0xA001, "Pineview G",         I915_GEN_G33 },
    { 0xA011, "Pineview G",         I915_GEN_G33 },
    
    /* Gen4: Ironlake/Arrandale */
    { 0x0042, "HD Graphics (Ironlake)", I915_GEN_IRONLAKE },
    { 0x0046, "HD Graphics (Ironlake)", I915_GEN_IRONLAKE },
    
    /* Gen5: Sandy Bridge */
    { 0x0102, "HD Graphics 2000/3000 (Sandy Bridge)", I915_GEN_SANDYBRIDGE },
    { 0x0112, "HD Graphics 2000/3000 (Sandy Bridge)", I915_GEN_SANDYBRIDGE },
    { 0x0122, "HD Graphics 3000 (Sandy Bridge-DT)",   I915_GEN_SANDYBRIDGE },
    { 0x0126, "HD Graphics 2000 (Sandy Bridge-DT)",   I915_GEN_SANDYBRIDGE },
    { 0x0162, "HD Graphics P3000 (Sandy Bridge Xeon)", I915_GEN_SANDYBRIDGE },
    { 0x010A, "HD Graphics (Sandy Bridge GT1)",       I915_GEN_SANDYBRIDGE },
    
    /* Gen6: Ivy Bridge */
    { 0x0152, "HD Graphics 2500/4000 (Ivy Bridge)", I915_GEN_IVYBRIDGE },
    { 0x0156, "HD Graphics 2500 (Ivy Bridge)",       I915_GEN_IVYBRIDGE },
    { 0x0162, "HD Graphics P4000 (Ivy Bridge Xeon)", I915_GEN_IVYBRIDGE },
    { 0x0166, "HD Graphics P4000 (Ivy Bridge Xeon)", I915_GEN_IVYBRIDGE },
    { 0x015A, "HD Graphics (Ivy Bridge GT1)",        I915_GEN_IVYBRIDGE },
    
    /* Gen7: Haswell */
    { 0x0402, "HD Graphics 4200/4400/4600 (Haswell)", I915_GEN_HASWELL },
    { 0x0412, "HD Graphics 4600 (Haswell-M)",         I915_GEN_HASWELL },
    { 0x0422, "HD Graphics 5000 (Haswell GT2)",       I915_GEN_HASWELL },
    { 0x0426, "HD Graphics P4600 (Haswell Xeon)",     I915_GEN_HASWELL },
    { 0x041A, "HD Graphics 4200 (Haswell)",           I915_GEN_HASWELL },
    { 0x041B, "HD Graphics 4400 (Haswell)",           I915_GEN_HASWELL },
    { 0x0406, "HD Graphics 5000 (Haswell GT2)",       I915_GEN_HASWELL },
    { 0x0A02, "HD Graphics (Haswell GT1)",            I915_GEN_HASWELL },
    { 0x0A06, "HD Graphics (Haswell GT1)",            I915_GEN_HASWELL },
    { 0x0A16, "HD Graphics (Haswell GT1)",            I915_GEN_HASWELL },
    { 0x0A1E, "HD Graphics (Haswell GT1)",            I915_GEN_HASWELL },
    { 0x0A26, "HD Graphics (Haswell GT2)",            I915_GEN_HASWELL },
    { 0x0A2E, "HD Graphics (Haswell GT2)",            I915_GEN_HASWELL },
    { 0x0C02, "HD Graphics (Haswell GT3e)",           I915_GEN_HASWELL },
    { 0x0C06, "HD Graphics (Haswell GT3e)",           I915_GEN_HASWELL },
    { 0x0C12, "HD Graphics (Haswell GT3e)",           I915_GEN_HASWELL },
    { 0x0C16, "HD Graphics (Haswell GT3e)",           I915_GEN_HASWELL },
    { 0x0C22, "HD Graphics (Haswell GT3e)",           I915_GEN_HASWELL },
    { 0x0C26, "HD Graphics (Haswell GT3e)",           I915_GEN_HASWELL },
    { 0x0D22, "HD Graphics (Haswell Iris Pro GT3e)",  I915_GEN_HASWELL },
    { 0x0D26, "HD Graphics (Haswell Iris Pro GT3e)",  I915_GEN_HASWELL },
    
    /* Gen8: Broadwell */
    { 0x1602, "HD Graphics 5300/5500/6000 (Broadwell)", I915_GEN_BROADWELL },
    { 0x1606, "HD Graphics 5500 (Broadwell U)",         I915_GEN_BROADWELL },
    { 0x160E, "HD Graphics (Broadwell)",                I915_GEN_BROADWELL },
    { 0x1612, "HD Graphics 5600 (Broadwell H)",         I915_GEN_BROADWELL },
    { 0x1616, "HD Graphics 5600 (Broadwell H)",         I915_GEN_BROADWELL },
    { 0x161E, "HD Graphics P5700 (Broadwell Xeon)",     I915_GEN_BROADWELL },
    { 0x1622, "HD Graphics 6000 (Broadwell)",           I915_GEN_BROADWELL },
    { 0x1626, "HD Graphics 6000 (Broadwell)",           I915_GEN_BROADWELL },
    { 0x162B, "HD Graphics Iris Pro P6300 (Broadwell)", I915_GEN_BROADWELL },
    { 0x162E, "Iris Pro 6200 (Broadwell)",              I915_GEN_BROADWELL },
    { 0x1632, "HD Graphics (Broadwell GT2)",            I915_GEN_BROADWELL },
    { 0x163B, "Iris Pro P6300 (Broadwell)",             I915_GEN_BROADWELL },
    { 0x163E, "HD Graphics (Broadwell)",                I915_GEN_BROADWELL },
    
    /* Gen9: Skylake */
    { 0x1902, "HD Graphics 510 (Skylake)",              I915_GEN_SKYLAKE },
    { 0x1906, "HD Graphics 510 (Skylake)",              I915_GEN_SKYLAKE },
    { 0x190B, "HD Graphics P530 (Skylake Xeon)",        I915_GEN_SKYLAKE },
    { 0x1912, "HD Graphics 520 (Skylake U)",            I915_GEN_SKYLAKE },
    { 0x1916, "HD Graphics 520 (Skylake U)",            I915_GEN_SKYLAKE },
    { 0x191B, "HD Graphics 530 (Skylake H)",            I915_GEN_SKYLAKE },
    { 0x191E, "HD Graphics 530 (Skylake H)",            I915_GEN_SKYLAKE },
    { 0x1921, "HD Graphics 530 (Skylake S)",            I915_GEN_SKYLAKE },
    { 0x1926, "HD Graphics P530 (Skylake Xeon)",        I915_GEN_SKYLAKE },
    { 0x1927, "HD Graphics (Skylake GT2)",              I915_GEN_SKYLAKE },
    { 0x192B, "HD Graphics (Skylake GT2)",              I915_GEN_SKYLAKE },
    { 0x192D, "HD Graphics (Skylake GT2)",              I915_GEN_SKYLAKE },
    { 0x1932, "HD Graphics Iris 540 (Skylake GT3e)",    I915_GEN_SKYLAKE },
    { 0x193B, "HD Graphics Iris 550 (Skylake GT3e)",    I915_GEN_SKYLAKE },
    { 0x193D, "Iris Pro P555 (Skylake GT4e)",           I915_GEN_SKYLAKE },
    
    /* Gen9: Kaby Lake */
    { 0x5902, "HD Graphics 610 (Kaby Lake)",            I915_GEN_KABYLAKE },
    { 0x5906, "HD Graphics 610 (Kaby Lake)",            I915_GEN_KABYLAKE },
    { 0x590B, "HD Graphics P630 (Kaby Lake Xeon)",      I915_GEN_KABYLAKE },
    { 0x5912, "HD Graphics 620 (Kaby Lake U)",          I915_GEN_KABYLAKE },
    { 0x5916, "HD Graphics 620 (Kaby Lake U)",          I915_GEN_KABYLAKE },
    { 0x591B, "HD Graphics 630 (Kaby Lake H)",          I915_GEN_KABYLAKE },
    { 0x591E, "HD Graphics 630 (Kaby Lake H)",          I915_GEN_KABYLAKE },
    { 0x5921, "HD Graphics 630 (Kaby Lake S)",          I915_GEN_KABYLAKE },
    { 0x5926, "HD Graphics P630 (Kaby Lake Xeon)",      I915_GEN_KABYLAKE },
    { 0x5927, "HD Graphics (Kaby Lake GT2)",            I915_GEN_KABYLAKE },
    { 0x592B, "HD Graphics (Kaby Lake GT2)",            I915_GEN_KABYLAKE },
    { 0x5932, "Iris Plus 640 (Kaby Lake GT3e)",         I915_GEN_KABYLAKE },
    { 0x593B, "Iris Plus 650 (Kaby Lake GT3e)",         I915_GEN_KABYLAKE },
    
    /* Gen9: Coffee Lake */
    { 0x3E90, "UHD Graphics 610 (Coffee Lake)",         I915_GEN_KABYLAKE },
    { 0x3E92, "UHD Graphics 610/630 (Coffee Lake)",     I915_GEN_KABYLAKE },
    { 0x3E93, "HD Graphics P630 (Coffee Lake Xeon)",    I915_GEN_KABYLAKE },
    { 0x3E94, "UHD Graphics P630 (Coffee Lake Xeon)",   I915_GEN_KABYLAKE },
    { 0x3E96, "UHD Graphics 630 (Coffee Lake)",         I915_GEN_KABYLAKE },
    { 0x3E98, "UHD Graphics P630 (Coffee Lake Xeon)",   I915_GEN_KABYLAKE },
    { 0x3E9A, "UHD Graphics (Coffee Lake)",             I915_GEN_KABYLAKE },
    { 0x3E9B, "UHD Graphics 630 (Coffee Lake H)",       I915_GEN_KABYLAKE },
    { 0x87C0, "UHD Graphics 610 (Whiskey Lake)",        I915_GEN_KABYLAKE },
    { 0x3EA0, "Iris Plus 645/655 (Coffee Lake GT3e)",   I915_GEN_KABYLAKE },
    { 0x3EA5, "Iris Plus 655 (Coffee Lake GT3e)",       I915_GEN_KABYLAKE },
    { 0x3EA6, "Iris Plus 645 (Coffee Lake GT3e)",       I915_GEN_KABYLAKE },
    { 0x3EA8, "Iris Plus 655 (Coffee Lake GT3e)",       I915_GEN_KABYLAKE },
    
    { 0,      "",                   I915_GEN_I915 }
};

/* Register read/write helpers */
static inline uint32_t i915_read(uint32_t reg) {
    if (!i915_priv.mmio_base_phys || !i915_priv.mmio_base_virt) return 0xFFFFFFFF;
    return *(volatile uint32_t *)((uint8_t *)i915_priv.mmio_base_virt + reg);
}

static inline void i915_write(uint32_t reg, uint32_t val) {
    if (!i915_priv.mmio_base_phys || !i915_priv.mmio_base_virt) return;
    *(volatile uint32_t *)((uint8_t *)i915_priv.mmio_base_virt + reg) = val;
}

static void i915_mmio_wait(uint32_t reg, uint32_t mask, uint32_t val, uint32_t timeout) {
    uint32_t i;
    for (i = 0; i < timeout; i++) {
        if ((i915_read(reg) & mask) == val) break;
        for (volatile int j = 0; j < 100; j++);
    }
}

/* Look up device info */
static const char *i915_get_device_name(uint16_t device_id, uint32_t *gen) {
    for (int i = 0; i915_known_devices[i].device_id != 0; i++) {
        if (i915_known_devices[i].device_id == device_id) {
            if (gen) *gen = i915_known_devices[i].gen;
            return i915_known_devices[i].name;
        }
    }
    if (gen) *gen = I915_GEN_I915;
    return "Unknown Intel GPU";
}

/* GTT initialization */
static int i915_gtt_init(void) {
    uint32_t gtt_entries;
    uint32_t gtt_size;
    
    if (i915_priv.gen >= I915_GEN_SKYLAKE) {
        gtt_entries = 256 * 1024;
    } else if (i915_priv.gen >= I915_GEN_IVYBRIDGE) {
        gtt_entries = 128 * 1024;
    } else if (i915_priv.gen >= I915_GEN_IRONLAKE) {
        gtt_entries = 64 * 1024;
    } else {
        gtt_entries = 32 * 1024;
    }
    
    gtt_size = gtt_entries * sizeof(uint32_t);
    gtt_size = (gtt_size + 4095) & ~4095;
    
    i915_priv.gtt_map = (uint32_t *)kmalloc(gtt_size);
    if (!i915_priv.gtt_map) return -1;
    
    memset(i915_priv.gtt_map, 0, gtt_size);
    i915_priv.gtt_size = gtt_size;
    i915_priv.gtt_phys = 0;
    
    for (uint32_t i = 0; i < gtt_entries; i++) {
        i915_priv.gtt_map[i] = 0;
    }
    
    i915_priv.has_gtt = 1;
    return 0;
}

/* BLT engine initialization */
static int i915_blt_init(void) {
    if (i915_priv.gen >= I915_GEN_SANDYBRIDGE) {
        i915_priv.has_blt_ring = 1;
        i915_priv.blt_ring.base = (uint32_t *)kmalloc(64 * 1024);
        if (i915_priv.blt_ring.base) {
            memset(i915_priv.blt_ring.base, 0, 64 * 1024);
            i915_priv.blt_ring.size = 64 * 1024;
            i915_priv.blt_ring.rptr = 0;
            i915_priv.blt_ring.wptr = 0;
            i915_priv.blt_ring.ready = 1;
        } else {
            i915_priv.has_blt_ring = 0;
        }
    } else {
        i915_priv.has_blt_ring = 0;
    }
    return 0;
}

/* Display pipe detection */
static int i915_detect_pipes(void) {
    uint32_t pipe_a_conf = 0;
    uint32_t pipe_b_conf = 0;
    uint32_t pipe_c_conf = 0;
    
    i915_priv.pipe_a_enabled = 0;
    i915_priv.pipe_b_enabled = 0;
    i915_priv.pipe_c_enabled = 0;
    
    if (i915_priv.mmio_base_virt) {
        if (i915_priv.gen >= I915_GEN_IVYBRIDGE) {
            pipe_a_conf = i915_read(0x70008);
            pipe_b_conf = i915_read(0x71008);
            pipe_c_conf = i915_read(0x72008);
        } else if (i915_priv.gen >= I915_GEN_I945) {
            pipe_a_conf = i915_read(0x70008);
            pipe_b_conf = i915_read(0x71008);
        } else {
            pipe_a_conf = i915_read(0x70008);
        }
        
        i915_priv.pipe_a_enabled = (pipe_a_conf & (1 << 31)) ? 1 : 0;
        i915_priv.pipe_b_enabled = (pipe_b_conf & (1 << 31)) ? 1 : 0;
        i915_priv.pipe_c_enabled = (pipe_c_conf & (1 << 31)) ? 1 : 0;
    }
    
    i915_priv.num_pipes = 2;
    if (i915_priv.gen >= I915_GEN_IVYBRIDGE) {
        i915_priv.num_pipes = 3;
    }
    
    if (!i915_priv.pipe_a_enabled && !i915_priv.pipe_b_enabled && !i915_priv.pipe_c_enabled) {
        i915_priv.pipe_a_enabled = 1;
    }
    
    return 0;
}

/* Software fill rectangle */
void i915_fill_rect(uint32_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!fb) fb = i915_fb_addr;
    if (!fb || i915_fb_width == 0) return;
    
    if (x + w > i915_fb_width) w = i915_fb_width - x;
    if (y + h > i915_fb_height) h = i915_fb_height - y;
    
    uint32_t *dst = fb + y * (i915_fb_pitch / 4) + x;
    uint32_t pitch_dwords = i915_fb_pitch / 4;
    
    for (uint32_t row = 0; row < h; row++) {
        uint32_t *line = dst + row * pitch_dwords;
        for (uint32_t col = 0; col < w; col++) {
            line[col] = color;
        }
    }
}

/* Software blit */
void i915_blit(uint32_t *dst, uint32_t *src, uint32_t w, uint32_t h, uint32_t dst_pitch, uint32_t src_pitch) {
    if (!dst || !src) return;
    
    if (src < dst) {
        int32_t row, col;
        for (row = h - 1; row >= 0; row--) {
            uint32_t *src_line = src + row * (src_pitch / 4);
            uint32_t *dst_line = dst + row * (dst_pitch / 4);
            for (col = w - 1; col >= 0; col--) {
                dst_line[col] = src_line[col];
            }
        }
    } else {
        for (uint32_t row = 0; row < h; row++) {
            uint32_t *src_line = src + row * (src_pitch / 4);
            uint32_t *dst_line = dst + row * (dst_pitch / 4);
            for (uint32_t col = 0; col < w; col++) {
                dst_line[col] = src_line[col];
            }
        }
    }
}

/* Bresenham line draw */
static void i915_draw_line(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, uint32_t color) {
    if (!i915_fb_addr) return;
    
    int32_t dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int32_t dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int32_t sx = (x0 < x1) ? 1 : -1;
    int32_t sy = (y0 < y1) ? 1 : -1;
    int32_t err = dx - dy;
    uint32_t pitch_dwords = i915_fb_pitch / 4;
    
    while (1) {
        if (x0 < i915_fb_width && y0 < i915_fb_height) {
            i915_fb_addr[y0 * pitch_dwords + x0] = color;
        }
        if (x0 == x1 && y0 == y1) break;
        int32_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx) { err += dx; y0 += sy; }
    }
}

/* Get GPU info string */
void i915_get_info(char *buf, int bufsize) {
    if (!buf || bufsize <= 0) return;
    
    char info[512];
    int len = sprintf(info,
        "Intel GPU: %s\n"
        "  Generation: Gen%d\n"
        "  Resolution: %dx%d @ %d bpp\n"
        "  VRAM/Stolen: %d MB\n"
        "  Engine Clock: %d MHz\n"
        "  Memory Clock: %d MHz\n"
        "  MMIO: 0x%08X\n"
        "  GTT: %s (%d entries)\n"
        "  Pipes: %d\n"
        "  BLT Engine: %s",
        i915_priv.gpu_name[0] ? i915_priv.gpu_name : "Intel HD Graphics",
        i915_priv.gen,
        i915_fb_width, i915_fb_height, i915_fb_bpp,
        i915_priv.vram_size / (1024 * 1024),
        i915_priv.engine_clock,
        i915_priv.memory_clock,
        i915_priv.mmio_base_phys,
        i915_priv.has_gtt ? "Enabled" : "Disabled",
        i915_priv.gtt_size / 4,
        i915_priv.num_pipes,
        i915_priv.has_blt_ring ? "Yes" : "No"
    );
    
    if (len < bufsize) {
        memcpy(buf, info, len);
        buf[len] = 0;
    } else {
        memcpy(buf, info, bufsize - 1);
        buf[bufsize - 1] = 0;
    }
}

/* DRM driver callbacks */
static int i915_drm_load(drm_driver_t *drv, uint8_t bus, uint8_t dev, uint8_t func) {
    return 0;
}

static void i915_drm_unload(drm_driver_t *drv) {
}

static int i915_drm_modeset_init(drm_driver_t *drv) {
    /* Setup default mode */
    drm_mode_t *mode = &drv->modes[0];
    memset(mode, 0, sizeof(drm_mode_t));
    mode->clock = 148500;
    mode->hdisplay = 1024;
    mode->hsync_start = 1048;
    mode->hsync_end = 1184;
    mode->htotal = 1344;
    mode->vdisplay = 768;
    mode->vsync_start = 771;
    mode->vsync_end = 777;
    mode->vtotal = 806;
    mode->flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC;
    strcpy(mode->name, "1024x768");
    drv->mode_count = 1;
    
    /* Setup one CRTC */
    drm_crtc_t *crtc = &drv->crtcs[0];
    memset(crtc, 0, sizeof(drm_crtc_t));
    crtc->id = 1;
    crtc->width = 1024;
    crtc->height = 768;
    crtc->mode = mode;
    crtc->enabled = 1;
    drv->crtc_count = 1;
    
    /* Setup one connector */
    drm_connector_t *conn = &drv->connectors[0];
    memset(conn, 0, sizeof(drm_connector_t));
    conn->id = 1;
    conn->type = 11; /* HDMI */
    conn->status = DRM_MODE_CONNECTED;
    conn->modes = mode;
    conn->mode_count = 1;
    drv->connector_count = 1;
    
    /* Setup one encoder */
    drm_encoder_t *enc = &drv->encoders[0];
    memset(enc, 0, sizeof(drm_encoder_t));
    enc->id = 1;
    enc->type = 2; /* TMDS */
    enc->crtc_id = 1;
    enc->possible_crtcs = 0x1;
    drv->encoder_count = 1;
    
    return 0;
}

static int i915_drm_fb_create(drm_framebuffer_t *fb) {
    fb->vaddr = (uint32_t *)kmalloc(fb->size);
    if (!fb->vaddr) return -1;
    memset(fb->vaddr, 0, fb->size);
    fb->paddr = 0;
    return 0;
}

static void i915_drm_fb_destroy(drm_framebuffer_t *fb) {
    if (fb->vaddr && fb->paddr == 0) {
        kfree(fb->vaddr);
    }
}

static int i915_drm_accel_fill(drm_framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    uint32_t *saved_fb = i915_fb_addr;
    uint32_t saved_w = i915_fb_width;
    uint32_t saved_h = i915_fb_height;
    uint32_t saved_pitch = i915_fb_pitch;
    
    i915_fb_addr = fb->vaddr;
    i915_fb_width = fb->width;
    i915_fb_height = fb->height;
    i915_fb_pitch = fb->pitch;
    
    i915_fill_rect(fb->vaddr, x, y, w, h, color);
    
    i915_fb_addr = saved_fb;
    i915_fb_width = saved_w;
    i915_fb_height = saved_h;
    i915_fb_pitch = saved_pitch;
    
    return 0;
}

static int i915_drm_accel_blit(drm_framebuffer_t *src, uint32_t sx, uint32_t sy, uint32_t sw, uint32_t sh,
                                drm_framebuffer_t *dst, uint32_t dx, uint32_t dy) {
    i915_blit(dst->vaddr + dy * (dst->pitch / 4) + dx,
              src->vaddr + sy * (src->pitch / 4) + sx,
              sw, sh, dst->pitch, src->pitch);
    return 0;
}

static int i915_drm_accel_copy(drm_framebuffer_t *fb, uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) {
    i915_blit(fb->vaddr + dy * (fb->pitch / 4) + dx,
              fb->vaddr + sy * (fb->pitch / 4) + sx,
              w, h, fb->pitch, fb->pitch);
    return 0;
}

/* Main init function */
int i915_init(void) {
    memset(&i915_priv, 0, sizeof(i915_private_t));
    i915_fb_addr = (void *)0;
    i915_initialized = 0;
    
    strcpy(i915_priv.gpu_name, "Intel HD Graphics (Software)");
    i915_priv.gen = I915_GEN_SANDYBRIDGE;
    i915_priv.vram_size = 16 * 1024 * 1024;
    i915_priv.engine_clock = 350;
    i915_priv.memory_clock = 400;
    
    /* Try to enumerate PCI for Intel GPU */
    for (uint8_t bus = 0; bus < 8; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t vendor_device = pci_read_config(bus, dev, func, 0);
                uint16_t vendor = vendor_device & 0xFFFF;
                uint16_t device = (vendor_device >> 16) & 0xFFFF;
                
                if (vendor == 0x8086) {
                    const char *name = i915_get_device_name(device, &i915_priv.gen);
                    if (i915_priv.gen > I915_GEN_PRE_HISTORIC) {
                        strncpy(i915_priv.gpu_name, name, sizeof(i915_priv.gpu_name) - 1);
                        i915_priv.pci_bus = bus;
                        i915_priv.pci_dev = dev;
                        i915_priv.pci_func = func;
                        i915_priv.pci_device_id = device;
                        i915_priv.pci_vendor_id = vendor;
                        
                        uint32_t cmd = pci_read_config(bus, dev, func, 4);
                        cmd |= 0x4 | 0x2;
                        pci_write_config(bus, dev, func, 4, cmd);
                        
                        i915_priv.mmio_base_phys = pci_read_config(bus, dev, func, 0x10) & ~0xF;
                        
                        if (i915_priv.mmio_base_phys) {
                            i915_priv.mmio_size = 2 * 1024 * 1024;
                            i915_priv.mmio_base_virt = vmm_map_physical(i915_priv.mmio_base_phys, i915_priv.mmio_size);
                        }
                        goto found_gpu;
                    }
                }
            }
        }
    }
    
found_gpu:
    /* Set default clocks based on generation */
    switch (i915_priv.gen) {
        case I915_GEN_HASWELL:
        case I915_GEN_BROADWELL:
            i915_priv.engine_clock = 1000;
            i915_priv.memory_clock = 800;
            i915_priv.vram_size = 32 * 1024 * 1024;
            break;
        case I915_GEN_SKYLAKE:
        case I915_GEN_KABYLAKE:
            i915_priv.engine_clock = 1150;
            i915_priv.memory_clock = 933;
            i915_priv.vram_size = 64 * 1024 * 1024;
            break;
        default:
            i915_priv.engine_clock = 600;
            i915_priv.memory_clock = 500;
            i915_priv.vram_size = 16 * 1024 * 1024;
            break;
    }
    
    /* Initialize display pipes */
    i915_detect_pipes();
    
    /* Initialize GTT */
    i915_gtt_init();
    
    /* Initialize BLT engine */
    i915_blt_init();
    
    /* Setup default framebuffer if not already set */
    if (!i915_fb_addr) {
        vbe_mode_info_t *vmi = vbe_get_current_mode();
        if (vmi && vmi->framebuffer) {
            i915_fb_width = vmi->width;
            i915_fb_height = vmi->height;
            i915_fb_pitch = vmi->pitch;
            i915_fb_bpp = vmi->bpp;
            i915_fb_addr = (uint32_t *)vmi->framebuffer;
        } else {
            i915_fb_width = 1024;
            i915_fb_height = 768;
            i915_fb_pitch = 1024 * 4;
            i915_fb_bpp = 32;
            i915_fb_addr = (uint32_t *)kmalloc(i915_fb_pitch * i915_fb_height);
            if (i915_fb_addr) {
                memset(i915_fb_addr, 0, i915_fb_pitch * i915_fb_height);
            }
        }
    }
    
    if (i915_fb_addr) {
        i915_fill_rect(i915_fb_addr, 0, 0, i915_fb_width, i915_fb_height, 0x00000000);
    }
    
    /* Initialize DRM driver structure */
    memset(&i915_drm_driver, 0, sizeof(drm_driver_t));
    strcpy(i915_drm_driver.name, "i915");
    strcpy(i915_drm_driver.desc, "Intel HD Graphics DRM driver");
    i915_drm_driver.pci_vendor = 0x8086;
    i915_drm_driver.pci_bus = i915_priv.pci_bus;
    i915_drm_driver.pci_dev = i915_priv.pci_dev;
    i915_drm_driver.pci_func = i915_priv.pci_func;
    i915_drm_driver.load = i915_drm_load;
    i915_drm_driver.unload = i915_drm_unload;
    i915_drm_driver.modeset_init = i915_drm_modeset_init;
    i915_drm_driver.fb_create = i915_drm_fb_create;
    i915_drm_driver.fb_destroy = i915_drm_fb_destroy;
    i915_drm_driver.accel_fill = i915_drm_accel_fill;
    i915_drm_driver.accel_blit = i915_drm_accel_blit;
    i915_drm_driver.accel_copy = i915_drm_accel_copy;
    i915_drm_driver.mmio_base = i915_priv.mmio_base_virt;
    i915_drm_driver.vram_map = i915_fb_addr;
    i915_drm_driver.vram_size = i915_priv.vram_size;
    i915_drm_driver.driver_private = &i915_priv;
    
    /* Register with DRM */
    drm_register_driver(&i915_drm_driver);
    
    i915_initialized = 1;
    return 0;
}

void i915_shutdown(void) {
    if (!i915_initialized) return;
    
    if (i915_priv.gtt_map) {
        kfree(i915_priv.gtt_map);
        i915_priv.gtt_map = (void *)0;
    }
    
    if (i915_priv.blt_ring.base) {
        kfree(i915_priv.blt_ring.base);
        i915_priv.blt_ring.base = (void *)0;
    }
    
    i915_initialized = 0;
}

uint32_t *i915_get_fb(void) {
    return i915_fb_addr;
}
