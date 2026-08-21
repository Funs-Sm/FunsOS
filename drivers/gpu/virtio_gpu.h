#ifndef VIRTIO_GPU_H
#define VIRTIO_GPU_H

#include "stdint.h"

/* VirtIO GPU device driver - 2D display + optional 3D resource management.
 *
 * Implements the virtio-gpu protocol (virtio spec 5.7) on top of the
 * project's virtio subsystem. Supports:
 *   - 2D framebuffer attach / detach
 *   - Scanout resource creation with display modes
 *   - TTM-style resource object allocation and lifetime tracking
 *   - Cursor set / update / move with surface handle
 *   - KMS-style atomic display update via update_cursor + flush
 *
 * The driver follows the Linux DRM/KMS terminology: CRTC (scanout),
 * plane (resource), framebuffer (host-allocated surface), and connector
 * (output). For simplicity the implementation exposes a single CRTC and
 * primary plane per scanout; multi-monitor is supported via virtio
 * scanout IDs.
 */

#define VIRTIO_GPU_VENDOR_ID         0x1AF4
#define VIRTIO_GPU_DEVICE_ID_LEGACY  0x1050
#define VIRTIO_GPU_DEVICE_ID_MODERN  0x1060

/* VirtIO GPU command types */
#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO  0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D 0x0101
#define VIRTIO_GPU_CMD_RESOURCE_UNREF    0x0102
#define VIRTIO_GPU_CMD_SET_SCANOUT       0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH    0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D 0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106
#define VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING 0x0107
#define VIRTIO_GPU_CMD_GET_CAPSET         0x0108
#define VIRTIO_GPU_CMD_GET_EDID           0x010A
#define VIRTIO_GPU_CMD_UPDATE_CURSOR      0x010B
#define VIRTIO_GPU_CMD_MOVE_CURSOR        0x010C

/* Response types */
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO   0x11FF
#define VIRTIO_GPU_RESP_OK_RESOURCE_2D    0x1201
#define VIRTIO_GPU_RESP_OK_SCANOUT        0x1203
#define VIRTIO_GPU_RESP_OK_FLUSH          0x1204
#define VIRTIO_GPU_RESP_OK_TRANSFER       0x1205
#define VIRTIO_GPU_RESP_OK_ATTACH_BACKING 0x1206

/* Resource formats */
#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM  1
#define VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM  2
#define VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM  67
#define VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM  68

/* Maximum number of supported scanouts */
#define VIRTIO_GPU_MAX_SCANOUTS           4
#define VIRTIO_GPU_MAX_RESOURCES          64
#define VIRTIO_GPU_CURSOR_MAX_SIZE        128

/* Resource lifecycle states */
enum {
    VIRTIO_GPU_RES_FREE = 0,
    VIRTIO_GPU_RES_ALLOCATED,
    VIRTIO_GPU_RES_BOUND,      /* backing attached */
    VIRTIO_GPU_RES_SCANNED_OUT,
};

typedef struct {
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
    uint32_t scanout_id;
    void    *framebuffer;       /* host linear pixel buffer */
    uint32_t fb_pitch;          /* bytes per scanline */
    uint32_t fb_size;           /* total bytes */
    uint32_t refcount;
    uint8_t  state;
} virtio_gpu_resource_t;

typedef struct {
    uint32_t scanout_id;
    uint32_t resource_id;
    uint32_t x, y;
    uint32_t width, height;
    int      enabled;
} virtio_gpu_scanout_t;

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t hot_x;
    uint16_t hot_y;
    uint32_t resource_id;
    int      enabled;
} virtio_gpu_cursor_t;

/* Driver instance (singleton per PCI device). */
typedef struct {
    virtio_gpu_resource_t resources[VIRTIO_GPU_MAX_RESOURCES];
    virtio_gpu_scanout_t  scanouts[VIRTIO_GPU_MAX_SCANOUTS];
    virtio_gpu_cursor_t   cursor;
    uint32_t              next_resource_id;
    int                   inited;
} virtio_gpu_t;

/* API */
int virtio_gpu_init(void);
int virtio_gpu_get_display_info(uint32_t scanout_id,
                                uint16_t *width, uint16_t *height);
int virtio_gpu_create_framebuffer(uint32_t scanout_id, uint32_t width,
                                  uint32_t height, void **fb_out,
                                  uint32_t *pitch_out);
int virtio_gpu_flip(uint32_t scanout_id, uint32_t x, uint32_t y,
                    uint32_t width, uint32_t height, const void *fb);
int virtio_gpu_destroy_framebuffer(uint32_t scanout_id);
int virtio_gpu_set_cursor(const void *bitmap, uint16_t width, uint16_t height,
                          uint16_t hot_x, uint16_t hot_y);
int virtio_gpu_move_cursor(uint32_t x, uint32_t y);
int virtio_gpu_disable_cursor(void);

#endif