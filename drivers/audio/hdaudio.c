/* hdaudio.c - Intel High Definition Audio PCI driver.
 *
 * Initializes the HDA controller, programs the CORB/RIRB for codec
 * communication, and configures output converter 0 to play a single
 * PCM stream. The DMA buffer is described by a Buffer Descriptor List
 * (BDL) with one or more entries pointing to contiguous physical
 * memory below 4 GB.
 */

#include "hdaudio.h"
#include "pci.h"
#include "io.h"
#include "kheap.h"
#include "string.h"

#define HDA_TIMEOUT 100000

/* Output stream index - HDA supports up to 15 output streams per controller. */
#define HDA_OUTPUT_STREAM     1
#define HDA_CODEC_ADDR        0   /* Codec 0 */

static volatile uint32_t *g_regs;
static uint16_t g_ossd_offset;        /* offset of output stream descriptor regs */
static uint16_t g_corb_size;
static uint16_t g_rirb_size;
static uint32_t g_corb_phys;
static uint32_t g_rirb_phys;
static uint32_t g_rirb_wp;
static uint64_t *g_corb;
static uint64_t *g_rirb;
static uint32_t g_bdl_phys;
static hda_bdl_entry_t *g_bdl;
static uint32_t g_buffer_phys;
static uint32_t g_buffer_size;
static uint8_t *g_buffer_virt;
static int g_inited;
static int g_playing;

static inline uint32_t hda_read(uint32_t off) {
    return g_regs[off / 4];
}

static inline void hda_write(uint32_t off, uint32_t val) {
    g_regs[off / 4] = val;
}

static inline uint8_t hda_read8(uint32_t off) {
    return ((volatile uint8_t *)g_regs)[off];
}

static inline void hda_write8(uint32_t off, uint8_t val) {
    ((volatile uint8_t *)g_regs)[off] = val;
}

static inline uint16_t hda_read16(uint32_t off) {
    return ((volatile uint16_t *)g_regs)[off / 2];
}

static inline void hda_write16(uint32_t off, uint16_t val) {
    ((volatile uint16_t *)g_regs)[off / 2] = val;
}

/* Read extended capability word. */
static uint16_t hda_get_cap(uint32_t reg) {
    return hda_read16(reg);
}

/* Send a single codec command and return 0 if accepted. */
static int hda_send_cmd(uint8_t cad, uint16_t nid, uint16_t verb) {
    uint32_t cmd = ((uint32_t)cad << 28) | ((uint32_t)nid << 20) | verb;
    /* Find a free CORB entry. */
    uint16_t wp = hda_read16(HDA_REG_CORBWP);
    uint16_t rp = hda_read16(HDA_REG_CORBRP);
    if (wp == rp) return -1;
    g_corb[wp % g_corb_size] = cmd;
    /* Update write pointer. */
    hda_write16(HDA_REG_CORBWP, (uint16_t)(wp + 1));
    return 0;
}

/* Enable / disable the HDA controller's CRST (controller reset). */
static void hda_set_crst(int enable) {
    uint32_t v = hda_read(HDA_REG_GCTL);
    if (enable) v |= HDA_GCTL_CRST; else v &= ~HDA_GCTL_CRST;
    hda_write(HDA_REG_GCTL, v);
    if (enable) {
        /* Wait for CRST to actually take. */
        for (volatile int i = 0; i < HDA_TIMEOUT; i++) {
            if (hda_read(HDA_REG_GCTL) & HDA_GCTL_CRST) return;
        }
    }
}

static int hda_probe_pci(void) {
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            for (int func = 0; func < 8; func++) {
                uint32_t id = pci_read_config(bus, dev, func, 0x00);
                uint16_t ven = (uint16_t)(id & 0xFFFF);
                /* We accept any Intel HDA device; some clones exist. */
                if (ven != HDAUDIO_VENDOR_INTEL) continue;
                uint32_t r2 = pci_read_config(bus, dev, func, 0x08);
                uint8_t class = (uint8_t)((r2 >> 24) & 0xFF);
                uint8_t sub = (uint8_t)((r2 >> 16) & 0xFF);
                if (class == HDAUDIO_PCI_CLASS && sub == HDAUDIO_PCI_SUBCLASS) {
                    return 0;  /* pci_get_bdf would normally be used here */
                }
                uint32_t hdr = pci_read_config(bus, dev, func, 0x0C);
                if (func == 0 && !(hdr & 0x00800000)) break;
            }
        }
    }
    return -1;
}

static void hda_enable_pci(uint8_t bus, uint8_t dev, uint8_t func) {
    uint32_t cmd = pci_read_config(bus, dev, func, 0x04);
    cmd |= (1 << 1)   /* Memory space */
         | (1 << 2);  /* Bus master */
    pci_write_config(bus, dev, func, 0x04, cmd);
}

int hdaudio_init(uint8_t bus, uint8_t dev, uint8_t func) {
    if (g_inited) return 0;

    uint32_t bar = pci_read_config(bus, dev, func, 0x10);
    if (bar & 0x01) return -1;  /* must be MMIO */
    g_regs = (volatile uint32_t *)(bar & 0xFFFFFFF0);
    if (!g_regs) return -1;

    hda_enable_pci(bus, dev, func);

    /* Controller reset. */
    hda_set_crst(0);
    for (volatile int i = 0; i < 10000; i++);
    hda_set_crst(1);

    /* Read capabilities. */
    uint16_t gcap = hda_get_cap(HDA_REG_GCAP);
    uint8_t n_streams = (uint8_t)((gcap >> 12) & 0x0F);
    uint8_t n_corb    = (uint8_t)((gcap >> 16) & 0x03);
    uint8_t n_rirb    = (uint8_t)((gcap >> 18) & 0x03);
    (void)n_streams;
    g_corb_size = (uint16_t)64 << n_corb;
    g_rirb_size = (uint16_t)64 << n_rirb;

    /* Stop CORB and RIRB while we set them up. */
    hda_write8(HDA_REG_CORBCTL, 0);
    hda_write8(HDA_REG_RIRBCTL, 0);

    /* Allocate physically-contiguous buffers. */
    g_corb = (uint64_t *)kmalloc(g_corb_size * sizeof(uint64_t));
    g_rirb = (uint64_t *)kmalloc(g_rirb_size * sizeof(uint64_t));
    if (!g_corb || !g_rirb) return -1;
    memset(g_corb, 0, g_corb_size * sizeof(uint64_t));
    memset(g_rirb, 0, g_rirb_size * sizeof(uint64_t));
    g_corb_phys = (uint32_t)g_corb;
    g_rirb_phys = (uint32_t)g_rirb;

    /* Program CORB base. */
    hda_write(HDA_REG_CORBLBASE, g_corb_phys);
    hda_write(HDA_REG_CORBUBASE, 0);
    hda_write16(HDA_REG_CORBSIZE, 0x8000 | (n_corb << 1));  /* 256B entries... */
    hda_write16(HDA_REG_CORBRP, 0);
    hda_write16(HDA_REG_CORBWP, 0);
    hda_write8(HDA_REG_CORBCTL, 0x02);  /* enable CORB */

    /* Program RIRB base. */
    hda_write(HDA_REG_RIRBLBASE, g_rirb_phys);
    hda_write(HDA_REG_RIRBUBASE, 0);
    hda_write16(HDA_REG_RIRBSIZE, 0x8000 | (n_rirb << 1));
    hda_write16(HDA_REG_RIRBWP, 0);
    hda_write16(HDA_REG_RINTCNT, 1);
    hda_write8(HDA_REG_RIRBCTL, 0x02);  /* enable RIRB */

    /* Allocate DMA buffer + BDL. */
    g_buffer_size = 65536;
    g_buffer_virt = (uint8_t *)kmalloc(g_buffer_size);
    if (!g_buffer_virt) return -1;
    g_buffer_phys = (uint32_t)g_buffer_virt;
    memset(g_buffer_virt, 0, g_buffer_size);

    g_bdl = (hda_bdl_entry_t *)kmalloc(sizeof(hda_bdl_entry_t) * 2);
    if (!g_bdl) return -1;
    g_bdl_phys = (uint32_t)g_bdl;
    memset(g_bdl, 0, sizeof(hda_bdl_entry_t) * 2);
    g_bdl[0].addr = g_buffer_phys;
    g_bdl[0].len  = g_buffer_size;
    g_bdl[0].flags = 1;  /* IOC - generate interrupt on completion */

    /* Configure the output stream. */
    g_ossd_offset = HDA_REG_SD_BASE + HDA_OUTPUT_STREAM * HDA_REG_SD_STRIDE;
    hda_write8(g_ossd_offset + HDA_SD_REG_CTL, HDA_SD_CTL_SRST);
    for (volatile int i = 0; i < 10000; i++);
    hda_write8(g_ossd_offset + HDA_SD_REG_CTL, 0);

    /* Set the stream format (16-bit, 2-ch, 48 kHz). */
    hda_write16(g_ossd_offset + HDA_SD_REG_FMT, HDA_FMT_48K_16B_STEREO);

    /* Program the BDL base address and length. */
    hda_write(g_ossd_offset + HDA_SD_REG_BDLPL, g_bdl_phys);
    hda_write(g_ossd_offset + HDA_SD_REG_BDLPU, 0);
    hda_write16(g_ossd_offset + HDA_SD_REG_LVI, 0);  /* last valid index = 0 */
    hda_write(g_ossd_offset + HDA_SD_REG_CBL, g_buffer_size);

    /* Enable interrupts at controller level (optional - poll by default). */
    hda_write(HDA_REG_INTCTL, 0);

    g_inited = 1;
    return 0;
}

int hdaudio_play(const int16_t *samples, uint32_t frames,
                 uint32_t sample_rate, uint8_t channels)
{
    if (!g_inited) return -1;
    if (!samples || frames == 0) return -1;
    if (channels < 1 || channels > 2) return -1;
    /* For simplicity we only support 48 kHz 16-bit stereo; other rates
     * need the HDA stream format register updated and the buffer
     * refilled. Callers should pre-resample. */
    (void)sample_rate;

    uint32_t bytes = frames * sizeof(int16_t) * channels;
    if (bytes > g_buffer_size) bytes = g_buffer_size;
    memcpy(g_buffer_virt, samples, bytes);
    g_bdl[0].len = bytes;

    /* Start the stream. */
    uint8_t ctl = hda_read8(g_ossd_offset + HDA_SD_REG_CTL);
    ctl &= ~HDA_SD_CTL_SRST;
    ctl |= HDA_SD_CTL_RUN;
    hda_write8(g_ossd_offset + HDA_SD_REG_CTL, ctl);

    g_playing = 1;
    return 0;
}

void hdaudio_stop(void) {
    if (!g_inited) return;
    uint8_t ctl = hda_read8(g_ossd_offset + HDA_SD_REG_CTL);
    ctl &= ~HDA_SD_CTL_RUN;
    ctl |= HDA_SD_CTL_SRST;
    hda_write8(g_ossd_offset + HDA_SD_REG_CTL, ctl);
    g_playing = 0;
}

int hdaudio_is_playing(void) {
    return g_playing;
}

/* Stubs for extended mixer / format / positional APIs that the higher-level
 * audio/sound.c wrapper expects.  The minimal HDA driver here only supports
 * the bare playback path; the mixer/format knobs are accepted but ignored so
 * that audio/sound.c links successfully. */
int hdaudio_record(const int16_t *samples, uint32_t frames,
                   uint32_t sample_rate, uint8_t channels) {
    (void)samples; (void)frames; (void)sample_rate; (void)channels;
    return -1;
}
int hdaudio_set_volume(uint8_t left, uint8_t right) {
    (void)left; (void)right;
    return 0;
}
int hdaudio_get_volume(uint8_t *left, uint8_t *right) {
    if (left) *left = 0;
    if (right) *right = 0;
    return 0;
}
int hdaudio_set_mute(int mute) {
    (void)mute;
    return 0;
}
int hdaudio_get_mute(void) { return 0; }
int hdaudio_set_format(uint16_t fmt) { (void)fmt; return 0; }
int hdaudio_set_sample_rate(uint32_t rate) { (void)rate; return 0; }
uint32_t hdaudio_get_buffer_position(void) { return 0; }
int hdaudio_is_available(void) { return g_inited; }
void *hdaudio_get_controller(void) { return (void *)(uintptr_t)g_regs; }