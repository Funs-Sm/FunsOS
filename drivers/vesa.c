#include "vesa.h"
#include "boot_info.h"
#include "kheap.h"
#include "vmm.h"

static vesa_info_t current_mode;
static vbe_mode_info_t vbe_current;
static uint32_t *framebuffer = (void *)0;
static int vbe_initialized = 0;

/* VBE 3.0 Protected Mode entry point */
typedef struct {
    uint16_t mode;
    uint16_t width;
    uint16_t height;
    uint8_t  bpp;
    uint32_t framebuffer_phys;
    uint32_t pitch;
} vbe_pm_mode_list_entry_t;

/* VBE PM interface signature: "PMID" */
#define VBE_PM_SIGNATURE 0x44494D50  /* "PMID" */

/* VBE PM entry point - found by scanning BIOS ROM */
static uint32_t vbe_pm_entry = 0;

static inline void *memset32(void *s, uint32_t c, uint32_t n) {
    uint32_t *p = (uint32_t *)s;
    while (n--) *p++ = c;
    return s;
}

static inline void *memcpy32(void *dst, const void *src, uint32_t n) {
    uint32_t *d = (uint32_t *)dst;
    const uint32_t *s = (const uint32_t *)src;
    while (n--) *d++ = *s++;
    return dst;
}

/*
 * Search for VBE 3.0 Protected Mode interface in BIOS ROM area.
 * The PM interface is identified by the "PMID" signature at offset 0,
 * followed by the entry point at offset 4.
 * We scan the BIOS ROM area from 0xC0000 to 0xC8000 on 16-byte boundaries.
 */
static void vbe_find_pm_interface(void) {
    vbe_pm_entry = 0;

    /* Ensure BIOS ROM area is mapped */
    for (uint32_t addr = 0xC0000; addr < 0xC8000; addr += 0x1000) {
        vmm_map_page(vmm_get_current_dir(), addr, addr, VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE);
    }

    for (uint32_t addr = 0xC0000; addr < 0xC8000; addr += 16) {
        volatile uint32_t *sig = (volatile uint32_t *)addr;
        if (*sig == VBE_PM_SIGNATURE) {
            /* PMID found: bytes 4-7 contain the protected mode entry point offset */
            uint16_t pm_off = *((volatile uint16_t *)(addr + 4));
            uint16_t pm_seg = *((volatile uint16_t *)(addr + 6));
            /* Convert real-mode seg:off to linear address */
            vbe_pm_entry = ((uint32_t)pm_seg << 4) + pm_off;

            /* Validate: entry should be within BIOS ROM range */
            if (vbe_pm_entry >= 0xC0000 && vbe_pm_entry < 0xC8000) {
                return;
            }
            vbe_pm_entry = 0;
        }
    }
}

/*
 * Call VBE PM interface to set a video mode.
 * Returns 0 on success, -1 on failure.
 *
 * The VBE PM entry point calling convention (32-bit):
 *   AL = VBE function number (0x02 for set mode)
 *   BH = 0x00 (reserved)
 *   BL = mode number (bit 14 = use linear framebuffer, bit 15 = clear display)
 *   ES:DI = pointer to CRTCInfoBlock (can be 0:0 for default timings)
 *
 * On return: AL = 0x4F if function supported, AH = 0x00 if successful
 */
static int vbe_pm_set_mode(uint16_t mode) {
    if (!vbe_pm_entry) return -1;

    /* Ensure the PM entry point is mapped */
    uint32_t entry_page = vbe_pm_entry & ~0xFFF;
    vmm_map_page(vmm_get_current_dir(), entry_page, entry_page,
                 VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE);

    /* Use linear framebuffer (bit 14) and clear display (bit 15) */
    uint16_t mode_flags = mode | (1 << 14) | (1 << 15);

    uint32_t result;
    asm volatile(
        "call *%[pm_entry]\n\t"
        : "=a"(result)
        : "a"((0x4F << 8) | 0x02),   /* AH=0x4F, AL=0x02 (set mode) */
          "b"(mode_flags),             /* BL=mode with LFB flag */
          "D"(0)                       /* ES:DI = 0:0 (no CRTC info) */
          , [pm_entry] "r"(vbe_pm_entry)
        : "memory", "cc"
    );

    /* Check: AL should be 0x4F (supported), AH should be 0x00 (success) */
    if ((result & 0xFF) != 0x4F || ((result >> 8) & 0xFF) != 0x00) {
        return -1;
    }
    return 0;
}

void vesa_init(vesa_info_t *info) {
    current_mode.fb_addr = info->fb_addr;
    current_mode.fb_width = info->fb_width;
    current_mode.fb_height = info->fb_height;
    current_mode.fb_bpp = info->fb_bpp;
    current_mode.fb_pitch = info->fb_pitch;
    current_mode.fb_size = info->fb_size;

    uint32_t fb_start = current_mode.fb_addr;
    uint32_t fb_end = fb_start + current_mode.fb_size;
    fb_end = (fb_end + 0xFFF) & ~0xFFF;

    for (uint32_t addr = fb_start & ~0xFFF; addr < fb_end; addr += 0x1000) {
        vmm_map_page(vmm_get_current_dir(), addr, addr, VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE);
    }

    framebuffer = (uint32_t *)(uintptr_t)current_mode.fb_addr;

    /* Also populate vbe_current for the new API */
    vbe_current.mode = 0;
    vbe_current.width = (uint16_t)info->fb_width;
    vbe_current.height = (uint16_t)info->fb_height;
    vbe_current.bpp = (uint8_t)info->fb_bpp;
    vbe_current.framebuffer = info->fb_addr;
    vbe_current.pitch = info->fb_pitch;
    vbe_initialized = 1;

    /* Try to locate VBE PM interface for future mode switches */
    vbe_find_pm_interface();
}

void vesa_set_mode(uint16_t mode) {
    (void)mode;
}

void vesa_get_mode_info(uint16_t mode, VBE_MODE_INFO *info) {
    (void)mode;
    /* VBE mode info not available without bootloader support */
    if (info) {
        /* Zero out the info structure */
        uint8_t *p = (uint8_t *)info;
        for (uint32_t i = 0; i < sizeof(VBE_MODE_INFO); i++) p[i] = 0;
    }
}

void vesa_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= current_mode.fb_width || y >= current_mode.fb_height) return;

    uint32_t offset = y * (current_mode.fb_pitch / 4) + x;

    if (current_mode.fb_bpp == 32) {
        framebuffer[offset] = color;
    } else if (current_mode.fb_bpp == 24) {
        uint8_t *fb8 = (uint8_t *)framebuffer;
        uint32_t byte_offset = y * current_mode.fb_pitch + x * 3;
        fb8[byte_offset] = color & 0xFF;
        fb8[byte_offset + 1] = (color >> 8) & 0xFF;
        fb8[byte_offset + 2] = (color >> 16) & 0xFF;
    }
}

void vesa_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t row = 0; row < h; row++) {
        for (uint32_t col = 0; col < w; col++) {
            vesa_put_pixel(x + col, y + row, color);
        }
    }
}

void vesa_draw_bitmap(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const void *data) {
    const uint32_t *src = (const uint32_t *)data;
    for (uint32_t row = 0; row < h; row++) {
        for (uint32_t col = 0; col < w; col++) {
            uint32_t color = src[row * w + col];
            vesa_put_pixel(x + col, y + row, color);
        }
    }
}

/* ---- New VBE API ---- */

/*
 * Initialize VBE from multiboot bootloader info.
 * This is the fallback path when we cannot call BIOS from protected mode.
 * The bootloader (GRUB etc.) may have already set a VBE mode and passes
 * the framebuffer parameters via the multiboot info structure.
 */
void vbe_init_from_multiboot(uint32_t vbe_mode, uint32_t fb_addr,
                             uint32_t fb_width, uint32_t fb_height,
                             uint32_t fb_bpp, uint32_t fb_pitch) {
    if (!fb_addr || !fb_width || !fb_height) return;

    vbe_current.mode = (uint16_t)vbe_mode;
    vbe_current.width = (uint16_t)fb_width;
    vbe_current.height = (uint16_t)fb_height;
    vbe_current.bpp = (uint8_t)fb_bpp;
    vbe_current.framebuffer = fb_addr;
    vbe_current.pitch = fb_pitch;

    /* Also populate the legacy vesa_info_t for compatibility */
    current_mode.fb_addr = fb_addr;
    current_mode.fb_width = fb_width;
    current_mode.fb_height = fb_height;
    current_mode.fb_bpp = fb_bpp;
    current_mode.fb_pitch = fb_pitch;
    current_mode.fb_size = fb_pitch * fb_height;

    /* Map framebuffer into virtual address space */
    uint32_t fb_start = fb_addr;
    uint32_t fb_end = fb_start + current_mode.fb_size;
    fb_end = (fb_end + 0xFFF) & ~0xFFF;

    for (uint32_t addr = fb_start & ~0xFFF; addr < fb_end; addr += 0x1000) {
        vmm_map_page(vmm_get_current_dir(), addr, addr, VMM_PAGE_PRESENT | VMM_PAGE_WRITABLE);
    }

    framebuffer = (uint32_t *)(uintptr_t)fb_addr;
    vbe_initialized = 1;

    /* Try to locate VBE PM interface for future mode switches */
    vbe_find_pm_interface();
}

/*
 * Set a VBE mode with the specified resolution and bpp.
 *
 * Strategy:
 *  1. If VBE PM interface was found, use it to set the mode directly.
 *     We need to know the VBE mode number for the requested resolution.
 *     Common VBE mode numbers for standard resolutions:
 *       0x118 = 1024x768x24, 0x11B = 1280x1024x32, etc.
 *     Since we cannot enumerate modes without BIOS, we try a lookup table.
 *  2. If PM interface not available, check if the requested mode matches
 *     the current mode (set by bootloader) - return success.
 *  3. Otherwise, return failure.
 *
 * Returns 0 on success, -1 on failure.
 */
int vbe_set_mode(uint32_t width, uint32_t height, uint32_t bpp) {
    /* If already in the requested mode, nothing to do */
    if (vbe_initialized &&
        vbe_current.width == (uint16_t)width &&
        vbe_current.height == (uint16_t)height &&
        vbe_current.bpp == (uint8_t)bpp) {
        return 0;
    }

    /* Common VBE mode number lookup table (linear framebuffer modes) */
    typedef struct {
        uint16_t width;
        uint16_t height;
        uint8_t  bpp;
        uint16_t vbe_mode;
    } vbe_mode_entry_t;

    static const vbe_mode_entry_t mode_table[] = {
        { 640,   480,  32, 0x112 },
        { 640,   480,  24, 0x111 },
        { 800,   600,  32, 0x115 },
        { 800,   600,  24, 0x114 },
        { 1024,  768,  32, 0x118 },
        { 1024,  768,  24, 0x117 },
        { 1280,  1024, 32, 0x11B },
        { 1280,  1024, 24, 0x11A },
        { 1600,  1200, 32, 0x11E },
        { 1600,  1200, 24, 0x11D },
        { 0, 0, 0, 0 }  /* sentinel */
    };

    /* Try VBE PM interface if available */
    if (vbe_pm_entry) {
        uint16_t target_mode = 0xFFFF;
        for (int i = 0; mode_table[i].width != 0; i++) {
            if (mode_table[i].width == (uint16_t)width &&
                mode_table[i].height == (uint16_t)height &&
                mode_table[i].bpp == (uint8_t)bpp) {
                target_mode = mode_table[i].vbe_mode;
                break;
            }
        }

        if (target_mode != 0xFFFF) {
            if (vbe_pm_set_mode(target_mode) == 0) {
                /* Mode set succeeded - update our info */
                vbe_current.mode = target_mode;
                vbe_current.width = (uint16_t)width;
                vbe_current.height = (uint16_t)height;
                vbe_current.bpp = (uint8_t)bpp;
                /* Note: framebuffer address and pitch may change.
                 * In a full implementation we'd query VBE mode info.
                 * For now, assume linear framebuffer at the same base. */
                vbe_current.pitch = (uint32_t)width * (bpp / 8);
                current_mode.fb_width = width;
                current_mode.fb_height = height;
                current_mode.fb_bpp = bpp;
                current_mode.fb_pitch = vbe_current.pitch;
                current_mode.fb_size = vbe_current.pitch * height;
                return 0;
            }
        }
    }

    /* Fallback: cannot switch mode without PM interface or matching mode */
    return -1;
}

int is_vbe_mode(void) {
    return vbe_initialized;
}

vbe_mode_info_t *vbe_get_current_mode(void) {
    if (!vbe_initialized) return (void *)0;
    return &vbe_current;
}

uint32_t *vbe_get_framebuffer(void) {
    return framebuffer;
}
