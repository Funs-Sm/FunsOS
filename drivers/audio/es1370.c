/* es1370.c - ESS ES1370 / Ensoniq AudioPCI PCI audio driver.
 *
 * Sets up the AC'97 codec and programs DMA channel 0 for PCM playback.
 * Supports sample rates from 8 kHz to 48 kHz via the SRC, with 16-bit
 * stereo as the only format. The DMA buffer must be contiguous physical
 * memory below 4 GB.
 */

#include "es1370.h"
#include "pci.h"
#include "io.h"
#include "kheap.h"
#include "string.h"

#define ES1370_TIMEOUT 100000

static uint32_t g_io_base;
static uint8_t  g_bus, g_dev, g_func;
static int      g_inited;
static int      g_playing;
static uint32_t g_buffer_phys;
static uint32_t g_buffer_size;
static uint8_t *g_buffer_virt;

static uint32_t es_read(uint32_t off) {
    return inl(g_io_base + off);
}

static void es_write(uint32_t off, uint32_t val) {
    outl(g_io_base + off, val);
}

static void es_write16(uint32_t off, uint16_t val) {
    outw(g_io_base + off, val);
}

/* Wait for AC'97 codec ready bit. */
static int es_codec_ready(void) {
    for (int i = 0; i < ES1370_TIMEOUT; i++) {
        uint32_t s = es_read(ES1370_REG_CODEC_STAT);
        if (s & 0x01) return 0;
    }
    return -1;
}

/* Write a value to a codec register. */
static void es_codec_write(uint8_t reg, uint16_t val) {
    /* Wait until previous write has completed. */
    es_codec_ready();
    es_write(ES1370_REG_CODEC, ((uint32_t)reg << 16) | val);
    /* Wait again for completion (codec ready bit clears during transfer). */
    es_codec_ready();
}

static uint16_t es_codec_read(uint8_t reg) {
    es_write(ES1370_REG_CODEC_STAT, ((uint32_t)reg << 16) | 0x80000000u);
    es_codec_ready();
    return (uint16_t)(es_read(ES1370_REG_CODEC_STAT) & 0xFFFF);
}

/* Cold reset the AC'97 codec. */
static int es_codec_reset(void) {
    /* Toggle the SYNC bit (RESET) and wait for codec to come back ready. */
    es_write(ES1370_REG_CODEC, 0x00080000);   /* issue reset */
    for (volatile int i = 0; i < 100000; i++);
    es_codec_ready();
    uint16_t ext = es_codec_read(AC97_REG_EXT_AUDIO);
    if (!(ext & 0x0001)) {
        /* Codec doesn't advertise basic audio capability; abort. */
        return -1;
    }
    return 0;
}

/* Probe the PCI bus for the ES1370 device and enables its BAR0. */
static int es_probe_pci(void) {
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            for (int func = 0; func < 8; func++) {
                uint32_t id = pci_read_config(bus, dev, func, 0x00);
                uint16_t ven = (uint16_t)(id & 0xFFFF);
                uint16_t did = (uint16_t)((id >> 16) & 0xFFFF);
                if (ven == ES1370_VENDOR_ID && did == ES1370_DEVICE_ID) {
                    g_bus = (uint8_t)bus;
                    g_dev = (uint8_t)dev;
                    g_func = (uint8_t)func;
                    return 0;
                }
                /* Multi-function devices: if bit 7 of header type is 0,
                 * we can skip functions 1..7. */
                uint32_t hdr = pci_read_config(bus, dev, func, 0x0C);
                if (func == 0 && !(hdr & 0x00800000)) break;
            }
        }
    }
    return -1;
}

/* Enable bus-mastering + memory space for the ES1370 PCI device. */
static void es_enable_pci(void) {
    uint32_t cmd = pci_read_config(g_bus, g_dev, g_func, ES1370_PCI_CMD);
    cmd |= (1 << 0)   /* I/O space enable */
         | (1 << 1)   /* Memory space enable */
         | (1 << 2);  /* Bus master enable */
    pci_write_config(g_bus, g_dev, g_func, ES1370_PCI_CMD, cmd);
}

/* Compute the sample-count divisor for the desired rate. The ES1370 has a
 * fixed 48 kHz DMA clock; we use the SRC to resample. */
static uint32_t es_src_divisor(uint32_t sample_rate) {
    /* SRC divisor = (sample_rate << 16) / 48000. */
    if (sample_rate == 0) sample_rate = ES1370_SAMPLE_RATE_DEFAULT;
    if (sample_rate > 48000) sample_rate = 48000;
    return (sample_rate << 16) / 48000;
}

int es1370_init(uint8_t bus, uint8_t dev, uint8_t func) {
    if (g_inited) return 0;
    g_bus = bus;
    g_dev = dev;
    g_func = func;

    /* Read BAR0 (I/O space). */
    uint32_t bar = pci_read_config(bus, dev, func, 0x10);
    if (bar & 0x01) {
        g_io_base = bar & 0xFFFFFFFC;
    } else {
        /* Memory-mapped BAR; this driver uses I/O registers for simplicity. */
        return -1;
    }
    if (g_io_base == 0) return -1;

    es_enable_pci();

    /* Software reset. */
    es_write(ES1370_REG_CONTROL, ES1370_CTRL_RESET);
    for (volatile int i = 0; i < 10000; i++);
    es_write(ES1370_REG_CONTROL, 0);

    if (es_codec_reset() != 0) return -1;

    /* Set master volume to ~80% (0 = full volume for AC'97). */
    es_codec_write(AC97_REG_MASTER_VOL, 0x0808);
    es_codec_write(AC97_REG_PCM_VOL, 0x0808);
    /* Route DAC0 to PCM output by default. */
    es_codec_write(AC97_REG_EXT_CTRL, 0x0000);

    /* Allocate a 64KB DMA buffer (must be below 4GB). */
    g_buffer_size = 65536;
    g_buffer_virt = (uint8_t *)kmalloc(g_buffer_size);
    if (!g_buffer_virt) return -1;
    g_buffer_phys = (uint32_t)g_buffer_virt;  /* identity mapped */
    memset(g_buffer_virt, 0, g_buffer_size);

    g_inited = 1;
    return 0;
}

int es1370_reset(void) {
    if (!g_inited) return -1;
    es1370_stop();
    return es_codec_reset();
}

/* Start playing a sample buffer. The buffer must remain valid until
 * es1370_stop() is called or the playback completes naturally. */
int es1370_play(const int16_t *samples, uint32_t sample_count,
                uint32_t sample_rate, uint8_t channels)
{
    if (!g_inited) return -1;
    if (!samples || sample_count == 0) return -1;
    if (channels < 1 || channels > 2) return -1;

    /* Copy samples into the DMA buffer. */
    uint32_t bytes = sample_count * sizeof(int16_t) * channels;
    if (bytes > g_buffer_size) bytes = g_buffer_size;
    memcpy(g_buffer_virt, samples, bytes);
    uint32_t frames = bytes / (sizeof(int16_t) * channels);

    /* Configure the SRC for the desired sample rate. */
    es_write(ES1370_REG_SRC_VOL_L, 0xFFFFFFFFu);
    es_write(ES1370_REG_SRC_VOL_R, 0xFFFFFFFFu);
    es_write(ES1370_REG_SRC_ROUTE, 0x00000000);   /* DAC0 -> output */
    es_write(ES1370_REG_TIMING, es_src_divisor(sample_rate));

    /* Program DMA channel 0. */
    es_write(ES1370_REG_DAC0_COUNT, frames);
    es_write(ES1370_REG_DAC0_ADDR,  g_buffer_phys);
    es_write(ES1370_REG_DAC0_FRAME, frames - 1);

    /* Enable playback: clear reset, set IRQs. */
    es_write(ES1370_REG_CONTROL, ES1370_CTRL_EN_IRQ0);

    g_playing = 1;
    return 0;
}

void es1370_stop(void) {
    if (!g_inited) return;
    es_write(ES1370_REG_CONTROL, 0);
    g_playing = 0;
}

int es1370_is_playing(void) {
    return g_playing;
}