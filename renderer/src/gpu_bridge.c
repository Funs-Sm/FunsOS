/* gpu_bridge.c - GPU 加速桥接实现
 * 实现 GPU 加速抽象层: 命令缓冲、矩形填充、位图传输、
 * Alpha 混合、GPU 内存管理、表面管理和 VSync 同步。
 *
 * 在无硬件 GPU 时自动回退到软件渲染路径。
 */

#include "funrender.h"
#include "fr_context.h"
#include "fr_gpu.h"
#include "string.h"

/* ---- 内部缓冲区追踪 ---- */

/* 最大同时分配的 GPU 缓冲区 */
#define FR_GPU_MAX_BUFFERS  64

/* GPU 缓冲区分配记录 */
typedef struct {
    fr_gpu_buffer_t handle;
    void *cpu_addr;
    uint32_t size;
    uint32_t flags;
    int in_use;
} fr_gpu_buffer_info_t;

/* 全局 GPU 缓冲区表 (静态生命周期) */
static fr_gpu_buffer_info_t g_buffers[FR_GPU_MAX_BUFFERS];
static uint32_t g_next_handle = 1;

/* ---- 内部辅助函数 ---- */

/* 查找缓冲区记录 */
static fr_gpu_buffer_info_t *find_buffer(fr_gpu_buffer_t handle)
{
    for (int i = 0; i < FR_GPU_MAX_BUFFERS; i++) {
        if (g_buffers[i].in_use && g_buffers[i].handle == handle) {
            return &g_buffers[i];
        }
    }
    return NULL;
}

/* 申请一个新的缓冲区槽位 */
static fr_gpu_buffer_info_t *alloc_buffer_slot(void)
{
    for (int i = 0; i < FR_GPU_MAX_BUFFERS; i++) {
        if (!g_buffers[i].in_use) {
            memset(&g_buffers[i], 0, sizeof(g_buffers[i]));
            g_buffers[i].handle = g_next_handle++;
            if (g_next_handle == FR_GPU_INVALID_BUFFER) {
                g_next_handle = 1;
            }
            g_buffers[i].in_use = 1;
            return &g_buffers[i];
        }
    }
    return NULL;
}

/* 软件矩形填充 (回退路径) */
static void sw_fill_rect(uint32_t *fb, int fb_w, int fb_h,
                         int x, int y, int w, int h, uint32_t color)
{
    if (fb == NULL) return;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > fb_w) w = fb_w - x;
    if (y + h > fb_h) h = fb_h - y;
    if (w <= 0 || h <= 0) return;

    for (int py = 0; py < h; py++) {
        for (int px = 0; px < w; px++) {
            fb[(y + py) * fb_w + (x + px)] = color;
        }
    }
}

/* 软件位图传输 (回退路径) */
static void sw_bitblt(uint32_t *fb, int fb_w, int fb_h,
                      int dx, int dy,
                      const uint32_t *src, int src_w, int src_h,
                      int sx, int sy, int sw, int sh)
{
    if (fb == NULL || src == NULL) return;

    for (int py = 0; py < sh; py++) {
        for (int px = 0; px < sw; px++) {
            int tx = dx + px;
            int ty = dy + py;

            if (tx < 0 || tx >= fb_w || ty < 0 || ty >= fb_h) continue;

            int sxi = sx + px;
            int syi = sy + py;

            if (sxi < 0 || sxi >= src_w || syi < 0 || syi >= src_h) continue;

            fb[ty * fb_w + tx] = src[syi * src_w + sxi];
        }
    }
}

/* 软件 Alpha 混合位图传输 (回退路径) */
static void sw_blend(uint32_t *fb, int fb_w, int fb_h,
                     int dx, int dy,
                     const uint32_t *src, int src_w, int src_h,
                     int sx, int sy, int sw, int sh, uint8_t alpha)
{
    if (fb == NULL || src == NULL) return;
    if (alpha == 0) return;

    for (int py = 0; py < sh; py++) {
        for (int px = 0; px < sw; px++) {
            int tx = dx + px;
            int ty = dy + py;

            if (tx < 0 || tx >= fb_w || ty < 0 || ty >= fb_h) continue;

            int sxi = sx + px;
            int syi = sy + py;

            if (sxi < 0 || sxi >= src_w || syi < 0 || syi >= src_h) continue;

            uint32_t sp = src[syi * src_w + sxi];
            uint32_t dp = fb[ty * fb_w + tx];

            uint8_t sr = (sp >> 16) & 0xFF;
            uint8_t sg = (sp >> 8) & 0xFF;
            uint8_t sb = sp & 0xFF;
            uint8_t dr = (dp >> 16) & 0xFF;
            uint8_t dg = (dp >> 8) & 0xFF;
            uint8_t db = dp & 0xFF;

            uint16_t inv = 255 - alpha;
            uint8_t rr = (uint8_t)(((uint16_t)sr * alpha + (uint16_t)dr * inv) / 255);
            uint8_t rg = (uint8_t)(((uint16_t)sg * alpha + (uint16_t)dg * inv) / 255);
            uint8_t rb = (uint8_t)(((uint16_t)sb * alpha + (uint16_t)db * inv) / 255);

            fb[ty * fb_w + tx] = ((uint32_t)rr << 16) | ((uint32_t)rg << 8) | rb;
        }
    }
}

/* 检测可用的 GPU 后端 */
static uint32_t detect_gpu_driver(void)
{
    /* 
     * 在实际内核中, 这里会通过 PCI 枚举或 ACPI 检测 GPU 硬件。
     * 当前在 freestanding 环境下, 尝试检测已知的 GPU 设备:
     *   - VMware SVGA II (PCI vendor 0x15AD)
     *   - VirtIO GPU (PCI vendor 0x1AF4, device 0x1050)
     *   - Bochs VBE (BGA)
     *   - Intel GMA (PCI vendor 0x8086)
     *
     * 简化的实现: 返回 FR_GPU_DRIVER_SOFTWARE (软件渲染)
     * 如果环境中有可用的图形硬件, 驱动程序会覆盖此值。
     */

    /* 默认使用软件渲染 */
    return FR_GPU_DRIVER_SOFTWARE;
}

/* ================================================================
 *  GPU 上下文管理
 * ================================================================ */

/*
 * fr_gpu_init - 初始化 GPU 上下文
 *
 * 自动检测可用的 GPU 后端, 如果无硬件则使用软件回退。
 */
fr_gpu_context_t *fr_gpu_init(int width, int height, void *framebuffer)
{
    fr_gpu_context_t *gpu = (fr_gpu_context_t *)fr_alloc(
        (uint32_t)sizeof(fr_gpu_context_t));
    if (gpu == NULL) return NULL;

    memset(gpu, 0, sizeof(fr_gpu_context_t));

    gpu->driver_type = detect_gpu_driver();
    gpu->state = FR_GPU_STATE_READY;

    if (gpu->driver_type == FR_GPU_DRIVER_NONE ||
        gpu->driver_type == FR_GPU_DRIVER_SOFTWARE) {
        gpu->hw_available = 0;
    } else {
        gpu->hw_available = 1;
    }

    /* 设置帧缓冲 */
    gpu->framebuffer = (uint32_t *)framebuffer;
    gpu->fb_width = width;
    gpu->fb_height = height;
    gpu->fb_pitch = width * 4;

    gpu->frame_begun = 0;
    gpu->frame_id = 0;

    gpu->rect_fill_count = 0;
    gpu->bitblt_count = 0;
    gpu->blend_count = 0;
    gpu->fallback_count = 0;
    gpu->alloc_count = 0;
    gpu->alloc_bytes = 0;

    /* 填充 GPU 能力 */
    memset(&gpu->caps, 0, sizeof(gpu->caps));

    if (gpu->hw_available) {
        /* 硬件模式下的能力 (取决于实际驱动) */
        gpu->caps.features = FR_GPU_FEATURE_RECT_FILL |
                             FR_GPU_FEATURE_BITBLT |
                             FR_GPU_FEATURE_ALPHA_BLEND |
                             FR_GPU_FEATURE_2D_ACCEL;
        gpu->caps.max_texture_width = 8192;
        gpu->caps.max_texture_height = 8192;
        gpu->caps.max_surface_count = 256;
        gpu->caps.memory_total_kb = 256 * 1024;  /* 256 MB */
        gpu->caps.memory_avail_kb = 256 * 1024;
        gpu->caps.supports_yuv = 1;
        gpu->caps.supports_argb = 1;
    } else {
        /* 软件回退模式: 最小能力集 */
        gpu->caps.features = 0;
        gpu->caps.max_texture_width = (uint32_t)width;
        gpu->caps.max_texture_height = (uint32_t)height;
        gpu->caps.max_surface_count = 16;
        gpu->caps.memory_total_kb = 0;
        gpu->caps.memory_avail_kb = 0;
        gpu->caps.supports_yuv = 0;
        gpu->caps.supports_argb = 0;
    }

    /* 供应商字符串 */
    {
        const char *vendor = "Unknown";
        switch (gpu->driver_type) {
        case FR_GPU_DRIVER_SOFTWARE: vendor = "Software Renderer"; break;
        case FR_GPU_DRIVER_VMWARE:   vendor = "VMware Inc."; break;
        case FR_GPU_DRIVER_VIRTIO:   vendor = "Red Hat Inc."; break;
        case FR_GPU_DRIVER_BOCHS:    vendor = "Bochs Project"; break;
        case FR_GPU_DRIVER_INTEL:    vendor = "Intel Corp."; break;
        case FR_GPU_DRIVER_GENERIC:  vendor = "Generic VBE"; break;
        default: break;
        }

        int vi = 0;
        while (vendor[vi] && vi < 63) {
            gpu->caps.vendor[vi] = vendor[vi];
            vi++;
        }
        gpu->caps.vendor[vi] = '\0';
    }

    {
        const char *renderer = "FUNRender GPU Bridge";
        int ri = 0;
        while (renderer[ri] && ri < 127) {
            gpu->caps.renderer[ri] = renderer[ri];
            ri++;
        }
        gpu->caps.renderer[ri] = '\0';
    }

    gpu->caps.version_major = 1;
    gpu->caps.version_minor = 0;

    return gpu;
}

/*
 * fr_gpu_shutdown - 关闭 GPU 上下文
 */
void fr_gpu_shutdown(fr_gpu_context_t *gpu)
{
    if (gpu == NULL) return;

    /* 释放所有分配的缓冲区 */
    for (int i = 0; i < FR_GPU_MAX_BUFFERS; i++) {
        if (g_buffers[i].in_use) {
            if (g_buffers[i].cpu_addr != NULL) {
                fr_free(g_buffers[i].cpu_addr);
                gpu->alloc_bytes -= g_buffers[i].size;
                gpu->alloc_count--;
            }
            memset(&g_buffers[i], 0, sizeof(g_buffers[i]));
        }
    }

    fr_free(gpu);
}

/*
 * fr_gpu_begin_frame - 开始新帧
 */
void fr_gpu_begin_frame(fr_gpu_context_t *gpu)
{
    if (gpu == NULL) return;
    gpu->frame_begun = 1;
    gpu->frame_id++;
}

/*
 * fr_gpu_end_frame - 结束当前帧
 */
void fr_gpu_end_frame(fr_gpu_context_t *gpu)
{
    if (gpu == NULL) return;

    /* 在硬件模式下, 这里会触发命令缓冲区提交和缓冲区交换 */
    /* 在软件模式下, 帧缓冲内容已经直接写入, 无需额外操作 */
    gpu->frame_begun = 0;
}

/*
 * fr_gpu_is_available - 查询 GPU 是否可用
 */
int fr_gpu_is_available(fr_gpu_context_t *gpu)
{
    if (gpu == NULL) return 0;
    return gpu->hw_available;
}

/*
 * fr_gpu_get_caps - 获取 GPU 能力
 */
const fr_gpu_caps_t *fr_gpu_get_caps(fr_gpu_context_t *gpu)
{
    if (gpu == NULL) return NULL;
    return &gpu->caps;
}

/* ================================================================
 *  命令缓冲区
 * ================================================================ */

/*
 * fr_gpu_cmd_buffer_create - 创建命令缓冲区
 */
fr_gpu_cmd_buffer_t *fr_gpu_cmd_buffer_create(void)
{
    fr_gpu_cmd_buffer_t *cmds = (fr_gpu_cmd_buffer_t *)fr_alloc(
        (uint32_t)sizeof(fr_gpu_cmd_buffer_t));
    if (cmds == NULL) return NULL;

    memset(cmds, 0, sizeof(fr_gpu_cmd_buffer_t));
    return cmds;
}

/*
 * fr_gpu_cmd_buffer_destroy - 销毁命令缓冲区
 */
void fr_gpu_cmd_buffer_destroy(fr_gpu_cmd_buffer_t *cmds)
{
    if (cmds != NULL) {
        fr_free(cmds);
    }
}

/*
 * fr_gpu_cmd_buffer_clear - 清空命令缓冲区
 */
void fr_gpu_cmd_buffer_clear(fr_gpu_cmd_buffer_t *cmds)
{
    if (cmds == NULL) return;
    cmds->count = 0;
    cmds->flushed = 0;
}

/*
 * fr_gpu_cmd_buffer_flush - 提交并执行命令缓冲区
 *
 * 在有硬件的模式下, 将命令批量发送到 GPU 执行。
 * 在软件模式下, 命令已经在添加时直接执行 (非批处理模式)。
 */
void fr_gpu_cmd_buffer_flush(fr_gpu_context_t *gpu,
                             fr_gpu_cmd_buffer_t *cmds)
{
    if (gpu == NULL || cmds == NULL) return;

    if (gpu->hw_available) {
        /* 硬件模式下: 向 GPU 提交所有命令 */
        /* 此处为抽象实现, 实际会调用特定硬件驱动 */
        for (uint32_t i = cmds->flushed; i < cmds->count; i++) {
            fr_gpu_command_t *cmd = &cmds->commands[i];
            switch (cmd->type) {
            case FR_GPU_CMD_FILL_RECT:
                /* GPU 填充矩形命令 */
                break;
            case FR_GPU_CMD_BITBLT:
                /* GPU 位图传输命令 */
                break;
            case FR_GPU_CMD_BLEND:
                /* GPU Alpha 混合命令 */
                break;
            case FR_GPU_CMD_CLEAR:
                /* GPU 清除命令 */
                break;
            case FR_GPU_CMD_FLUSH:
                /* GPU 刷新管线 */
                break;
            default:
                break;
            }
        }
    }

    cmds->flushed = cmds->count;
}

/* ================================================================
 *  硬件加速绘制
 * ================================================================ */

/*
 * fr_gpu_fill_rect - 硬件加速矩形填充
 *
 * 如果有硬件加速则使用 GPU 加速, 否则回退到软件渲染。
 */
void fr_gpu_fill_rect(fr_gpu_context_t *gpu,
                      int x, int y, int w, int h, uint32_t color)
{
    if (gpu == NULL) return;
    if (w <= 0 || h <= 0) return;

    if (gpu->hw_available &&
        (gpu->caps.features & FR_GPU_FEATURE_RECT_FILL)) {
        /* 硬件加速路径 */ 
        /* 实际硬件调用, 这里只是计数并回退 */
        gpu->fallback_count++;
        gpu->rect_fill_count++;
    }

    /* 软件回退路径 (总是执行以确保正确性) */
    sw_fill_rect(gpu->framebuffer, gpu->fb_width, gpu->fb_height,
                 x, y, w, h, color);
    gpu->rect_fill_count++;
}

/*
 * fr_gpu_bitblt - 硬件加速位图传输
 */
void fr_gpu_bitblt(fr_gpu_context_t *gpu,
                   int dx, int dy,
                   const uint32_t *src, int src_w, int src_h,
                   int sx, int sy, int sw, int sh)
{
    if (gpu == NULL || src == NULL) return;
    if (sw <= 0 || sh <= 0) return;

    if (gpu->hw_available &&
        (gpu->caps.features & FR_GPU_FEATURE_BITBLT)) {
        gpu->fallback_count++;
    }

    sw_bitblt(gpu->framebuffer, gpu->fb_width, gpu->fb_height,
              dx, dy, src, src_w, src_h, sx, sy, sw, sh);
    gpu->bitblt_count++;
}

/*
 * fr_gpu_blend - 硬件加速 Alpha 混合位图传输
 */
void fr_gpu_blend(fr_gpu_context_t *gpu,
                  int dx, int dy,
                  const uint32_t *src, int src_w, int src_h,
                  int sx, int sy, int sw, int sh, uint8_t alpha)
{
    if (gpu == NULL || src == NULL) return;
    if (sw <= 0 || sh <= 0 || alpha == 0) return;

    if (gpu->hw_available &&
        (gpu->caps.features & FR_GPU_FEATURE_ALPHA_BLEND)) {
        gpu->fallback_count++;
    }

    sw_blend(gpu->framebuffer, gpu->fb_width, gpu->fb_height,
             dx, dy, src, src_w, src_h, sx, sy, sw, sh, alpha);
    gpu->blend_count++;
}

/* ================================================================
 *  GPU 内存管理
 * ================================================================ */

/*
 * fr_gpu_alloc_buffer - 分配 GPU 可访问的缓冲区
 *
 * 在硬件模式下, 分配 GPU 可见 (可能 GPU-local) 的缓冲区。
 * 在软件模式下, 退化为普通的系统内存分配。
 */
fr_gpu_buffer_t fr_gpu_alloc_buffer(fr_gpu_context_t *gpu,
                                     uint32_t size_bytes, uint32_t flags,
                                     void **cpu_addr_out)
{
    if (gpu == NULL || size_bytes == 0) return FR_GPU_INVALID_BUFFER;

    fr_gpu_buffer_info_t *info = alloc_buffer_slot();
    if (info == NULL) return FR_GPU_INVALID_BUFFER;

    /* 分配内存 */
    void *mem = fr_alloc(size_bytes);
    if (mem == NULL) {
        info->in_use = 0;
        return FR_GPU_INVALID_BUFFER;
    }

    memset(mem, 0, size_bytes);

    info->cpu_addr = mem;
    info->size = size_bytes;
    info->flags = flags;

    gpu->alloc_count++;
    gpu->alloc_bytes += size_bytes;

    if (cpu_addr_out != NULL) {
        *cpu_addr_out = mem;
    }

    return info->handle;
}

/*
 * fr_gpu_free_buffer - 释放 GPU 缓冲区
 */
void fr_gpu_free_buffer(fr_gpu_context_t *gpu, fr_gpu_buffer_t handle)
{
    if (gpu == NULL || handle == FR_GPU_INVALID_BUFFER) return;

    fr_gpu_buffer_info_t *info = find_buffer(handle);
    if (info == NULL) return;

    if (info->cpu_addr != NULL) {
        fr_free(info->cpu_addr);
        gpu->alloc_bytes -= info->size;
        gpu->alloc_count--;
    }

    info->in_use = 0;
}

/*
 * fr_gpu_lock_buffer - 获取 GPU 缓冲区的 CPU 可访问地址
 */
void *fr_gpu_lock_buffer(fr_gpu_context_t *gpu, fr_gpu_buffer_t handle)
{
    if (gpu == NULL) return NULL;

    fr_gpu_buffer_info_t *info = find_buffer(handle);
    if (info == NULL) return NULL;

    return info->cpu_addr;
}

/*
 * fr_gpu_unlock_buffer - 解除 GPU 缓冲区的锁定
 */
void fr_gpu_unlock_buffer(fr_gpu_context_t *gpu, fr_gpu_buffer_t handle)
{
    /* 在软件模式下无需特殊操作 */
    /* 在硬件模式中可能需要刷新缓存一致性 */
    (void)gpu;
    (void)handle;
}

/* ================================================================
 *  GPU 表面管理
 * ================================================================ */

/*
 * fr_gpu_surface_create - 创建 GPU 表面
 */
fr_gpu_surface_t *fr_gpu_surface_create(fr_gpu_context_t *gpu,
                                         int width, int height,
                                         uint32_t format)
{
    if (gpu == NULL || width <= 0 || height <= 0) return NULL;

    fr_gpu_surface_t *surf = (fr_gpu_surface_t *)fr_alloc(
        (uint32_t)sizeof(fr_gpu_surface_t));
    if (surf == NULL) return NULL;

    memset(surf, 0, sizeof(fr_gpu_surface_t));

    /* 计算每像素字节数 */
    int bpp;
    switch (format) {
    case FR_GPU_FORMAT_RGB565:
    case FR_GPU_FORMAT_ARGB1555: bpp = 2; break;
    case FR_GPU_FORMAT_A8:       bpp = 1; break;
    case FR_GPU_FORMAT_ARGB8888:
    case FR_GPU_FORMAT_XRGB8888:
    default:                     bpp = 4; break;
    }

    int pitch = width * bpp;
    uint32_t size = (uint32_t)(pitch * height);

    void *cpu_mem = NULL;
    fr_gpu_buffer_t handle = FR_GPU_INVALID_BUFFER;

    if (gpu->hw_available) {
        /* 尝试分配 GPU 内存 */
        handle = fr_gpu_alloc_buffer(gpu, size,
                                     FR_GPU_MEM_READ | FR_GPU_MEM_WRITE,
                                     &cpu_mem);
    }

    if (handle == FR_GPU_INVALID_BUFFER) {
        /* 回退到系统内存 */
        cpu_mem = fr_alloc(size);
        if (cpu_mem == NULL) {
            fr_free(surf);
            return NULL;
        }
        memset(cpu_mem, 0, size);
        handle = FR_GPU_INVALID_BUFFER;
    }

    surf->handle = (handle != FR_GPU_INVALID_BUFFER) ? handle : 0;
    surf->width = width;
    surf->height = height;
    surf->pitch = pitch;
    surf->format = format;
    surf->flags = 0;
    surf->cpu_addr = cpu_mem;
    surf->gpu_addr = 0;  /* 软件模式无 GPU 地址 */
    surf->size_bytes = size;

    return surf;
}

/*
 * fr_gpu_surface_destroy - 销毁 GPU 表面
 */
void fr_gpu_surface_destroy(fr_gpu_context_t *gpu, fr_gpu_surface_t *surface)
{
    if (surface == NULL) return;

    if (surface->handle != 0 && surface->handle != FR_GPU_INVALID_BUFFER) {
        fr_gpu_free_buffer(gpu, surface->handle);
    } else if (surface->cpu_addr != NULL) {
        fr_free(surface->cpu_addr);
    }

    fr_free(surface);
}

/*
 * fr_gpu_surface_upload - 上传数据到 GPU 表面
 */
int fr_gpu_surface_upload(fr_gpu_context_t *gpu,
                          fr_gpu_surface_t *surface,
                          const void *data, int pitch)
{
    if (gpu == NULL || surface == NULL || data == NULL) return -1;
    if (surface->cpu_addr == NULL) return -1;

    int copy_pitch = (pitch > 0 && pitch < surface->pitch) ?
                     pitch : surface->pitch;

    /* 逐行拷贝到表面缓冲区 */
    const uint8_t *src = (const uint8_t *)data;
    uint8_t *dst = (uint8_t *)surface->cpu_addr;

    for (int y = 0; y < surface->height; y++) {
        /* 使用 memcpy 是安全的, 因为我们在逐行拷贝 */
        for (int x = 0; x < copy_pitch; x++) {
            dst[y * surface->pitch + x] = src[y * pitch + x];
        }
    }

    /* 在硬件模式下, 可能需要刷新 GPU 缓存 */
    return 0;
}

/*
 * fr_gpu_surface_download - 将 GPU 表面数据下载到系统内存
 */
int fr_gpu_surface_download(fr_gpu_context_t *gpu,
                            fr_gpu_surface_t *surface,
                            void *out_data, int out_pitch)
{
    if (gpu == NULL || surface == NULL || out_data == NULL) return -1;
    if (surface->cpu_addr == NULL) return -1;

    int copy_pitch = (out_pitch > 0 && out_pitch < surface->pitch) ?
                     out_pitch : surface->pitch;

    /* 逐行拷贝出去 */
    uint8_t *src = (uint8_t *)surface->cpu_addr;
    uint8_t *dst = (uint8_t *)out_data;

    for (int y = 0; y < surface->height; y++) {
        for (int x = 0; x < copy_pitch; x++) {
            dst[y * out_pitch + x] = src[y * surface->pitch + x];
        }
    }

    return 0;
}

/* ================================================================
 *  VSync 同步
 * ================================================================ */

/*
 * fr_gpu_vsync_wait - 等待垂直同步
 *
 * 在硬件模式下, 等待下一个垂直同步信号以确保无撕裂的帧输出。
 * 在软件模式下, 这是一个无操作/sleep 的替代。
 */
void fr_gpu_vsync_wait(fr_gpu_context_t *gpu)
{
    if (gpu == NULL) return;

    if (gpu->hw_available &&
        (gpu->caps.features & FR_GPU_FEATURE_VSYNC)) {
        /* 等待 GPU VSync 中断 */
        /* 在实际驱动中, 这里会阻塞等待硬件中断或轮询状态寄存器 */
    }

    /* 软件模式下没有 VSync 支持, 直接返回 */
}

/*
 * fr_gpu_vsync_supported - 检查是否支持 VSync
 */
int fr_gpu_vsync_supported(fr_gpu_context_t *gpu)
{
    if (gpu == NULL) return 0;
    return (gpu->caps.features & FR_GPU_FEATURE_VSYNC) ? 1 : 0;
}

/* ================================================================
 *  软件回退
 * ================================================================ */

/*
 * fr_gpu_needs_fallback - 检查是否需要软件回退
 */
int fr_gpu_needs_fallback(fr_gpu_context_t *gpu)
{
    if (gpu == NULL) return 1;
    if (!gpu->hw_available) return 1;
    if (gpu->state == FR_GPU_STATE_LOST) return 1;
    return 0;
}

/*
 * fr_gpu_get_fallback_buffer - 获取回退用的软件帧缓冲
 */
uint32_t *fr_gpu_get_fallback_buffer(fr_gpu_context_t *gpu)
{
    if (gpu == NULL) return NULL;
    return gpu->framebuffer;
}