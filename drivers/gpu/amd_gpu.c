#include "amd_gpu.h"
#include "pci.h"
#include "vmm.h"
#include "string.h"

static amd_gpu_t amd_gpu;

static uint32_t amd_mmio_read(uint32_t offset) {
    *((volatile uint32_t *)(amd_gpu.mmio_base + AMD_MM_INDEX)) = offset;
    return *((volatile uint32_t *)(amd_gpu.mmio_base + AMD_MM_DATA));
}

static void amd_mmio_write(uint32_t offset, uint32_t val) {
    *((volatile uint32_t *)(amd_gpu.mmio_base + AMD_MM_INDEX)) = offset;
    *((volatile uint32_t *)(amd_gpu.mmio_base + AMD_MM_DATA)) = val;
}

int amd_gpu_init(void) {
    memset(&amd_gpu, 0, sizeof(amd_gpu_t));

    /* Scan PCI for AMD GPU device */
    uint16_t bus;
    uint8_t dev, func;
    int found = 0;
    for (bus = 0; bus < PCI_MAX_BUSES && !found; bus++) {
        for (dev = 0; dev < PCI_MAX_DEVICES && !found; dev++) {
            for (func = 0; func < PCI_MAX_FUNCTIONS && !found; func++) {
                uint32_t vd = pci_read_config((uint8_t)bus, dev, func, 0x00);
                if (vd == 0xFFFFFFFF || vd == 0) continue;
                uint16_t vendor = vd & 0xFFFF;
                if (vendor == AMD_VENDOR_ID) {
                    uint8_t base_class = (pci_read_config((uint8_t)bus, dev, func, 0x08) >> 24) & 0xFF;
                    if (base_class == 0x03) { /* Display controller */
                        found = 1;
                    }
                }
            }
        }
    }
    if (!found) return -1;
    bus--; dev--; func--;

    amd_gpu.pci_bus = (uint8_t)bus;
    amd_gpu.pci_dev = dev;
    amd_gpu.pci_func = func;
    amd_gpu.device_id = (pci_read_config((uint8_t)bus, dev, func, 0x00) >> 16) & 0xFFFF;

    /* Enable PCI bus master and memory access */
    uint32_t cmd = pci_read_config((uint8_t)bus, dev, func, 0x04);
    pci_write_config((uint8_t)bus, dev, func, 0x04, cmd | 0x07);

    /* Map MMIO BAR (BAR0 or BAR2 depending on device) */
    uint32_t bar0 = pci_read_config((uint8_t)bus, dev, func, 0x10);
    uint32_t mmio_phys = bar0 & 0xFFFFFFF0;
    if (mmio_phys == 0) {
        /* Try BAR2 for some AMD GPUs */
        uint32_t bar2 = pci_read_config((uint8_t)bus, dev, func, 0x18);
        mmio_phys = bar2 & 0xFFFFFFF0;
    }
    if (mmio_phys == 0) return -1;

    amd_gpu.mmio_base = (uint32_t)vmm_map_physical(mmio_phys, 0x80000);

    /* Map framebuffer BAR (BAR0 for older, BAR6 for newer) */
    uint32_t fb_bar = pci_read_config((uint8_t)bus, dev, func, 0x10);
    uint32_t fb_phys = fb_bar & 0xFFFFFFF0;
    if (fb_phys == 0) {
        fb_bar = pci_read_config((uint8_t)bus, dev, func, 0x14);
        fb_phys = fb_bar & 0xFFFFFFF0;
    }
    if (fb_phys == 0) {
        fb_bar = pci_read_config((uint8_t)bus, dev, func, 0x30);
        fb_phys = fb_bar & 0xFFFFFFF0;
    }

    /* Detect VRAM size - read from config space or use a default */
    amd_gpu.vram_size = 256 * 1024 * 1024; /* Default 256MB, real detection needs AtomBIOS */

    if (fb_phys != 0) {
        amd_gpu.fb_phys = fb_phys;
        /* Map enough pages for the framebuffer */
        uint32_t fb_map_size = (amd_gpu.vram_size + 0xFFF) & ~0xFFF;
        amd_gpu.fb_mapped = (uint32_t *)vmm_map_physical(fb_phys, fb_map_size);
    }

    /* Default mode - will be overridden by VBE mode from bootloader */
    amd_gpu.width = 1024;
    amd_gpu.height = 768;
    amd_gpu.pitch = amd_gpu.width * 4;
    amd_gpu.enabled = 1;

    return 0;
}

int amd_gpu_set_mode(uint32_t width, uint32_t height) {
    if (!amd_gpu.enabled) return -1;

    /* Real mode setting requires AtomBIOS which is complex.
     * For now, just store the desired mode and assume the
     * bootloader has already set a VBE mode. */
    amd_gpu.width = width;
    amd_gpu.height = height;
    amd_gpu.pitch = width * 4;

    return 0;
}

uint32_t *amd_gpu_get_framebuffer(void) {
    if (!amd_gpu.enabled) return (void *)0;
    return amd_gpu.fb_mapped;
}

uint32_t amd_gpu_get_vram_size(void) {
    return amd_gpu.vram_size;
}

void amd_gpu_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!amd_gpu.enabled || !amd_gpu.fb_mapped) return;

    uint32_t row, col;
    for (row = y; row < y + h && row < amd_gpu.height; row++) {
        for (col = x; col < x + w && col < amd_gpu.width; col++) {
            amd_gpu.fb_mapped[row * amd_gpu.width + col] = color;
        }
    }
}

void amd_gpu_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint32_t *data) {
    if (!amd_gpu.enabled || !amd_gpu.fb_mapped) return;

    uint32_t row, col;
    for (row = 0; row < h && (y + row) < amd_gpu.height; row++) {
        for (col = 0; col < w && (x + col) < amd_gpu.width; col++) {
            amd_gpu.fb_mapped[(y + row) * amd_gpu.width + (x + col)] = data[row * w + col];
        }
    }
}
