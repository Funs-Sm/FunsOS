/* fr_gpu.h - GPU 加速桥接
 * 抽象 GPU 驱动接口, 提供硬件加速的渲染操作:
 * 矩形填充、图像传输、Alpha 混合、GPU 内存管理、
 * VSync 同步和软件回退机制
 */

#ifndef FR_GPU_H
#define FR_GPU_H

#include "stdint.h"

struct fr_context;

/* GPU 特性标志 */
#define FR_GPU_FEATURE_RECT_FILL        0x0001
#define FR_GPU_FEATURE_BITBLT           0x0002
#define FR_GPU_FEATURE_ALPHA_BLEND      0x0004
#define FR_GPU_FEATURE_2D_ACCEL         0x0008
#define FR_GPU_FEATURE_3D_ACCEL         0x0010
#define FR_GPU_FEATURE_VSYNC            0x0020
#define FR_GPU_FEATURE_DOUBLE_BUF       0x0040
#define FR_GPU_FEATURE_TRIPLE_BUF       0x0080
#define FR_GPU_FEATURE_HW_CURSOR        0x0100
#define FR_GPU_FEATURE_SCALING          0x0200
#define FR_GPU_FEATURE_ROTATION         0x0400
#define FR_GPU_FEATURE_COMPOSITING      0x0800
#define FR_GPU_FEATURE_VIDEO_OVERLAY    0x1000
#define FR_GPU_FEATURE_MULTI_SURFACE    0x2000

/* GPU 能力描述 */
typedef struct {
    uint32_t features;
    uint32_t max_texture_width;
    uint32_t max_texture_height;
    uint32_t max_surface_count;
    uint32_t memory_total_kb;
    uint32_t memory_avail_kb;
    int supports_yuv;
    int supports_argb;
    char vendor[64];
    char renderer[128];
    uint32_t version_major;
    uint32_t version_minor;
} fr_gpu_caps_t;

/* 驱动后端类型 */
#define FR_GPU_DRIVER_NONE      0
#define FR_GPU_DRIVER_SOFTWARE  1
#define FR_GPU_DRIVER_VMWARE    2
#define FR_GPU_DRIVER_VIRTIO    3
#define FR_GPU_DRIVER_BOCHS     4
#define FR_GPU_DRIVER_INTEL     5
#define FR_GPU_DRIVER_GENERIC   6

/* GPU 状态 */
#define FR_GPU_STATE_UNINITIALIZED  0
#define FR_GPU_STATE_READY          1
#define FR_GPU_STATE_ERROR          2
#define FR_GPU_STATE_LOST           3

/* GPU 内存标志 */
#define FR_GPU_MEM_READ         0x01
#define FR_GPU_MEM_WRITE        0x02
#define FR_GPU_MEM_GPU_LOCAL    0x04
#define FR_GPU_MEM_COHERENT     0x08
#define FR_GPU_MEM_CACHED       0x10
#define FR_GPU_MEM_UNCACHED     0x20

/* GPU 缓冲区句柄 */
typedef uint32_t fr_gpu_buffer_t;
#define FR_GPU_INVALID_BUFFER   0xFFFFFFFF

/* GPU 上下文 */
typedef struct fr_gpu_context {
    uint32_t driver_type;
    uint32_t state;
    fr_gpu_caps_t caps;
    int hw_available;
    uint32_t *framebuffer;
    int fb_width, fb_height;
    int fb_pitch;
    int frame_begun;
    uint32_t frame_id;
    uint32_t rect_fill_count;
    uint32_t bitblt_count;
    uint32_t blend_count;
    uint32_t fallback_count;
    uint32_t alloc_count;
    uint32_t alloc_bytes;
} fr_gpu_context_t;

/* 命令类型 */
#define FR_GPU_CMD_NOP          0
#define FR_GPU_CMD_FILL_RECT    1
#define FR_GPU_CMD_BITBLT       2
#define FR_GPU_CMD_BLEND        3
#define FR_GPU_CMD_CLEAR        4
#define FR_GPU_CMD_FLUSH        5
#define FR_GPU_CMD_SYNC         6

/* 最大命令数 */
#define FR_GPU_MAX_COMMANDS     1024

/* 命令结构 */
typedef struct {
    uint32_t type;
    union {
        struct { int x, y, w, h; uint32_t color; } fill;
        struct { int dx, dy, w, h; uint32_t src_handle; int sx, sy; } blit;
        struct { int dx, dy, w, h; uint32_t src_handle;
                 int sx, sy; uint8_t alpha; } blend;
        struct { uint32_t color; } clear;
    } data;
} fr_gpu_command_t;

/* 命令缓冲区 */
typedef struct {
    fr_gpu_command_t commands[FR_GPU_MAX_COMMANDS];
    uint32_t count;
    uint32_t flushed;
} fr_gpu_cmd_buffer_t;

/* GPU 表面 */
typedef struct {
    uint32_t handle;
    int width, height, pitch;
    uint32_t format;
    uint32_t flags;
    void *cpu_addr;
    uint32_t gpu_addr;
    uint32_t size_bytes;
} fr_gpu_surface_t;

/* 像素格式 */
#define FR_GPU_FORMAT_ARGB8888    0
#define FR_GPU_FORMAT_XRGB8888    1
#define FR_GPU_FORMAT_RGB565      2
#define FR_GPU_FORMAT_ARGB1555    3
#define FR_GPU_FORMAT_A8          4

/* ---- API 声明 ---- */

fr_gpu_context_t *fr_gpu_init(int width, int height, void *framebuffer);
void fr_gpu_shutdown(fr_gpu_context_t *gpu);
void fr_gpu_begin_frame(fr_gpu_context_t *gpu);
void fr_gpu_end_frame(fr_gpu_context_t *gpu);
int fr_gpu_is_available(fr_gpu_context_t *gpu);
const fr_gpu_caps_t *fr_gpu_get_caps(fr_gpu_context_t *gpu);

fr_gpu_cmd_buffer_t *fr_gpu_cmd_buffer_create(void);
void fr_gpu_cmd_buffer_destroy(fr_gpu_cmd_buffer_t *cmds);
void fr_gpu_cmd_buffer_clear(fr_gpu_cmd_buffer_t *cmds);
void fr_gpu_cmd_buffer_flush(fr_gpu_context_t *gpu, fr_gpu_cmd_buffer_t *cmds);

void fr_gpu_fill_rect(fr_gpu_context_t *gpu,
                      int x, int y, int w, int h, uint32_t color);
void fr_gpu_bitblt(fr_gpu_context_t *gpu,
                   int dx, int dy,
                   const uint32_t *src, int src_w, int src_h,
                   int sx, int sy, int sw, int sh);
void fr_gpu_blend(fr_gpu_context_t *gpu,
                  int dx, int dy,
                  const uint32_t *src, int src_w, int src_h,
                  int sx, int sy, int sw, int sh, uint8_t alpha);

fr_gpu_buffer_t fr_gpu_alloc_buffer(fr_gpu_context_t *gpu,
                                     uint32_t size_bytes, uint32_t flags,
                                     void **cpu_addr_out);
void fr_gpu_free_buffer(fr_gpu_context_t *gpu, fr_gpu_buffer_t handle);
void *fr_gpu_lock_buffer(fr_gpu_context_t *gpu, fr_gpu_buffer_t handle);
void fr_gpu_unlock_buffer(fr_gpu_context_t *gpu, fr_gpu_buffer_t handle);

fr_gpu_surface_t *fr_gpu_surface_create(fr_gpu_context_t *gpu,
                                         int width, int height, uint32_t format);
void fr_gpu_surface_destroy(fr_gpu_context_t *gpu, fr_gpu_surface_t *surface);
int fr_gpu_surface_upload(fr_gpu_context_t *gpu, fr_gpu_surface_t *surface,
                          const void *data, int pitch);
int fr_gpu_surface_download(fr_gpu_context_t *gpu, fr_gpu_surface_t *surface,
                            void *out_data, int out_pitch);

void fr_gpu_vsync_wait(fr_gpu_context_t *gpu);
int fr_gpu_vsync_supported(fr_gpu_context_t *gpu);
int fr_gpu_needs_fallback(fr_gpu_context_t *gpu);
uint32_t *fr_gpu_get_fallback_buffer(fr_gpu_context_t *gpu);

#endif /* FR_GPU_H */