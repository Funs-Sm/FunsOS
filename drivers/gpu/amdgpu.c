#include "amdgpu.h"
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

    uint32_t pitch_dwords = amdgpu_dev.pitch / 4;
    uint32_t row, col;

    /* 尝试使用 MMIO 加速填充 */
    if (amdgpu_dev.mmio_base && amdgpu_dev.fb_base) {
        /* 使用图形引擎快速填充 */
        amdgpu_reg_write(AMDGPU_GRPH_X_START, x);
        amdgpu_reg_write(AMDGPU_GRPH_Y_START, y);
        amdgpu_reg_write(AMDGPU_GRPH_X_END, x + w - 1);
        amdgpu_reg_write(AMDGPU_GRPH_Y_END, y + h - 1);
        amdgpu_reg_write(AMDGPU_GRPH_UPDATE, 1);
    }

    /* 软件回退：直接写入帧缓冲区 */
    for (row = y; row < y + h && row < amdgpu_dev.height; row++) {
        for (col = x; col < x + w && col < amdgpu_dev.width; col++) {
            fb[row * pitch_dwords + col] = color;
        }
    }
}

void amdgpu_blit(uint32_t *dst, uint32_t *src, uint32_t w, uint32_t h, uint32_t dst_pitch, uint32_t src_pitch) {
    if (!dst || !src) return;

    uint32_t dst_pitch_dwords = dst_pitch / 4;
    uint32_t src_pitch_dwords = src_pitch / 4;

    uint32_t row, col;
    for (row = 0; row < h; row++) {
        for (col = 0; col < w; col++) {
            dst[row * dst_pitch_dwords + col] = src[row * src_pitch_dwords + col];
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

    /* 设置默认模式 */
    amdgpu_dev.bpp = 32;
    amdgpu_dev.width = 1024;
    amdgpu_dev.height = 768;
    amdgpu_dev.pitch = 1024 * 4;
    amdgpu_dev.engine_clock = amdgpu_default_engine_clock;
    amdgpu_dev.memory_clock = amdgpu_default_memory_clock;
    amdgpu_dev.initialized = 1;

    /* 尝试应用默认模式 */
    amdgpu_set_mode(1024, 768, 32);

    klog_info("[AMDGPU] Initialization complete: %s", amdgpu_model_name);

    return 0;
}

/* ---- 驱动初始化和关闭 ---- */

int amdgpu_init(void) {
    klog_info("[AMDGPU] Initializing AMD GPU driver...");

    int ret = amdgpu_probe();
    if (ret != 0) {
        klog_info("[AMDGPU] No AMD GPU found or probe failed");
        return ret;
    }

    return 0;
}

void amdgpu_shutdown(void) {
    if (!amdgpu_dev.initialized) return;

    /* 禁用图形引擎 */
    if (amdgpu_dev.mmio_base) {
        amdgpu_reg_write(AMDGPU_GRPH_ENABLE, 0);
        amdgpu_reg_write(AMDGPU_DC_CRTC_CONTROL, 0);
    }

    /* 取消映射 */
    if (amdgpu_dev.framebuffer) {
        uint32_t fb_map_size = (amdgpu_dev.fb_size + 0xFFF) & ~0xFFF;
        vmm_unmap_physical(amdgpu_dev.framebuffer, fb_map_size);
        amdgpu_dev.framebuffer = (void *)0;
    }

    if (amdgpu_dev.mmio_base) {
        vmm_unmap_physical((void *)amdgpu_dev.mmio_base, 0x80000);
        amdgpu_dev.mmio_base = 0;
    }

    amdgpu_model_name = "";
    memset(&amdgpu_dev, 0, sizeof(amdgpu_device_t));

    klog_info("[AMDGPU] Driver shutdown complete");
}