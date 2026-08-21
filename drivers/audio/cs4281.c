/* cs4281.c - Cirrus Logic CS4281 PCI audio driver.
 *
 * Implements a minimal AC'97-compatible playback path using the CS4281's
 * on-board DMA. Two DMA channels are supported (front + rear), but the
 * driver only programs channel 0 by default.
 */

#include "cs4281.h"
#include "pci.h"
#include "io.h"
#include "kheap.h"
#include "string.h"

#define CS4281_TIMEOUT 100000

static uint32_t g_ba0;          /* Native register base */
static uint16_t g_ba1;          /* AC'97 I/O base */
static uint8_t  g_bus, g_dev, g_func;
static int      g_inited;
static int      g_playing;
static uint32_t g_buffer_phys;
static uint32_t g_buffer_size;
static uint8_t *g_buffer_virt;

/* Read/write native registers (32-bit, memory-mapped). */
static inline uint32_t cs_read(uint32_t off) {
    volatile uint32_t *p = (volatile uint32_t *)(g_ba0 + off);
    return *p;
}

static inline void cs_write(uint32_t off, uint32_t val) {
    volatile uint32_t *p = (volatile uint32_t *)(g_ba0 + off);
    *p = val;
}

/* Wait for an AC'97 codec register access to complete. */
static int cs_ac97_ready(void) {
    for (int i = 0; i < CS4281_TIMEOUT; i++) {
        uint8_t s = inb(g_ba1 + CS4281_AC97_INDEX);
        if (!(s & 0x80)) return 0;
    }
    return -1;
}

static uint16_t cs_ac97_read(uint8_t reg) {
    cs_ac97_ready();
    outw(g_ba1 + CS4281_AC97_INDEX, reg);
    cs_ac97_ready();
    return inw(g_ba1 + CS4281_AC97_DATA);
}

static void cs_ac97_write(uint8_t reg, uint16_t val) {
    cs_ac97_ready();
    outw(g_ba1 + CS4281_AC97_INDEX, reg);
    cs_ac97_ready();
    outw(g_ba1 + CS4281_AC97_DATA, val);
}

/* Cold reset the codec. */
static int cs_ac97_reset(void) {
    outw(g_ba1 + CS4281_AC97_RESET, 0xFFFF);
    /* Wait for codec to come back. */
    for (volatile int i = 0; i < 100000; i++);
    cs_ac97_ready();
    uint16_t ext = cs_ac97_read(0x2A);
    if (!(ext & 0x0001)) return -1;
    return 0;
}

static int cs_probe_pci(void) {
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            for (int func = 0; func < 8; func++) {
                uint32_t id = pci_read_config(bus, dev, func, 0x00);
                uint16_t ven = (uint16_t)(id & 0xFFFF);
                uint16_t did = (uint16_t)((id >> 16) & 0xFFFF);
                if (ven == CS4281_VENDOR_ID && did == CS4281_DEVICE_ID) {
                    g_bus = (uint8_t)bus;
                    g_dev = (uint8_t)dev;
                    g_func = (uint8_t)func;
                    return 0;
                }
                uint32_t hdr = pci_read_config(bus, dev, func, 0x0C);
                if (func == 0 && !(hdr & 0x00800000)) break;
            }
        }
    }
    return -1;
}

static void cs_enable_pci(void) {
    uint32_t cmd = pci_read_config(g_bus, g_dev, g_func, CS4281_PCI_CMD);
    cmd |= (1 << 0)   /* I/O enable */
         | (1 << 1)   /* Memory enable */
         | (1 << 2);  /* Bus master */
    pci_write_config(g_bus, g_dev, g_func, CS4281_PCI_CMD, cmd);
}

int cs4281_init(uint8_t bus, uint8_t dev, uint8_t func) {
    if (g_inited) return 0;
    g_bus = bus;
    g_dev = dev;
    g_func = func;

    uint32_t bar0 = pci_read_config(bus, dev, func, CS4281_PCI_BAR0);
    if (bar0 & 0x01) {
        /* The CS4281 BA0 is typically memory-mapped. Reject I/O mode. */
        return -1;
    }
    g_ba0 = bar0 & 0xFFFFFFF0;

    uint32_t bar1 = pci_read_config(bus, dev, func, CS4281_PCI_BAR1);
    if (!(bar1 & 0x01)) return -1;
    g_ba1 = (uint16_t)(bar1 & 0xFFFFFFFC);
    if (g_ba1 == 0) return -1;

    cs_enable_pci();

    /* Reset the CODEC. */
    cs_ac97_reset();

    /* Set master volume to ~80% (AC'97: 0 = full volume). */
    cs_ac97_write(0x02, 0x0808);
    cs_ac97_write(0x18, 0x0808);

    /* Reset the playback DMA engine. */
    cs_write(CS4281_D0CC, CS4281_DMA_RESET);
    cs_write(CS4281_D0CL, 0);
    cs_write(CS4281_D0CA, 0);
    cs_write(CS4281_D0CS, 0);

    /* Allocate DMA buffer (must be physically contiguous, < 4GB). */
    g_buffer_size = 65536;
    g_buffer_virt = (uint8_t *)kmalloc(g_buffer_size);
    if (!g_buffer_virt) return -1;
    g_buffer_phys = (uint32_t)g_buffer_virt;
    memset(g_buffer_virt, 0, g_buffer_size);

    g_inited = 1;
    return 0;
}

int cs4281_reset(void) {
    if (!g_inited) return -1;
    cs4281_stop();
    return cs_ac97_reset();
}

int cs4281_play(const int16_t *samples, uint32_t frames,
                uint32_t sample_rate, uint8_t channels)
{
    if (!g_inited) return -1;
    if (!samples || frames == 0) return -1;
    if (channels < 1 || channels > 2) return -1;
    /* The CS4281 has no SRC; sample rate must be a multiple of the codec's
     * configured rate (typically 48 kHz). Callers should pre-resample. */
    (void)sample_rate;

    uint32_t bytes = frames * sizeof(int16_t) * channels;
    if (bytes > g_buffer_size) bytes = g_buffer_size;
    memcpy(g_buffer_virt, samples, bytes);
    uint32_t actual_frames = bytes / (sizeof(int16_t) * channels);

    /* Program DMA channel 0. */
    cs_write(CS4281_D0CA, g_buffer_phys);
    cs_write(CS4281_D0CL, actual_frames);
    cs_write(CS4281_D0CS, 0);
    uint32_t ctrl = CS4281_DMA_ENABLE | CS4281_DMA_FMT_16;
    if (channels == 2) ctrl |= CS4281_DMA_STEREO;
    cs_write(CS4281_D0CC, ctrl);

    g_playing = 1;
    return 0;
}

void cs4281_stop(void) {
    if (!g_inited) return;
    cs_write(CS4281_D0CC, CS4281_DMA_RESET);
    g_playing = 0;
}