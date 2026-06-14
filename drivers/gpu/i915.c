#include "i915.h"
#include "drm.h"
#include "pci.h"
#include "vmm.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"
#include "io.h"

#define INTEL_VENDOR_ID 0x8086

/* Known Intel GPU device IDs */
static const struct {
    uint16_t device_id;
    const char name[32];
    uint32_t is_gen4;
    uint32_t is_gen5;
} i915_known_devices[] = {
    { 0x2582, "855GM",       0, 0 },
    { 0x2592, "855GM",       0, 0 },
    { 0x2772, "82945G",      1, 0 },
    { 0x27A2, "945GM",       1, 0 },
    { 0x27AE, "945GME",      1, 0 },
    { 0x2972, "946GZ",       1, 0 },
    { 0x2982, "G35",         1, 0 },
    { 0x2992, "965Q",        1, 0 },
    { 0x29A2, "965G",        1, 0 },
    { 0x2A02, "965GM",       1, 0 },
    { 0x2A12, "965GME",      1, 0 },
    { 0x2A42, "GM45",        1, 1 },
    { 0x2E02, "4 Series",    1, 1 },
    { 0x2E12, "Q45",         1, 1 },
    { 0x2E22, "G45",         1, 1 },
    { 0x2E32, "G41",         1, 1 },
    { 0x2E42, "B43",         1, 1 },
    { 0x2E92, "B43",         1, 1 },
    { 0x0042, "Ironlake",    1, 1 },
    { 0x0046, "Ironlake M",  1, 1 },
    { 0x0102, "SandyBridge", 1, 1 },
    { 0x0112, "SandyBridge", 1, 1 },
    { 0x0122, "SandyBridge", 1, 1 },
    { 0x0152, "IvyBridge",   1, 1 },
    { 0x0162, "IvyBridge",   1, 1 },
    { 0x0F31, "Bay Trail",   1, 1 },
    { 0,      "",            0, 0 }
};

static drm_driver_t i915_drm_driver;
static i915_private_t i915_priv;

/* ---- MMIO access helpers ---- */

static inline uint32_t i915_mmio_read(volatile void *mmio, uint32_t offset) {
    return *((volatile uint32_t *)((uint8_t *)mmio + offset));
}

static inline void i915_mmio_write(volatile void *mmio, uint32_t offset, uint32_t val) {
    *((volatile uint32_t *)((uint8_t *)mmio + offset)) = val;
}

/* ---- i915 driver callbacks ---- */

static int i915_load(drm_driver_t *drv, uint8_t bus, uint8_t dev, uint8_t func) {
    i915_private_t *priv = (i915_private_t *)drv->driver_private;

    /* Enable PCI bus master, memory space, I/O space */
    uint32_t cmd = pci_read_config(bus, dev, func, 0x04);
    pci_write_config(bus, dev, func, 0x04, cmd | 0x07);

    /* Read device ID */
    uint32_t vd = pci_read_config(bus, dev, func, 0x00);
    priv->device_id = (vd >> 16) & 0xFFFF;

    /* Look up device info */
    int i;
    for (i = 0; i915_known_devices[i].device_id != 0; i++) {
        if (i915_known_devices[i].device_id == priv->device_id) {
            priv->is_gen4 = i915_known_devices[i].is_gen4;
            priv->is_gen5 = i915_known_devices[i].is_gen5;
            break;
        }
    }

    /* Map MMIO BAR (BAR0 for Intel) */
    uint32_t bar0 = pci_read_config(bus, dev, func, 0x10);
    uint32_t mmio_phys = bar0 & 0xFFFFFFF0;
    if (mmio_phys == 0) {
        /* Try BAR1 */
        uint32_t bar1 = pci_read_config(bus, dev, func, 0x14);
        mmio_phys = bar1 & 0xFFFFFFF0;
    }
    if (mmio_phys == 0) return -1;

    priv->mmio_base_phys = mmio_phys;
    drv->mmio_base = vmm_map_physical(mmio_phys, 0x80000);
    if (!drv->mmio_base) return -1;

    /* Detect GTT and stolen memory */
    uint32_t pgtbl_ctl = i915_mmio_read(drv->mmio_base, I915_PGTBL_CTL);
    priv->gtt_phys = pgtbl_ctl & 0xFFFFF000;

    /* Stolen memory size from GMCH control register (PCI config 0x52 on host bridge) */
    /* For simplicity, use a default stolen size */
    priv->stolen_size = 8 * 1024 * 1024; /* 8MB default stolen memory */
    priv->stolen_base = 0; /* Will be determined from GTT entries */

    /* Try to read stolen base from the first GTT entry */
    if (priv->gtt_phys) {
        uint32_t *gtt_map = (uint32_t *)vmm_map_physical(priv->gtt_phys, 0x4000);
        if (gtt_map) {
            uint32_t first_entry = gtt_map[0];
            if (first_entry & 0x01) {
                priv->stolen_base = first_entry & 0xFFFFF000;
            }
            vmm_unmap_physical(gtt_map, 0x4000);
        }
    }

    /* Determine VRAM size from GTT */
    drv->vram_phys = priv->stolen_base;
    drv->vram_size = priv->stolen_size;

    /* Map stolen memory as VRAM */
    if (priv->stolen_base) {
        uint32_t map_size = (priv->stolen_size + 0xFFF) & ~0xFFF;
        drv->vram_map = (uint32_t *)vmm_map_physical(priv->stolen_base, map_size);
    }

    /* Detect BLT ring */
    priv->has_blt_ring = priv->is_gen5 ? 1 : 0;

    return 0;
}

static void i915_unload(drm_driver_t *drv) {
    if (drv->mmio_base) {
        vmm_unmap_physical(drv->mmio_base, 0x80000);
        drv->mmio_base = (void *)0;
    }
    if (drv->vram_map) {
        vmm_unmap_physical(drv->vram_map, (drv->vram_size + 0xFFF) & ~0xFFF);
        drv->vram_map = (void *)0;
    }
}

static int i915_modeset_init(drm_driver_t *drv) {
    volatile void *mmio = drv->mmio_base;

    /* Read current pipe A configuration */
    uint32_t pipe_conf = i915_mmio_read(mmio, I915_PIPEACONF);
    int pipe_enabled = (pipe_conf & I915_PIPE_ENABLE) ? 1 : 0;

    /* Read current display mode from hardware registers */
    uint32_t htotal = i915_mmio_read(mmio, I915_HTOTAL_A);
    uint32_t hsync  = i915_mmio_read(mmio, I915_HSYNC_A);
    uint32_t vtotal = i915_mmio_read(mmio, I915_VTOTAL_A);
    uint32_t vsync  = i915_mmio_read(mmio, I915_VSYNC_A);

    /* Extract timing values from register format:
     * Register format: [active-1:16] [total-1:0] */
    uint16_t hdisplay_val = (htotal >> 16) + 1;
    uint16_t htotal_val   = (htotal & 0xFFFF) + 1;
    uint16_t hsync_start  = (hsync >> 16) + 1;
    uint16_t hsync_end    = (hsync & 0xFFFF) + 1;
    uint16_t vdisplay_val = (vtotal >> 16) + 1;
    uint16_t vtotal_val   = (vtotal & 0xFFFF) + 1;
    uint16_t vsync_start  = (vsync >> 16) + 1;
    uint16_t vsync_end    = (vsync & 0xFFFF) + 1;

    /* Set up a mode from the current hardware state */
    drm_mode_t *mode = &drv->modes[0];
    memset(mode, 0, sizeof(drm_mode_t));
    mode->hdisplay    = hdisplay_val;
    mode->hsync_start = hsync_start;
    mode->hsync_end   = hsync_end;
    mode->htotal      = htotal_val;
    mode->vdisplay    = vdisplay_val;
    mode->vsync_start = vsync_start;
    mode->vsync_end   = vsync_end;
    mode->vtotal      = vtotal_val;
    mode->flags       = DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC;
    mode->type        = 1; /* preferred */
    mode->clock       = 0; /* unknown */
    /* Generate mode name */
    sprintf(mode->name, "%dx%d", hdisplay_val, vdisplay_val);

    drv->mode_count = 1;

    /* Set up CRTC */
    drm_crtc_t *crtc = &drv->crtcs[0];
    memset(crtc, 0, sizeof(drm_crtc_t));
    crtc->id      = 1;
    crtc->x       = 0;
    crtc->y       = 0;
    crtc->width   = hdisplay_val;
    crtc->height  = vdisplay_val;
    crtc->mode    = mode;
    crtc->enabled = pipe_enabled;
    drv->crtc_count = 1;

    /* Set up connector */
    drm_connector_t *conn = &drv->connectors[0];
    memset(conn, 0, sizeof(drm_connector_t));
    conn->id          = 1;
    conn->type        = DRM_OBJECT_CONNECTOR;
    conn->status      = pipe_enabled ? DRM_MODE_CONNECTED : DRM_MODE_DISCONNECTED;
    conn->modes       = drv->modes;
    conn->mode_count  = 1;
    conn->encoder_id  = 1;
    drv->connector_count = 1;

    /* Set up encoder */
    drm_encoder_t *enc = &drv->encoders[0];
    memset(enc, 0, sizeof(drm_encoder_t));
    enc->id             = 1;
    enc->type           = DRM_OBJECT_ENCODER;
    enc->crtc_id        = 1;
    enc->possible_crtcs = 0x1;
    drv->encoder_count  = 1;

    /* Set up primary plane */
    drm_plane_t *plane = &drv->planes[0];
    memset(plane, 0, sizeof(drm_plane_t));
    plane->id             = 1;
    plane->possible_crtcs = 0x1;
    plane->crtc_id        = 1;
    drv->plane_count      = 1;

    /* Create default framebuffer using stolen memory */
    if (drv->vram_map && pipe_enabled) {
        drm_framebuffer_t *fb = &drv->framebuffers[0];
        memset(fb, 0, sizeof(drm_framebuffer_t));
        fb->id     = 1;
        fb->width  = hdisplay_val;
        fb->height = vdisplay_val;
        fb->bpp    = 32;
        fb->depth  = 24;
        fb->pitch  = hdisplay_val * 4;
        fb->size   = fb->pitch * fb->height;
        fb->vaddr  = drv->vram_map;
        fb->paddr  = drv->vram_phys;
        drv->fb_count = 1;

        crtc->fb_id = fb->id;
    }

    return 0;
}

static int i915_crtc_set_mode(drm_crtc_t *crtc, drm_mode_t *mode) {
    if (!crtc || !mode) return -1;

    /* Find the driver */
    drm_driver_t *drv = (void *)0;
    for (uint32_t i = 0; i < drm_get_driver_count(); i++) {
        drm_driver_t *d = drm_get_driver(i);
        if (!d) continue;
        for (uint32_t j = 0; j < d->crtc_count; j++) {
            if (&d->crtcs[j] == crtc) {
                drv = d;
                break;
            }
        }
        if (drv) break;
    }
    if (!drv || !drv->mmio_base) return -1;

    volatile void *mmio = drv->mmio_base;

    /* Disable pipe A before changing mode */
    uint32_t pipe_conf = i915_mmio_read(mmio, I915_PIPEACONF);
    i915_mmio_write(mmio, I915_PIPEACONF, pipe_conf & ~I915_PIPE_ENABLE);

    /* Wait for pipe to disable */
    int timeout;
    for (timeout = 0; timeout < 1000; timeout++) {
        pipe_conf = i915_mmio_read(mmio, I915_PIPEACONF);
        if (!(pipe_conf & I915_PIPE_ENABLE)) break;
    }

    /* Disable display plane A */
    uint32_t dspacntr = i915_mmio_read(mmio, I915_DSPACNTR);
    i915_mmio_write(mmio, I915_DSPACNTR, dspacntr & ~I915_DSP_ENABLE);

    /* Program pipe timing registers */
    uint32_t htotal_val = ((uint32_t)(mode->hdisplay - 1) << 16) | (mode->htotal - 1);
    uint32_t hblank_val = ((uint32_t)(mode->hdisplay - 1) << 16) | (mode->htotal - 1);
    uint32_t hsync_val  = ((uint32_t)(mode->hsync_start - 1) << 16) | (mode->hsync_end - 1);
    uint32_t vtotal_val = ((uint32_t)(mode->vdisplay - 1) << 16) | (mode->vtotal - 1);
    uint32_t vblank_val = ((uint32_t)(mode->vdisplay - 1) << 16) | (mode->vtotal - 1);
    uint32_t vsync_val  = ((uint32_t)(mode->vsync_start - 1) << 16) | (mode->vsync_end - 1);

    i915_mmio_write(mmio, I915_HTOTAL_A, htotal_val);
    i915_mmio_write(mmio, I915_HBLANK_A, hblank_val);
    i915_mmio_write(mmio, I915_HSYNC_A,  hsync_val);
    i915_mmio_write(mmio, I915_VTOTAL_A, vtotal_val);
    i915_mmio_write(mmio, I915_VBLANK_A, vblank_val);
    i915_mmio_write(mmio, I915_VSYNC_A,  vsync_val);

    /* Set pipe source size */
    uint32_t pipesrc = ((mode->vdisplay - 1) << 16) | (mode->hdisplay - 1);
    i915_mmio_write(mmio, I915_PIPEASRC, pipesrc);

    /* Set display stride */
    uint32_t stride = mode->hdisplay * 4;
    i915_mmio_write(mmio, I915_DSPASTRIDE, stride);

    /* Set display surface address (use stolen memory base) */
    if (drv->vram_phys) {
        i915_mmio_write(mmio, I915_DSPALINOFF, 0);
        i915_mmio_write(mmio, I915_DSPASURF, (uint32_t)drv->vram_phys);
    }

    /* Enable display plane A */
    dspacntr = I915_DSP_ENABLE | I915_DSP_FORMAT_RGB;
    i915_mmio_write(mmio, I915_DSPACNTR, dspacntr);

    /* Enable pipe A */
    i915_mmio_write(mmio, I915_PIPEACONF, I915_PIPE_ENABLE);

    /* Wait for pipe to enable */
    for (timeout = 0; timeout < 1000; timeout++) {
        pipe_conf = i915_mmio_read(mmio, I915_PIPEACONF);
        if (pipe_conf & I915_PIPE_ENABLE) break;
    }

    /* Update CRTC state */
    crtc->mode    = mode;
    crtc->width   = mode->hdisplay;
    crtc->height  = mode->vdisplay;
    crtc->enabled = 1;

    return 0;
}

static int i915_crtc_page_flip(drm_crtc_t *crtc, drm_framebuffer_t *fb) {
    if (!crtc || !fb) return -1;

    /* Find the driver */
    drm_driver_t *drv = (void *)0;
    for (uint32_t i = 0; i < drm_get_driver_count(); i++) {
        drm_driver_t *d = drm_get_driver(i);
        if (!d) continue;
        for (uint32_t j = 0; j < d->crtc_count; j++) {
            if (&d->crtcs[j] == crtc) {
                drv = d;
                break;
            }
        }
        if (drv) break;
    }
    if (!drv || !drv->mmio_base) return -1;

    volatile void *mmio = drv->mmio_base;

    /* Update display stride */
    i915_mmio_write(mmio, I915_DSPASTRIDE, fb->pitch);

    /* Update surface address */
    if (fb->paddr) {
        i915_mmio_write(mmio, I915_DSPALINOFF, 0);
        i915_mmio_write(mmio, I915_DSPASURF, (uint32_t)fb->paddr);
    }

    /* Update CRTC state */
    crtc->fb_id  = fb->id;
    crtc->width  = fb->width;
    crtc->height = fb->height;

    return 0;
}

static int i915_fb_create(drm_framebuffer_t *fb) {
    /* Find the i915 driver */
    drm_driver_t *drv = (void *)0;
    for (uint32_t i = 0; i < drm_get_driver_count(); i++) {
        drm_driver_t *d = drm_get_driver(i);
        if (d && d->vram_map) {
            drv = d;
            break;
        }
    }

    if (!drv || !drv->vram_map) return -1;

    /* Try to allocate from stolen/VRAM memory */
    /* Simple bump allocator from VRAM */
    uint32_t offset = 0;
    for (uint32_t i = 0; i < drv->fb_count; i++) {
        uint32_t end = (uint32_t)(drv->framebuffers[i].paddr - drv->vram_phys) +
                       drv->framebuffers[i].size;
        if (end > offset) offset = end;
    }

    if (offset + fb->size > drv->vram_size) return -1; /* Out of VRAM */

    fb->paddr = drv->vram_phys + offset;
    fb->vaddr = (uint32_t *)((uint8_t *)drv->vram_map + offset);

    return 0;
}

static void i915_fb_destroy(drm_framebuffer_t *fb) {
    /* Stolen memory is not individually freed */
    fb->vaddr = (void *)0;
    fb->paddr = 0;
}

static int i915_accel_fill(drm_framebuffer_t *fb, uint32_t x, uint32_t y,
                           uint32_t w, uint32_t h, uint32_t color) {
    /* Find the i915 driver */
    drm_driver_t *drv = (void *)0;
    for (uint32_t i = 0; i < drm_get_driver_count(); i++) {
        drm_driver_t *d = drm_get_driver(i);
        if (d && d->mmio_base) {
            drv = d;
            break;
        }
    }

    if (!drv || !drv->mmio_base || !fb->vaddr) return -1;

    i915_private_t *priv = (i915_private_t *)drv->driver_private;

    /* Try BLT engine for Gen5+ */
    if (priv->has_blt_ring && fb->paddr) {
        volatile void *mmio = drv->mmio_base;

        /* Use simple color fill via BLT command:
         * We write a BLT command sequence to the BLT ring buffer.
         * For simplicity, we use a synchronous approach. */

        /* BLT_COLOR_BLT command format:
         * DW0: 0x40000002 | (depth << 24)
         * DW1: pitch/4 | (rop << 16)   -- rop 0xF0 = copy, 0xCC = no-op
         * DW2: (height << 16) | width
         * DW3: destination offset
         * DW4: color
         * DW5: (0) */

        uint32_t pitch_dwords = fb->pitch / 4;
        uint32_t dst_offset = y * pitch_dwords + x;

        /* Write BLT command - COLOR_BLT */
        i915_mmio_write(mmio, 0x22000, (3 << 24) | 0x40000002); /* 32-bit color blt */
        i915_mmio_write(mmio, 0x22004, (0xF0 << 16) | pitch_dwords);
        i915_mmio_write(mmio, 0x22008, (h << 16) | w);
        i915_mmio_write(mmio, 0x2200C, (uint32_t)fb->paddr + dst_offset * 4);
        i915_mmio_write(mmio, 0x22010, color);
        i915_mmio_write(mmio, 0x22014, 0);

        /* Flush */
        i915_mmio_write(mmio, 0x22000, 0);

        return 0;
    }

    /* Fallback to software fill */
    return -1; /* Let DRM core do software fallback */
}

static int i915_accel_blit(drm_framebuffer_t *src, uint32_t sx, uint32_t sy,
                           uint32_t sw, uint32_t sh,
                           drm_framebuffer_t *dst, uint32_t dx, uint32_t dy) {
    /* Find the i915 driver */
    drm_driver_t *drv = (void *)0;
    for (uint32_t i = 0; i < drm_get_driver_count(); i++) {
        drm_driver_t *d = drm_get_driver(i);
        if (d && d->mmio_base) {
            drv = d;
            break;
        }
    }

    if (!drv || !drv->mmio_base) return -1;

    i915_private_t *priv = (i915_private_t *)drv->driver_private;

    /* Try BLT engine for Gen5+ */
    if (priv->has_blt_ring && src->paddr && dst->paddr) {
        volatile void *mmio = drv->mmio_base;

        uint32_t src_pitch_dwords = src->pitch / 4;
        uint32_t dst_pitch_dwords = dst->pitch / 4;
        uint32_t src_offset = sy * src_pitch_dwords + sx;
        uint32_t dst_offset = dy * dst_pitch_dwords + dx;

        /* SRC_COPY_BLT command */
        i915_mmio_write(mmio, 0x22000, (3 << 24) | 0x00000002); /* 32-bit src copy blt */
        i915_mmio_write(mmio, 0x22004, (0xCC << 16) | dst_pitch_dwords);
        i915_mmio_write(mmio, 0x22008, (sh << 16) | sw);
        i915_mmio_write(mmio, 0x2200C, (uint32_t)dst->paddr + dst_offset * 4);
        i915_mmio_write(mmio, 0x22010, src_pitch_dwords);
        i915_mmio_write(mmio, 0x22014, (uint32_t)src->paddr + src_offset * 4);

        /* Flush */
        i915_mmio_write(mmio, 0x22000, 0);

        return 0;
    }

    /* Fallback to software blit */
    return -1;
}

static int i915_accel_copy(drm_framebuffer_t *fb, uint32_t sx, uint32_t sy,
                           uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) {
    /* For intra-framebuffer copy, use blit with same src and dst */
    return i915_accel_blit(fb, sx, sy, w, h, fb, dx, dy);
}

/* ---- i915 initialization ---- */

int i915_init(void) {
    memset(&i915_priv, 0, sizeof(i915_private_t));
    memset(&i915_drm_driver, 0, sizeof(drm_driver_t));

    /* Scan PCI for Intel GPU */
    int found = 0;
    uint8_t found_bus = 0, found_dev = 0, found_func = 0;
    uint16_t b;
    uint8_t d, f;
    for (b = 0; b < PCI_MAX_BUSES && !found; b++) {
        for (d = 0; d < PCI_MAX_DEVICES && !found; d++) {
            for (f = 0; f < PCI_MAX_FUNCTIONS && !found; f++) {
                uint32_t vd = pci_read_config((uint8_t)b, d, f, 0x00);
                if (vd == 0xFFFFFFFF || vd == 0) continue;
                uint16_t vendor = vd & 0xFFFF;
                if (vendor == INTEL_VENDOR_ID) {
                    uint8_t base_class = (pci_read_config((uint8_t)b, d, f, 0x08) >> 24) & 0xFF;
                    if (base_class == 0x03) { /* Display controller */
                        found_bus = (uint8_t)b;
                        found_dev = d;
                        found_func = f;
                        found = 1;
                    }
                }
            }
        }
    }

    if (!found) return -1;

    /* Set up the DRM driver structure */
    strcpy(i915_drm_driver.name, "i915");
    strcpy(i915_drm_driver.desc, "Intel Integrated Graphics");
    i915_drm_driver.pci_vendor = INTEL_VENDOR_ID;

    i915_drm_driver.pci_bus  = found_bus;
    i915_drm_driver.pci_dev  = found_dev;
    i915_drm_driver.pci_func = found_func;

    i915_drm_driver.driver_private = &i915_priv;

    /* Set up callbacks */
    i915_drm_driver.load           = i915_load;
    i915_drm_driver.unload         = i915_unload;
    i915_drm_driver.modeset_init   = i915_modeset_init;
    i915_drm_driver.crtc_set_mode  = i915_crtc_set_mode;
    i915_drm_driver.crtc_page_flip = i915_crtc_page_flip;
    i915_drm_driver.fb_create      = i915_fb_create;
    i915_drm_driver.fb_destroy     = i915_fb_destroy;
    i915_drm_driver.accel_fill     = i915_accel_fill;
    i915_drm_driver.accel_blit     = i915_accel_blit;
    i915_drm_driver.accel_copy     = i915_accel_copy;

    /* Register with DRM core */
    int ret = drm_register_driver(&i915_drm_driver);
    if (ret != 0) return ret;

    return 0;
}
