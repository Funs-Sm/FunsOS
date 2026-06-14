#include "drm.h"
#include "kheap.h"
#include "vmm.h"
#include "string.h"
#include "stdio.h"

#define DRM_MAX_DRIVERS 8

static drm_driver_t *drm_drivers[DRM_MAX_DRIVERS];
static uint32_t drm_driver_count = 0;
static uint32_t drm_next_fb_id = 1;

/* ---- Software fallback implementations ---- */

static void sw_fill(drm_framebuffer_t *fb, uint32_t x, uint32_t y,
                    uint32_t w, uint32_t h, uint32_t color) {
    if (!fb || !fb->vaddr) return;

    uint32_t bytes_per_pixel = fb->bpp / 8;
    uint32_t row, col;

    for (row = y; row < y + h && row < fb->height; row++) {
        for (col = x; col < x + w && col < fb->width; col++) {
            if (bytes_per_pixel == 4) {
                fb->vaddr[row * (fb->pitch / 4) + col] = color;
            } else if (bytes_per_pixel == 2) {
                uint16_t *p = (uint16_t *)fb->vaddr;
                p[row * (fb->pitch / 2) + col] = (uint16_t)color;
            }
        }
    }
}

static void sw_blit(drm_framebuffer_t *src, uint32_t sx, uint32_t sy,
                    uint32_t sw, uint32_t sh,
                    drm_framebuffer_t *dst, uint32_t dx, uint32_t dy) {
    if (!src || !dst || !src->vaddr || !dst->vaddr) return;

    uint32_t row, col;
    uint32_t src_pitch32 = src->pitch / 4;
    uint32_t dst_pitch32 = dst->pitch / 4;

    for (row = 0; row < sh && (sy + row) < src->height && (dy + row) < dst->height; row++) {
        for (col = 0; col < sw && (sx + col) < src->width && (dx + col) < dst->width; col++) {
            uint32_t pixel = src->vaddr[(sy + row) * src_pitch32 + (sx + col)];
            dst->vaddr[(dy + row) * dst_pitch32 + (dx + col)] = pixel;
        }
    }
}

static void sw_copy(drm_framebuffer_t *fb, uint32_t sx, uint32_t sy,
                    uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) {
    if (!fb || !fb->vaddr) return;

    uint32_t pitch32 = fb->pitch / 4;

    /* Determine copy direction to avoid overlap issues */
    if (sy < dy || (sy == dy && sx < dx)) {
        /* Copy backwards */
        int32_t row, col;
        for (row = (int32_t)h - 1; row >= 0; row--) {
            for (col = (int32_t)w - 1; col >= 0; col--) {
                if ((sy + row) < fb->height && (sx + col) < fb->width &&
                    (dy + row) < fb->height && (dx + col) < fb->width) {
                    fb->vaddr[(dy + row) * pitch32 + (dx + col)] =
                        fb->vaddr[(sy + row) * pitch32 + (sx + col)];
                }
            }
        }
    } else {
        /* Copy forwards */
        uint32_t row, col;
        for (row = 0; row < h; row++) {
            for (col = 0; col < w; col++) {
                if ((sy + row) < fb->height && (sx + col) < fb->width &&
                    (dy + row) < fb->height && (dx + col) < fb->width) {
                    fb->vaddr[(dy + row) * pitch32 + (dx + col)] =
                        fb->vaddr[(sy + row) * pitch32 + (sx + col)];
                }
            }
        }
    }
}

/* ---- DRM core functions ---- */

void drm_init(void) {
    drm_driver_count = 0;
    drm_next_fb_id = 1;
    for (int i = 0; i < DRM_MAX_DRIVERS; i++) {
        drm_drivers[i] = (void *)0;
    }
}

int drm_register_driver(drm_driver_t *drv) {
    if (!drv || drm_driver_count >= DRM_MAX_DRIVERS) return -1;

    drm_drivers[drm_driver_count] = drv;

    /* Call driver load if available */
    if (drv->load) {
        int ret = drv->load(drv, drv->pci_bus, drv->pci_dev, drv->pci_func);
        if (ret != 0) {
            drm_drivers[drm_driver_count] = (void *)0;
            return ret;
        }
    }

    /* Initialize modeset if available */
    if (drv->modeset_init) {
        drv->modeset_init(drv);
    }

    drm_driver_count++;
    return 0;
}

drm_driver_t *drm_get_driver(uint32_t index) {
    if (index >= drm_driver_count) return (void *)0;
    return drm_drivers[index];
}

uint32_t drm_get_driver_count(void) {
    return drm_driver_count;
}

drm_framebuffer_t *drm_fb_create(uint32_t width, uint32_t height, uint32_t bpp) {
    /* Find a driver to create the framebuffer */
    drm_driver_t *drv = (void *)0;
    for (uint32_t i = 0; i < drm_driver_count; i++) {
        if (drm_drivers[i]) {
            drv = drm_drivers[i];
            break;
        }
    }

    if (!drv) return (void *)0;

    /* Find a free framebuffer slot in the driver */
    if (drv->fb_count >= 16) return (void *)0;

    drm_framebuffer_t *fb = &drv->framebuffers[drv->fb_count];
    memset(fb, 0, sizeof(drm_framebuffer_t));

    fb->id = drm_next_fb_id++;
    fb->width = width;
    fb->height = height;
    fb->bpp = bpp;
    fb->depth = bpp;
    fb->pitch = width * (bpp / 8);
    fb->size = fb->pitch * height;

    /* Try driver-specific fb_create first */
    if (drv->fb_create) {
        int ret = drv->fb_create(fb);
        if (ret == 0) {
            drv->fb_count++;
            return fb;
        }
    }

    /* Fallback: allocate backing storage from kernel heap */
    fb->vaddr = (uint32_t *)kmalloc(fb->size);
    if (!fb->vaddr) return (void *)0;

    fb->paddr = 0; /* Not physically contiguous */
    drv->fb_count++;

    return fb;
}

void drm_fb_destroy(drm_framebuffer_t *fb) {
    if (!fb) return;

    /* Find the driver that owns this framebuffer */
    for (uint32_t i = 0; i < drm_driver_count; i++) {
        drm_driver_t *drv = drm_drivers[i];
        if (!drv) continue;

        for (uint32_t j = 0; j < drv->fb_count; j++) {
            if (&drv->framebuffers[j] == fb) {
                if (drv->fb_destroy) {
                    drv->fb_destroy(fb);
                } else if (fb->vaddr && fb->paddr == 0) {
                    kfree(fb->vaddr);
                }
                memset(fb, 0, sizeof(drm_framebuffer_t));
                return;
            }
        }
    }

    /* Not found in any driver, just free if heap-allocated */
    if (fb->vaddr && fb->paddr == 0) {
        kfree(fb->vaddr);
    }
}

int drm_set_mode(drm_crtc_t *crtc, drm_mode_t *mode) {
    if (!crtc || !mode) return -1;

    /* Find the driver for this CRTC */
    for (uint32_t i = 0; i < drm_driver_count; i++) {
        drm_driver_t *drv = drm_drivers[i];
        if (!drv) continue;

        for (uint32_t j = 0; j < drv->crtc_count; j++) {
            if (&drv->crtcs[j] == crtc) {
                if (drv->crtc_set_mode) {
                    return drv->crtc_set_mode(crtc, mode);
                }
                /* No driver callback, just update the mode pointer */
                crtc->mode = mode;
                return 0;
            }
        }
    }

    return -1;
}

int drm_page_flip(drm_crtc_t *crtc, drm_framebuffer_t *fb) {
    if (!crtc || !fb) return -1;

    /* Find the driver for this CRTC */
    for (uint32_t i = 0; i < drm_driver_count; i++) {
        drm_driver_t *drv = drm_drivers[i];
        if (!drv) continue;

        for (uint32_t j = 0; j < drv->crtc_count; j++) {
            if (&drv->crtcs[j] == crtc) {
                if (drv->crtc_page_flip) {
                    return drv->crtc_page_flip(crtc, fb);
                }
                /* No driver callback, just update fb_id */
                crtc->fb_id = fb->id;
                return 0;
            }
        }
    }

    return -1;
}

int drm_accel_fill(drm_framebuffer_t *fb, uint32_t x, uint32_t y,
                   uint32_t w, uint32_t h, uint32_t color) {
    if (!fb) return -1;

    /* Try hardware acceleration via driver */
    for (uint32_t i = 0; i < drm_driver_count; i++) {
        drm_driver_t *drv = drm_drivers[i];
        if (!drv || !drv->accel_fill) continue;

        for (uint32_t j = 0; j < drv->fb_count; j++) {
            if (&drv->framebuffers[j] == fb) {
                int ret = drv->accel_fill(fb, x, y, w, h, color);
                if (ret == 0) return 0;
                break;
            }
        }
    }

    /* Software fallback */
    sw_fill(fb, x, y, w, h, color);
    return 0;
}

int drm_accel_blit(drm_framebuffer_t *src, uint32_t sx, uint32_t sy,
                   uint32_t sw, uint32_t sh,
                   drm_framebuffer_t *dst, uint32_t dx, uint32_t dy) {
    if (!src || !dst) return -1;

    /* Try hardware acceleration via driver */
    for (uint32_t i = 0; i < drm_driver_count; i++) {
        drm_driver_t *drv = drm_drivers[i];
        if (!drv || !drv->accel_blit) continue;

        for (uint32_t j = 0; j < drv->fb_count; j++) {
            if (&drv->framebuffers[j] == src || &drv->framebuffers[j] == dst) {
                int ret = drv->accel_blit(src, sx, sy, sw, sh, dst, dx, dy);
                if (ret == 0) return 0;
                break;
            }
        }
    }

    /* Software fallback */
    sw_blit(src, sx, sy, sw, sh, dst, dx, dy);
    return 0;
}

int drm_accel_copy(drm_framebuffer_t *fb, uint32_t sx, uint32_t sy,
                   uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) {
    if (!fb) return -1;

    /* Try hardware acceleration via driver */
    for (uint32_t i = 0; i < drm_driver_count; i++) {
        drm_driver_t *drv = drm_drivers[i];
        if (!drv || !drv->accel_copy) continue;

        for (uint32_t j = 0; j < drv->fb_count; j++) {
            if (&drv->framebuffers[j] == fb) {
                int ret = drv->accel_copy(fb, sx, sy, dx, dy, w, h);
                if (ret == 0) return 0;
                break;
            }
        }
    }

    /* Software fallback */
    sw_copy(fb, sx, sy, dx, dy, w, h);
    return 0;
}
