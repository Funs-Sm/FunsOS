#ifndef DRM_H
#define DRM_H

#include "stdint.h"

/* DRM connector status */
#define DRM_MODE_CONNECTED     1
#define DRM_MODE_DISCONNECTED  2
#define DRM_MODE_UNKNOWN       3

/* DRM mode flags */
#define DRM_MODE_FLAG_PHSYNC   (1 << 0)
#define DRM_MODE_FLAG_NHSYNC   (1 << 1)
#define DRM_MODE_FLAG_PVSYNC   (1 << 2)
#define DRM_MODE_FLAG_NVSYNC   (1 << 3)

/* DRM object types */
#define DRM_OBJECT_CRTC        1
#define DRM_OBJECT_CONNECTOR    2
#define DRM_OBJECT_ENCODER      3
#define DRM_OBJECT_PLANE        4

typedef struct drm_mode {
    uint32_t clock;       /* kHz */
    uint16_t hdisplay;
    uint16_t hsync_start;
    uint16_t hsync_end;
    uint16_t htotal;
    uint16_t hskew;
    uint16_t vdisplay;
    uint16_t vsync_start;
    uint16_t vsync_end;
    uint16_t vtotal;
    uint16_t vscan;
    uint32_t flags;
    uint32_t type;
    char name[32];
} drm_mode_t;

typedef struct drm_connector drm_connector_t;
typedef int (*drm_hotplug_callback_t)(drm_connector_t *conn, uint32_t event);

typedef struct drm_crtc {
    uint32_t id;
    uint32_t x, y;
    uint32_t width, height;
    drm_mode_t *mode;
    uint32_t fb_id;
    int enabled;
    void *driver_private;
} drm_crtc_t;

typedef struct drm_connector {
    uint32_t id;
    uint32_t type;
    uint32_t status;
    drm_mode_t *modes;
    uint32_t mode_count;
    uint32_t encoder_id;
    uint32_t last_status;
    uint32_t poll_interval_ms;
    uint32_t last_poll_tick;
    drm_hotplug_callback_t hotplug_callback;
    void *driver_private;
} drm_connector_t;

typedef struct drm_encoder {
    uint32_t id;
    uint32_t type;
    uint32_t crtc_id;
    uint32_t possible_crtcs;
    void *driver_private;
} drm_encoder_t;

typedef struct drm_framebuffer {
    uint32_t id;
    uint32_t width, height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t depth;
    uint32_t *vaddr;
    uint64_t paddr;
    uint32_t size;
} drm_framebuffer_t;

typedef struct drm_plane {
    uint32_t id;
    uint32_t possible_crtcs;
    uint32_t fb_id;
    uint32_t crtc_id;
    uint32_t x, y;
    void *driver_private;
} drm_plane_t;

/* DRM driver operations */
typedef struct drm_driver {
    char name[32];
    char desc[64];
    uint32_t pci_vendor;
    uint32_t pci_device_start;
    uint32_t pci_device_end;

    int (*load)(struct drm_driver *drv, uint8_t bus, uint8_t dev, uint8_t func);
    void (*unload)(struct drm_driver *drv);
    int (*modeset_init)(struct drm_driver *drv);
    int (*crtc_set_mode)(struct drm_crtc *crtc, drm_mode_t *mode);
    int (*crtc_page_flip)(struct drm_crtc *crtc, struct drm_framebuffer *fb);
    int (*fb_create)(struct drm_framebuffer *fb);
    void (*fb_destroy)(struct drm_framebuffer *fb);

    /* 2D acceleration */
    int (*accel_fill)(struct drm_framebuffer *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
    int (*accel_blit)(struct drm_framebuffer *src, uint32_t sx, uint32_t sy, uint32_t sw, uint32_t sh,
                      struct drm_framebuffer *dst, uint32_t dx, uint32_t dy);
    int (*accel_copy)(struct drm_framebuffer *fb, uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h);

    /* Driver state */
    drm_crtc_t crtcs[4];
    uint32_t crtc_count;
    drm_connector_t connectors[8];
    uint32_t connector_count;
    drm_encoder_t encoders[8];
    uint32_t encoder_count;
    drm_framebuffer_t framebuffers[16];
    uint32_t fb_count;
    drm_plane_t planes[8];
    uint32_t plane_count;
    drm_mode_t modes[32];
    uint32_t mode_count;

    void *mmio_base;
    uint32_t *vram_map;
    uint64_t vram_phys;
    uint32_t vram_size;

    uint8_t pci_bus, pci_dev, pci_func;
    void *driver_private;
} drm_driver_t;

/* DRM page flip flags */
#define DRM_PAGE_FLIP_WAIT     0x01
#define DRM_PAGE_FLIP_ASYNC    0x02
#define DRM_PAGE_FLIP_VSYNC    0x04

/* DRM hotplug event types */
#define DRM_HOTPLUG_CONNECTED    1
#define DRM_HOTPLUG_DISCONNECTED 2

/* DRM dumb buffer flags */
#define DRM_DUMB_BUF_WC      (1 << 0)
#define DRM_DUMB_BUF_CACHED  (1 << 1)

void drm_init(void);
int drm_register_driver(drm_driver_t *drv);
drm_driver_t *drm_get_driver(uint32_t index);
uint32_t drm_get_driver_count(void);
drm_framebuffer_t *drm_fb_create(uint32_t width, uint32_t height, uint32_t bpp);
void drm_fb_destroy(drm_framebuffer_t *fb);
int drm_fb_modify(drm_framebuffer_t *fb, uint32_t width, uint32_t height, uint32_t bpp, uint32_t pitch);
int drm_set_mode(drm_crtc_t *crtc, drm_mode_t *mode);
int drm_page_flip(drm_crtc_t *crtc, drm_framebuffer_t *fb);
int drm_page_flip_with_flags(drm_crtc_t *crtc, drm_framebuffer_t *fb, uint32_t flags);
int drm_wait_vblank(drm_crtc_t *crtc);
int drm_accel_fill(drm_framebuffer_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
int drm_accel_blit(drm_framebuffer_t *src, uint32_t sx, uint32_t sy, uint32_t sw, uint32_t sh,
                    drm_framebuffer_t *dst, uint32_t dx, uint32_t dy);
int drm_accel_copy(drm_framebuffer_t *fb, uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h);

/* DRM framebuffer management */
drm_framebuffer_t *drm_fb_add(drm_driver_t *drv, uint32_t width, uint32_t height, uint32_t bpp, uint32_t pitch, uint32_t flags);
int drm_fb_remove(drm_driver_t *drv, drm_framebuffer_t *fb);
drm_framebuffer_t *drm_fb_lookup(drm_driver_t *drv, uint32_t id);
drm_framebuffer_t *drm_fb_alloc_dumb(drm_driver_t *drv, uint32_t width, uint32_t height, uint32_t bpp, uint32_t flags);
int drm_fb_free_dumb(drm_driver_t *drv, drm_framebuffer_t *fb);

/* DRM connector detection */
int drm_connector_detect(drm_connector_t *conn);
int drm_connector_detect_all(drm_driver_t *drv);
int drm_connector_set_hotplug_callback(drm_connector_t *conn, drm_hotplug_callback_t cb);
void drm_hotplug_poll(drm_driver_t *drv);
int drm_connector_get_modes(drm_connector_t *conn);
drm_mode_t *drm_connector_find_mode(drm_connector_t *conn, uint32_t width, uint32_t height, uint32_t refresh);

/* DRM mode setting */
int drm_mode_validate(drm_crtc_t *crtc, drm_mode_t *mode);
int drm_crtc_set_mode_with_fb(drm_crtc_t *crtc, drm_mode_t *mode, drm_framebuffer_t *fb);
int drm_crtc_disable(drm_crtc_t *crtc);
int drm_crtc_enable(drm_crtc_t *crtc, drm_mode_t *mode);
int drm_mode_equal(drm_mode_t *a, drm_mode_t *b);
void drm_crtc_get_current_mode(drm_crtc_t *crtc, drm_mode_t *mode);

#endif
