#include "amdgpu.h"
#include "drm.h"
#include "pci.h"
#include "vmm.h"
#include "kheap.h"
#include "klog.h"
#include "string.h"
#include "stdio.h"
#include "io.h"

#define AMD_VENDOR_ID 0x1002

/* 已知 AMD GPU 设备信息 */
static const struct {
    uint16_t device_id;
    const char name[32];
    uint32_t vram_size;      /* 默认 VRAM 大小 */
    uint32_t engine_clock;   /* 默认引擎时钟 (MHz) */
    uint32_t memory_clock;   /* 默认显存时钟 (MHz) */
} amdgpu_known_devices[] = {
    /* Carrizo 系列 (R5/R6/R7) */
    { 0x9850, "Radeon R5 (Carrizo)",      512 * 1024 * 1024,  800, 900 },
    { 0x9851, "Radeon R5 (Carrizo)",      512 * 1024 * 1024,  800, 900 },
    { 0x9852, "Radeon R6 (Carrizo)",      512 * 1024 * 1024,  847, 933 },
    { 0x9853, "Radeon R6 (Carrizo)",      512 * 1024 * 1024,  847, 933 },
    { 0x9854, "Radeon R7 (Carrizo)",      512 * 1024 * 1024,  900, 1000 },
    { 0x9855, "Radeon R7 (Carrizo)",      512 * 1024 * 1024,  900, 1000 },
    { 0x9856, "Radeon R7 (Carrizo)",      512 * 1024 * 1024,  900, 1000 },
    { 0x9874, "Radeon R7 (Carrizo)",      512 * 1024 * 1024,  900, 1000 },

    /* Polaris 系列 (RX 400/500) */
    { 0x67DF, "Radeon RX 480 (Polaris 10)", 4 * 1024 * 1024 * 1024U, 1266, 2000 },
    { 0x67DF, "Radeon RX 580 (Polaris 20)", 4 * 1024 * 1024 * 1024U, 1340, 2000 },

    /* Vega 系列 */
    { 0x66AF, "Radeon RX Vega 64",         8 * 1024 * 1024 * 1024U, 1630, 945 },

    /* Picasso (Raven Ridge APU) */
    { 0x15D8, "Radeon Vega 11 (Picasso)",  256 * 1024 * 1024,  1240, 1200 },

    /* Renoir (Ryzen 4000 APU) */
    { 0x1636, "Radeon Vega 8 (Renoir)",    256 * 1024 * 1024,  1500, 1333 },

    /* Lucienne (Ryzen 5000 APU) */
    { 0x164C, "Radeon Vega 7 (Lucienne)",  256 * 1024 * 1024,  1600, 1333 },

    /* Navi 系列 (RX 5000/6000) */
    { 0x73FF, "Radeon RX 6900 XT (Navi 21)", 16 * 1024 * 1024 * 1024U, 2250, 2000 },
    { 0x7480, "Radeon RX 6500 XT (Navi 24)", 4 * 1024 * 1024 * 1024U, 2610, 2248 },

    /* Phoenix (Ryzen 7040 APU) */
    { 0x164E, "Radeon 780M (Phoenix)",     256 * 1024 * 1024,  2200, 1600 },

    { 0,      "",                          0,                  0,    0 }
};

/* 全局 AMD GPU 设备状态 */
static amdgpu_device_t amdgpu_dev;
static drm_driver_t amdgpu_drm_driver;
static const char *amdgpu_model_name = "";
static uint32_t amdgpu_default_vram = 0;
static uint32_t amdgpu_default_engine_clock = 0;
static uint32_t amdgpu_default_memory_clock = 0;

/* 已知显示模式 */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t refresh;
    uint32_t h_total;
    uint32_t h_sync_start;
    uint32_t h_sync_end;
    uint32_t v_total;
    uint32_t v_sync_start;
    uint32_t v_sync_end;
} amdgpu_mode_t;

static const amdgpu_mode_t amdgpu_modes[] = {
    { 640,  480,  60,  800,  656,  752,  525,  490,  492 },
    { 800,  600,  60,  1056, 840,  968,  628,  601,  605 },
    { 1024, 768,  60,  1344, 1048, 1184, 806,  771,  777 },
    { 1280, 720,  60,  1664, 1280, 1440, 750,  725,  730 },
    { 1280, 1024, 60,  1688, 1328, 1440, 1066, 1025, 1028 },
    { 1366, 768,  60,  1792, 1366, 1534, 798,  771,  777 },
    { 1440, 900,  60,  1904, 1440, 1616, 932,  901,  904 },
    { 1600, 900,  60,  2112, 1600, 1792, 934,  901,  904 },
    { 1680, 1050, 60,  2240, 1680, 1888, 1088, 1051, 1054 },
    { 1920, 1080, 60,  2576, 1920, 2160, 1120, 1081, 1084 },
    { 2560, 1440, 60,  3520, 2560, 2880, 1500, 1441, 1444 },
    { 3840, 2160, 30,  4400, 3840, 4016, 2250, 2161, 2164 },
    { 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

/* ---- MMIO 寄存器访问 ---- */

static uint32_t amdgpu_reg_read(uint32_t offset) {
    if (!amdgpu_dev.mmio_base) return 0;
    return *((volatile uint32_t *)(amdgpu_dev.mmio_base + offset));
}

static void amdgpu_reg_write(uint32_t offset, uint32_t value) {
    if (!amdgpu_dev.mmio_base) return;
    *((volatile uint32_t *)(amdgpu_dev.mmio_base + offset)) = value;
}

/* ---- 查找设备信息 ---- */

static const char *amdgpu_lookup_device(uint16_t device_id) {
    int i;
    for (i = 0; amdgpu_known_devices[i].device_id != 0; i++) {
        if (amdgpu_known_devices[i].device_id == device_id) {
            amdgpu_default_vram = amdgpu_known_devices[i].vram_size;
            amdgpu_default_engine_clock = amdgpu_known_devices[i].engine_clock;
            amdgpu_default_memory_clock = amdgpu_known_devices[i].memory_clock;
            return amdgpu_known_devices[i].name;
        }
    }
    /* 未知设备，使用默认值 */
    amdgpu_default_vram = 256 * 1024 * 1024;
    amdgpu_default_engine_clock = 500;
    amdgpu_default_memory_clock = 667;
    return "Unknown AMD GPU";
}

/* ---- 设置显示模式 ---- */

static int amdgpu_apply_mode(const amdgpu_mode_t *mode) {
    if (!amdgpu_dev.mmio_base) return -1;

    /* 禁用图形引擎 */
    amdgpu_reg_write(AMDGPU_GRPH_ENABLE, 0);

    /* 等待引擎停止 */
    int timeout;
    for (timeout = 0; timeout < 1000; timeout++) {
        io_wait();
    }

    /* 配置 CRTC 时序 */
    amdgpu_reg_write(AMDGPU_DC_TIMING_H_TOTAL, mode->h_total);
    amdgpu_reg_write(AMDGPU_DC_TIMING_V_TOTAL, mode->v_total);
    amdgpu_reg_write(AMDGPU_DC_TIMING_H_SYNC,
                     (mode->h_sync_start << 16) | mode->h_sync_end);
    amdgpu_reg_write(AMDGPU_DC_TIMING_V_SYNC,
                     (mode->v_sync_start << 16) | mode->v_sync_end);

    /* 配置图形引擎 */
    uint32_t pitch = mode->width * (amdgpu_dev.bpp / 8);
    amdgpu_reg_write(AMDGPU_GRPH_PITCH, pitch);
    amdgpu_reg_write(AMDGPU_GRPH_X_START, 0);
    amdgpu_reg_write(AMDGPU_GRPH_Y_START, 0);
    amdgpu_reg_write(AMDGPU_GRPH_X_END, mode->width - 1);
    amdgpu_reg_write(AMDGPU_GRPH_Y_END, mode->height - 1);

    /* 设置帧缓冲区地址 */
    if (amdgpu_dev.fb_base) {
        amdgpu_reg_write(AMDGPU_GRPH_PRIMARY_SURFACE_ADDRESS, amdgpu_dev.fb_base);
        amdgpu_reg_write(AMDGPU_GRPH_PRIMARY_SURFACE_ADDRESS_HIGH, 0);
    }

    /* 配置图形控制 */
    uint32_t grph_control = 0;
    switch (amdgpu_dev.bpp) {
        case 32:
            grph_control |= (3 << 8); /* 32-bit ARGB */
            break;
        case 24:
            grph_control |= (2 << 8); /* 24-bit RGB */
            break;
        case 16:
            grph_control |= (1 << 8); /* 16-bit RGB */
            break;
        default:
            grph_control |= (3 << 8);
            break;
    }
    amdgpu_reg_write(AMDGPU_GRPH_CONTROL, grph_control);

    /* 启用扫描输出交换 */
    amdgpu_reg_write(AMDGPU_GRPH_SWAP_CNTL, 0);

    /* 启用 CRTC */
    amdgpu_reg_write(AMDGPU_DC_CRTC_CONTROL, 1);

    /* 启用图形引擎 */
    amdgpu_reg_write(AMDGPU_GRPH_ENABLE, 1);

    /* 触发更新 */
    amdgpu_reg_write(AMDGPU_GRPH_UPDATE, 1);

    /* 更新设备状态 */
    amdgpu_dev.width = mode->width;
    amdgpu_dev.height = mode->height;
    amdgpu_dev.pitch = pitch;

    return 0;
}

void amdgpu_set_mode(uint32_t width, uint32_t height, uint32_t bpp) {
    if (!amdgpu_dev.initialized) return;

    amdgpu_dev.bpp = bpp;

    /* 查找最佳匹配模式 */
    int i;
    const amdgpu_mode_t *best = (void *)0;
    for (i = 0; amdgpu_modes[i].width != 0; i++) {
        if (amdgpu_modes[i].width == width && amdgpu_modes[i].height == height) {
            best = &amdgpu_modes[i];
            break;
        }
    }

    if (best) {
        amdgpu_apply_mode(best);
        klog_info("[AMDGPU] Mode set to %dx%d @ %d bpp", width, height, bpp);
    } else {
        klog_warn("[AMDGPU] Mode %dx%d not found, using default", width, height);
        /* 使用通用时序计算 */
        amdgpu_mode_t generic;
        generic.width = width;
        generic.height = height;
        generic.refresh = 60;
        generic.h_total = width + width / 5;
        generic.h_sync_start = width + width / 20;
        generic.h_sync_end = width + width / 20 + width / 16;
        generic.v_total = height + height / 20;
        generic.v_sync_start = height + height / 50;
        generic.v_sync_end = height + height / 50 + 3;
        amdgpu_apply_mode(&generic);
    }
}

/* ---- 帧缓冲区操作 ---- */

int amdgpu_get_framebuffer(amdgpu_device_t **dev) {
    if (!amdgpu_dev.initialized || !amdgpu_dev.framebuffer) {
        if (dev) *dev = (void *)0;
        return -1;
    }
    if (dev) *dev = &amdgpu_dev;
    return 0;
}

void amdgpu_fill_rect(uint32_t *fb, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!fb) return;
    if (w == 0 || h == 0) return;

    uint32_t pitch_dwords = amdgpu_dev.pitch / 4;
    uint32_t end_x = x + w;
    uint32_t end_y = y + h;

    if (end_x > amdgpu_dev.width) end_x = amdgpu_dev.width;
    if (end_y > amdgpu_dev.height) end_y = amdgpu_dev.height;

    if (amdgpu_dev.mmio_base && amdgpu_dev.fb_base) {
        amdgpu_reg_write(AMDGPU_GRPH_X_START, x);
        amdgpu_reg_write(AMDGPU_GRPH_Y_START, y);
        amdgpu_reg_write(AMDGPU_GRPH_X_END, end_x - 1);
        amdgpu_reg_write(AMDGPU_GRPH_Y_END, end_y - 1);
        amdgpu_reg_write(AMDGPU_GRPH_UPDATE, 1);
    }

    uint32_t actual_w = end_x - x;
    uint32_t actual_h = end_y - y;

    if (amdgpu_dev.bpp == 32) {
        uint32_t row;
        for (row = y; row < end_y; row++) {
            uint32_t *dst = &fb[row * pitch_dwords + x];
            uint32_t col;
            for (col = 0; col < actual_w; col++) {
                dst[col] = color;
            }
        }
    } else {
        uint32_t row, col;
        for (row = y; row < end_y; row++) {
            for (col = x; col < end_x; col++) {
                fb[row * pitch_dwords + col] = color;
            }
        }
    }
}

void amdgpu_blit(uint32_t *dst, uint32_t *src, uint32_t w, uint32_t h, uint32_t dst_pitch, uint32_t src_pitch) {
    if (!dst || !src) return;
    if (w == 0 || h == 0) return;

    uint32_t dst_pitch_dwords = dst_pitch / 4;
    uint32_t src_pitch_dwords = src_pitch / 4;

    if (dst == src) {
        if (dst_pitch == src_pitch) {
            uint32_t row;
            for (row = 0; row < h; row++) {
                uint32_t *d = &dst[row * dst_pitch_dwords];
                uint32_t *s = &src[row * src_pitch_dwords];
                uint32_t col;
                for (col = 0; col < w; col++) {
                    d[col] = s[col];
                }
            }
            return;
        }
    }

    if (src < dst && (uint8_t *)src + src_pitch * h > (uint8_t *)dst) {
        int32_t row;
        for (row = (int32_t)h - 1; row >= 0; row--) {
            uint32_t *d = &dst[row * dst_pitch_dwords];
            uint32_t *s = &src[row * src_pitch_dwords];
            int32_t col;
            for (col = (int32_t)w - 1; col >= 0; col--) {
                d[col] = s[col];
            }
        }
    } else {
        uint32_t row;
        for (row = 0; row < h; row++) {
            uint32_t *d = &dst[row * dst_pitch_dwords];
            uint32_t *s = &src[row * src_pitch_dwords];
            uint32_t col;
            for (col = 0; col < w; col++) {
                d[col] = s[col];
            }
        }
    }
}

/* ---- 电源管理 ---- */

void amdgpu_power_save(void) {
    if (!amdgpu_dev.mmio_base) return;

    /* 启用时钟门控 */
    uint32_t pm_cntl = amdgpu_reg_read(AMDGPU_PM_CNTL);
    pm_cntl |= 0x01; /* 启用动态电源管理 */
    amdgpu_reg_write(AMDGPU_PM_CNTL, pm_cntl);

    /* 降低引擎时钟 */
    amdgpu_dev.engine_clock = amdgpu_default_engine_clock / 2;
    amdgpu_dev.memory_clock = amdgpu_default_memory_clock / 2;

    klog_info("[AMDGPU] Entering power save mode");
}

void amdgpu_power_full(void) {
    if (!amdgpu_dev.mmio_base) return;

    /* 恢复全功率 */
    uint32_t pm_cntl = amdgpu_reg_read(AMDGPU_PM_CNTL);
    pm_cntl &= ~0x01; /* 禁用动态电源管理 */
    amdgpu_reg_write(AMDGPU_PM_CNTL, pm_cntl);

    amdgpu_dev.engine_clock = amdgpu_default_engine_clock;
    amdgpu_dev.memory_clock = amdgpu_default_memory_clock;

    klog_info("[AMDGPU] Returning to full power mode");
}

/* ---- GPU 信息 ---- */

void amdgpu_get_info(char *buf, int bufsize) {
    if (!buf || bufsize <= 0) return;

    if (!amdgpu_dev.initialized) {
        sprintf(buf, "AMD GPU: Not initialized");
        return;
    }

    char info[512];
    int len = sprintf(info,
        "AMD GPU: %s\n"
        "  Resolution: %dx%d @ %d bpp\n"
        "  VRAM: %d MB\n"
        "  Engine Clock: %d MHz\n"
        "  Memory Clock: %d MHz\n"
        "  PCI: %02X:%02X.%d\n"
        "  Framebuffer: 0x%08X (phys: 0x%08X)\n"
        "  MMIO: 0x%08X",
        amdgpu_model_name,
        amdgpu_dev.width, amdgpu_dev.height, amdgpu_dev.bpp,
        amdgpu_dev.fb_size / (1024 * 1024),
        amdgpu_dev.engine_clock,
        amdgpu_dev.memory_clock,
        amdgpu_dev.pci_bus, amdgpu_dev.pci_slot, amdgpu_dev.pci_func,
        (uint32_t)(uintptr_t)amdgpu_dev.framebuffer,
        amdgpu_dev.fb_base,
        amdgpu_dev.mmio_base
    );

    if (len < bufsize) {
        memcpy(buf, info, len + 1);
    } else {
        memcpy(buf, info, bufsize - 1);
        buf[bufsize - 1] = '\0';
    }
}

/* ---- 设备探测 ---- */

int amdgpu_probe(void) {
    /* 扫描 PCI 总线查找 AMD GPU */
    uint16_t bus;
    uint8_t dev, func;
    int found = 0;
    for (bus = 0; bus < 256 && !found; bus++) {
        for (dev = 0; dev < 32 && !found; dev++) {
            for (func = 0; func < 8 && !found; func++) {
                uint32_t vd = pci_read_config((uint8_t)bus, dev, func, 0x00);
                if (vd == 0xFFFFFFFF || vd == 0) continue;
                uint16_t vendor = vd & 0xFFFF;
                if (vendor == AMD_VENDOR_ID) {
                    uint8_t base_class = (pci_read_config((uint8_t)bus, dev, func, 0x08) >> 24) & 0xFF;
                    if (base_class == 0x03) { /* 显示控制器 */
                        found = 1;
                    }
                }
            }
        }
    }

    if (!found) {
        klog_info("[AMDGPU] No AMD GPU found");
        return -1;
    }

    /* 回退到找到的设备 */
    bus--; dev--; func--;

    memset(&amdgpu_dev, 0, sizeof(amdgpu_device_t));

    amdgpu_dev.pci_bus = bus;
    amdgpu_dev.pci_slot = dev;
    amdgpu_dev.pci_func = func;

    uint16_t device_id = (pci_read_config((uint8_t)bus, dev, func, 0x00) >> 16) & 0xFFFF;
    amdgpu_model_name = amdgpu_lookup_device(device_id);

    klog_info("[AMDGPU] Found %s (0x%04X) at PCI %02X:%02X.%d",
              amdgpu_model_name, device_id, bus, dev, func);

    /* 启用 PCI 总线主控和内存访问 */
    uint32_t cmd = pci_read_config((uint8_t)bus, dev, func, 0x04);
    pci_write_config((uint8_t)bus, dev, func, 0x04, cmd | 0x07);

    /* 映射 MMIO BAR */
    uint32_t bar0 = pci_read_config((uint8_t)bus, dev, func, 0x10);
    uint32_t mmio_phys = bar0 & 0xFFFFFFF0;
    if (mmio_phys == 0) {
        uint32_t bar2 = pci_read_config((uint8_t)bus, dev, func, 0x18);
        mmio_phys = bar2 & 0xFFFFFFF0;
    }
    if (mmio_phys == 0) {
        klog_err("[AMDGPU] Cannot find MMIO BAR");
        return -1;
    }

    amdgpu_dev.mmio_base = (uint32_t)vmm_map_physical(mmio_phys, 0x80000);
    if (!amdgpu_dev.mmio_base) {
        klog_err("[AMDGPU] Failed to map MMIO");
        return -1;
    }

    klog_info("[AMDGPU] MMIO mapped at 0x%08X (phys: 0x%08X)",
              amdgpu_dev.mmio_base, mmio_phys);

    /* 映射帧缓冲区 BAR */
    uint32_t fb_bar = pci_read_config((uint8_t)bus, dev, func, 0x10);
    uint32_t fb_phys = fb_bar & 0xFFFFFFF0;

    /* 尝试 BAR0、BAR1、BAR2、BAR5 作为帧缓冲区 */
    if (fb_phys == 0) {
        fb_bar = pci_read_config((uint8_t)bus, dev, func, 0x14);
        fb_phys = fb_bar & 0xFFFFFFF0;
    }
    if (fb_phys == 0) {
        fb_bar = pci_read_config((uint8_t)bus, dev, func, 0x18);
        fb_phys = fb_bar & 0xFFFFFFF0;
    }
    if (fb_phys == 0) {
        fb_bar = pci_read_config((uint8_t)bus, dev, func, 0x24);
        fb_phys = fb_bar & 0xFFFFFFF0;
    }

    amdgpu_dev.fb_size = amdgpu_default_vram;
    amdgpu_dev.fb_base = fb_phys;

    if (fb_phys != 0) {
        uint32_t fb_map_size = (amdgpu_dev.fb_size + 0xFFF) & ~0xFFF;
        amdgpu_dev.framebuffer = (uint32_t *)vmm_map_physical(fb_phys, fb_map_size);
        if (amdgpu_dev.framebuffer) {
            klog_info("[AMDGPU] Framebuffer mapped at 0x%08X (phys: 0x%08X, size: %d MB)",
                      (uint32_t)(uintptr_t)amdgpu_dev.framebuffer,
                      fb_phys, amdgpu_dev.fb_size / (1024 * 1024));
        }
    }

    if (!amdgpu_dev.framebuffer) {
        uint32_t default_w = 1024;
        uint32_t default_h = 768;
        uint32_t fb_alloc_size = default_w * default_h * 4;
        amdgpu_dev.framebuffer = (uint32_t *)kmalloc(fb_alloc_size);
        if (amdgpu_dev.framebuffer) {
            memset(amdgpu_dev.framebuffer, 0, fb_alloc_size);
            amdgpu_dev.fb_size = fb_alloc_size;
            amdgpu_dev.fb_base = (uint32_t)(uintptr_t)amdgpu_dev.framebuffer;
            klog_info("[AMDGPU] Software framebuffer allocated (%d bytes)", fb_alloc_size);
        }
    }

    /* 设置默认模式 */
    amdgpu_dev.bpp = 32;
    amdgpu_dev.width = 1024;
    amdgpu_dev.height = 768;
    amdgpu_dev.pitch = 1024 * 4;
    amdgpu_dev.engine_clock = amdgpu_default_engine_clock;
    amdgpu_dev.memory_clock = amdgpu_default_memory_clock;
    amdgpu_dev.initialized = 1;

    /* 尝试应用默认模式 */
    if (amdgpu_dev.mmio_base) {
        amdgpu_set_mode(1024, 768, 32);
    }

    klog_info("[AMDGPU] Initialization complete: %s", amdgpu_model_name);

    return 0;
}

/* ---- 驱动初始化和关闭 ---- */

int amdgpu_init(void) {
    klog_info("[AMDGPU] Initializing AMD GPU driver...");

    memset(&amdgpu_dev, 0, sizeof(amdgpu_device_t));

    int ret = amdgpu_probe();
    if (ret != 0) {
        klog_info("[AMDGPU] No AMD GPU found, initializing software fallback");
        memset(&amdgpu_dev, 0, sizeof(amdgpu_device_t));
        amdgpu_dev.bpp = 32;
        amdgpu_dev.width = 1024;
        amdgpu_dev.height = 768;
        amdgpu_dev.pitch = 1024 * 4;
        amdgpu_default_vram = 1024 * 768 * 4;
        amdgpu_dev.fb_size = amdgpu_default_vram;
        amdgpu_dev.engine_clock = 500;
        amdgpu_dev.memory_clock = 667;
        amdgpu_model_name = "AMD Radeon (Software)";

        amdgpu_dev.framebuffer = (uint32_t *)kmalloc(amdgpu_dev.fb_size);
        if (amdgpu_dev.framebuffer) {
            memset(amdgpu_dev.framebuffer, 0, amdgpu_dev.fb_size);
        }
        amdgpu_dev.fb_base = (uint32_t)(uintptr_t)amdgpu_dev.framebuffer;
        amdgpu_dev.mmio_base = 0;
        amdgpu_dev.initialized = 1;
    }

    amdgpu_atombios_parse(&amdgpu_dev);
    amdgpu_connector_detect(&amdgpu_dev);
    amdgpu_2d_init(&amdgpu_dev.engine_2d);
    amdgpu_ring_init(&amdgpu_dev.gfx_ring, 64);
    amdgpu_ring_init(&amdgpu_dev.compute_ring, 64);
    amdgpu_vram_config_init(&amdgpu_dev);
    amdgpu_pp_init(&amdgpu_dev);
    amdgpu_thermal_init(&amdgpu_dev);
    amdgpu_dma_init(&amdgpu_dev.sdma0, 64);
    amdgpu_ih_init(&amdgpu_dev);

    if (amdgpu_dev.framebuffer) {
        amdgpu_2d_clear(&amdgpu_dev.engine_2d,
                       amdgpu_dev.fb_base, amdgpu_dev.pitch,
                       amdgpu_dev.width, amdgpu_dev.height, 0xFF000000);
    }

    /* DRM driver callbacks */
    memset(&amdgpu_drm_driver, 0, sizeof(drm_driver_t));
    strcpy(amdgpu_drm_driver.name, "amdgpu");
    strcpy(amdgpu_drm_driver.desc, "AMD Radeon DRM driver");
    amdgpu_drm_driver.pci_vendor = AMD_VENDOR_ID;
    amdgpu_drm_driver.pci_bus = amdgpu_dev.pci_bus;
    amdgpu_drm_driver.pci_dev = amdgpu_dev.pci_slot;
    amdgpu_drm_driver.pci_func = amdgpu_dev.pci_func;
    amdgpu_drm_driver.mmio_base = (void *)amdgpu_dev.mmio_base;
    amdgpu_drm_driver.vram_map = amdgpu_dev.framebuffer;
    amdgpu_drm_driver.vram_size = amdgpu_dev.fb_size;
    amdgpu_drm_driver.driver_private = &amdgpu_dev;

    /* Setup default DRM mode */
    drm_mode_t *mode = &amdgpu_drm_driver.modes[0];
    memset(mode, 0, sizeof(drm_mode_t));
    mode->clock = 148500;
    mode->hdisplay = amdgpu_dev.width;
    mode->hsync_start = mode->hdisplay + 24;
    mode->hsync_end = mode->hsync_start + 136;
    mode->htotal = mode->hsync_end + 160;
    mode->vdisplay = amdgpu_dev.height;
    mode->vsync_start = mode->vdisplay + 3;
    mode->vsync_end = mode->vsync_start + 6;
    mode->vtotal = mode->vsync_end + 29;
    mode->flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC;
    sprintf(mode->name, "%ux%u", amdgpu_dev.width, amdgpu_dev.height);
    amdgpu_drm_driver.mode_count = 1;

    /* Setup one CRTC */
    drm_crtc_t *crtc = &amdgpu_drm_driver.crtcs[0];
    memset(crtc, 0, sizeof(drm_crtc_t));
    crtc->id = 1;
    crtc->width = amdgpu_dev.width;
    crtc->height = amdgpu_dev.height;
    crtc->mode = mode;
    crtc->enabled = 1;
    amdgpu_drm_driver.crtc_count = 1;

    /* Setup one connector */
    drm_connector_t *conn = &amdgpu_drm_driver.connectors[0];
    memset(conn, 0, sizeof(drm_connector_t));
    conn->id = 1;
    conn->type = 11; /* HDMI */
    conn->status = DRM_MODE_CONNECTED;
    conn->modes = mode;
    conn->mode_count = 1;
    amdgpu_drm_driver.connector_count = 1;

    /* Setup one encoder */
    drm_encoder_t *enc = &amdgpu_drm_driver.encoders[0];
    memset(enc, 0, sizeof(drm_encoder_t));
    enc->id = 1;
    enc->type = 2; /* TMDS */
    enc->crtc_id = 1;
    enc->possible_crtcs = 0x1;
    amdgpu_drm_driver.encoder_count = 1;

    /* DRM framebuffer callbacks */
    amdgpu_drm_driver.fb_create = NULL;
    amdgpu_drm_driver.fb_destroy = NULL;

    /* Software acceleration callbacks */
    amdgpu_drm_driver.accel_fill = NULL;
    amdgpu_drm_driver.accel_blit = NULL;
    amdgpu_drm_driver.accel_copy = NULL;

    /* Register with DRM */
    drm_register_driver(&amdgpu_drm_driver);

    klog_info("[AMDGPU] All subsystems initialized");
    return 0;
}

void amdgpu_shutdown(void) {
    if (!amdgpu_dev.initialized) return;

    if (amdgpu_dev.mmio_base) {
        amdgpu_reg_write(AMDGPU_GRPH_ENABLE, 0);
        amdgpu_reg_write(AMDGPU_DC_CRTC_CONTROL, 0);
        amdgpu_reg_write(AMDGPU_IH_RB_CNTL, 0);
        amdgpu_reg_write(AMDGPU_SDMA0_RB_CNTL, 0);
    }

    amdgpu_2d_shutdown(&amdgpu_dev.engine_2d);
    amdgpu_ring_destroy(&amdgpu_dev.gfx_ring);
    amdgpu_ring_destroy(&amdgpu_dev.compute_ring);
    amdgpu_dma_shutdown(&amdgpu_dev.sdma0);
    amdgpu_dma_shutdown(&amdgpu_dev.sdma1);

    if (amdgpu_dev.framebuffer) {
        int is_software_fb = (amdgpu_dev.mmio_base == 0) ||
                             (amdgpu_dev.fb_base == (uint32_t)(uintptr_t)amdgpu_dev.framebuffer);
        if (is_software_fb) {
            kfree(amdgpu_dev.framebuffer);
        } else {
            uint32_t fb_map_size = (amdgpu_dev.fb_size + 0xFFF) & ~0xFFF;
            vmm_unmap_physical(amdgpu_dev.framebuffer, fb_map_size);
        }
        amdgpu_dev.framebuffer = (void *)0;
    }

    if (amdgpu_dev.mmio_base) {
        vmm_unmap_physical((void *)amdgpu_dev.mmio_base, 0x80000);
        amdgpu_dev.mmio_base = 0;
    }

    if (amdgpu_dev.sdma0.pending) {
        kfree(amdgpu_dev.sdma0.pending);
        amdgpu_dev.sdma0.pending = NULL;
    }
    if (amdgpu_dev.sdma1.pending) {
        kfree(amdgpu_dev.sdma1.pending);
        amdgpu_dev.sdma1.pending = NULL;
    }

    amdgpu_model_name = "";
    amdgpu_default_vram = 0;
    amdgpu_default_engine_clock = 0;
    amdgpu_default_memory_clock = 0;
    memset(&amdgpu_dev, 0, sizeof(amdgpu_device_t));

    klog_info("[AMDGPU] Driver shutdown complete");
}

/* ================================================================ */
/*  1. AtomBIOS 解析                                                   */
/* ================================================================ */

static atom_bios_info_t g_atombios_info;

static int atombios_find_signature(uint8_t *rom_base, uint32_t rom_size) {
    if (rom_size < 2) return -1;
    if (rom_base[0] == 0x55 && rom_base[1] == 0xAA) return 0;
    return -1;
}

static int atombios_read_string(uint8_t *rom_base, uint32_t offset, char *out, uint32_t out_size) {
    uint32_t i;
    for (i = 0; i < out_size - 1; i++) {
        uint8_t c = rom_base[offset + i];
        if (c == 0 || c == 0xFF) break;
        if (c < 0x20 || c > 0x7E) { out[i] = '.'; continue; }
        out[i] = (char)c;
    }
    out[i] = '\0';
    return 0;
}

static inline uint32_t atombios_read32(uint8_t *rom, uint32_t off) {
    return (uint32_t)rom[off] | ((uint32_t)rom[off+1] << 8) |
           ((uint32_t)rom[off+2] << 16) | ((uint32_t)rom[off+3] << 24);
}

static inline uint16_t atombios_read16(uint8_t *rom, uint32_t off) {
    return (uint16_t)(rom[off] | ((uint16_t)rom[off+1] << 8));
}

int amdgpu_atombios_parse(amdgpu_device_t *dev) {
    if (!dev) return -1;
    memset(&g_atombios_info, 0, sizeof(g_atombios_info));

    g_atombios_info.valid = 1;
    strcpy(g_atombios_info.gpu_name, "AMD Radeon");
    g_atombios_info.vram_size = dev->fb_size;
    g_atombios_info.max_engine_clock = dev->engine_clock;
    g_atombios_info.max_memory_clock = dev->memory_clock;
    g_atombios_info.pci_vendor_id = AMD_VENDOR_ID;
    g_atombios_info.core_count = 8;
    g_atombios_info.cu_count = 4;
    g_atombios_info.rop_count = 4;
    g_atombios_info.tmu_count = 16;

    if (dev->mmio_base) {
        uint8_t *rom = (uint8_t *)(dev->mmio_base + AMDGPU_ATOMBIOS_ROM_BASE);
        int rom_found = 0;
        if (atombios_find_signature(rom, AMDGPU_ATOMBIOS_ROM_SIZE) == 0) {
            rom_found = 1;
        } else {
            rom = (uint8_t *)(dev->mmio_base + 0xC0000);
            if (atombios_find_signature(rom, 0x10000) == 0) {
                rom_found = 1;
            }
        }

        if (rom_found) {
            uint16_t pci_offset = atombios_read16(rom, 0x18);
            if (pci_offset > 0 && pci_offset < 0x10000) {
                g_atombios_info.pci_vendor_id = atombios_read16(rom, pci_offset + 4);
                g_atombios_info.pci_device_id = atombios_read16(rom, pci_offset + 6);
            }

            uint32_t name_offset = atombios_read16(rom, 0x2E);
            if (name_offset > 0 && name_offset < 0x10000) {
                char name_buf[32];
                atombios_read_string(rom, name_offset, name_buf, 32);
                if (name_buf[0] != '\0') {
                    strcpy(g_atombios_info.gpu_name, name_buf);
                }
            }

            uint16_t mdt_offset = atombios_read16(rom, AMDGPU_ATOMBIOS_TABLE_OFFSET);
            if (mdt_offset > 0 && mdt_offset < 0x10000) {
                g_atombios_info.gpu_chip_id = atombios_read32(rom, mdt_offset);
                uint32_t clk = atombios_read32(rom, mdt_offset + 8);
                if (clk > 0) g_atombios_info.max_engine_clock = clk;
                clk = atombios_read32(rom, mdt_offset + 12);
                if (clk > 0) g_atombios_info.max_memory_clock = clk;
                uint32_t vram = atombios_read32(rom, mdt_offset + 16);
                if (vram > 0) g_atombios_info.vram_size = vram;
                g_atombios_info.vram_type = rom[mdt_offset + 20];
                uint16_t cores = atombios_read16(rom, mdt_offset + 22);
                if (cores > 0) g_atombios_info.core_count = cores;
                if (rom[mdt_offset + 24] > 0) g_atombios_info.cu_count = rom[mdt_offset + 24];
                if (rom[mdt_offset + 25] > 0) g_atombios_info.rop_count = rom[mdt_offset + 25];
                if (rom[mdt_offset + 26] > 0) g_atombios_info.tmu_count = rom[mdt_offset + 26];
            }
        }
    }

    klog_info("[AMDGPU] AtomBIOS: %s (vram=%dMB)",
              g_atombios_info.gpu_name, g_atombios_info.vram_size / (1024*1024));

    memcpy(&dev->atombios, &g_atombios_info, sizeof(atom_bios_info_t));
    return 0;
}

int amdgpu_atombios_get_clocks(amdgpu_device_t *dev) {
    if (!dev || !g_atombios_info.valid) return -1;

    uint8_t *rom = (uint8_t *)(dev->mmio_base + AMDGPU_ATOMBIOS_ROM_BASE);
    uint32_t clock_off = atombios_read16(rom, AMDGPU_ATOMBIOS_CLOCK_TABLE);

    if (clock_off > 0 && clock_off < 0x10000) {
        uint32_t count = rom[clock_off] & 0x1F;
        if (count > 16) count = 16;
        g_atombios_info.sclk_count = count;
        g_atombios_info.mclk_count = count;

        for (uint32_t i = 0; i < count; i++) {
            g_atombios_info.sclk_table[i] = atombios_read32(rom, clock_off + 4 + i * 8);
            g_atombios_info.mclk_table[i] = atombios_read32(rom, clock_off + 8 + i * 8);
        }
        dev->atombios.sclk_count = count;
        dev->atombios.mclk_count = count;
        for (uint32_t i = 0; i < count; i++) {
            dev->atombios.sclk_table[i] = g_atombios_info.sclk_table[i];
            dev->atombios.mclk_table[i] = g_atombios_info.mclk_table[i];
        }
    }
    return 0;
}

int amdgpu_atombios_get_connectors(amdgpu_device_t *dev) {
    if (!dev || !g_atombios_info.valid) return -1;

    uint8_t *rom = (uint8_t *)(dev->mmio_base + AMDGPU_ATOMBIOS_ROM_BASE);
    uint32_t conn_off = atombios_read16(rom, AMDGPU_ATOMBIOS_CONNECTOR_TABLE);

    if (conn_off > 0 && conn_off < 0x10000) {
        uint32_t count = rom[conn_off];
        if (count > 8) count = 8;
        g_atombios_info.connector_count = count;
        for (uint32_t i = 0; i < count; i++) {
            g_atombios_info.connector_types[i] = rom[conn_off + 1 + i * 2];
        }
        dev->atombios.connector_count = count;
        for (uint32_t i = 0; i < count; i++) {
            dev->atombios.connector_types[i] = g_atombios_info.connector_types[i];
        }
    }
    return 0;
}

const atom_bios_info_t *amdgpu_atombios_get_info(void) {
    return g_atombios_info.valid ? &g_atombios_info : NULL;
}

/* ================================================================ */
/*  2. 显示输出 - DisplayPort / HDMI / DVI                              */
/* ================================================================ */

static void amdgpu_i2c_start(uint32_t i2c_channel) {
    amdgpu_reg_write(AMDGPU_DC_I2C_CONTROL, (1 << 0) | (i2c_channel << 4));
    io_wait();
    amdgpu_reg_write(AMDGPU_DC_I2C_CONTROL, (2 << 0) | (i2c_channel << 4));
    io_wait();
}

static void amdgpu_i2c_stop(uint32_t i2c_channel) {
    amdgpu_reg_write(AMDGPU_DC_I2C_CONTROL, (3 << 0) | (i2c_channel << 4));
    io_wait();
}

static int amdgpu_i2c_write_byte(uint32_t i2c_channel, uint8_t data) {
    uint32_t timeout;
    amdgpu_reg_write(AMDGPU_DC_I2C_DATA, data);
    amdgpu_reg_write(AMDGPU_DC_I2C_CONTROL, (4 << 0) | (i2c_channel << 4));

    for (timeout = 0; timeout < 1000; timeout++) {
        uint32_t status = amdgpu_reg_read(AMDGPU_DC_I2C_STATUS);
        if (status & 0x01) return 0; /* ACK */
        if (status & 0x02) break;    /* NACK */
        io_wait();
    }
    return -1;
}

static uint8_t amdgpu_i2c_read_byte(uint32_t i2c_channel, int last) {
    uint32_t cmd = last ? (6 << 0) : (5 << 0);
    amdgpu_reg_write(AMDGPU_DC_I2C_CONTROL, cmd | (i2c_channel << 4));

    uint32_t timeout;
    for (timeout = 0; timeout < 1000; timeout++) {
        uint32_t status = amdgpu_reg_read(AMDGPU_DC_I2C_STATUS);
        if (status & 0x10) break; /* data ready */
        io_wait();
    }
    return (uint8_t)(amdgpu_reg_read(AMDGPU_DC_I2C_DATA) & 0xFF);
}

int amdgpu_edid_read(amdgpu_device_t *dev, uint32_t connector_idx) {
    if (!dev || connector_idx >= 8) return -1;
    amdgpu_connector_t *conn = &dev->connectors[connector_idx];
    if (!conn->plugged) return -1;

    uint32_t i2c_ch = conn->i2c_channel;
    uint8_t *edid = conn->edid;

    /* EDID I2C address = 0x50 (0xA0 with R/W bit) */
    amdgpu_i2c_start(i2c_ch);
    if (amdgpu_i2c_write_byte(i2c_ch, 0xA0) < 0) {
        amdgpu_i2c_stop(i2c_ch);
        return -1;
    }
    amdgpu_i2c_write_byte(i2c_ch, 0x00); /* start offset */

    /* Restart for read */
    amdgpu_i2c_start(i2c_ch);
    if (amdgpu_i2c_write_byte(i2c_ch, 0xA1) < 0) {
        amdgpu_i2c_stop(i2c_ch);
        return -1;
    }

    for (uint32_t i = 0; i < 128; i++) {
        edid[i] = amdgpu_i2c_read_byte(i2c_ch, (i == 127) ? 1 : 0);
    }
    amdgpu_i2c_stop(i2c_ch);

    /* Verify EDID header (00 FF FF FF FF FF FF 00) */
    if (edid[0] == 0x00 && edid[1] == 0xFF && edid[2] == 0xFF &&
        edid[3] == 0xFF && edid[4] == 0xFF && edid[5] == 0xFF &&
        edid[6] == 0xFF && edid[7] == 0x00) {
        conn->edid_valid = 1;

        /* Extract monitor name from descriptor blocks */
        for (uint32_t d = 0; d < 4; d++) {
            uint32_t base = 54 + d * 18;
            if (edid[base] == 0x00 && edid[base+1] == 0x00 &&
                edid[base+2] == 0x00 && edid[base+3] == 0xFC) {
                for (uint32_t j = 0; j < 13; j++) {
                    uint8_t c = edid[base + 5 + j];
                    if (c == 0x0A || c == 0x00) break;
                    conn->monitor_name[j] = (c >= 0x20 && c <= 0x7E) ? (char)c : ' ';
                }
                break;
            }
        }

        /* Preferred timing from first detailed timing descriptor */
        uint32_t pixclk_khz = ((uint32_t)edid[54] | ((uint32_t)edid[55] << 8)) * 10;
        conn->pref_width  = ((uint32_t)(edid[58] & 0xF0) << 4) | edid[56];
        conn->pref_height = ((uint32_t)(edid[61] & 0xF0) << 4) | edid[59];
        conn->pref_refresh = (uint32_t)(pixclk_khz * 1000) /
                             (uint32_t)((edid[57] << 8 | edid[56]) *
                                       (edid[60] << 8 | edid[59]));
        conn->max_pixel_clock = pixclk_khz;
        conn->max_hres = conn->pref_width;
        conn->max_vres = conn->pref_height;
        conn->max_refresh = conn->pref_refresh;
    }

    return conn->edid_valid ? 0 : -1;
}

int amdgpu_dp_link_train(amdgpu_device_t *dev, uint32_t connector_idx) {
    if (!dev || connector_idx >= dev->connector_count) return -1;
    amdgpu_connector_t *conn = &dev->connectors[connector_idx];
    if (!conn->dpcd_capable || !conn->plugged) return -1;

    /* Start link training pattern 1 */
    amdgpu_reg_write(AMDGPU_DC_DP_CONFIG, 0x01);
    amdgpu_reg_write(AMDGPU_DC_DP_LANE_COUNT, 4);  /* 4 lanes */
    amdgpu_reg_write(AMDGPU_DC_DP_LINK_RATE, 0x06); /* HBR2 (5.4 Gbps) */

    uint32_t retry;
    int trained = 0;
    for (retry = 0; retry < 5 && !trained; retry++) {
        amdgpu_reg_write(AMDGPU_DC_DP_LINK_TRAINING, 0x01); /* clock recovery */

        uint32_t timeout;
        for (timeout = 0; timeout < 1000; timeout++) {
            uint32_t status = amdgpu_reg_read(AMDGPU_DC_DP_LINK_TRAINING);
            if (status & 0x100) { trained = 1; break; }
            if (status & 0x200) break; /* lane failure */
            io_wait();
        }
        if (trained) break;

        /* Fallback: reduce lane count */
        uint32_t lanes = amdgpu_reg_read(AMDGPU_DC_DP_LANE_COUNT);
        if (lanes > 1) {
            lanes--;
            amdgpu_reg_write(AMDGPU_DC_DP_LANE_COUNT, lanes);
        }
    }

    if (!trained) {
        klog_warn("[AMDGPU] DP link training failed on connector %d", connector_idx);
        return -1;
    }

    klog_info("[AMDGPU] DP link trained successfully on connector %d", connector_idx);
    return 0;
}

int amdgpu_hdmi_configure(amdgpu_device_t *dev, uint32_t connector_idx) {
    if (!dev || connector_idx >= dev->connector_count) return -1;
    amdgpu_connector_t *conn = &dev->connectors[connector_idx];
    if (!conn->hdmi_capable || !conn->plugged) return -1;

    /* Enable HDMI mode */
    amdgpu_reg_write(AMDGPU_DC_HDMI_CONFIG, 0x03); /* HDMI + TMDS */

    /* Write AVI InfoFrame */
    uint32_t iframe[8];
    iframe[0] = 0x820D0282; /* packet type=0x82, version=2, length=13 */
    iframe[1] = 0;          /* S=0, B=0, A=0, Y=0 */
    iframe[2] = 0;          /* C=0, M=0, R=0... */
    iframe[3] = 0;
    iframe[4] = 0;
    iframe[5] = 0;
    iframe[6] = 0;
    iframe[7] = 0;

    for (int i = 0; i < 8; i++) {
        amdgpu_reg_write(AMDGPU_DC_HDMI_INFOFRAME + i, iframe[i]);
    }

    /* Enable HDMI audio */
    amdgpu_reg_write(AMDGPU_DC_HDMI_AUDIO, 0x01);

    klog_info("[AMDGPU] HDMI configured on connector %d", connector_idx);
    return 0;
}

int amdgpu_dvi_configure(amdgpu_device_t *dev, uint32_t connector_idx) {
    if (!dev || connector_idx >= dev->connector_count) return -1;
    amdgpu_connector_t *conn = &dev->connectors[connector_idx];
    if (conn->type != AMDGPU_CONNECTOR_DVI_I &&
        conn->type != AMDGPU_CONNECTOR_DVI_D &&
        conn->type != AMDGPU_CONNECTOR_DVI_A) return -1;
    if (!conn->plugged) return -1;

    /* Configure TMDS or analog output */
    uint32_t mode = conn->type == AMDGPU_CONNECTOR_DVI_A ? 0x01 : 0x00;
    if (conn->dvi_dual_link) mode |= 0x02;
    amdgpu_reg_write(AMDGPU_DC_DVI_CONFIG, mode);

    klog_info("[AMDGPU] DVI configured on connector %d (dual_link=%d)",
              connector_idx, conn->dvi_dual_link);
    return 0;
}

int amdgpu_connector_detect(amdgpu_device_t *dev) {
    if (!dev) return -1;

    dev->connector_count = 0;

    for (int i = 0; i < 4 && dev->connector_count < 8; i++) {
        amdgpu_connector_t *conn = &dev->connectors[dev->connector_count];
        memset(conn, 0, sizeof(amdgpu_connector_t));

        if (i == 0) {
            conn->type = AMDGPU_CONNECTOR_eDP;
            conn->plugged = 1;
        } else if (i == 1) {
            conn->type = AMDGPU_CONNECTOR_HDMI_A;
            conn->hdmi_capable = 1;
        } else if (i == 2) {
            conn->type = AMDGPU_CONNECTOR_DisplayPort;
            conn->dpcd_capable = 1;
        } else {
            conn->type = AMDGPU_CONNECTOR_DVI_D;
            conn->dvi_dual_link = 1;
        }

        conn->i2c_channel = i;
        conn->hpd_pin = i;
        conn->max_pixel_clock = 600000;
        conn->max_hres = 3840;
        conn->max_vres = 2160;
        conn->max_refresh = 144;
        conn->pref_width = 1920;
        conn->pref_height = 1080;
        conn->pref_refresh = 60;
        strcpy(conn->monitor_name, "Virtual Display");

        if (dev->mmio_base) {
            uint32_t hpd_ctrl = amdgpu_reg_read(AMDGPU_DC_HPD_CONTROL);
            conn->plugged = ((hpd_ctrl >> (i + 8)) & 0x01) ? 1 : (i == 0 ? 1 : 0);

            if (conn->type == AMDGPU_CONNECTOR_DisplayPort || conn->type == AMDGPU_CONNECTOR_eDP) {
                conn->dpcd_capable = 1;
            }
            if (conn->type == AMDGPU_CONNECTOR_HDMI_A || conn->type == AMDGPU_CONNECTOR_HDMI_B) {
                conn->hdmi_capable = 1;
            }
        }

        if (i == 0) {
            conn->plugged = 1;
            conn->edid_valid = 1;
        }

        if (conn->plugged) {
            dev->connector_count++;
        }
    }

    if (dev->connector_count == 0) {
        amdgpu_connector_t *conn = &dev->connectors[0];
        memset(conn, 0, sizeof(amdgpu_connector_t));
        conn->type = AMDGPU_CONNECTOR_eDP;
        conn->plugged = 1;
        conn->edid_valid = 1;
        conn->i2c_channel = 0;
        conn->hpd_pin = 0;
        conn->max_pixel_clock = 600000;
        conn->max_hres = 3840;
        conn->max_vres = 2160;
        conn->max_refresh = 144;
        conn->pref_width = 1920;
        conn->pref_height = 1080;
        conn->pref_refresh = 60;
        strcpy(conn->monitor_name, "Default Display");
        dev->connector_count = 1;
    }

    klog_info("[AMDGPU] Detected %d connected displays", dev->connector_count);
    return (int)dev->connector_count;
}

int amdgpu_connector_set_mode(amdgpu_device_t *dev, uint32_t connector_idx,
                               uint32_t width, uint32_t height, uint32_t bpp, uint32_t refresh) {
    if (!dev || connector_idx >= dev->connector_count) return -1;

    amdgpu_connector_t *conn = &dev->connectors[connector_idx];
    if (!conn->plugged) return -1;

    /* Configure connector-specific settings */
    if (conn->dpcd_capable) {
        amdgpu_dp_link_train(dev, connector_idx);
    } else if (conn->hdmi_capable) {
        amdgpu_hdmi_configure(dev, connector_idx);
    } else {
        amdgpu_dvi_configure(dev, connector_idx);
    }

    /* Set CRTC mode */
    amdgpu_set_mode(width, height, bpp);

    klog_info("[AMDGPU] Connector %d set to %dx%d@%d %dbpp",
              connector_idx, width, height, refresh, bpp);
    return 0;
}

/* ================================================================ */
/*  3. GPU 环形缓冲区管理                                              */
/* ================================================================ */

int amdgpu_ring_init(amdgpu_ring_t *ring, uint32_t size_kb) {
    if (!ring) return -1;

    uint32_t actual_size = size_kb * 1024;
    ring->base = (uint32_t *)kmalloc(actual_size);
    if (!ring->base) return -1;

    memset(ring->base, 0, actual_size);
    ring->size = actual_size / 4; /* in dwords */
    ring->rptr = 0;
    ring->wptr = 0;
    ring->align_mask = 0xF; /* 64-byte alignment */
    ring->ready = 1;

    /* Allocate doorbell page */
    ring->doorbell_ptr = (uint32_t *)kmalloc(4096);
    if (ring->doorbell_ptr) {
        memset(ring->doorbell_ptr, 0, 4096);
    }

    klog_info("[AMDGPU] Ring buffer initialized: %dKB at 0x%08X",
              size_kb, (uint32_t)(uintptr_t)ring->base);
    return 0;
}

int amdgpu_ring_submit(amdgpu_ring_t *ring, const uint32_t *entries, uint32_t count) {
    if (!ring || !ring->ready || !entries || count == 0) return -1;

    /* Wait for space */
    uint32_t free_slots = ring->size - (ring->wptr - ring->rptr);
    if (free_slots < count * 4) {
        int waited = amdgpu_ring_wait(ring, 100);
        if (waited < 0) return -1;
    }

    /* Copy entries to ring */
    for (uint32_t i = 0; i < count * 4; i++) {
        ring->base[ring->wptr] = entries[i];
        ring->wptr = (ring->wptr + 1) % ring->size;
    }

    /* Ring doorbell to notify GPU */
    amdgpu_ring_doorbell(ring);

    return 0;
}

int amdgpu_ring_wait(amdgpu_ring_t *ring, uint32_t timeout_ms) {
    if (!ring || !ring->ready) return -1;
    (void)timeout_ms;

    /* In a real implementation, we'd wait for GPU to advance rptr.
     * Here we simulate by checking the doorbell response. */
    uint32_t deadline = 0; /* timer_get_ticks() * 10 + timeout_ms; */
    while (ring->wptr == ring->rptr) {
        /* if (timer_get_ticks() * 10 > deadline) return -1; */
        io_wait();
    }
    return 0;
}

void amdgpu_ring_destroy(amdgpu_ring_t *ring) {
    if (!ring) return;
    if (ring->base) { kfree(ring->base); ring->base = NULL; }
    if (ring->doorbell_ptr) { kfree(ring->doorbell_ptr); ring->doorbell_ptr = NULL; }
    ring->size = 0;
    ring->ready = 0;
}

int amdgpu_ib_create(amdgpu_ib_t *ib, uint32_t size) {
    if (!ib || size == 0 || size > AMDGPU_IB_MAX_SIZE) return -1;
    ib->cpu_addr = (uint32_t *)kmalloc(size);
    if (!ib->cpu_addr) return -1;
    ib->size = size;
    ib->used = 0;
    /* GPU address would be set by the MMU/VMM layer */
    ib->gpu_addr = (uint32_t)(uintptr_t)ib->cpu_addr;
    return 0;
}

int amdgpu_ib_submit(amdgpu_ring_t *ring, amdgpu_ib_t *ib) {
    if (!ring || !ib || ib->used == 0) return -1;

    uint32_t entries[8];
    memset(entries, 0, sizeof(entries));
    /* IB packet: type=INDIRECT_BUFFER, count=number of dwords */
    entries[0] = (0x01 << 30) | ((ib->used / 4) & 0x3FFFFF);
    entries[1] = ib->gpu_addr;      /* IB GPU address lo */
    entries[2] = 0;                  /* IB GPU address hi */
    entries[3] = 0;                  /* VMID */
    /* Padding */
    entries[4] = 0; entries[5] = 0; entries[6] = 0; entries[7] = 0;

    int ret = amdgpu_ring_submit(ring, entries, 2);
    if (ret == 0) {
        /* In real HW, the CPU marks the fence after GPU completion.
         * Here we only bump the write-pointer for the simulated ring. */
    }
    return ret;
}

void amdgpu_ring_doorbell(amdgpu_ring_t *ring) {
    if (!ring || !ring->doorbell_ptr) return;

    /* Write wptr to doorbell to notify GPU */
    *(volatile uint32_t *)(ring->doorbell_ptr + ring->doorbell_offset / 4) = ring->wptr;

    /* MFENCE to ensure write is visible */
    __asm__ volatile("mfence" ::: "memory");
}

/* ================================================================ */
/*  4. 2D 加速引擎                                                     */
/* ================================================================ */

int amdgpu_2d_init(amdgpu_2d_engine_t *engine) {
    if (!engine) return -1;

    engine->command_buffer_size = 4096;
    engine->command_buffer = (uint32_t *)kmalloc(engine->command_buffer_size * 4);
    if (!engine->command_buffer) return -1;

    memset(engine->command_buffer, 0, engine->command_buffer_size * 4);
    engine->command_count = 0;
    engine->op_count = 0;
    engine->bytes_copied = 0;
    engine->pixels_filled = 0;
    engine->lines_drawn = 0;
    engine->busy = 0;
    engine->initialized = 1;

    klog_info("[AMDGPU] 2D acceleration engine initialized");
    return 0;
}

static int amdgpu_2d_wait_idle(amdgpu_2d_engine_t *engine) {
    if (!engine || !amdgpu_dev.mmio_base) return -1;

    uint32_t timeout;
    for (timeout = 0; timeout < 10000; timeout++) {
        uint32_t status = amdgpu_reg_read(AMDGPU_2D_STATUS);
        if (!(status & 0x01)) { /* busy bit */
            engine->busy = 0;
            return 0;
        }
        io_wait();
    }
    return -1;
}

int amdgpu_2d_bitblt(amdgpu_2d_engine_t *engine,
                     uint32_t dst_addr, uint32_t dst_pitch, uint32_t dst_x, uint32_t dst_y,
                     uint32_t src_addr, uint32_t src_pitch, uint32_t src_x, uint32_t src_y,
                     uint32_t w, uint32_t h, uint8_t rop) {
    if (!engine || !engine->initialized) return -1;

    engine->busy = 1;
    engine->op_count++;

    if (amdgpu_dev.framebuffer) {
        uint32_t *fb = amdgpu_dev.framebuffer;
        uint32_t dst_pitch_dw = dst_pitch / 4;
        uint32_t src_pitch_dw = src_pitch / 4;

        uint32_t row, col;
        for (row = 0; row < h; row++) {
            uint32_t sy = src_y + row;
            uint32_t dy = dst_y + row;
            if (sy >= amdgpu_dev.height || dy >= amdgpu_dev.height) break;
            for (col = 0; col < w; col++) {
                uint32_t sx = src_x + col;
                uint32_t dx = dst_x + col;
                if (sx >= amdgpu_dev.width || dx >= amdgpu_dev.width) break;

                uint32_t src_pixel = fb[sy * src_pitch_dw + sx];
                uint32_t *dst_pixel = &fb[dy * dst_pitch_dw + dx];

                switch (rop) {
                case AMDGPU_ROP3_SRCCOPY:
                    *dst_pixel = src_pixel;
                    break;
                case AMDGPU_ROP3_SRCPAINT:
                    *dst_pixel = src_pixel | *dst_pixel;
                    break;
                case AMDGPU_ROP3_SRCAND:
                    *dst_pixel = src_pixel & *dst_pixel;
                    break;
                case AMDGPU_ROP3_SRCINVERT:
                    *dst_pixel = src_pixel ^ *dst_pixel;
                    break;
                case AMDGPU_ROP3_BLACKNESS:
                    *dst_pixel = 0xFF000000;
                    break;
                case AMDGPU_ROP3_WHITENESS:
                    *dst_pixel = 0xFFFFFFFF;
                    break;
                default:
                    *dst_pixel = src_pixel;
                    break;
                }
            }
        }
        engine->bytes_copied += w * h * 4;
    }

    if (amdgpu_dev.mmio_base) {
        amdgpu_reg_write(AMDGPU_2D_SRC_ADDR, src_addr);
        amdgpu_reg_write(AMDGPU_2D_SRC_PITCH, src_pitch);
        amdgpu_reg_write(AMDGPU_2D_SRC_X, src_x);
        amdgpu_reg_write(AMDGPU_2D_SRC_Y, src_y);
        amdgpu_reg_write(AMDGPU_2D_DST_ADDR, dst_addr);
        amdgpu_reg_write(AMDGPU_2D_DST_PITCH, dst_pitch);
        amdgpu_reg_write(AMDGPU_2D_DST_X, dst_x);
        amdgpu_reg_write(AMDGPU_2D_DST_Y, dst_y);
        amdgpu_reg_write(AMDGPU_2D_WIDTH, w);
        amdgpu_reg_write(AMDGPU_2D_HEIGHT, h);
        amdgpu_reg_write(AMDGPU_2D_ROP3, rop);
        amdgpu_reg_write(AMDGPU_2D_COMMAND, AMDGPU_2D_CMD_BITBLT);
    }

    engine->busy = 0;
    return 0;
}

int amdgpu_2d_solid_fill(amdgpu_2d_engine_t *engine,
                         uint32_t dst_addr, uint32_t dst_pitch,
                         uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                         uint32_t color) {
    if (!engine || !engine->initialized) return -1;

    engine->busy = 1;
    engine->op_count++;

    if (amdgpu_dev.framebuffer) {
        uint32_t *fb = amdgpu_dev.framebuffer;
        uint32_t dst_pitch_dw = dst_pitch / 4;

        uint32_t row, col;
        uint32_t end_y = y + h;
        uint32_t end_x = x + w;
        if (end_y > amdgpu_dev.height) end_y = amdgpu_dev.height;
        if (end_x > amdgpu_dev.width) end_x = amdgpu_dev.width;

        for (row = y; row < end_y; row++) {
            uint32_t *line = &fb[row * dst_pitch_dw + x];
            for (col = x; col < end_x; col++) {
                *line++ = color;
            }
        }
        engine->pixels_filled += (end_y - y) * (end_x - x);
    }

    if (amdgpu_dev.mmio_base) {
        amdgpu_reg_write(AMDGPU_2D_DST_ADDR, dst_addr);
        amdgpu_reg_write(AMDGPU_2D_DST_PITCH, dst_pitch);
        amdgpu_reg_write(AMDGPU_2D_DST_X, x);
        amdgpu_reg_write(AMDGPU_2D_DST_Y, y);
        amdgpu_reg_write(AMDGPU_2D_WIDTH, w);
        amdgpu_reg_write(AMDGPU_2D_HEIGHT, h);
        amdgpu_reg_write(AMDGPU_2D_FILL_COLOR, color);
        amdgpu_reg_write(AMDGPU_2D_COMMAND, AMDGPU_2D_CMD_SOLID_FILL);
    }

    engine->busy = 0;
    return 0;
}

int amdgpu_2d_line_draw(amdgpu_2d_engine_t *engine,
                        uint32_t dst_addr, uint32_t dst_pitch,
                        int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                        uint32_t color, uint32_t thickness) {
    if (!engine || !engine->initialized) return -1;

    engine->busy = 1;
    engine->op_count++;
    engine->lines_drawn++;

    if (amdgpu_dev.framebuffer) {
        uint32_t *fb = amdgpu_dev.framebuffer;
        uint32_t pitch_dw = dst_pitch / 4;

        int32_t dx = x1 - x0;
        int32_t dy = y1 - y0;
        int32_t sx = (dx >= 0) ? 1 : -1;
        int32_t sy = (dy >= 0) ? 1 : -1;
        dx = (dx >= 0) ? dx : -dx;
        dy = (dy >= 0) ? dy : -dy;

        int32_t err = dx - dy;
        uint32_t pixels = 0;

        int32_t t = (thickness > 0) ? (int32_t)thickness : 1;

        while (1) {
            if (x0 >= 0 && x0 < (int32_t)amdgpu_dev.width &&
                y0 >= 0 && y0 < (int32_t)amdgpu_dev.height) {
                if (t <= 1) {
                    fb[y0 * pitch_dw + x0] = color;
                    pixels++;
                } else {
                    for (int32_t ty = -t/2; ty <= t/2; ty++) {
                        for (int32_t tx = -t/2; tx <= t/2; tx++) {
                            int32_t px = x0 + tx;
                            int32_t py = y0 + ty;
                            if (px >= 0 && px < (int32_t)amdgpu_dev.width &&
                                py >= 0 && py < (int32_t)amdgpu_dev.height) {
                                fb[py * pitch_dw + px] = color;
                                pixels++;
                            }
                        }
                    }
                }
            }

            if (x0 == x1 && y0 == y1) break;

            int32_t e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 < dx) { err += dx; y0 += sy; }
        }
        engine->pixels_filled += pixels;
    }

    if (amdgpu_dev.mmio_base) {
        amdgpu_reg_write(AMDGPU_2D_DST_ADDR, dst_addr);
        amdgpu_reg_write(AMDGPU_2D_DST_PITCH, dst_pitch);
        amdgpu_reg_write(AMDGPU_2D_FILL_COLOR, color);
        amdgpu_reg_write(AMDGPU_2D_LINE_X0, (uint32_t)x0);
        amdgpu_reg_write(AMDGPU_2D_LINE_Y0, (uint32_t)y0);
        amdgpu_reg_write(AMDGPU_2D_LINE_X1, (uint32_t)x1);
        amdgpu_reg_write(AMDGPU_2D_LINE_Y1, (uint32_t)y1);
        amdgpu_reg_write(AMDGPU_2D_BRESENHAM_CTL, (thickness & 0xFF) | 0x100);
        amdgpu_reg_write(AMDGPU_2D_COMMAND, AMDGPU_2D_CMD_LINE_DRAW);
    }

    engine->busy = 0;
    return 0;
}

int amdgpu_2d_clear(amdgpu_2d_engine_t *engine,
                    uint32_t dst_addr, uint32_t dst_pitch,
                    uint32_t w, uint32_t h, uint32_t color) {
    return amdgpu_2d_solid_fill(engine, dst_addr, dst_pitch, 0, 0, w, h, color);
}

void amdgpu_2d_shutdown(amdgpu_2d_engine_t *engine) {
    if (!engine || !engine->initialized) return;
    amdgpu_2d_wait_idle(engine);
    if (engine->command_buffer) {
        kfree(engine->command_buffer);
        engine->command_buffer = NULL;
    }
    engine->initialized = 0;
}

/* ================================================================ */
/*  5. VRAM 带宽优化                                                    */
/* ================================================================ */

int amdgpu_vram_config_init(amdgpu_device_t *dev) {
    if (!dev) return -1;

    amdgpu_vram_config_t *cfg = &dev->vram_config;
    memset(cfg, 0, sizeof(amdgpu_vram_config_t));

    cfg->channel_count = 2;
    cfg->bank_count = 4;
    cfg->num_banks = 16;
    cfg->tile_mode = 0;
    cfg->macro_tile_mode = 0;
    cfg->micro_tile_mode = 0;
    cfg->surface_alignment = AMDGPU_SURF_ALIGN_LINEAR;
    cfg->tiling_enabled = 0;
    cfg->dcc_enabled = 0;
    cfg->bandwidth_optimized = 0;
    cfg->bank_width = 1;
    cfg->bank_height = 1;

    if (dev->mmio_base) {
        uint32_t mc_config = amdgpu_reg_read(AMDGPU_VRAM_MC_CONFIG);
        if (mc_config != 0xFFFFFFFF) {
            cfg->channel_count = (mc_config & 0x0F) + 1;
            cfg->bank_count = ((mc_config >> 4) & 0x0F) + 1;
            cfg->num_banks = ((mc_config >> 8) & 0x1F) + 1;
        }

        amdgpu_reg_write(AMDGPU_VRAM_TILING_CFG, cfg->tile_mode);
        amdgpu_reg_write(AMDGPU_VRAM_SURFACE_ALIGN, cfg->surface_alignment);
        amdgpu_reg_write(AMDGPU_VRAM_BANDWIDTH_CTL, 0);
    }

    klog_info("[AMDGPU] VRAM config: %d channels, %d banks, %d bank groups (linear mode)",
              cfg->channel_count, cfg->bank_count, cfg->num_banks);
    return 0;
}

int amdgpu_vram_set_tiling(amdgpu_device_t *dev, uint32_t mode) {
    if (!dev || !dev->mmio_base) return -1;
    if (mode > 4) return -1;

    dev->vram_config.tile_mode = mode;
    dev->vram_config.tiling_enabled = (mode > 0) ? 1 : 0;

    uint32_t tile_config = amdgpu_reg_read(AMDGPU_VRAM_TILING_CFG);
    tile_config &= ~0x07;
    tile_config |= (mode & 0x07);

    /* Configure tile dimensions based on mode */
    switch (mode) {
    case 0: /* Linear */
        tile_config |= (0x00 << 4);
        dev->vram_config.bank_width = 1;
        dev->vram_config.bank_height = 1;
        break;
    case 1: /* 256B micro-tiling */
        tile_config |= (0x01 << 4);
        dev->vram_config.bank_width = 8;
        dev->vram_config.bank_height = 8;
        break;
    case 2: /* 4KB macro-tiling */
        tile_config |= (0x02 << 4);
        dev->vram_config.bank_width = 32;
        dev->vram_config.bank_height = 32;
        break;
    case 3: /* 64KB macro-tiling */
        tile_config |= (0x03 << 4);
        dev->vram_config.bank_width = 64;
        dev->vram_config.bank_height = 64;
        break;
    case 4: /* Mixed tiling */
        tile_config |= (0x04 << 4);
        break;
    }

    amdgpu_reg_write(AMDGPU_VRAM_TILING_CFG, tile_config);
    amdgpu_reg_write(AMDGPU_VRAM_TILE_CONFIG, dev->vram_config.bank_width |
                     (dev->vram_config.bank_height << 16));
    amdgpu_reg_write(AMDGPU_VRAM_MACROTILE_MODE, dev->vram_config.macro_tile_mode);
    amdgpu_reg_write(AMDGPU_VRAM_MICROTILE_MODE, dev->vram_config.micro_tile_mode);

    /* Configure bandwidth optimization */
    uint32_t bw_ctl = 0;
    if (dev->vram_config.tiling_enabled) {
        bw_ctl |= 0x01; /* Enable tiling-aware burst */
        bw_ctl |= (dev->vram_config.channel_count << 4); /* Channel interleave */
    }
    amdgpu_reg_write(AMDGPU_VRAM_BANDWIDTH_CTL, bw_ctl);
    dev->vram_config.bandwidth_optimized = (bw_ctl != 0) ? 1 : 0;

    klog_info("[AMDGPU] VRAM tiling set to mode %d", mode);
    return 0;
}

int amdgpu_vram_enable_dcc(amdgpu_device_t *dev, int enable) {
    if (!dev || !dev->mmio_base) return -1;

    dev->vram_config.dcc_enabled = (enable != 0) ? 1 : 0;

    if (enable) {
        /* DCC requires at least 4KB tiling */
        if (dev->vram_config.tile_mode < 2) {
            amdgpu_vram_set_tiling(dev, 2);
        }
        amdgpu_reg_write(AMDGPU_VRAM_DCC_CTL, 0x01); /* DCC enable */
        dev->vram_config.optimized_bytes_saved = 0;
    } else {
        amdgpu_reg_write(AMDGPU_VRAM_DCC_CTL, 0x00); /* DCC disable */
    }

    klog_info("[AMDGPU] DCC %s", enable ? "enabled" : "disabled");
    return 0;
}

int amdgpu_vram_calc_alignment(uint32_t width, uint32_t height, uint32_t bpp,
                                uint32_t *pitch, uint32_t *size) {
    if (!pitch || !size) return -1;

    uint32_t bytes_per_pixel = bpp / 8;
    uint32_t raw_pitch = width * bytes_per_pixel;

    /* Align to 256 bytes optimal for GPU memory controller */
    uint32_t aligned_pitch = (raw_pitch + 255) & ~255U;
    *pitch = aligned_pitch;
    *size = aligned_pitch * height;

    return 0;
}

int amdgpu_vram_get_bandwidth(amdgpu_device_t *dev, uint32_t *read_bw, uint32_t *write_bw) {
    if (!dev) return -1;

    /* Estimated bandwidth based on memory clock and bus width */
    uint32_t mem_clk_mhz = dev->memory_clock;
    uint32_t bus_width_bits = dev->vram_config.channel_count * 32; /* 32-bit per channel */
    uint32_t bw_raw_mbps = mem_clk_mhz * bus_width_bits / 8;

    /* Efficiency factor */
    float efficiency = 0.75f;
    if (dev->vram_config.tiling_enabled) efficiency += 0.08f;
    if (dev->vram_config.dcc_enabled) efficiency += 0.05f;

    uint32_t effective_bw = (uint32_t)((float)bw_raw_mbps * efficiency);

    if (read_bw) *read_bw = effective_bw;
    if (write_bw) *write_bw = (uint32_t)((float)effective_bw * 0.9f); /* write is slightly slower */

    return 0;
}

/* ================================================================ */
/*  6. PowerPlay 时钟/电压控制                                           */
/* ================================================================ */

static int amdgpu_smu_send_msg(uint32_t msg, uint32_t arg) {
    if (!amdgpu_dev.mmio_base) return -1;

    amdgpu_reg_write(AMDGPU_PP_SMU_ARG, arg);
    amdgpu_reg_write(AMDGPU_PP_SMU_CMD, msg);

    /* Wait for response */
    uint32_t timeout;
    for (timeout = 0; timeout < 5000; timeout++) {
        uint32_t resp = amdgpu_reg_read(AMDGPU_PP_SMU_RESP);
        if (resp == 0x01) return 0; /* OK */
        if (resp == 0xFF) return -1; /* error */
        io_wait();
    }

    return -1;
}

int amdgpu_pp_init(amdgpu_device_t *dev) {
    if (!dev) return -1;

    amdgpu_powerplay_t *pp = &dev->powerplay;
    memset(pp, 0, sizeof(amdgpu_powerplay_t));

    pp->dpm_enabled = 0;
    pp->current_pstate = 0;
    pp->gpu_load = 0;
    pp->gpu_power = 0;
    pp->fan_speed = 1500;
    pp->clock_policy = 1;
    pp->initialized = 1;

    pp->pstate_count = 4;
    pp->pstates[0].sclk = dev->engine_clock / 4;
    pp->pstates[0].mclk = dev->memory_clock / 4;
    pp->pstates[0].vcore = 800;
    pp->pstates[0].fan_speed_pwm = 30;
    pp->pstates[0].power_limit = 15000;

    pp->pstates[1].sclk = dev->engine_clock / 2;
    pp->pstates[1].mclk = dev->memory_clock / 2;
    pp->pstates[1].vcore = 900;
    pp->pstates[1].fan_speed_pwm = 50;
    pp->pstates[1].power_limit = 35000;

    pp->pstates[2].sclk = dev->engine_clock * 3 / 4;
    pp->pstates[2].mclk = dev->memory_clock * 3 / 4;
    pp->pstates[2].vcore = 1000;
    pp->pstates[2].fan_speed_pwm = 70;
    pp->pstates[2].power_limit = 55000;

    pp->pstates[3].sclk = dev->engine_clock;
    pp->pstates[3].mclk = dev->memory_clock;
    pp->pstates[3].vcore = 1100;
    pp->pstates[3].fan_speed_pwm = 100;
    pp->pstates[3].power_limit = 75000;

    if (dev->mmio_base) {
        amdgpu_reg_write(AMDGPU_PP_DPM_STATE, AMDGPU_DPM_STATE_BALANCED);
    }

    klog_info("[AMDGPU] PowerPlay initialized (%d P-states, balanced mode)", pp->pstate_count);
    return 0;
}

int amdgpu_pp_set_clock(uint32_t sclk_khz, uint32_t mclk_khz) {
    if (!amdgpu_dev.mmio_base) return -1;

    /* Request SMU to set clock frequencies */
    amdgpu_smu_send_msg(AMDGPU_SMU_MSG_SET_CLK_FREQ, sclk_khz);

    /* Update current clocks in device */
    amdgpu_dev.engine_clock = sclk_khz / 1000;
    amdgpu_dev.memory_clock = mclk_khz / 1000;

    amdgpu_reg_write(AMDGPU_PP_SCLK_CURRENT, sclk_khz);
    amdgpu_reg_write(AMDGPU_PP_MCLK_CURRENT, mclk_khz);

    return 0;
}

int amdgpu_pp_set_voltage(uint32_t vcore_mv) {
    if (!amdgpu_dev.mmio_base) return -1;

    amdgpu_smu_send_msg(AMDGPU_SMU_MSG_SET_VOLTAGE, vcore_mv);
    amdgpu_reg_write(AMDGPU_PP_VCORE_CURRENT, vcore_mv);

    return 0;
}

int amdgpu_pp_set_fan_speed(uint32_t pwm) {
    if (!amdgpu_dev.mmio_base) return -1;

    if (pwm > 255) pwm = 255;
    amdgpu_smu_send_msg(AMDGPU_SMU_MSG_SET_FAN_SPEED, pwm);
    amdgpu_reg_write(AMDGPU_PP_FAN_PWM, pwm);

    return 0;
}

int amdgpu_pp_get_gpu_load(uint32_t *load_pct) {
    if (!load_pct || !amdgpu_dev.mmio_base) return -1;

    /* Try SMU first */
    amdgpu_smu_send_msg(AMDGPU_SMU_MSG_GET_GPU_LOAD, 0);
    uint32_t load = amdgpu_reg_read(AMDGPU_PP_GPU_LOAD);

    if (load > 100) load = 100;
    *load_pct = load;
    return 0;
}

int amdgpu_pp_get_power(uint32_t *power_mw) {
    if (!power_mw || !amdgpu_dev.mmio_base) return -1;

    amdgpu_smu_send_msg(AMDGPU_SMU_MSG_GET_POWER_CONSUMP, 0);
    *power_mw = 0; /* would read from SMU response */
    return 0;
}

int amdgpu_pp_set_policy(uint32_t policy) {
    if (!amdgpu_dev.powerplay.initialized) return -1;

    amdgpu_dev.powerplay.clock_policy = policy;

    switch (policy) {
    case 0: /* Battery saver */
        if (amdgpu_dev.powerplay.pstate_count > 0) {
            amdgpu_dev.powerplay.current_pstate = 0;
            amdgpu_pp_set_clock(amdgpu_dev.powerplay.pstates[0].sclk,
                               amdgpu_dev.powerplay.pstates[0].mclk);
            amdgpu_pp_set_voltage(amdgpu_dev.powerplay.pstates[0].vcore);
            amdgpu_pp_set_fan_speed(amdgpu_dev.powerplay.pstates[0].fan_speed_pwm);
        }
        amdgpu_power_save();
        break;
    case 1: /* Balanced */
        if (amdgpu_dev.powerplay.pstate_count > 1) {
            uint32_t mid = amdgpu_dev.powerplay.pstate_count / 2;
            amdgpu_dev.powerplay.current_pstate = mid;
            amdgpu_pp_set_clock(amdgpu_dev.powerplay.pstates[mid].sclk,
                               amdgpu_dev.powerplay.pstates[mid].mclk);
            amdgpu_pp_set_voltage(amdgpu_dev.powerplay.pstates[mid].vcore);
            amdgpu_pp_set_fan_speed(amdgpu_dev.powerplay.pstates[mid].fan_speed_pwm);
        }
        break;
    case 2: /* Performance */
        if (amdgpu_dev.powerplay.pstate_count > 0) {
            uint32_t max = amdgpu_dev.powerplay.pstate_count - 1;
            amdgpu_dev.powerplay.current_pstate = max;
            amdgpu_pp_set_clock(amdgpu_dev.powerplay.pstates[max].sclk,
                               amdgpu_dev.powerplay.pstates[max].mclk);
            amdgpu_pp_set_voltage(amdgpu_dev.powerplay.pstates[max].vcore);
            amdgpu_pp_set_fan_speed(amdgpu_dev.powerplay.pstates[max].fan_speed_pwm);
        }
        amdgpu_power_full();
        break;
    default:
        break;
    }

    return 0;
}

int amdgpu_pp_get_current_pstate(amdgpu_pstate_t *pstate) {
    if (!pstate || !amdgpu_dev.powerplay.initialized) return -1;

    uint32_t idx = amdgpu_dev.powerplay.current_pstate;
    if (idx >= amdgpu_dev.powerplay.pstate_count) return -1;

    memcpy(pstate, &amdgpu_dev.powerplay.pstates[idx], sizeof(amdgpu_pstate_t));
    return 0;
}

int amdgpu_pp_dpm_enable(int enable) {
    if (!amdgpu_dev.mmio_base) return -1;

    if (enable) {
        amdgpu_smu_send_msg(AMDGPU_SMU_MSG_ENTER_DPM, 0);
        amdgpu_reg_write(AMDGPU_PP_DPM_STATE, AMDGPU_DPM_STATE_BALANCED);
        amdgpu_dev.powerplay.dpm_enabled = 1;
    } else {
        amdgpu_smu_send_msg(AMDGPU_SMU_MSG_EXIT_DPM, 0);
        amdgpu_reg_write(AMDGPU_PP_DPM_STATE, AMDGPU_DPM_STATE_BOOT);
        amdgpu_dev.powerplay.dpm_enabled = 0;
    }

    klog_info("[AMDGPU] DPM %s", enable ? "enabled" : "disabled");
    return 0;
}

/* ================================================================ */
/*  7. 多显示器支持                                                     */
/* ================================================================ */

int amdgpu_multihead_init(amdgpu_device_t *dev) {
    if (!dev) return -1;

    memset(dev->crtc, 0, sizeof(dev->crtc));
    dev->crtc_count = 0;

    /* Detect connected displays */
    amdgpu_connector_detect(dev);

    klog_info("[AMDGPU] Multi-head initialized (%d connectors detected)",
              dev->connector_count);
    return 0;
}

int amdgpu_multihead_add_crtc(amdgpu_device_t *dev, uint32_t connector_idx,
                               uint32_t width, uint32_t height, uint32_t bpp, uint32_t refresh) {
    if (!dev || dev->crtc_count >= 4) return -1;
    if (connector_idx >= dev->connector_count) return -1;

    amdgpu_crtc_t *crtc = &dev->crtc[dev->crtc_count];
    crtc->crtc_id = dev->crtc_count;
    crtc->connector_id = connector_idx;
    crtc->width = width;
    crtc->height = height;
    crtc->bpp = bpp;
    crtc->refresh = refresh;

    /* Calculate framebuffer offset (tiled layout for multi-head) */
    crtc->fb_offset = crtc->crtc_id * (width * height * (bpp / 8));
    crtc->pitch = width * (bpp / 8);
    crtc->fb_base = dev->fb_base + crtc->fb_offset;

    /* Configure CRTC timing */
    uint32_t crtc_base = AMDGPU_DC_CRTC_BASE(crtc->crtc_id);

    /* Generate mode timings */
    crtc->h_total = width + width / 5;
    crtc->h_sync_start = width + width / 20;
    crtc->h_sync_end = width + width / 20 + width / 16;
    crtc->v_total = height + height / 20;
    crtc->v_sync_start = height + height / 50;
    crtc->v_sync_end = height + height / 50 + 3;

    /* Write CRTC registers */
    amdgpu_reg_write(crtc_base + 0x02, crtc->h_total);
    amdgpu_reg_write(crtc_base + 0x03, crtc->v_total);
    amdgpu_reg_write(crtc_base + 0x04, (crtc->h_sync_start << 16) | crtc->h_sync_end);
    amdgpu_reg_write(crtc_base + 0x05, (crtc->v_sync_start << 16) | crtc->v_sync_end);

    /* Set framebuffer for this CRTC */
    amdgpu_reg_write(crtc_base + 0x06, crtc->fb_base);
    amdgpu_reg_write(crtc_base + 0x07, 0); /* high address */
    amdgpu_reg_write(crtc_base + 0x08, crtc->pitch);

    /* Enable CRTC */
    amdgpu_reg_write(crtc_base, 0x01); /* CRTC enable */
    crtc->enabled = 1;
    dev->crtc_count++;

    klog_info("[AMDGPU] CRTC %d added: %dx%d@%d on connector %d",
              crtc->crtc_id, width, height, refresh, connector_idx);
    return 0;
}

int amdgpu_multihead_remove_crtc(amdgpu_device_t *dev, uint32_t crtc_id) {
    if (!dev || crtc_id >= dev->crtc_count) return -1;

    /* Disable CRTC */
    uint32_t crtc_base = AMDGPU_DC_CRTC_BASE(crtc_id);
    amdgpu_reg_write(crtc_base, 0x00);

    dev->crtc[crtc_id].enabled = 0;

    /* Compact array */
    for (uint32_t i = crtc_id; i < dev->crtc_count - 1; i++) {
        memcpy(&dev->crtc[i], &dev->crtc[i + 1], sizeof(amdgpu_crtc_t));
        dev->crtc[i].crtc_id = i;
    }
    dev->crtc_count--;

    klog_info("[AMDGPU] CRTC %d removed", crtc_id);
    return 0;
}

int amdgpu_multihead_get_crtc_count(amdgpu_device_t *dev) {
    return dev ? (int)dev->crtc_count : 0;
}

void amdgpu_multihead_scan_displays(amdgpu_device_t *dev) {
    if (!dev) return;

    /* Re-scan HPD status */
    uint32_t hpd_status = amdgpu_reg_read(AMDGPU_DC_HPD_STATUS);
    uint32_t old_count = dev->connector_count;

    amdgpu_connector_detect(dev);

    /* Handle hotplug events */
    if (dev->connector_count > old_count) {
        klog_info("[AMDGPU] Display connected (now %d displays)", dev->connector_count);
    } else if (dev->connector_count < old_count) {
        klog_info("[AMDGPU] Display disconnected (now %d displays)", dev->connector_count);
        /* Remove any CRTCs that reference disconnected connectors */
        for (uint32_t i = 0; i < dev->crtc_count; i++) {
            if (dev->crtc[i].connector_id >= dev->connector_count) {
                amdgpu_multihead_remove_crtc(dev, i);
                i--; /* re-check this index */
            }
        }
    }

    (void)hpd_status;
}

int amdgpu_multihead_set_primary(amdgpu_device_t *dev, uint32_t crtc_id) {
    if (!dev || crtc_id >= dev->crtc_count) return -1;

    /* Swap primary CRTC to position 0 for simplicity */
    if (crtc_id > 0) {
        amdgpu_crtc_t temp = dev->crtc[0];
        dev->crtc[0] = dev->crtc[crtc_id];
        dev->crtc[crtc_id] = temp;
        dev->crtc[0].crtc_id = 0;
        dev->crtc[crtc_id].crtc_id = crtc_id;
    }

    dev->width = dev->crtc[0].width;
    dev->height = dev->crtc[0].height;
    dev->pitch = dev->crtc[0].pitch;

    klog_info("[AMDGPU] Primary display set to CRTC %d (%dx%d)",
              crtc_id, dev->crtc[0].width, dev->crtc[0].height);
    return 0;
}

/* ================================================================ */
/*  8. 温度监控                                                       */
/* ================================================================ */

int amdgpu_thermal_init(amdgpu_device_t *dev) {
    if (!dev) return -1;

    amdgpu_thermal_t *th = &dev->thermal;
    memset(th, 0, sizeof(amdgpu_thermal_t));

    th->throttle_temp = 85000;
    th->critical_temp = 95000;
    th->emergency_temp = 105000;
    th->throttling_active = 0;
    th->throttle_level = 0;
    th->overheat_count = 0;
    th->temp_edge = 45000;
    th->temp_junction = 50000;
    th->temp_mem = 43000;
    th->temp_vrm = 48000;
    th->last_reading_ms = 0;

    if (dev->mmio_base) {
        amdgpu_reg_write(AMDGPU_THERMAL_THROTTLE_TEMP, th->throttle_temp);
        amdgpu_reg_write(AMDGPU_THERMAL_CRITICAL_TEMP, th->critical_temp);
        amdgpu_reg_write(AMDGPU_THERMAL_EMERGENCY_TEMP, th->emergency_temp);
    }

    th->initialized = 1;

    klog_info("[AMDGPU] Thermal monitoring initialized (throttle=%dC, critical=%dC, emergency=%dC)",
              th->throttle_temp / 1000, th->critical_temp / 1000, th->emergency_temp / 1000);
    return 0;
}

int amdgpu_thermal_read(amdgpu_device_t *dev) {
    if (!dev || !dev->thermal.initialized) return -1;

    amdgpu_thermal_t *th = &dev->thermal;
    static uint32_t sim_tick = 0;
    sim_tick++;

    if (dev->mmio_base) {
        if (amdgpu_smu_send_msg(AMDGPU_SMU_MSG_GET_TEMPERATURE, 0) == 0) {
            th->temp_edge = amdgpu_reg_read(AMDGPU_THERMAL_TEMP_EDGE);
            th->temp_junction = amdgpu_reg_read(AMDGPU_THERMAL_TEMP_JUNCTION);
            th->temp_mem = amdgpu_reg_read(AMDGPU_THERMAL_TEMP_MEM);
            th->temp_vrm = amdgpu_reg_read(AMDGPU_THERMAL_TEMP_VRM);

            uint32_t status = amdgpu_reg_read(AMDGPU_THERMAL_STATUS);
            th->throttling_active = (status & 0x01) ? 1 : 0;
            th->throttle_level = (status >> 1) & 0x03;
            return 0;
        }
    }

    uint32_t base_temp = 45000 + (sim_tick % 20000);
    if (base_temp > 75000) base_temp = 45000;

    th->temp_edge = base_temp;
    th->temp_junction = base_temp + 5000;
    th->temp_mem = base_temp - 2000;
    th->temp_vrm = base_temp + 3000;

    if (th->temp_junction >= th->emergency_temp) {
        th->throttling_active = 1;
        th->throttle_level = 3;
        th->overheat_count++;
    } else if (th->temp_junction >= th->critical_temp) {
        th->throttling_active = 1;
        th->throttle_level = 2;
    } else if (th->temp_junction >= th->throttle_temp) {
        th->throttling_active = 1;
        th->throttle_level = 1;
    } else {
        th->throttling_active = 0;
        th->throttle_level = 0;
    }

    return 0;
}

int amdgpu_thermal_get_temp(uint32_t *edge, uint32_t *junction, uint32_t *mem, uint32_t *vrm) {
    if (!amdgpu_dev.thermal.initialized) return -1;

    amdgpu_thermal_read(&amdgpu_dev);

    if (edge) *edge = amdgpu_dev.thermal.temp_edge;
    if (junction) *junction = amdgpu_dev.thermal.temp_junction;
    if (mem) *mem = amdgpu_dev.thermal.temp_mem;
    if (vrm) *vrm = amdgpu_dev.thermal.temp_vrm;
    return 0;
}

int amdgpu_thermal_get_throttle(uint32_t *level) {
    if (!level || !amdgpu_dev.thermal.initialized) return -1;
    *level = amdgpu_dev.thermal.throttle_level;
    return 0;
}

int amdgpu_thermal_set_thresholds(uint32_t throttle, uint32_t critical) {
    if (!amdgpu_dev.thermal.initialized || !amdgpu_dev.mmio_base) return -1;

    amdgpu_dev.thermal.throttle_temp = throttle;
    amdgpu_dev.thermal.critical_temp = critical;

    amdgpu_reg_write(AMDGPU_THERMAL_THROTTLE_TEMP, throttle);
    amdgpu_reg_write(AMDGPU_THERMAL_CRITICAL_TEMP, critical);

    return 0;
}

void amdgpu_thermal_check(amdgpu_device_t *dev) {
    if (!dev || !dev->thermal.initialized) return;

    amdgpu_thermal_read(dev);
    amdgpu_thermal_t *th = &dev->thermal;

    if (th->temp_junction >= th->emergency_temp) {
        /* Emergency: immediate shutdown */
        klog_err("[AMDGPU] EMERGENCY: GPU junction temp %dC exceeds %dC!",
                 th->temp_junction / 1000, th->emergency_temp / 1000);
        th->overheat_count++;
        /* Force minimum clocks */
        amdgpu_pp_set_clock(300000, 300000); /* 300 MHz */
        amdgpu_pp_set_fan_speed(255); /* max fan */
        th->throttle_level = 3;
    } else if (th->temp_junction >= th->critical_temp) {
        /* Critical throttling */
        th->throttle_level = 2;
        amdgpu_pp_set_clock(amdgpu_dev.engine_clock * 500 / 1000,
                           amdgpu_dev.memory_clock * 500 / 1000); /* 50% */
        amdgpu_pp_set_fan_speed(200);
    } else if (th->temp_junction >= th->throttle_temp) {
        /* Moderate throttling */
        th->throttle_level = 1;
        amdgpu_pp_set_clock(amdgpu_dev.engine_clock * 750 / 1000,
                           amdgpu_dev.memory_clock * 750 / 1000); /* 75% */
        amdgpu_pp_set_fan_speed(128);
    } else {
        th->throttle_level = 0;
    }
}

/* ================================================================ */
/*  9. DMA 引擎                                                        */
/* ================================================================ */

int amdgpu_dma_init(amdgpu_dma_engine_t *dma, uint32_t ring_size) {
    if (!dma) return -1;

    memset(dma, 0, sizeof(amdgpu_dma_engine_t));

    int ret = amdgpu_ring_init(&dma->ring, ring_size);
    if (ret < 0) return ret;

    dma->pending_max = 64;
    dma->pending = (amdgpu_dma_cmd_t *)kmalloc(dma->pending_max * sizeof(amdgpu_dma_cmd_t));
    if (!dma->pending) {
        amdgpu_ring_destroy(&dma->ring);
        return -1;
    }
    memset(dma->pending, 0, dma->pending_max * sizeof(amdgpu_dma_cmd_t));

    dma->pending_count = 0;
    dma->bytes_transferred = 0;
    dma->transfer_count = 0;
    dma->fence_value = 1;
    dma->busy = 0;
    dma->initialized = 1;

    if (amdgpu_dev.mmio_base) {
        amdgpu_reg_write(AMDGPU_SDMA0_RB_BASE, dma->ring.base ? (uint32_t)(uintptr_t)dma->ring.base : 0);
        amdgpu_reg_write(AMDGPU_SDMA0_RB_RPTR, 0);
        amdgpu_reg_write(AMDGPU_SDMA0_RB_WPTR, 0);
        amdgpu_reg_write(AMDGPU_SDMA0_RB_CNTL, 0x01);
    }

    klog_info("[AMDGPU] DMA engine initialized (ring=%dKB)", ring_size);
    return 0;
}

static int amdgpu_dma_submit_cmd(amdgpu_dma_engine_t *dma, uint32_t src_addr,
                                  uint32_t dst_addr, uint32_t size, uint32_t flags) {
    if (!dma || !dma->initialized || !amdgpu_dev.mmio_base) return -1;

    /* Wait for DMA engine to be ready */
    uint32_t timeout;
    for (timeout = 0; timeout < 10000; timeout++) {
        if (!(amdgpu_reg_read(AMDGPU_SDMA0_STATUS) & 0x01)) break;
        io_wait();
    }

    /* Program source and destination */
    amdgpu_reg_write(AMDGPU_SDMA0_SRC_ADDR, src_addr);
    amdgpu_reg_write(AMDGPU_SDMA0_SRC_ADDR_HI, 0);
    amdgpu_reg_write(AMDGPU_SDMA0_DST_ADDR, dst_addr);
    amdgpu_reg_write(AMDGPU_SDMA0_DST_ADDR_HI, 0);
    amdgpu_reg_write(AMDGPU_SDMA0_COPY_SIZE, size);

    /* Program fence if requested */
    if (flags & 0x01) { /* interrupt */
        amdgpu_reg_write(AMDGPU_SDMA0_FENCE_ADDR, 0);
        amdgpu_reg_write(AMDGPU_SDMA0_FENCE_DATA, dma->fence_value);
    }

    /* Fire copy command */
    uint32_t cmd = AMDGPU_SDMA_PKT_COPY_LINEAR;
    if (!(flags & 0x04)) cmd = AMDGPU_SDMA_PKT_COPY_LINEAR;
    amdgpu_reg_write(AMDGPU_SDMA0_COPY_CNTL, (flags << 16) | cmd);

    dma->busy = 1;
    dma->bytes_transferred += size;
    dma->transfer_count++;

    return 0;
}

int amdgpu_dma_copy(amdgpu_dma_engine_t *dma,
                    uint32_t src_addr, uint32_t dst_addr,
                    uint32_t size, int use_interrupt) {
    if (!dma || !dma->initialized) return -1;

    uint32_t flags = (use_interrupt ? 0x01 : 0x00) | 0x04; /* linear copy */
    int ret = amdgpu_dma_submit_cmd(dma, src_addr, dst_addr, size, flags);

    if (ret == 0 && !use_interrupt) {
        ret = amdgpu_dma_wait(dma, 5000);
    }

    return ret;
}

int amdgpu_dma_copy_async(amdgpu_dma_engine_t *dma,
                          uint32_t src_addr, uint32_t dst_addr, uint32_t size) {
    return amdgpu_dma_copy(dma, src_addr, dst_addr, size, 1);
}

int amdgpu_dma_wait(amdgpu_dma_engine_t *dma, uint32_t timeout_ms) {
    if (!dma || !dma->initialized || !amdgpu_dev.mmio_base) return -1;

    uint32_t deadline = timeout_ms;
    uint32_t elapsed = 0;

    while (dma->busy && elapsed < deadline) {
        uint32_t status = amdgpu_reg_read(AMDGPU_SDMA0_STATUS);
        if (!(status & 0x01)) {
            dma->busy = 0;
            return 0;
        }
        io_wait();
        elapsed++;
    }

    if (dma->busy) return -1; /* timeout */
    return 0;
}

int amdgpu_dma_fence_signal(amdgpu_dma_engine_t *dma) {
    if (!dma || !dma->initialized || !amdgpu_dev.mmio_base) return -1;

    dma->fence_value++;
    amdgpu_reg_write(AMDGPU_SDMA0_FENCE_DATA, dma->fence_value);

    return 0;
}

int amdgpu_dma_fence_wait(amdgpu_dma_engine_t *dma, uint32_t fence_value, uint32_t timeout_ms) {
    if (!dma || !dma->initialized || !amdgpu_dev.mmio_base) return -1;

    uint32_t elapsed = 0;
    while (elapsed < timeout_ms) {
        uint32_t current = amdgpu_reg_read(AMDGPU_SDMA0_FENCE_DATA);
        if (current >= fence_value) return 0;
        io_wait();
        elapsed++;
    }
    return -1;
}

void amdgpu_dma_shutdown(amdgpu_dma_engine_t *dma) {
    if (!dma || !dma->initialized) return;

    /* Disable SDMA */
    if (amdgpu_dev.mmio_base) {
        amdgpu_reg_write(AMDGPU_SDMA0_RB_CNTL, 0x00);
    }

    if (dma->pending) { kfree(dma->pending); dma->pending = NULL; }
    amdgpu_ring_destroy(&dma->ring);

    dma->pending_count = 0;
    dma->initialized = 0;
}

/* ================================================================ */
/*  10. 中断处理                                                       */
/* ================================================================ */

#define AMDGPU_IH_MAX_HANDLERS 32

static struct {
    uint32_t src_id;
    void (*handler)(const amdgpu_iv_entry_t*);
    uint32_t active;
} g_ih_handlers[AMDGPU_IH_MAX_HANDLERS];

static uint32_t g_ih_handler_count = 0;

int amdgpu_ih_init(amdgpu_device_t *dev) {
    if (!dev) return -1;

    amdgpu_ih_t *ih = &dev->ih;
    memset(ih, 0, sizeof(amdgpu_ih_t));

    ih->rptr = 0;
    ih->wptr = 0;
    ih->enabled = 0;
    ih->interrupt_count = 0;
    ih->vsync_count = 0;
    ih->page_flip_count = 0;
    ih->cmd_complete_count = 0;
    ih->sdma_trap_count = 0;
    ih->thermal_trip_count = 0;
    ih->hpd_count = 0;
    ih->gpu_fault_count = 0;

    g_ih_handler_count = 0;
    for (uint32_t i = 0; i < AMDGPU_IH_MAX_HANDLERS; i++) {
        g_ih_handlers[i].active = 0;
        g_ih_handlers[i].handler = NULL;
        g_ih_handlers[i].src_id = 0;
    }

    if (dev->mmio_base) {
        amdgpu_reg_write(AMDGPU_IH_RB_CNTL, 0x00);
        amdgpu_reg_write(AMDGPU_IH_RB_BASE, (uint32_t)(uintptr_t)&ih->ring[0]);
        amdgpu_reg_write(AMDGPU_IH_RB_WPTR, 0);
        amdgpu_reg_write(AMDGPU_IH_RB_RPTR, 0);
        amdgpu_reg_write(AMDGPU_IH_INT_MASK, 0xFFFFFFFF);
    }

    klog_info("[AMDGPU] Interrupt handler initialized");
    return 0;
}

int amdgpu_ih_enable(amdgpu_device_t *dev) {
    if (!dev || !dev->mmio_base) return -1;

    /* Enable all common interrupt sources */
    uint32_t mask = ~(AMDGPU_IH_SRCID_VSYNC |
                      AMDGPU_IH_SRCID_PAGE_FLIP |
                      AMDGPU_IH_SRCID_CMD_COMPLETE |
                      AMDGPU_IH_SRCID_SDMA_TRAP |
                      AMDGPU_IH_SRCID_THERMAL_TRIP |
                      AMDGPU_IH_SRCID_HPD |
                      AMDGPU_IH_SRCID_GPU_FAULT);

    amdgpu_reg_write(AMDGPU_IH_INT_MASK, mask);
    amdgpu_reg_write(AMDGPU_IH_RB_CNTL, 0x01); /* RB enable + interrupt enable */

    dev->ih.enabled = 1;
    klog_info("[AMDGPU] Interrupts enabled");
    return 0;
}

int amdgpu_ih_disable(amdgpu_device_t *dev) {
    if (!dev || !dev->mmio_base) return -1;

    amdgpu_reg_write(AMDGPU_IH_RB_CNTL, 0x00);
    amdgpu_reg_write(AMDGPU_IH_INT_MASK, 0xFFFFFFFF);
    dev->ih.enabled = 0;
    return 0;
}

int amdgpu_ih_process(amdgpu_device_t *dev) {
    if (!dev || !dev->mmio_base || !dev->ih.enabled) return 0;

    amdgpu_ih_t *ih = &dev->ih;
    int processed = 0;

    /* Read pending interrupts from ring buffer */
    while (ih->rptr != ih->wptr) {
        amdgpu_iv_entry_t *iv = &ih->ring[ih->rptr];
        ih->rptr = (ih->rptr + 1) % AMDGPU_IH_RING_SIZE;
        ih->interrupt_count++;
        processed++;

        /* Update counters */
        switch (iv->src_id) {
        case AMDGPU_IH_SRCID_VSYNC:      ih->vsync_count++; break;
        case AMDGPU_IH_SRCID_PAGE_FLIP:  ih->page_flip_count++; break;
        case AMDGPU_IH_SRCID_CMD_COMPLETE: ih->cmd_complete_count++; break;
        case AMDGPU_IH_SRCID_SDMA_TRAP:  ih->sdma_trap_count++; break;
        case AMDGPU_IH_SRCID_THERMAL_TRIP: ih->thermal_trip_count++; break;
        case AMDGPU_IH_SRCID_HPD:
        case AMDGPU_IH_SRCID_HPD1:
        case AMDGPU_IH_SRCID_HPD2:
        case AMDGPU_IH_SRCID_HPD3:
            ih->hpd_count++;
            break;
        case AMDGPU_IH_SRCID_GPU_FAULT:
            ih->gpu_fault_count++;
            klog_err("[AMDGPU] GPU fault detected! (src_data=0x%08X)", iv->src_data);
            break;
        default:
            break;
        }

        /* Dispatch to registered handlers */
        for (uint32_t i = 0; i < g_ih_handler_count; i++) {
            if (g_ih_handlers[i].active &&
                g_ih_handlers[i].src_id == iv->src_id &&
                g_ih_handlers[i].handler) {
                g_ih_handlers[i].handler(iv);
            }
        }

        /* Handle hotplug */
        if (iv->src_id >= AMDGPU_IH_SRCID_HPD &&
            iv->src_id <= AMDGPU_IH_SRCID_HPD3) {
            amdgpu_multihead_scan_displays(dev);
        }

        /* Handle thermal trip */
        if (iv->src_id == AMDGPU_IH_SRCID_THERMAL_TRIP) {
            amdgpu_thermal_check(dev);
        }
    }

    /* Update RPTR to acknowledge interrupts */
    amdgpu_reg_write(AMDGPU_IH_RB_RPTR, ih->rptr);
    amdgpu_reg_write(AMDGPU_IH_INT_ACK, 0x01);

    return processed;
}

void amdgpu_ih_register_handler(uint32_t src_id, void (*handler)(const amdgpu_iv_entry_t*)) {
    if (!handler) return;

    /* Check for duplicate */
    for (uint32_t i = 0; i < g_ih_handler_count; i++) {
        if (g_ih_handlers[i].active && g_ih_handlers[i].src_id == src_id) {
            g_ih_handlers[i].handler = handler;
            return;
        }
    }

    /* Add new handler */
    if (g_ih_handler_count < AMDGPU_IH_MAX_HANDLERS) {
        g_ih_handlers[g_ih_handler_count].src_id = src_id;
        g_ih_handlers[g_ih_handler_count].handler = handler;
        g_ih_handlers[g_ih_handler_count].active = 1;
        g_ih_handler_count++;
    }
}

void amdgpu_ih_unregister_handler(uint32_t src_id) {
    for (uint32_t i = 0; i < g_ih_handler_count; i++) {
        if (g_ih_handlers[i].active && g_ih_handlers[i].src_id == src_id) {
            g_ih_handlers[i].active = 0;
            g_ih_handlers[i].handler = NULL;
            return;
        }
    }
}

int amdgpu_ih_wait_vsync(uint32_t crtc_id, uint32_t timeout_ms) {
    if (!amdgpu_dev.ih.enabled) return -1;
    (void)crtc_id;
    (void)timeout_ms;

    /* In a real driver, this would wait on a vsync interrupt.
     * Here we simulate by reading the CRTC status. */
    uint32_t crtc_base = AMDGPU_DC_CRTC_BASE(crtc_id);
    uint32_t timeout;
    for (timeout = 0; timeout < timeout_ms * 10; timeout++) {
        uint32_t status = amdgpu_reg_read(crtc_base + 0x01); /* CRTC_STATUS */
        if (status & 0x08) { /* vsync active */
            io_wait(); /* wait for it to clear */
            return 0;
        }
        io_wait();
    }
    return -1;
}

int amdgpu_ih_wait_page_flip(uint32_t crtc_id, uint32_t timeout_ms) {
    if (!amdgpu_dev.ih.enabled) return -1;
    (void)crtc_id;

    uint32_t timeout;
    for (timeout = 0; timeout < timeout_ms * 10; timeout++) {
        if (amdgpu_dev.ih.page_flip_count > 0) {
            amdgpu_dev.ih.page_flip_count--;
            return 0;
        }
        io_wait();
    }
    return -1;
}