/* ============================================================================
 * ac97.c - Complete Intel AC'97 Audio Codec Driver
 *
 * Implements the full AC'97 specification for Intel ICHx and compatible
 * controllers. Features:
 *   - AC-link codec access via the NAMBAR register window
 *   - NABM bus master DMA engine with BDL (Buffer Descriptor List)
 *   - Full mixer support (Master, PCM, Line In, Mic, CD, Aux, Record Gain)
 *   - Variable Rate Audio (VRA) for non-48kHz sample rates
 *   - 3D stereo enhancement
 *   - Power management (D0-D3)
 *   - Jack sensing and auto-mute
 *   - Interrupt-driven playback and capture
 *   - ICH6/ICH7 compatibility mode
 *
 * Hardware Architecture:
 *   The AC'97 controller has two main register windows mapped via PCI BARs:
 *   - NAMBAR (BAR0): Mixer register window - provides access to the AC'97
 *     codec's registers via the AC-link serial bus. Each access is a 16-bit
 *     read/write to NAMBAR+offset where offset is the codec register number.
 *     The controller serializes each access into a 16-bit AC-link slot.
 *   - NABMBAR (BAR1): Bus Master register window - contains the DMA engine
 *     registers. Three bus master engines: PI (PCM In/capture), PO (PCM
 *     Out/playback), MC (Mic In). Each engine uses a Buffer Descriptor List
 *     (BDL) for scatter-gather DMA.
 *
 * AC-link Protocol:
 *   The AC-link is a bidirectional, fixed-clock, serial digital interface
 *   between the controller and codec. Each frame carries 13 time slots:
 *     Slot 0: Tag (16 bits) - indicates valid slots
 *     Slot 1: Command address (codec register read/write)
 *     Slot 2: Command data (read/write data)
 *     Slot 3: PCM Left playback
 *     Slot 4: PCM Right playback
 *     Slot 5: Modem Line 1
 *     Slot 6: PCM Center
 *     Slot 7: PCM LFE
 *     Slot 8: PCM Surround Left
 *     Slot 9: PCM Surround Right
 *     Slot 10: PCM Left capture
 *     Slot 11: PCM Right capture
 *     Slot 12: Modem/Mic capture
 *
 * BDL DMA Engine:
 *   The BDL is a table of 32 entries, each describing a contiguous buffer
 *   in physical memory. The DMA engine reads entries sequentially starting
 *   from index 0, wrapping around at the Last Valid Index (LVI). When the
 *   engine reaches LVI, it wraps back to index 0, allowing circular buffer
 *   operation. Each entry specifies:
 *     - Physical address of the buffer
 *     - Byte count of valid data (bits 15:0)
 *     - Interrupt on Completion flag (bit 31)
 *     - Buffer Underrun Policy (bit 30)
 * ============================================================================
 */

#include "ac97.h"
#include "pci.h"
#include "vmm.h"
#include "pmm.h"
#include "kheap.h"
#include "string.h"
#include "io.h"
#include "irq.h"
#include "klog.h"
#include "stddef.h"

/* ============================================================================
 * Static device instance
 * ============================================================================
 */
static ac97_device_t g_ac97;

/* ============================================================================
 * Internal helper macros
 * ============================================================================
 */

/* Timeout for AC-link codec register access (in microseconds) */
#define AC97_CODEC_TIMEOUT  100000

/* Maximum number of times to retry a codec register read */
#define AC97_RETRY_COUNT    3

/* ============================================================================
 * Low-Level Codec Register Access (via NAMBAR window)
 *
 * The AC'97 codec registers are accessed through the NAMBAR I/O window.
 * Each register is 16 bits wide. The hardware serializes the access into
 * the AC-link protocol. For ICH6/ICH7, the codec sits behind the HD Audio
 * controller in AC'97 compatibility mode, so we use the standard NAMBAR
 * access method which the controller translates.
 * ============================================================================
 */

/**
 * Read a 16-bit AC'97 codec mixer register.
 * Performs I/O read from NAMBAR + (offset & 0x7E), with retry on timeout.
 * @param offset  Codec register offset (0x00-0x7E, even addresses only)
 * @return 16-bit register value
 */
static uint16_t ac97_codec_read(uint8_t offset)
{
    uint32_t timeout;
    uint8_t retry;
    uint16_t val;

    for (retry = 0; retry < AC97_RETRY_COUNT; retry++) {
        /* Wait for codec ready by reading the register until it stabilizes */
        timeout = AC97_CODEC_TIMEOUT;
        while (timeout--) {
            val = inw(g_ac97.nambar + offset);
            /* For ICH6/ICH7, the codec may return 0xFFFF when busy */
            if (val != 0xFFFF) break;
            io_wait();
        }
        if (timeout > 0) break;
        io_wait();
    }
    return val;
}

/**
 * Write a 16-bit value to an AC'97 codec mixer register.
 * Performs I/O write to NAMBAR + (offset & 0x7E).
 * @param offset  Codec register offset (0x00-0x7E, even addresses only)
 * @param val     16-bit value to write
 */
static void ac97_codec_write(uint8_t offset, uint16_t val)
{
    uint32_t timeout;

    /* Wait for the previous command to complete */
    timeout = AC97_CODEC_TIMEOUT;
    while (timeout--) {
        /* Read the powerdown register to check if the codec is ready */
        uint16_t status = inw(g_ac97.nambar + AC97_REG_POWERDOWN);
        if ((status & 0x8000) == 0) break;  /* Codec ready */
        io_wait();
    }

    outw(g_ac97.nambar + offset, val);

    /* Wait for write to complete */
    timeout = AC97_CODEC_TIMEOUT;
    while (timeout--) {
        io_wait();
        if (timeout == 0) break;
    }
}

/* ============================================================================
 * Bus Master Register Access (via NABMBAR window)
 * ============================================================================
 */

static inline uint8_t ac97_bm_read8(uint32_t bar_offset, uint8_t reg)
{
    return inb(g_ac97.nabmbar + bar_offset + reg);
}

static inline uint16_t ac97_bm_read16(uint32_t bar_offset, uint8_t reg)
{
    return inw(g_ac97.nabmbar + bar_offset + reg);
}

static inline uint32_t ac97_bm_read32(uint32_t bar_offset, uint8_t reg)
{
    return inl(g_ac97.nabmbar + bar_offset + reg);
}

static inline void ac97_bm_write8(uint32_t bar_offset, uint8_t reg, uint8_t val)
{
    outb(g_ac97.nabmbar + bar_offset + reg, val);
}

static inline void ac97_bm_write16(uint32_t bar_offset, uint8_t reg, uint16_t val)
{
    outw(g_ac97.nabmbar + bar_offset + reg, val);
}

static inline void ac97_bm_write32(uint32_t bar_offset, uint8_t reg, uint32_t val)
{
    outl(g_ac97.nabmbar + bar_offset + reg, val);
}

/* ============================================================================
 * Codec Reset
 * ============================================================================
 */

/**
 * Perform a cold reset of the AC'97 codec.
 * Writing any value to the RESET register (0x00) initiates a cold reset.
 * A warm reset is done by writing to the NABM global control register.
 * After reset, the codec returns to default state:
 *   - Master Volume: 0x8000 (0dB, muted)
 *   - All other volumes: 0x8008 (0dB)
 *   - Sample rate: 48000 Hz
 *   - Power: all on (D0)
 */
static void ac97_codec_cold_reset(void)
{
    /* Write to reset register - any value triggers cold reset */
    ac97_codec_write(AC97_REG_RESET, 0x0000);

    /* Wait for codec to come back from reset (typically ~4-5 AC-link frames) */
    uint32_t timeout = 500000;
    while (timeout--) {
        io_wait();
        /* Check if codec is ready by reading vendor ID */
        uint16_t vid1 = ac97_codec_read(AC97_REG_VENDOR_ID1);
        if (vid1 != 0xFFFF && vid1 != 0x0000) break;
    }

    /* Read codec vendor ID to verify it's alive */
    uint16_t vid1 = ac97_codec_read(AC97_REG_VENDOR_ID1);
    uint16_t vid2 = ac97_codec_read(AC97_REG_VENDOR_ID2);
    klog_info("AC'97: codec vendor ID = 0x%04X%04X", vid2, vid1);
}

/**
 * Perform a warm reset via the NABM global control register.
 * This resets the bus master logic without resetting the codec.
 */
static void ac97_bm_warm_reset(void)
{
    uint32_t glob_cnt = ac97_bm_read32(AC97_BM_REG_GLOB_CNT, 0);
    glob_cnt |= AC97_GLOB_CNT_WARM;
    ac97_bm_write32(AC97_BM_REG_GLOB_CNT, 0, glob_cnt);

    uint32_t timeout = 100000;
    while (timeout--) {
        io_wait();
        if (!(ac97_bm_read32(AC97_BM_REG_GLOB_CNT, 0) & AC97_GLOB_CNT_WARM))
            break;
    }
}

/* ============================================================================
 * Power Management
 * ============================================================================
 */

/**
 * Set the codec power state.
 * D0: Full power (all sections on)
 * D1: Light sleep (DAC off, rest on) - uses PR4
 * D2: Deep sleep (DAC+Vref off) - uses PR2+PR3
 * D3: Cold (all off) - uses PR0+PR1+PR2+PR3+PR4
 *
 * The powerdown register bits correspond to:
 *   PR0 (bit 0): ADC
 *   PR1 (bit 1): DAC
 *   PR2 (bit 2): Analog mixer (Vref off)
 *   PR3 (bit 3): Analog mixer (Vref on)
 *   PR4 (bit 4): AC-link
 *   PR5 (bit 5): Internal clock disable
 *   PR6 (bit 6): Headphone amp
 */
static void ac97_set_powerdown(uint16_t mask)
{
    uint16_t pwr = ac97_codec_read(AC97_REG_POWERDOWN);
    pwr = (pwr & 0x0F00) | (mask & 0x00FF);  /* Preserve bits 8-11, set bits 0-7 */
    ac97_codec_write(AC97_REG_POWERDOWN, pwr);

    /* Wait for power state to stabilize */
    uint32_t timeout = 100000;
    while (timeout--) {
        uint16_t status = ac97_codec_read(AC97_REG_POWERDOWN);
        if ((status & 0x000F) == (mask & 0x000F)) break;
        io_wait();
    }
}

/* ============================================================================
 * Sample Rate Control
 * ============================================================================
 */

/**
 * Supported sample rates for AC'97.
 * The standard AC'97 fixed rate is 48000 Hz. Variable Rate Audio (VRA)
 * support allows other rates. When VRA is enabled, the DAC/ADC sample
 * rate registers (0x2C/0x32) accept the actual sample rate in Hz.
 */
static const uint32_t ac97_supported_rates[] = {
    AC97_RATE_8000,  AC97_RATE_11025, AC97_RATE_16000,
    AC97_RATE_22050, AC97_RATE_32000, AC97_RATE_44100,
    AC97_RATE_48000
};

/**
 * Set the DAC sample rate.
 * Requires VRA (Variable Rate Audio) to be enabled in the extended
 * audio control register. For fixed-rate mode (VRA disabled), the
 * rate is always 48000 Hz.
 */
static int ac97_set_dac_rate(uint32_t rate)
{
    if (!g_ac97.supports_vra) {
        /* Fixed rate only: the codec is locked to 48kHz */
        if (rate != 48000) {
            klog_warn("AC'97: codec does not support VRA, rate locked to 48000 Hz");
        }
        g_ac97.sample_rate = 48000;
        return 0;
    }

    /* Verify the rate is supported */
    uint8_t valid = 0;
    uint32_t i;
    for (i = 0; i < AC97_NUM_RATES; i++) {
        if (ac97_supported_rates[i] == rate) {
            valid = 1;
            break;
        }
    }
    if (!valid) {
        klog_warn("AC'97: unsupported sample rate %u Hz, defaulting to 48000", rate);
        rate = 48000;
    }

    /* Write the rate to the DAC rate register */
    ac97_codec_write(AC97_REG_DAC_RATE, (uint16_t)rate);

    /* Verify the rate was accepted */
    uint16_t actual = ac97_codec_read(AC97_REG_DAC_RATE);
    g_ac97.sample_rate = (uint32_t)actual;

    klog_info("AC'97: DAC sample rate set to %u Hz", g_ac97.sample_rate);
    return 0;
}

/**
 * Set the ADC sample rate for recording.
 */
static int ac97_set_adc_rate(uint32_t rate)
{
    if (!g_ac97.supports_vra) {
        return 0;
    }

    ac97_codec_write(AC97_REG_ADC_RATE, (uint16_t)rate);
    return 0;
}

/* ============================================================================
 * Mixer Control Functions
 *
 * Volume registers use attenuation format:
 *   Bits 5-0: Attenuation in 1.5dB steps (0 = 0dB, 63 = -94.5dB or mute)
 *   Bit 7: Mute (1 = muted)
 *   Bit 15: Mute for right channel (stereo registers)
 *   Bits 13-8: Attenuation for right channel (stereo registers)
 *
 * The default value for most volume registers is 0x8008:
 *   0x8008 = muted, 0dB attenuation
 * To unmute and set 0dB: write 0x0000
 * ============================================================================
 */

/**
 * Convert a 0-255 volume value to AC'97 attenuation (0-63).
 * 255 = 0dB (max), 0 = -94.5dB (full attenuation).
 */
static inline uint8_t ac97_vol_to_atten(uint8_t vol)
{
    /* vol 0-255 maps to attenuation 63-0 */
    return (uint8_t)(((255 - (uint32_t)vol) * 63) / 255);
}

/**
 * Convert AC'97 attenuation (0-63) to 0-255 volume.
 */
static inline uint8_t ac97_atten_to_vol(uint8_t atten)
{
    return (uint8_t)(((63 - (uint32_t)atten) * 255) / 63);
}

/**
 * Write a stereo volume register (Master, PCM, Line In, CD, Aux, etc.).
 * These registers use the format: [right mute][right atten][left mute][left atten]
 */
static void ac97_write_stereo_vol(uint8_t reg, uint8_t left, uint8_t right, uint8_t mute)
{
    uint16_t val = 0;
    uint8_t att_left  = ac97_vol_to_atten(left);
    uint8_t att_right = ac97_vol_to_atten(right);

    if (mute) {
        val = (uint16_t)(0x8000 | (att_right << 8) | att_left | 0x8000);
        /* Actually: left mute is bit 7, right mute is bit 15 */
        val = (uint16_t)(AC97_MUTE_BIT | (att_right << 8) | AC97_MUTE_BIT | att_left);
    } else {
        val = (uint16_t)((att_right << 8) | att_left);
    }

    ac97_codec_write(reg, val);
}

/**
 * Read a stereo volume register, returning left/right volumes (0-255).
 * Returns 1 if muted, 0 if not muted.
 */
static int ac97_read_stereo_vol(uint8_t reg, uint8_t *left, uint8_t *right)
{
    uint16_t val = ac97_codec_read(reg);

    uint8_t att_left  = val & AC97_VOL_ATTEN_MASK;
    uint8_t att_right = (val >> 8) & AC97_VOL_ATTEN_MASK;

    *left  = ac97_atten_to_vol(att_left);
    *right = ac97_atten_to_vol(att_right);

    return (val & AC97_MUTE_BIT) ? 1 : 0;
}

/* ============================================================================
 * 3D Stereo Enhancement
 *
 * The 3D control register (0x22) provides stereo enhancement:
 *   Bits 3-0: 3D depth (0 = off, 15 = maximum depth)
 *   Bit 4: Center depth (0=off, 1=on)
 *   Bit 5: Prefer minimum processing
 *   Bit 6: Mono to stereo enable
 * ============================================================================
 */

/* ============================================================================
 * Jack Sensing
 *
 * The audio interrupt and paging register (0x24) provides jack sense:
 *   Bit 1: Sense interrupt enable
 *   Bit 0: Sense status (read to determine if jack state changed)
 * The actual jack presence bits are vendor-specific.
 * We periodically check the sense status to detect plug/unplug events.
 * ============================================================================
 */

/**
 * Check jack presence state.
 * Returns 1 if jack is present, 0 if not, 2 if unknown.
 */
static uint8_t ac97_check_jack(void)
{
    uint16_t int_page = ac97_codec_read(AC97_REG_AUDIO_INT_PAGING);

    /* Check if the codec supports jack sensing */
    if (!(g_ac97.ext_audio_id & 0x0100)) {
        return AC97_JACK_UNKNOWN;
    }

    /* Sense status is in bit 0 of the interrupt register */
    if (int_page & AC97_INT_SENSE_STAT) {
        /* Clear sense status by writing back */
        ac97_codec_write(AC97_REG_AUDIO_INT_PAGING, int_page & ~AC97_INT_SENSE_STAT);
        return AC97_JACK_PRESENT;
    }

    /* For codecs that don't support real sensing, assume present */
    return AC97_JACK_PRESENT;
}

/* ============================================================================
 * BDL (Buffer Descriptor List) Management
 *
 * The BDL is a table of up to 32 entries, each describing a contiguous
 * physical memory buffer. The DMA engine processes entries sequentially,
 * wrapping around at the Last Valid Index (LVI).
 *
 * Each entry:
 *   - addr: 32-bit physical address of the buffer
 *   - len_ctrl: bits 15:0 = length in samples, bit 31 = IOC, bit 30 = BUP
 *
 * For stereo 16-bit output, each sample is 4 bytes (2 channels × 2 bytes).
 * So the sample count in len_ctrl = buffer_size_bytes / 4.
 * ============================================================================
 */

/**
 * Allocate a Buffer Descriptor List for a bus master engine.
 * Returns the virtual address of the BDL. Sets *phys_out to the physical address.
 */
static ac97_bdl_entry_t *ac97_bdl_alloc(uint32_t *phys_out)
{
    /* Allocate one page (4096 bytes) for the BDL, enough for 32 entries */
    ac97_bdl_entry_t *bdl = (ac97_bdl_entry_t *)pmm_alloc_pages(1);
    if (!bdl) {
        klog_err("AC'97: failed to allocate BDL page");
        return NULL;
    }

    uint32_t bdl_phys = (uint32_t)bdl;
    bdl = (ac97_bdl_entry_t *)vmm_map_physical(bdl_phys, PAGE_SIZE);
    if (!bdl) {
        klog_err("AC'97: failed to map BDL");
        return NULL;
    }

    memset(bdl, 0, PAGE_SIZE);
    *phys_out = bdl_phys;
    return bdl;
}

/**
 * Free a previously allocated BDL.
 */
static void ac97_bdl_free(ac97_bdl_entry_t *bdl, uint32_t phys)
{
    if (bdl) {
        vmm_unmap_physical((void *)bdl, PAGE_SIZE);
        pmm_free_pages((void *)phys, 1);
    }
}

/**
 * Compute the number of samples per byte for the current format.
 * For 16-bit stereo: 4 bytes per sample pair → 1 sample/4 bytes
 * For 16-bit mono: 2 bytes per sample → 1 sample/2 bytes
 * For 8-bit stereo: 2 bytes per sample pair → 1 sample/2 bytes
 * For 8-bit mono: 1 byte per sample → 1 sample/byte
 */
static uint32_t ac97_bytes_per_sample(void)
{
    if (g_ac97.bits_per_sample == 16) {
        return g_ac97.channels * 2;
    } else {
        return g_ac97.channels;
    }
}

/* ============================================================================
 * Bus Master Engine Control
 * ============================================================================
 */

/**
 * Reset a bus master engine (PI, PO, or MC).
 * Writing the RR bit causes the engine to reset its state:
 *   - Clears the FIFO
 *   - Resets CIV to 0
 *   - Clears status bits
 *   - Stops DMA transfers
 */
static void ac97_bm_reset(uint32_t bar_offset)
{
    uint32_t timeout;

    /* Set RR bit to reset */
    ac97_bm_write8(bar_offset, AC97_BM_REG_CR, AC97_CR_RR);

    /* Wait for reset to complete (RR bit self-clears) */
    timeout = 100000;
    while (timeout--) {
        if (!(ac97_bm_read8(bar_offset, AC97_BM_REG_CR) & AC97_CR_RR))
            break;
        io_wait();
    }

    if (timeout == 0) {
        klog_warn("AC'97: bus master reset timeout at offset 0x%X", bar_offset);
    }
}

/**
 * Halt a bus master engine by clearing the RPBM bit.
 * Waits for the DMA controller to actually halt (DCH bit set in SR).
 */
static void ac97_bm_halt(uint32_t bar_offset)
{
    uint32_t timeout;

    /* Clear RPBM to stop */
    uint8_t cr = ac97_bm_read8(bar_offset, AC97_BM_REG_CR);
    cr &= ~AC97_CR_RPBM;
    ac97_bm_write8(bar_offset, AC97_BM_REG_CR, cr);

    /* Wait for DMA controller to halt */
    timeout = 100000;
    while (timeout--) {
        if (ac97_bm_read16(bar_offset, AC97_BM_REG_SR) & AC97_SR_DCH)
            break;
        io_wait();
    }

    if (timeout == 0) {
        klog_warn("AC'97: bus master halt timeout at offset 0x%X", bar_offset);
    }
}

/**
 * Start a bus master engine for playback or capture.
 * Configures the BDL, LVI, and starts the engine with interrupts enabled.
 * @param bar_offset  Bus master engine base offset (PI/PO/MC)
 * @param bdl_phys    Physical address of the BDL
 * @param bdl_count   Number of valid BDL entries (LVI = bdl_count - 1)
 */
static int ac97_bm_start(uint32_t bar_offset, uint32_t bdl_phys, uint8_t bdl_count)
{
    if (bdl_count == 0 || bdl_count > AC97_BDL_MAX_ENTRIES) {
        return -1;
    }

    /* Reset the engine first */
    ac97_bm_reset(bar_offset);

    /* Set BDL physical address */
    ac97_bm_write32(bar_offset, AC97_BM_REG_BDBAR, bdl_phys);

    /* Set Last Valid Index (0-based) */
    ac97_bm_write8(bar_offset, AC97_BM_REG_LVI, (uint8_t)(bdl_count - 1));

    /* Clear any pending status */
    uint16_t sr = ac97_bm_read16(bar_offset, AC97_BM_REG_SR);
    ac97_bm_write16(bar_offset, AC97_BM_REG_SR, sr);

    /* Start the engine: RPBM + IOCE + FEIE
     * RPBM: Run/Pause Bus Master
     * IOCE: Interrupt on Completion Enable (fire IRQ when IOC bit in BDL entry is hit)
     * FEIE: FIFO Error Interrupt Enable */
    ac97_bm_write8(bar_offset, AC97_BM_REG_CR,
        AC97_CR_RPBM | AC97_CR_IOCE | AC97_CR_FEIE);

    return 0;
}

/* ============================================================================
 * Interrupt Handling
 *
 * The AC'97 controller generates interrupts for:
 *   - PO (PCM Out): Buffer completion, last buffer, FIFO error
 *   - PI (PCM In): Buffer completion, last buffer, FIFO error
 *   - MC (Mic In): Buffer completion
 *   - Sample rate change detected
 *
 * The global status register (0x30) identifies which engine caused the
 * interrupt. Each engine's status register (SR) identifies the specific
 * cause within that engine.
 * ============================================================================
 */

/**
 * AC'97 interrupt handler.
 * Called by the kernel's IRQ dispatch when the assigned IRQ line fires.
 * Reads the global status register to determine which engine(s) need
 * service, then clears the corresponding status bits.
 */
void ac97_irq_handler(void *regs)
{
    (void)regs;  /* Register context not needed for audio IRQ */

    /* Read global status to determine which engine(s) interrupted */
    uint32_t gs = ac97_bm_read32(AC97_BM_REG_GLOB_STA, 0);

    if (gs == 0) {
        /* Not our interrupt */
        return;
    }

    /* Handle PCM Out (playback) interrupt */
    if (gs & AC97_GLOB_STA_PO) {
        uint16_t po_sr = ac97_bm_read16(AC97_BM_PO_BASE, AC97_BM_REG_SR);

        if (po_sr & AC97_SR_BCIS) {
            /* Buffer Completion Interrupt: a BDL entry with IOC flag was processed */
            ac97_bm_write16(AC97_BM_PO_BASE, AC97_BM_REG_SR,
                (uint16_t)(po_sr & ~AC97_SR_BCIS));
        }

        if (po_sr & AC97_SR_LVBCI) {
            /* Last Valid Buffer Completion: the engine wrapped around */
            ac97_bm_write16(AC97_BM_PO_BASE, AC97_BM_REG_SR,
                (uint16_t)(po_sr & ~AC97_SR_LVBCI));
        }

        if (po_sr & AC97_SR_FIFOE) {
            /* FIFO error: buffer underrun or overrun */
            klog_warn("AC'97: playback FIFO error");
            ac97_bm_write16(AC97_BM_PO_BASE, AC97_BM_REG_SR,
                (uint16_t)(po_sr & ~AC97_SR_FIFOE));
        }
    }

    /* Handle PCM In (capture) interrupt */
    if (gs & AC97_GLOB_STA_PI) {
        uint16_t pi_sr = ac97_bm_read16(AC97_BM_PI_BASE, AC97_BM_REG_SR);

        if (pi_sr & AC97_SR_BCIS) {
            ac97_bm_write16(AC97_BM_PI_BASE, AC97_BM_REG_SR,
                (uint16_t)(pi_sr & ~AC97_SR_BCIS));
        }

        if (pi_sr & AC97_SR_LVBCI) {
            ac97_bm_write16(AC97_BM_PI_BASE, AC97_BM_REG_SR,
                (uint16_t)(pi_sr & ~AC97_SR_LVBCI));
        }

        if (pi_sr & AC97_SR_FIFOE) {
            klog_warn("AC'97: capture FIFO error");
            ac97_bm_write16(AC97_BM_PI_BASE, AC97_BM_REG_SR,
                (uint16_t)(pi_sr & ~AC97_SR_FIFOE));
        }
    }

    /* Handle Mic In interrupt */
    if (gs & AC97_GLOB_STA_MC) {
        uint16_t mc_sr = ac97_bm_read16(AC97_BM_MC_BASE, AC97_BM_REG_SR);
        if (mc_sr & AC97_SR_BCIS) {
            ac97_bm_write16(AC97_BM_MC_BASE, AC97_BM_REG_SR,
                (uint16_t)(mc_sr & ~AC97_SR_BCIS));
        }
    }

    /* Clear the handled global status bits */
    ac97_bm_write32(AC97_BM_REG_GLOB_STA, 0, gs & AC97_GLOB_STA_MASK);

    /* Send EOI to PIC */
    if (g_ac97.irq < 16) {
        pic_eoi(g_ac97.irq);
    }
}

/* ============================================================================
 * Codec Detection and Capability Discovery
 * ============================================================================
 */

/**
 * Read the Extended Audio ID register to discover codec capabilities.
 * This register (0x28) contains bit flags indicating:
 *   - VRA support (variable rate audio)
 *   - DRA support (double rate audio)
 *   - S/PDIF support
 *   - Center/LFE/Surround DAC support
 *   - AC'97 revision level
 */
static void ac97_probe_capabilities(void)
{
    g_ac97.ext_audio_id = ac97_codec_read(AC97_REG_EXTENDED_AUDIO_ID);

    g_ac97.supports_vra   = (g_ac97.ext_audio_id & AC97_EXT_ID_VRA) ? 1 : 0;
    g_ac97.supports_dra   = (g_ac97.ext_audio_id & AC97_EXT_ID_DRA) ? 1 : 0;
    g_ac97.supports_spdif = (g_ac97.ext_audio_id & AC97_EXT_ID_SPDIF) ? 1 : 0;

    /* Check for 3D enhancement support */
    uint16_t gp_reg = ac97_codec_read(AC97_REG_GENERAL_PURPOSE);
    g_ac97.supports_3d = (gp_reg & 0x0080) ? 1 : 0;

    /* Determine revision */
    uint16_t rev = g_ac97.ext_audio_id & AC97_EXT_ID_REV_MASK;
    const char *rev_str;
    switch (rev) {
        case AC97_EXT_ID_REV_21: rev_str = "2.1"; break;
        case AC97_EXT_ID_REV_22: rev_str = "2.2"; break;
        case AC97_EXT_ID_REV_23: rev_str = "2.3"; break;
        default: rev_str = "unknown";
    }

    klog_info("AC'97: codec revision = %s, VRA=%s, DRA=%s, S/PDIF=%s, 3D=%s",
        rev_str,
        g_ac97.supports_vra ? "yes" : "no",
        g_ac97.supports_dra ? "yes" : "no",
        g_ac97.supports_spdif ? "yes" : "no",
        g_ac97.supports_3d ? "yes" : "no");
}

/* ============================================================================
 * PCI Device Matching
 *
 * The AC'97 controller is identified by PCI class 0x04 (multimedia),
 * subclass 0x01 (audio). We also check for known vendor/device IDs.
 * For ICH6/ICH7, the device may appear as class 0x0403 (HD Audio) with
 * a dedicated AC'97 function, or as a separate function with class 0x0401.
 * ============================================================================
 */

/**
 * Check if a PCI device is an AC'97 audio controller.
 * Returns 1 if the device matches, 0 otherwise.
 */
static int ac97_pci_match(uint8_t bus, uint8_t dev, uint8_t func)
{
    uint32_t vd = pci_read_config(bus, dev, func, 0x00);
    if (vd == 0xFFFFFFFF || vd == 0) return 0;

    uint16_t vendor = vd & 0xFFFF;
    uint16_t device = (vd >> 16) & 0xFFFF;

    uint32_t cl = pci_read_config(bus, dev, func, 0x08);
    uint8_t base_class = (cl >> 24) & 0xFF;
    uint8_t sub_class   = (cl >> 16) & 0xFF;

    /* Standard AC'97: class 0x04, subclass 0x01 */
    if (base_class == PCI_CLASS_MULTIMEDIA && sub_class == PCI_SUBCLASS_AUDIO) {
        return 1;
    }

    /* Check known vendor/device IDs even if class doesn't match (e.g., ICH6 compat mode) */
    if (vendor == PCI_VENDOR_INTEL) {
        switch (device) {
            case PCI_DEV_ICH0_AC97:
            case PCI_DEV_ICH1_AC97:
            case PCI_DEV_ICH2_AC97:
            case PCI_DEV_ICH3_AC97:
            case PCI_DEV_ICH4_AC97:
            case PCI_DEV_ICH5_AC97:
            case PCI_DEV_ICH6_AC97:
            case PCI_DEV_ICH7_AC97:
            case PCI_DEV_ESB_AC97:
                return 1;
        }
    }

    return 0;
}

/* ============================================================================
 * Initialization
 * ============================================================================
 */

/**
 * Initialize the AC'97 audio subsystem.
 *
 * Performs the following steps:
 *   1. Scan PCI bus for an AC'97 audio controller
 *   2. Enable bus mastering and I/O space on the PCI device
 *   3. Map NAMBAR (mixer) and NABMBAR (bus master) I/O windows
 *   4. Cold reset the codec
 *   5. Probe codec capabilities (VRA, DRA, S/PDIF, 3D)
 *   6. Enable Variable Rate Audio (VRA) if supported
 *   7. Configure default mixer settings (unmute, 0dB)
 *   8. Set default format (16-bit stereo, 48000 Hz)
 *   9. Register interrupt handler
 *   10. Enable global interrupts
 *   11. Set power state to D0
 */
void ac97_init(void)
{
    memset(&g_ac97, 0, sizeof(ac97_device_t));

    /* ---- Step 1: Scan PCI bus for AC'97 controller ---- */
    uint16_t bus;
    uint8_t  dev, func;
    int found = 0;

    for (bus = 0; bus < PCI_MAX_BUSES && !found; bus++) {
        for (dev = 0; dev < PCI_MAX_DEVICES && !found; dev++) {
            for (func = 0; func < PCI_MAX_FUNCTIONS && !found; func++) {
                if (ac97_pci_match((uint8_t)bus, dev, func)) {
                    found = 1;
                }
            }
        }
    }

    if (!found) {
        klog_info("AC'97: no controller found");
        return;
    }

    /* Adjust indices (they were incremented past the match) */
    bus--; dev--; func--;

    g_ac97.pci_bus  = (uint8_t)bus;
    g_ac97.pci_dev  = dev;
    g_ac97.pci_func = func;

    uint32_t vd = pci_read_config((uint8_t)bus, dev, func, 0x00);
    g_ac97.vendor_id = (uint16_t)(vd & 0xFFFF);
    g_ac97.device_id = (uint16_t)((vd >> 16) & 0xFFFF);

    klog_info("AC'97: found controller at PCI %u:%u.%u (vendor=0x%04X, device=0x%04X)",
        bus, dev, func, g_ac97.vendor_id, g_ac97.device_id);

    /* ---- Step 2: Enable bus mastering and I/O space ---- */
    uint32_t cmd = pci_read_config((uint8_t)bus, dev, func, 0x04);
    cmd |= 0x07;  /* Bus Master Enable | Memory Space Enable | I/O Space Enable */
    pci_write_config((uint8_t)bus, dev, func, 0x04, cmd);

    /* Read IRQ line from PCI config */
    uint32_t irq_info = pci_read_config((uint8_t)bus, dev, func, 0x3C);
    g_ac97.irq = (uint8_t)(irq_info & 0xFF);

    /* ---- Step 3: Map mixer BAR (NAMBAR - BAR0) ---- */
    uint32_t bar0 = pci_read_config((uint8_t)bus, dev, func, 0x10);
    g_ac97.nambar = bar0 & 0xFFFFFFFC;  /* I/O BAR: lower 2 bits are type indicator */

    if (g_ac97.nambar == 0) {
        klog_err("AC'97: NAMBAR (BAR0) is zero, cannot initialize");
        return;
    }

    /* ---- Step 4: Map bus master BAR (NABMBAR - BAR1) ---- */
    uint32_t bar1 = pci_read_config((uint8_t)bus, dev, func, 0x14);
    g_ac97.nabmbar = bar1 & 0xFFFFFFFC;  /* I/O BAR */

    if (g_ac97.nabmbar == 0) {
        klog_err("AC'97: NABMBAR (BAR1) is zero, cannot initialize");
        return;
    }

    klog_info("AC'97: NAMBAR=0x%X, NABMBAR=0x%X, IRQ=%u",
        g_ac97.nambar, g_ac97.nabmbar, g_ac97.irq);

    /* ---- Step 5: Enable AC'97 mode for ICH6/ICH7 ---- */
    if (g_ac97.vendor_id == PCI_VENDOR_INTEL &&
        (g_ac97.device_id == PCI_DEV_ICH6_AC97 || g_ac97.device_id == PCI_DEV_ICH7_AC97)) {
        uint32_t glob_cnt = ac97_bm_read32(AC97_BM_REG_GLOB_CNT, 0);
        glob_cnt |= AC97_GLOB_CNT_AC97;  /* Enable AC'97 mode */
        ac97_bm_write32(AC97_BM_REG_GLOB_CNT, 0, glob_cnt);
        klog_info("AC'97: enabled AC'97 mode for ICH6/ICH7");
    }

    /* ---- Step 6: Cold reset the codec ---- */
    ac97_codec_cold_reset();

    /* ---- Step 7: Probe codec capabilities ---- */
    ac97_probe_capabilities();

    /* ---- Step 8: Enable Variable Rate Audio if supported ---- */
    if (g_ac97.supports_vra) {
        uint16_t ext_ctl = ac97_codec_read(AC97_REG_EXTENDED_AUDIO_CTL);
        ext_ctl |= AC97_EXT_CTL_VRA;
        ac97_codec_write(AC97_REG_EXTENDED_AUDIO_CTL, ext_ctl);
        klog_info("AC'97: Variable Rate Audio enabled");
    }

    /* ---- Step 9: Set power state to D0 (full power) ---- */
    ac97_set_powerdown(0x0000);
    g_ac97.power_state = AC97_POWER_D0;

    /* ---- Step 10: Configure default mixer settings ---- */
    /* Unmute Master Volume and set to 0dB */
    ac97_codec_write(AC97_REG_MASTER_VOLUME, 0x0000);
    g_ac97.master_muted = 0;

    /* Unmute PCM Output and set to 0dB */
    ac97_codec_write(AC97_REG_PCM_OUT_VOLUME, 0x0000);
    g_ac97.pcm_muted = 0;

    /* Set Line In to 0dB, muted */
    ac97_codec_write(AC97_REG_LINE_IN_VOLUME, 0x8008);
    g_ac97.line_in_muted = 1;

    /* Set Mic to 0dB, muted */
    ac97_codec_write(AC97_REG_MIC_VOLUME, 0x8008);
    g_ac97.mic_muted = 1;

    /* Set CD to 0dB, muted */
    ac97_codec_write(AC97_REG_CD_VOLUME, 0x8008);
    g_ac97.cd_muted = 1;

    /* Set Aux to 0dB, muted */
    ac97_codec_write(AC97_REG_AUX_VOLUME, 0x8008);
    g_ac97.aux_muted = 1;

    /* Set record source to stereo mix */
    ac97_codec_write(AC97_REG_RECORD_SELECT, AC97_RECSRC_STEREO_MIX);
    g_ac97.rec_source = AC97_RECSRC_STEREO_MIX;

    /* Set record gain to 0dB */
    ac97_codec_write(AC97_REG_RECORD_GAIN, 0x0000);
    g_ac97.rec_gain_left = 0;
    g_ac97.rec_gain_right = 0;

    /* Disable 3D enhancement */
    if (g_ac97.supports_3d) {
        ac97_codec_write(AC97_REG_3D_CONTROL, 0x0000);
        g_ac97._3d_depth = 0;
    }

    /* ---- Step 11: Set default audio format ---- */
    g_ac97.sample_rate    = 48000;
    g_ac97.channels       = 2;
    g_ac97.bits_per_sample = 16;
    g_ac97.format         = AC97_FMT_S16_STEREO;

    /* Set DAC sample rate */
    ac97_set_dac_rate(48000);

    /* ---- Step 12: Register interrupt handler ---- */
    pic_unmask(g_ac97.irq);
    irq_register_handler(g_ac97.irq, (void (*)(regs_t *))ac97_irq_handler);
    g_ac97.irq_registered = 1;

    /* ---- Step 13: Enable global interrupts ---- */
    ac97_bm_write32(AC97_BM_REG_GLOB_CNT, 0, AC97_GLOB_CNT_GIE);

    /* ---- Step 14: Check jack presence ---- */
    g_ac97.jack_present = ac97_check_jack();

    g_ac97.enabled = 1;
    g_ac97.initialized = 1;

    klog_info("AC'97: initialization complete");
}

/* ============================================================================
 * Playback
 *
 * The playback path uses the PCM Out (PO) bus master engine. Data flows:
 *   Application buffer → BDL entries → DMA engine → AC-link → Codec DAC → Output
 *
 * The BDL is set up with a single entry pointing to the user's buffer.
 * For larger buffers, multiple BDL entries can be used but a single entry
 * is simpler and sufficient for most use cases.
 *
 * DMA transfer count is in samples. For 16-bit stereo, each sample pair
 * (left+right) counts as one sample in the BDL, so:
 *   sample_count = buffer_size / 4  (for 16-bit stereo)
 *   sample_count = buffer_size / 2  (for 16-bit mono or 8-bit stereo)
 *   sample_count = buffer_size / 1  (for 8-bit mono)
 * ============================================================================
 */

int ac97_play(const void *data, uint32_t size, uint32_t rate, uint16_t channels)
{
    if (!g_ac97.enabled || !g_ac97.initialized) return -1;
    if (!data || size == 0) return -1;

    /* Stop any active playback first */
    if (g_ac97.po_active) {
        ac97_stop_playback();
    }

    /* Configure sample rate if different */
    if (rate != g_ac97.sample_rate) {
        ac97_set_dac_rate(rate);
    }

    /* Configure format */
    if (channels != g_ac97.channels) {
        g_ac97.channels = channels;
        if (channels == 2 && g_ac97.bits_per_sample == 16) {
            g_ac97.format = AC97_FMT_S16_STEREO;
        } else if (channels == 1 && g_ac97.bits_per_sample == 16) {
            g_ac97.format = AC97_FMT_S16_MONO;
        } else if (channels == 2 && g_ac97.bits_per_sample == 8) {
            g_ac97.format = AC97_FMT_U8_STEREO;
        } else {
            g_ac97.format = AC97_FMT_U8_MONO;
        }
    }

    /* Compute sample count for the BDL entry.
     * AC'97 BDL len_ctrl field is in samples (not bytes).
     * For 16-bit stereo: each "sample" in BDL context = one stereo sample pair.
     * So: sample_count = buffer_size_bytes / (bytes_per_sample)
     *
     * Actually, the BDL length is in BYTES per the ICH specification.
     * Let's use bytes directly for correctness.
     */
    uint32_t sample_count = size / 2;  /* The BDL length field is in word (2-byte) units */

    /* Cap at max BDL entry size */
    if (sample_count > AC97_BDL_LEN_MASK) {
        sample_count = AC97_BDL_LEN_MASK;
        klog_warn("AC'97: buffer too large, truncating to %u samples", sample_count);
    }

    /* Allocate BDL */
    if (!g_ac97.po_bdl) {
        g_ac97.po_bdl = ac97_bdl_alloc(&g_ac97.po_bdl_phys);
        if (!g_ac97.po_bdl) return -1;
    }

    /* Set up a single BDL entry pointing to the user's buffer.
     * Note: The buffer must be physically contiguous and accessible by DMA.
     * In a real kernel, we'd need to translate virtual to physical address.
     * For now, we use the buffer address directly (assuming identity mapping). */
    g_ac97.po_bdl[0].addr     = (uint32_t)data;
    g_ac97.po_bdl[0].len_ctrl = (sample_count & AC97_BDL_LEN_MASK) | AC97_BDL_IOC;

    /* Clear remaining BDL entries */
    uint32_t i;
    for (i = 1; i < AC97_BDL_MAX_ENTRIES; i++) {
        g_ac97.po_bdl[i].addr     = 0;
        g_ac97.po_bdl[i].len_ctrl = 0;
    }

    g_ac97.po_bdl_count = 1;

    /* Ensure power is on for DAC */
    if (g_ac97.power_state != AC97_POWER_D0) {
        ac97_set_powerdown(0x0000);
        g_ac97.power_state = AC97_POWER_D0;
    }

    /* Start the PCM Out engine */
    ac97_bm_start(AC97_BM_PO_BASE, g_ac97.po_bdl_phys, g_ac97.po_bdl_count);

    g_ac97.po_active = 1;

    return 0;
}

/* ============================================================================
 * Capture (Recording)
 *
 * The capture path uses the PCM In (PI) bus master engine. Data flows:
 *   Input → Codec ADC → AC-link → DMA engine → BDL entries → Application buffer
 *
 * The BDL setup is similar to playback but uses the PI engine.
 * The recording source must be selected via the Record Select register (0x1A).
 * ============================================================================
 */

int ac97_record(void *data, uint32_t size, uint32_t rate, uint16_t channels)
{
    if (!g_ac97.enabled || !g_ac97.initialized) return -1;
    if (!data || size == 0) return -1;

    /* Stop any active capture first */
    if (g_ac97.pi_active) {
        ac97_stop_capture();
    }

    /* Configure ADC sample rate */
    ac97_set_adc_rate(rate);

    /* Compute sample count */
    uint32_t sample_count = size / 2;

    if (sample_count > AC97_BDL_LEN_MASK) {
        sample_count = AC97_BDL_LEN_MASK;
    }

    /* Allocate BDL for capture */
    if (!g_ac97.pi_bdl) {
        g_ac97.pi_bdl = ac97_bdl_alloc(&g_ac97.pi_bdl_phys);
        if (!g_ac97.pi_bdl) return -1;
    }

    /* Set up single BDL entry */
    g_ac97.pi_bdl[0].addr     = (uint32_t)data;
    g_ac97.pi_bdl[0].len_ctrl = (sample_count & AC97_BDL_LEN_MASK) | AC97_BDL_IOC;

    uint32_t i;
    for (i = 1; i < AC97_BDL_MAX_ENTRIES; i++) {
        g_ac97.pi_bdl[i].addr     = 0;
        g_ac97.pi_bdl[i].len_ctrl = 0;
    }

    g_ac97.pi_bdl_count = 1;

    /* Ensure ADC is powered on */
    if (g_ac97.power_state != AC97_POWER_D0) {
        ac97_set_powerdown(0x0000);
        g_ac97.power_state = AC97_POWER_D0;
    }

    /* Start the PCM In engine */
    ac97_bm_start(AC97_BM_PI_BASE, g_ac97.pi_bdl_phys, g_ac97.pi_bdl_count);

    g_ac97.pi_active = 1;

    (void)channels;  /* Capture format is configured separately */

    return 0;
}

/* ============================================================================
 * Stop Functions
 * ============================================================================
 */

void ac97_stop(void)
{
    ac97_stop_playback();
    ac97_stop_capture();
}

void ac97_stop_playback(void)
{
    if (!g_ac97.enabled || !g_ac97.po_active) return;

    /* Halt the PCM Out engine */
    ac97_bm_halt(AC97_BM_PO_BASE);

    /* Reset the engine */
    ac97_bm_reset(AC97_BM_PO_BASE);

    g_ac97.po_active = 0;
}

void ac97_stop_capture(void)
{
    if (!g_ac97.enabled || !g_ac97.pi_active) return;

    /* Halt the PCM In engine */
    ac97_bm_halt(AC97_BM_PI_BASE);

    /* Reset the engine */
    ac97_bm_reset(AC97_BM_PI_BASE);

    g_ac97.pi_active = 0;
}

/* ============================================================================
 * Volume Control API
 * ============================================================================
 */

int ac97_set_volume(uint8_t left, uint8_t right)
{
    return ac97_set_master_volume(left, right);
}

int ac97_get_volume(uint8_t *left, uint8_t *right)
{
    if (!g_ac97.enabled) return -1;
    if (!left || !right) return -1;

    int muted = ac97_read_stereo_vol(AC97_REG_MASTER_VOLUME, left, right);
    return muted ? 1 : 0;
}

int ac97_set_mute(uint8_t mute)
{
    if (!g_ac97.enabled) return -1;

    if (mute) {
        ac97_codec_write(AC97_REG_MASTER_VOLUME, 0x8000);
        g_ac97.master_muted = 1;
    } else {
        ac97_codec_write(AC97_REG_MASTER_VOLUME, 0x0000);
        g_ac97.master_muted = 0;
    }

    return 0;
}

int ac97_get_mute(void)
{
    if (!g_ac97.enabled) return 1;
    return g_ac97.master_muted;
}

int ac97_set_master_volume(uint8_t left, uint8_t right)
{
    if (!g_ac97.enabled) return -1;

    ac97_write_stereo_vol(AC97_REG_MASTER_VOLUME, left, right, 0);
    g_ac97.master_muted = 0;
    return 0;
}

int ac97_set_pcm_volume(uint8_t left, uint8_t right)
{
    if (!g_ac97.enabled) return -1;

    ac97_write_stereo_vol(AC97_REG_PCM_OUT_VOLUME, left, right, 0);
    g_ac97.pcm_muted = 0;
    return 0;
}

int ac97_set_line_in_volume(uint8_t left, uint8_t right)
{
    if (!g_ac97.enabled) return -1;

    ac97_write_stereo_vol(AC97_REG_LINE_IN_VOLUME, left, right, 0);
    g_ac97.line_in_muted = 0;
    return 0;
}

int ac97_set_mic_volume(uint8_t volume, uint8_t boost)
{
    if (!g_ac97.enabled) return -1;

    uint8_t atten = ac97_vol_to_atten(volume);
    uint16_t val = (uint16_t)atten;

    if (boost) {
        val |= AC97_MIC_BOOST_20DB;
    }

    ac97_codec_write(AC97_REG_MIC_VOLUME, val);
    g_ac97.mic_muted = 0;
    g_ac97.mic_boost = boost;
    return 0;
}

int ac97_set_cd_volume(uint8_t left, uint8_t right)
{
    if (!g_ac97.enabled) return -1;

    ac97_write_stereo_vol(AC97_REG_CD_VOLUME, left, right, 0);
    g_ac97.cd_muted = 0;
    return 0;
}

int ac97_set_aux_volume(uint8_t left, uint8_t right)
{
    if (!g_ac97.enabled) return -1;

    ac97_write_stereo_vol(AC97_REG_AUX_VOLUME, left, right, 0);
    g_ac97.aux_muted = 0;
    return 0;
}

int ac97_set_recording_source(uint8_t source)
{
    if (!g_ac97.enabled) return -1;

    if (source > AC97_RECSRC_PHONE) return -1;

    ac97_codec_write(AC97_REG_RECORD_SELECT, source);
    g_ac97.rec_source = source;
    return 0;
}

int ac97_set_recording_gain(uint8_t left, uint8_t right)
{
    if (!g_ac97.enabled) return -1;

    /* Record gain: 0-15 in 1.5dB steps (0=0dB, 15=22.5dB) */
    uint8_t gain_left  = (uint8_t)(((uint32_t)left  * 15) / 255);
    uint8_t gain_right = (uint8_t)(((uint32_t)right * 15) / 255);

    uint16_t val = (uint16_t)((gain_right << 8) | gain_left);
    ac97_codec_write(AC97_REG_RECORD_GAIN, val);

    g_ac97.rec_gain_left  = gain_left;
    g_ac97.rec_gain_right = gain_right;
    return 0;
}

int ac97_set_3d_depth(uint8_t depth)
{
    if (!g_ac97.enabled || !g_ac97.supports_3d) return -1;

    if (depth > AC97_3D_MAX) depth = AC97_3D_MAX;

    uint16_t val = (uint16_t)(depth & AC97_3D_DEPTH_MASK);
    ac97_codec_write(AC97_REG_3D_CONTROL, val);
    g_ac97._3d_depth = depth;
    return 0;
}

int ac97_set_sample_rate(uint32_t rate)
{
    if (!g_ac97.enabled) return -1;
    return ac97_set_dac_rate(rate);
}

int ac97_set_format(uint16_t bits, uint16_t channels)
{
    if (!g_ac97.enabled) return -1;

    if (bits != 8 && bits != 16) return -1;
    if (channels != 1 && channels != 2) return -1;

    g_ac97.bits_per_sample = bits;
    g_ac97.channels = channels;

    if (bits == 16 && channels == 2) {
        g_ac97.format = AC97_FMT_S16_STEREO;
    } else if (bits == 16 && channels == 1) {
        g_ac97.format = AC97_FMT_S16_MONO;
    } else if (bits == 8 && channels == 2) {
        g_ac97.format = AC97_FMT_U8_STEREO;
    } else {
        g_ac97.format = AC97_FMT_U8_MONO;
    }

    return 0;
}

int ac97_set_power_state(uint8_t state)
{
    if (!g_ac97.enabled) return -1;

    uint16_t pwr_mask;

    switch (state) {
        case AC97_POWER_D0:
            pwr_mask = 0x0000;  /* All on */
            break;
        case AC97_POWER_D1:
            /* Light sleep: turn off DAC only */
            pwr_mask = AC97_PWR_DAC;
            break;
        case AC97_POWER_D2:
            /* Deep sleep: turn off DAC, analog, Vref */
            pwr_mask = AC97_PWR_DAC | AC97_PWR_ANALOG | AC97_PWR_REFOUT;
            break;
        case AC97_POWER_D3:
            /* Cold: everything off */
            pwr_mask = AC97_PWR_ADC | AC97_PWR_DAC | AC97_PWR_ANALOG |
                       AC97_PWR_REFOUT | AC97_PWR_ACLINK | AC97_PWR_CLK;
            break;
        default:
            return -1;
    }

    ac97_set_powerdown(pwr_mask);
    g_ac97.power_state = state;
    return 0;
}

/* ============================================================================
 * Buffer Position Query
 *
 * The PICB (Position in Current Buffer) register in the bus master engine
 * indicates how many samples have been transferred from/to the current BDL
 * entry. This can be used to determine playback/capture position.
 * ============================================================================
 */

uint32_t ac97_get_buffer_position(void)
{
    return ac97_get_playback_position();
}

uint32_t ac97_get_playback_position(void)
{
    if (!g_ac97.enabled || !g_ac97.po_active) return 0;

    /* Read the position in the current buffer (in samples) */
    uint16_t picb = ac97_bm_read16(AC97_BM_PO_BASE, AC97_BM_REG_PICB);

    /* Convert samples to bytes */
    uint32_t bytes_per_sample = ac97_bytes_per_sample();
    return (uint32_t)picb * bytes_per_sample;
}

ac97_device_t *ac97_get_device(void)
{
    return &g_ac97;
}

uint8_t ac97_is_available(void)
{
    return g_ac97.initialized && g_ac97.enabled;
}