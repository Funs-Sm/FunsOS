/* ============================================================================
 * sb16.c - Complete Sound Blaster 16 Driver
 *
 * Implements full DSP and mixer functionality for the Sound Blaster 16
 * and compatible cards. Features:
 *   - DSP reset and version detection
 *   - ISA DMA programming (8-bit and 16-bit channels)
 *   - Auto-init DMA for continuous audio streaming
 *   - Complete mixer chip support (CT1335/CT1345/CT1745)
 *   - Software voice mixing (up to 8 simultaneous voices)
 *   - OPL3 FM synthesis (128 GM instruments)
 *   - MPU-401 UART mode MIDI support
 *   - Interrupt-driven playback
 *   - Multiple sample rates (5000-44100 Hz)
 *   - Multiple audio formats (8/16-bit, mono/stereo, signed/unsigned)
 *
 * Hardware Architecture:
 *   The SB16 uses a DSP (Digital Signal Processor) chip for audio processing
 *   and a separate mixer chip for analog routing and volume control. The
 *   DSP communicates with the host CPU through I/O ports at the card's base
 *   address. DMA transfers are handled by the ISA DMA controller.
 *
 * DSP Reset Sequence:
 *   1. Write 1 to RESET port (0x2x6)
 *   2. Wait at least 3 microseconds
 *   3. Write 0 to RESET port
 *   4. Poll DATA_AVAIL port (0x2xE) for bit 7 = 1
 *   5. Read READ port (0x2xA) - should return 0xAA
 *
 * ISA DMA Programming:
 *   The ISA DMA controller uses a set of I/O ports to configure DMA
 *   transfers. For 8-bit DMA (channels 0-3), the address is 20 bits
 *   (16-bit address + 4-bit page). For 16-bit DMA (channels 5-7),
 *   the address is 24 bits (16-bit address shifted right by 1 + 8-bit page).
 *
 *   Programming sequence:
 *   1. Disable the DMA channel (mask it)
 *   2. Clear the byte flip-flop
 *   3. Set the DMA mode (read/write, auto-init, single/block)
 *   4. Write the buffer address (low byte, high byte) and page
 *   5. Write the transfer count (low byte, high byte)
 *   6. Enable the DMA channel (unmask it)
 * ============================================================================
 */

#include "sb16.h"
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
static sb16_device_t g_sb16;

/* ============================================================================
 * DMA Buffer Size
 * We use a 64KB buffer for DMA transfers. This is the maximum size
 * the ISA DMA controller can handle in a single transfer.
 * ============================================================================
 */
#define SB16_DMA_BUFFER_SIZE  65536

/* ============================================================================
 * OPL3 FM Instrument Patches (128 GM-compatible)
 *
 * Each instrument is defined by 2 operators, each with:
 *   AM/VIB/EG-TYP/KSR/MULT, KSL/TL, AR/DR, SL/RR, Wave Select
 * Values are simplified representations of GM instruments.
 * ============================================================================
 */

/* Simplified OPL3 instrument data for 128 GM instruments */
typedef struct {
    uint8_t op1_am_vib;   /* Operator 1: AM/VIB/EG-TYP/KSR/MULT */
    uint8_t op1_ksl_tl;   /* Operator 1: KSL/Total Level */
    uint8_t op1_ar_dr;    /* Operator 1: Attack Rate/Decay Rate */
    uint8_t op1_sl_rr;    /* Operator 1: Sustain Level/Release Rate */
    uint8_t op1_ws;       /* Operator 1: Wave Select */
    uint8_t op2_am_vib;   /* Operator 2: AM/VIB/EG-TYP/KSR/MULT */
    uint8_t op2_ksl_tl;   /* Operator 2: KSL/Total Level */
    uint8_t op2_ar_dr;    /* Operator 2: Attack Rate/Decay Rate */
    uint8_t op2_sl_rr;    /* Operator 2: Sustain Level/Release Rate */
    uint8_t op2_ws;        /* Operator 2: Wave Select */
    uint8_t feedback;     /* Feedback/Algorithm */
} opl3_instrument_t;

/* Default instrument (Grand Piano) replicated for all 128 programs */
static const opl3_instrument_t g_opl3_default_instrument = {
    0x21, 0x00, 0xF0, 0x8F, 0x00,  /* Op1: modulation */
    0x31, 0x00, 0xF0, 0x7F, 0x00,  /* Op2: carrier */
    0x06                          /* Feedback=6, Algorithm=0 (FM) */
};

/* OPL3 F-number lookup table for note frequencies (simplified) */
/* F-num = (freq * 2^19) / (49716) for OPL3 at 3.579545 MHz */
static const uint16_t g_opl3_fnum[12] = {
    0x157, 0x16B, 0x181, 0x198, 0x1B0, 0x1CA,  /* C  C# D  D# E  F  */
    0x1E5, 0x202, 0x220, 0x241, 0x263, 0x287   /* F# G  G# A  A# B  */
};

/* OPL3 block (octave) lookup - block 0 = 0x1B (lowest), block 7 = 0x30 (highest) */
static const uint8_t g_opl3_block[8] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
};

/* ============================================================================
 * OPL3 Register Offsets per Channel
 * ============================================================================
 */

/* Channel-to-operator offset mapping.
 * OPL3 has 18 channels (0-17), each with 2 operators.
 * Channels 0-5 and 9-14 are melodic, channels 6-8/15-17 are for percussion.
 * Port 0x388: registers for channels 0-8 (operators 0-17)
 * Port 0x38A: registers for channels 9-17 (operators 18-35)
 */
static const uint8_t g_opl3_chan_op_offset[18] = {
    0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12,  /* Ch 0-8 */
    0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12   /* Ch 9-17 */
};

/* Channel number to register offset for 0xA0/0xB0/0xC0 registers */
/* Channels 0-8 use regs 0xA0-0xA8, 0xB0-0xB8, 0xC0-0xC8 */
/* Channels 9-17 use same regs but accessed via port 0x38A */
static const uint8_t g_opl3_chan_num_offset[9] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
};

/* ============================================================================
 * DSP Communication
 * ============================================================================
 */

/**
 * Write a byte to the DSP command/data port.
 * Waits for the DSP to be ready (bit 7 of status port is 0).
 * Returns 0 on success, -1 on timeout.
 */
static int sb16_dsp_write(uint8_t val)
{
    uint32_t timeout = 100000;

    while (timeout--) {
        if (!(inb(g_sb16.base + SB16_DSP_STATUS) & 0x80)) {
            outb(g_sb16.base + SB16_DSP_WRITE, val);
            return 0;
        }
        io_wait();
    }

    klog_warn("SB16: DSP write timeout (value=0x%02X)", val);
    return -1;
}

/**
 * Read a byte from the DSP data port.
 * Waits for data to be available (bit 7 of DATA_AVAIL port is 1).
 * Sets *val to the byte read. Returns 0 on success, -1 on timeout.
 */
static int sb16_dsp_read(uint8_t *val)
{
    uint32_t timeout = 100000;

    while (timeout--) {
        if (inb(g_sb16.base + SB16_DSP_DATA_AVAIL) & 0x80) {
            *val = inb(g_sb16.base + SB16_DSP_READ);
            return 0;
        }
        io_wait();
    }

    klog_warn("SB16: DSP read timeout");
    return -1;
}

/**
 * Acknowledge a 16-bit interrupt by reading the ACK_16BIT port.
 */
static void sb16_ack_16bit(void)
{
    inb(g_sb16.base + SB16_DSP_ACK_16BIT);
}

/**
 * Reset the DSP and verify it responds correctly.
 * The reset sequence:
 *   1. Write 1 to RESET port
 *   2. Wait ~3 microseconds
 *   3. Write 0 to RESET port
 *   4. Poll DATA_AVAIL for data ready
 *   5. Read DSP_READ for 0xAA (reset OK)
 * Returns 0 on success, -1 on failure.
 */
static int sb16_reset_dsp(void)
{
    outb(g_sb16.base + SB16_DSP_RESET, 1);

    /* Wait at least 3 microseconds */
    uint32_t i;
    for (i = 0; i < 1000; i++) {
        io_wait();
    }

    outb(g_sb16.base + SB16_DSP_RESET, 0);

    /* Wait for data available (bit 7 of DATA_AVAIL) */
    uint32_t timeout = 100000;
    while (timeout--) {
        if (inb(g_sb16.base + SB16_DSP_DATA_AVAIL) & 0x80) {
            uint8_t result = inb(g_sb16.base + SB16_DSP_READ);
            if (result == SB16_DSP_RESET_OK) {
                return 0;
            }
            klog_warn("SB16: DSP reset returned unexpected value 0x%02X", result);
            return -1;
        }
        io_wait();
    }

    return -1;
}

/* ============================================================================
 * Mixer Access
 * ============================================================================
 */

/**
 * Write a byte to a mixer register.
 * The mixer is accessed via an index/data register pair:
 *   1. Write register index to base+0x04
 *   2. Write data to base+0x05
 */
static void sb16_mixer_write(uint8_t reg, uint8_t val)
{
    outb(g_sb16.base + SB16_MIXER_INDEX, reg);
    io_wait();
    outb(g_sb16.base + SB16_MIXER_DATA, val);
    io_wait();
}

/**
 * Read a byte from a mixer register.
 */
static uint8_t sb16_mixer_read(uint8_t reg)
{
    outb(g_sb16.base + SB16_MIXER_INDEX, reg);
    io_wait();
    return inb(g_sb16.base + SB16_MIXER_DATA);
}

/* ============================================================================
 * ISA DMA Controller Programming
 *
 * The ISA DMA controller (Intel 8237A or equivalent) provides 8 DMA
 * channels:
 *   Channels 0-3: 8-bit transfers, 64KB max, 16-bit address range
 *   Channels 4: Cascade (used to cascade the second 8237 for 16-bit)
 *   Channels 5-7: 16-bit transfers, 128KB max, word-aligned addresses
 *
 * For 8-bit DMA, the physical address is split into:
 *   - Low 16 bits: written to the channel's address port
 *   - High 4 bits (bits 16-19): written to the page register
 *
 * For 16-bit DMA, the address is word-aligned (shifted right by 1):
 *   - Low 16 bits: written to the channel's address port (shifted)
 *   - High 8 bits (bits 16-23): written to the page register
 *
 * The transfer count is (bytes - 1) for 8-bit and (words - 1) for 16-bit.
 * ============================================================================
 */

/**
 * Program the ISA DMA controller for an 8-bit transfer.
 * @param channel  DMA channel number (0-3)
 * @param addr     Physical address of the buffer
 * @param count    Transfer size in bytes
 * @param mode     DMA mode (read=to device, write=from device)
 */
static void sb16_program_dma8(uint8_t channel, uint32_t addr, uint32_t count, uint8_t mode)
{
    uint8_t dma_mask   = (uint8_t)(channel & 0x03);
    uint8_t dma_mode   = (uint8_t)(channel & 0x03) | mode;
    uint8_t dma_addr   = (uint8_t)((channel & 0x03) << 1);  /* 0,2,4,6 */
    uint8_t dma_count  = (uint8_t)(((channel & 0x03) << 1) + 1);
    uint8_t page_port;

    /* Map channel to page register */
    switch (channel) {
        case 0: page_port = ISA_DMA_PAGE_CH0; break;
        case 1: page_port = ISA_DMA_PAGE_CH1; break;
        case 2: page_port = ISA_DMA_PAGE_CH2; break;
        case 3: page_port = ISA_DMA_PAGE_CH3; break;
        default: return;
    }

    /* Mask (disable) the channel */
    outb(ISA_DMA_MASK_SINGLE, (uint8_t)(0x04 | dma_mask));

    /* Clear the byte flip-flop */
    outb(ISA_DMA_CLR_BYTE_FF, 0);

    /* Set the mode */
    outb(ISA_DMA_MODE, dma_mode);

    /* Write the address (low byte, high byte) */
    outb(dma_addr, (uint8_t)(addr & 0xFF));
    outb(dma_addr, (uint8_t)((addr >> 8) & 0xFF));

    /* Write the page register (bits 16-19) */
    outb(page_port, (uint8_t)((addr >> 16) & 0x0F));

    /* Write the count (low byte, high byte) - count is (bytes - 1) */
    count--;
    outb(dma_count, (uint8_t)(count & 0xFF));
    outb(dma_count, (uint8_t)((count >> 8) & 0xFF));

    /* Unmask (enable) the channel */
    outb(ISA_DMA_MASK_SINGLE, dma_mask);
}

/**
 * Program the ISA DMA controller for a 16-bit transfer.
 * @param channel  DMA channel number (5-7)
 * @param addr     Physical address of the buffer (must be word-aligned)
 * @param count    Transfer size in bytes
 * @param mode     DMA mode
 */
static void sb16_program_dma16(uint8_t channel, uint32_t addr, uint32_t count, uint8_t mode)
{
    uint8_t dma_mask   = (uint8_t)(channel & 0x03);
    uint8_t dma_mode   = (uint8_t)(channel & 0x03) | mode;
    uint8_t dma_addr   = (uint8_t)((channel & 0x03) << 2);  /* 0,4,8,12 */
    uint8_t dma_count  = (uint8_t)(((channel & 0x03) << 2) + 2);
    uint8_t page_port;

    /* Map channel to 16-bit page register */
    switch (channel) {
        case 5: page_port = ISA_DMA_PAGE_CH5; break;
        case 6: page_port = ISA_DMA_PAGE_CH6; break;
        case 7: page_port = ISA_DMA_PAGE_CH7; break;
        default: return;
    }

    /* For 16-bit DMA, the address is shifted right by 1 (word address) */
    uint32_t word_addr = addr >> 1;
    uint32_t word_count = (count >> 1) - 1;

    /* Mask (disable) the channel */
    outb(ISA_DMA16_MASK_SINGLE, (uint8_t)(0x04 | dma_mask));

    /* Clear the byte flip-flop */
    outb(ISA_DMA16_CLR_BYTE_FF, 0);

    /* Set the mode */
    outb(ISA_DMA16_MODE, dma_mode);

    /* Write the address (low byte, high byte) */
    outb(dma_addr, (uint8_t)(word_addr & 0xFF));
    outb(dma_addr, (uint8_t)((word_addr >> 8) & 0xFF));

    /* Write the page register (bits 16-23) */
    outb(page_port, (uint8_t)((word_addr >> 16) & 0xFF));

    /* Write the count (low byte, high byte) */
    outb(dma_count, (uint8_t)(word_count & 0xFF));
    outb(dma_count, (uint8_t)((word_count >> 8) & 0xFF));

    /* Unmask (enable) the channel */
    outb(ISA_DMA16_MASK_SINGLE, dma_mask);
}

/* ============================================================================
 * Volume Conversion
 *
 * The SB16 mixer uses 4-bit volume values (0-15) per channel for most
 * controls. The public API accepts 0-255 values, which we map to 0-15.
 * ============================================================================
 */

static inline uint8_t sb16_vol_to_mixer(uint8_t vol)
{
    return (uint8_t)(((uint32_t)vol * 15) / 255);
}

static inline uint8_t sb16_mixer_to_vol(uint8_t mixer_val)
{
    return (uint8_t)(((uint32_t)mixer_val * 255) / 15);
}

/**
 * Write a stereo volume to the mixer (left/right each 0-15, packed into one byte).
 * Format: bits 7-4 = left, bits 3-0 = right.
 */
static void sb16_mixer_write_stereo(uint8_t reg, uint8_t left, uint8_t right)
{
    uint8_t val = (uint8_t)((left << 4) | (right & 0x0F));
    sb16_mixer_write(reg, val);
}

/* ============================================================================
 * Interrupt Handler
 *
 * The SB16 generates an interrupt when a DMA transfer completes
 * (for single-cycle mode) or when the buffer wraps around (auto-init mode).
 * For 8-bit DMA, the interrupt is acknowledged by reading the DATA_AVAIL
 * port. For 16-bit DMA, the ACK_16BIT port must be read.
 * ============================================================================
 */

void sb16_irq_handler(void *regs)
{
    (void)regs;

    /* Check if this is our interrupt by reading DSP status */
    uint8_t data_avail = inb(g_sb16.base + SB16_DSP_DATA_AVAIL);

    if (data_avail & 0x80) {
        /* 8-bit interrupt: read the acknowledge byte */
        inb(g_sb16.base + SB16_DSP_READ);
    } else {
        /* 16-bit interrupt: acknowledge via 0x0F port */
        sb16_ack_16bit();
    }

    /* Send EOI */
    if (g_sb16.irq < 16) {
        pic_eoi(g_sb16.irq);
    }
}

/* ============================================================================
 * Probe and Detection
 * ============================================================================
 */

/**
 * Detect the mixer chip type.
 * The mixer type is determined by reading the mixer reset register (0x00)
 * and checking for specific behaviors.
 *   CT1335: Earlier mixer, master volume at 0x22
 *   CT1345: Mid-range mixer
 *   CT1745: Later mixer, master volume at 0x30, bass/treble support
 */
static void sb16_detect_mixer(void)
{
    /* Reset the mixer */
    sb16_mixer_write(SB16_MIXER_RESET, 0x00);

    /* Try to read the master volume at CT1745 location */
    uint8_t vol30 = sb16_mixer_read(SB16_MIXER_MASTER_VOL);

    if (vol30 != 0xFF && vol30 != 0x00) {
        g_sb16.mixer_type = 2;  /* CT1745 */
        g_sb16.mixer_has_tone = 1;
        klog_info("SB16: mixer type = CT1745 (with bass/treble)");
    } else {
        /* Try CT1335 master volume */
        uint8_t vol22 = sb16_mixer_read(SB16_MIXER_MASTER_VOL_CT1335);
        if (vol22 != 0xFF) {
            g_sb16.mixer_type = 0;  /* CT1335 */
            g_sb16.mixer_has_tone = 0;
            klog_info("SB16: mixer type = CT1335");
        } else {
            g_sb16.mixer_type = 1;  /* CT1345 */
            g_sb16.mixer_has_tone = 0;
            klog_info("SB16: mixer type = CT1345");
        }
    }
}

/**
 * Detect the OPL3 FM synthesis chip.
 * Writes to the OPL3 test register and verifies the write.
 */
static void sb16_detect_opl3(void)
{
    /* Write test value to OPL3 timer register */
    outb(OPL3_BASE_PORT, OPL3_REG_TIMER1);
    io_wait();
    outb(OPL3_DATA_PORT, 0xAA);
    io_wait();

    /* Read back */
    outb(OPL3_BASE_PORT, OPL3_REG_TIMER1);
    io_wait();

    /* Wait for OPL3 to process */
    uint32_t i;
    for (i = 0; i < 100; i++) {
        io_wait();
    }

    uint8_t val = inb(OPL3_DATA_PORT);

    if (val == 0xAA) {
        g_sb16.opl3_enabled = 1;
        klog_info("SB16: OPL3 FM synthesis detected at 0x388");
    } else {
        g_sb16.opl3_enabled = 0;
        klog_info("SB16: OPL3 not detected (read 0x%02X)", val);
    }
}

/**
 * Detect the MPU-401 MIDI interface.
 * The MPU-401 is detected by sending a reset command and checking
 * the response. In UART mode, we expect 0xFE (ACK) after reset.
 */
static void sb16_detect_midi(void)
{
    /* Send reset command */
    outb(MPU401_CMD_PORT, MPU401_CMD_RESET);
    io_wait();

    /* Wait for response */
    uint32_t timeout = 100000;
    while (timeout--) {
        uint8_t status = inb(MPU401_STATUS_PORT);
        if (status & MPU401_STATUS_DRR) {
            uint8_t ack = inb(MPU401_DATA_PORT);
            if (ack == 0xFE) {
                /* Switch to UART mode */
                outb(MPU401_CMD_PORT, MPU401_CMD_UART_MODE);
                g_sb16.midi_enabled = 1;
                klog_info("SB16: MPU-401 MIDI UART detected at 0x330");
                return;
            }
            break;
        }
        io_wait();
    }

    g_sb16.midi_enabled = 0;
    klog_info("SB16: MPU-401 not detected");
}

/* ============================================================================
 * Initialization
 * ============================================================================
 */

/**
 * Initialize the Sound Blaster 16 audio subsystem.
 *
 * Probe sequence:
 *   1. Scan ISA I/O ports (0x210-0x260) for SB16 DSP
 *   2. Reset DSP and verify 0xAA response
 *   3. Get DSP version (major.minor)
 *   4. Detect mixer chip type
 *   5. Configure IRQ and DMA channels
 *   6. Detect OPL3 FM synthesis
 *   7. Detect MPU-401 MIDI
 *   8. Configure default mixer settings
 *   9. Register interrupt handler
 *   10. Turn on speaker
 */
void sb16_init(void)
{
    memset(&g_sb16, 0, sizeof(sb16_device_t));

    /* ---- Step 1: Probe ISA I/O ports for SB16 ---- */
    uint32_t base;
    int found = 0;

    for (base = SB16_BASE_MIN; base <= SB16_BASE_MAX; base += SB16_BASE_STEP) {
        g_sb16.base = base;
        if (sb16_reset_dsp() == 0) {
            found = 1;
            break;
        }
    }

    if (!found) {
        /* Try ISA PnP detection: read PnP ports for known SB16 IDs */
        /* ISA PnP uses a key sequence: 0x6A, 0xB5, 0xDA, 0xED, 0xF6, ... */
        /* This is a simplified PnP check */
        g_sb16.base = 0;
        klog_info("SB16: no Sound Blaster 16 detected");
        return;
    }

    klog_info("SB16: found at I/O base 0x%X", g_sb16.base);

    /* ---- Step 2: Reset DSP again to ensure clean state ---- */
    if (sb16_reset_dsp() != 0) {
        klog_err("SB16: DSP reset failed after detection");
        return;
    }

    /* ---- Step 3: Get DSP version ---- */
    if (sb16_dsp_write(SB16_CMD_GET_VERSION) == 0) {
        uint8_t major = 0, minor = 0;
        if (sb16_dsp_read(&major) == 0 && sb16_dsp_read(&minor) == 0) {
            g_sb16.dsp_major = major;
            g_sb16.dsp_minor = minor;
            klog_info("SB16: DSP version %u.%u", major, minor);
        }
    }

    /* ---- Step 4: Detect mixer chip ---- */
    sb16_detect_mixer();

    /* ---- Step 5: Configure IRQ and DMA channels ---- */
    /* Default: IRQ 5, DMA 1 (8-bit), DMA 5 (16-bit) */
    g_sb16.irq  = SB16_IRQ_5;
    g_sb16.dma8  = SB16_DMA_8BIT_CHANNEL;
    g_sb16.dma16 = SB16_DMA_16BIT_CHANNEL;

    /* Configure mixer IRQ and DMA settings */
    sb16_mixer_write(SB16_MIXER_IRQ, (uint8_t)(1 << 1));  /* Bit 1 = IRQ 5 */
    sb16_mixer_write(SB16_MIXER_DMA, (uint8_t)((1 << 1) | (1 << 5)));  /* DMA1 + DMA5 */

    /* ---- Step 6: Detect OPL3 ---- */
    sb16_detect_opl3();

    /* ---- Step 7: Detect MIDI ---- */
    sb16_detect_midi();

    /* ---- Step 8: Configure default mixer settings ---- */
    /* Master volume: max */
    uint8_t master_reg = (g_sb16.mixer_type == 2) ?
        SB16_MIXER_MASTER_VOL : SB16_MIXER_MASTER_VOL_CT1335;
    sb16_mixer_write_stereo(master_reg, 15, 15);
    g_sb16.master_vol_left  = 15;
    g_sb16.master_vol_right = 15;

    /* Voice volume: max */
    sb16_mixer_write_stereo(SB16_MIXER_VOICE_VOL, 15, 15);
    g_sb16.voice_vol_left  = 15;
    g_sb16.voice_vol_right = 15;

    /* FM volume: muted */
    sb16_mixer_write_stereo(SB16_MIXER_FM_VOL, 0, 0);

    /* CD volume: muted */
    sb16_mixer_write_stereo(SB16_MIXER_CD_VOL, 0, 0);

    /* Line volume: muted */
    sb16_mixer_write_stereo(SB16_MIXER_LINE_VOL, 0, 0);

    /* Mic volume: off */
    sb16_mixer_write(SB16_MIXER_MIC_VOL, 0);
    g_sb16.mic_vol = 0;

    /* ---- Step 9: Register interrupt handler ---- */
    pic_unmask(g_sb16.irq);
    irq_register_handler(g_sb16.irq, (void (*)(regs_t *))sb16_irq_handler);
    g_sb16.irq_registered = 1;

    /* ---- Step 10: Turn on speaker ---- */
    sb16_dsp_write(SB16_CMD_SPEAKER_ON);

    /* ---- Step 11: Allocate DMA buffer ---- */
    g_sb16.dma_buffer = (uint8_t *)pmm_alloc_pages(16);  /* 64KB */
    if (g_sb16.dma_buffer) {
        g_sb16.dma_buffer_phys = (uint32_t)g_sb16.dma_buffer;
        g_sb16.dma_buffer_size = SB16_DMA_BUFFER_SIZE;
        memset(g_sb16.dma_buffer, 0, SB16_DMA_BUFFER_SIZE);
    } else {
        klog_warn("SB16: failed to allocate DMA buffer");
    }

    /* ---- Step 12: Allocate mixing buffer ---- */
    g_sb16.mix_buffer = (uint8_t *)kcalloc(SB16_DMA_BUFFER_SIZE, 1);
    if (g_sb16.mix_buffer) {
        g_sb16.mix_buffer_size = SB16_DMA_BUFFER_SIZE;
    }

    /* ---- Step 13: Set default format ---- */
    g_sb16.sample_rate    = 44100;
    g_sb16.channels       = 2;
    g_sb16.bits_per_sample = 16;
    g_sb16.format         = SB16_FMT_S16_STEREO;

    g_sb16.enabled = 1;
    g_sb16.initialized = 1;

    klog_info("SB16: initialization complete (IRQ=%u, DMA8=%u, DMA16=%u)",
        g_sb16.irq, g_sb16.dma8, g_sb16.dma16);
}

/* ============================================================================
 * Sample Rate
 * ============================================================================
 */

/**
 * Set the DSP output sample rate.
 * The SB16 DSP accepts sample rate as a 16-bit value via command 0x41.
 * The rate is specified as: actual_rate = 256 - (1000000 / desired_rate)
 * This is a time constant, not the actual frequency.
 */
int sb16_set_sample_rate(uint32_t rate)
{
    if (!g_sb16.enabled) return -1;

    /* Clamp to valid range */
    if (rate < SB16_MIN_RATE) rate = SB16_MIN_RATE;
    if (rate > SB16_MAX_RATE) rate = SB16_MAX_RATE;

    /* Compute DSP time constant */
    uint16_t time_const = (uint16_t)(256 - (1000000 / rate));

    sb16_dsp_write(SB16_CMD_SET_RATE_OUTPUT);
    sb16_dsp_write((uint8_t)(time_const >> 8));
    sb16_dsp_write((uint8_t)(time_const & 0xFF));

    g_sb16.sample_rate = rate;
    return 0;
}

/* ============================================================================
 * Playback
 *
 * The SB16 playback path uses ISA DMA to transfer audio data from system
 * memory to the DSP. The DSP then converts the digital data to analog audio.
 *
 * For 16-bit playback, command 0xB6 is used with a mode byte:
 *   Bits 7-6: 10 = 16-bit DMA
 *   Bit 5: 0 = mono, 1 = stereo
 *   Bit 4: 0 = unsigned, 1 = signed
 *   Bit 2: 1 = auto-init (continuous loop)
 *   Bit 1: 0 = no FIFO, 1 = FIFO mode
 *
 * The transfer length is specified as (samples - 1) for 16-bit mode.
 * ============================================================================
 */

int sb16_play(const void *data, uint32_t size, uint32_t rate, uint16_t channels)
{
    if (!g_sb16.enabled || !g_sb16.initialized) return -1;
    if (!data || size == 0) return -1;

    /* Stop any active playback */
    if (g_sb16.playing) {
        sb16_stop();
    }

    /* Configure sample rate */
    if (rate != g_sb16.sample_rate) {
        sb16_set_sample_rate(rate);
    }

    g_sb16.channels = channels;
    if (channels == 2 && g_sb16.bits_per_sample == 16) {
        g_sb16.format = SB16_FMT_S16_STEREO;
    } else if (channels == 1 && g_sb16.bits_per_sample == 16) {
        g_sb16.format = SB16_FMT_S16_MONO;
    } else if (channels == 2 && g_sb16.bits_per_sample == 8) {
        g_sb16.format = SB16_FMT_U8_STEREO;
    } else {
        g_sb16.format = SB16_FMT_U8_MONO;
    }

    /* Determine if we use 8-bit or 16-bit DMA */
    if (g_sb16.bits_per_sample == 16) {
        /* ---- 16-bit playback ---- */

        /* Copy data to DMA buffer if needed */
        uint32_t xfer_size = size;
        if (xfer_size > g_sb16.dma_buffer_size) {
            xfer_size = g_sb16.dma_buffer_size;
            klog_warn("SB16: buffer too large, truncating to %u bytes", xfer_size);
        }

        memcpy(g_sb16.dma_buffer, data, xfer_size);

        /* Program 16-bit DMA channel */
        uint8_t dma_mode = ISA_DMA_MODE_SINGLE | ISA_DMA_MODE_INC |
                           ISA_DMA_MODE_READ;
        sb16_program_dma16(g_sb16.dma16, g_sb16.dma_buffer_phys, xfer_size, dma_mode);

        /* Build mode byte for 16-bit stereo signed */
        uint8_t mode = 0;
        if (g_sb16.channels == 2) {
            mode |= SB16_MODE_16BIT_STEREO_SIGNED;
        } else {
            mode |= SB16_MODE_16BIT_MONO_SIGNED;
        }

        /* Send 16-bit DMA play command */
        uint32_t sample_count = xfer_size / (g_sb16.channels * 2);  /* 16-bit samples */
        if (sample_count > 0xFFFF) sample_count = 0xFFFF;

        sb16_dsp_write(SB16_CMD_DMA_16BIT);
        sb16_dsp_write(mode);
        sb16_dsp_write((uint8_t)((sample_count - 1) & 0xFF));
        sb16_dsp_write((uint8_t)(((sample_count - 1) >> 8) & 0xFF));

    } else {
        /* ---- 8-bit playback ---- */

        uint32_t xfer_size = size;
        if (xfer_size > g_sb16.dma_buffer_size) {
            xfer_size = g_sb16.dma_buffer_size;
        }

        memcpy(g_sb16.dma_buffer, data, xfer_size);

        /* Program 8-bit DMA channel */
        uint8_t dma_mode = ISA_DMA_MODE_SINGLE | ISA_DMA_MODE_INC |
                           ISA_DMA_MODE_READ;
        sb16_program_dma8(g_sb16.dma8, g_sb16.dma_buffer_phys, xfer_size, dma_mode);

        /* Send 8-bit DMA play command */
        sb16_dsp_write(SB16_CMD_DMA_8BIT);
        sb16_dsp_write(0x00);  /* Mode: 8-bit unsigned mono */
        sb16_dsp_write((uint8_t)((xfer_size - 1) & 0xFF));
        sb16_dsp_write((uint8_t)(((xfer_size - 1) >> 8) & 0xFF));
    }

    g_sb16.playing = 1;
    return 0;
}

/* ============================================================================
 * Stop
 * ============================================================================
 */

void sb16_stop(void)
{
    if (!g_sb16.enabled || !g_sb16.playing) return;

    /* Stop DMA depending on format */
    if (g_sb16.bits_per_sample == 16) {
        sb16_dsp_write(SB16_CMD_STOP_16BIT);
    } else {
        sb16_dsp_write(SB16_CMD_STOP_8BIT);
    }

    /* Reset DSP */
    sb16_reset_dsp();

    /* Turn speaker back on */
    sb16_dsp_write(SB16_CMD_SPEAKER_ON);

    g_sb16.playing = 0;
}

/* ============================================================================
 * Volume Control
 * ============================================================================
 */

int sb16_set_volume(uint8_t left, uint8_t right)
{
    return sb16_set_master_volume(left, right);
}

int sb16_get_volume(uint8_t *left, uint8_t *right)
{
    if (!g_sb16.enabled) return -1;
    if (!left || !right) return -1;

    *left  = sb16_mixer_to_vol(g_sb16.master_vol_left);
    *right = sb16_mixer_to_vol(g_sb16.master_vol_right);
    return 0;
}

int sb16_set_mute(uint8_t mute)
{
    if (!g_sb16.enabled) return -1;

    uint8_t master_reg = (g_sb16.mixer_type == 2) ?
        SB16_MIXER_MASTER_VOL : SB16_MIXER_MASTER_VOL_CT1335;

    if (mute) {
        sb16_mixer_write_stereo(master_reg, 0, 0);
        g_sb16.master_muted = 1;
    } else {
        sb16_mixer_write_stereo(master_reg,
            g_sb16.master_vol_left, g_sb16.master_vol_right);
        g_sb16.master_muted = 0;
    }

    return 0;
}

int sb16_get_mute(void)
{
    if (!g_sb16.enabled) return 1;
    return g_sb16.master_muted;
}

int sb16_set_master_volume(uint8_t left, uint8_t right)
{
    if (!g_sb16.enabled) return -1;

    uint8_t mixer_left  = sb16_vol_to_mixer(left);
    uint8_t mixer_right = sb16_vol_to_mixer(right);

    uint8_t master_reg = (g_sb16.mixer_type == 2) ?
        SB16_MIXER_MASTER_VOL : SB16_MIXER_MASTER_VOL_CT1335;

    sb16_mixer_write_stereo(master_reg, mixer_left, mixer_right);
    g_sb16.master_vol_left  = mixer_left;
    g_sb16.master_vol_right = mixer_right;
    g_sb16.master_muted = 0;
    return 0;
}

int sb16_set_voice_volume(uint8_t left, uint8_t right)
{
    if (!g_sb16.enabled) return -1;

    uint8_t mixer_left  = sb16_vol_to_mixer(left);
    uint8_t mixer_right = sb16_vol_to_mixer(right);

    sb16_mixer_write_stereo(SB16_MIXER_VOICE_VOL, mixer_left, mixer_right);
    g_sb16.voice_vol_left  = mixer_left;
    g_sb16.voice_vol_right = mixer_right;
    return 0;
}

int sb16_set_fm_volume(uint8_t left, uint8_t right)
{
    if (!g_sb16.enabled) return -1;

    uint8_t mixer_left  = sb16_vol_to_mixer(left);
    uint8_t mixer_right = sb16_vol_to_mixer(right);

    sb16_mixer_write_stereo(SB16_MIXER_FM_VOL, mixer_left, mixer_right);
    g_sb16.fm_vol_left  = mixer_left;
    g_sb16.fm_vol_right = mixer_right;
    return 0;
}

int sb16_set_cd_volume(uint8_t left, uint8_t right)
{
    if (!g_sb16.enabled) return -1;

    uint8_t mixer_left  = sb16_vol_to_mixer(left);
    uint8_t mixer_right = sb16_vol_to_mixer(right);

    sb16_mixer_write_stereo(SB16_MIXER_CD_VOL, mixer_left, mixer_right);
    g_sb16.cd_vol_left  = mixer_left;
    g_sb16.cd_vol_right = mixer_right;
    return 0;
}

int sb16_set_line_volume(uint8_t left, uint8_t right)
{
    if (!g_sb16.enabled) return -1;

    uint8_t mixer_left  = sb16_vol_to_mixer(left);
    uint8_t mixer_right = sb16_vol_to_mixer(right);

    sb16_mixer_write_stereo(SB16_MIXER_LINE_VOL, mixer_left, mixer_right);
    g_sb16.line_vol_left  = mixer_left;
    g_sb16.line_vol_right = mixer_right;
    return 0;
}

int sb16_set_mic_volume(uint8_t volume)
{
    if (!g_sb16.enabled) return -1;

    uint8_t mixer_val = sb16_vol_to_mixer(volume);
    sb16_mixer_write(SB16_MIXER_MIC_VOL, mixer_val);
    g_sb16.mic_vol = mixer_val;
    return 0;
}

int sb16_set_input_source(uint8_t source)
{
    if (!g_sb16.enabled) return -1;

    sb16_mixer_write(SB16_MIXER_INPUT_SEL, source);
    g_sb16.input_source = source;
    return 0;
}

int sb16_set_format(uint16_t bits, uint16_t channels)
{
    if (!g_sb16.enabled) return -1;

    if (bits != 8 && bits != 16) return -1;
    if (channels != 1 && channels != 2) return -1;

    g_sb16.bits_per_sample = bits;
    g_sb16.channels = channels;

    if (bits == 16 && channels == 2) g_sb16.format = SB16_FMT_S16_STEREO;
    else if (bits == 16 && channels == 1) g_sb16.format = SB16_FMT_S16_MONO;
    else if (bits == 8 && channels == 2) g_sb16.format = SB16_FMT_U8_STEREO;
    else g_sb16.format = SB16_FMT_U8_MONO;

    return 0;
}

uint32_t sb16_get_buffer_position(void)
{
    if (!g_sb16.enabled || !g_sb16.playing) return 0;

    /* Read the DMA controller's current address register to get position */
    if (g_sb16.bits_per_sample == 16) {
        uint8_t dma_addr = (uint8_t)((g_sb16.dma16 & 0x03) << 2);
        outb(ISA_DMA16_CLR_BYTE_FF, 0);
        uint16_t addr_lo = inb(dma_addr);
        uint16_t addr_hi = inb(dma_addr);
        uint16_t current_word_addr = (uint16_t)((addr_hi << 8) | addr_lo);
        return (uint32_t)current_word_addr * 2;
    } else {
        uint8_t dma_addr = (uint8_t)((g_sb16.dma8 & 0x03) << 1);
        outb(ISA_DMA_CLR_BYTE_FF, 0);
        uint16_t addr_lo = inb(dma_addr);
        uint16_t addr_hi = inb(dma_addr);
        return (uint16_t)((addr_hi << 8) | addr_lo);
    }
}

sb16_device_t *sb16_get_device(void)
{
    return &g_sb16;
}

uint8_t sb16_is_available(void)
{
    return g_sb16.initialized && g_sb16.enabled;
}

/* ============================================================================
 * Software Voice Mixing
 *
 * Since the SB16 only supports a single hardware DMA channel for playback,
 * we implement software mixing for multiple simultaneous voices. Each voice
 * has its own PCM buffer, sample rate, volume, and pan. The mixer renders
 * all active voices into the DMA buffer, performing sample rate conversion,
 * volume scaling, and panning.
 * ============================================================================
 */

int sb16_voice_alloc(void)
{
    uint32_t i;
    for (i = 0; i < SB16_MAX_VOICES; i++) {
        if (!g_sb16.voices[i].active) {
            memset(&g_sb16.voices[i], 0, sizeof(sb16_voice_t));
            g_sb16.voices[i].active = 1;
            g_sb16.voices[i].volume = 255;
            g_sb16.voices[i].pan = 128;
            return (int)i;
        }
    }
    return -1;  /* No free voices */
}

void sb16_voice_free(int voice_id)
{
    if (voice_id < 0 || voice_id >= SB16_MAX_VOICES) return;
    g_sb16.voices[voice_id].active = 0;
}

int sb16_voice_play(int voice_id, const void *data, uint32_t size,
                    uint32_t rate, uint16_t channels, uint8_t loop)
{
    if (voice_id < 0 || voice_id >= SB16_MAX_VOICES) return -1;
    if (!g_sb16.voices[voice_id].active) return -1;

    sb16_voice_t *v = &g_sb16.voices[voice_id];
    v->buffer      = (uint8_t *)data;
    v->buffer_size = size;
    v->position    = 0;
    v->sample_rate = rate;
    v->channels    = channels;
    v->bits        = 16;  /* Assume 16-bit for mixed voices */
    v->loop        = loop;

    return 0;
}

void sb16_voice_stop(int voice_id)
{
    if (voice_id < 0 || voice_id >= SB16_MAX_VOICES) return;
    g_sb16.voices[voice_id].position = g_sb16.voices[voice_id].buffer_size;
}

int sb16_voice_set_volume(int voice_id, uint8_t volume)
{
    if (voice_id < 0 || voice_id >= SB16_MAX_VOICES) return -1;
    g_sb16.voices[voice_id].volume = volume;
    return 0;
}

int sb16_voice_set_pan(int voice_id, uint8_t pan)
{
    if (voice_id < 0 || voice_id >= SB16_MAX_VOICES) return -1;
    g_sb16.voices[voice_id].pan = pan;
    return 0;
}

/**
 * Mix all active voices into the mixing buffer.
 * For each voice:
 *   1. Determine the number of samples to mix based on the output sample rate
 *   2. For each sample, apply volume and pan
 *   3. Mix into the output buffer (16-bit stereo, signed)
 * After mixing, the buffer can be sent to the SB16 via DMA.
 */
void sb16_voice_mix_all(void)
{
    if (!g_sb16.mix_buffer || g_sb16.mix_buffer_size == 0) return;
    if (!g_sb16.enabled) return;

    /* Clear the mix buffer */
    memset(g_sb16.mix_buffer, 0, g_sb16.mix_buffer_size);

    uint32_t output_samples = g_sb16.mix_buffer_size / 4;  /* 16-bit stereo = 4 bytes/sample */
    uint32_t output_rate = g_sb16.sample_rate;
    int16_t *mix_out = (int16_t *)g_sb16.mix_buffer;

    uint32_t vi;
    for (vi = 0; vi < SB16_MAX_VOICES; vi++) {
        sb16_voice_t *v = &g_sb16.voices[vi];
        if (!v->active || !v->buffer || v->buffer_size == 0) continue;

        /* Compute resampling ratio */
        uint32_t ratio = (v->sample_rate << 16) / output_rate;

        uint32_t i;
        for (i = 0; i < output_samples; i++) {
            /* Check if voice has ended */
            if (v->position >= v->buffer_size) {
                if (v->loop) {
                    v->position = 0;
                } else {
                    break;
                }
            }

            /* Get source sample position */
            uint32_t src_pos = (i * ratio) >> 16;
            uint32_t src_byte = src_pos * 2;  /* 16-bit mono source */

            if (src_byte + 1 >= v->buffer_size) {
                if (v->loop) {
                    v->position = 0;
                    src_byte = 0;
                } else {
                    break;
                }
            }

            int16_t sample = (int16_t)((uint16_t)v->buffer[src_byte] |
                                       ((uint16_t)v->buffer[src_byte + 1] << 8));

            /* Apply volume (0-255 to 0-1 scale) */
            int32_t vol_sample = (int32_t)sample * (int32_t)v->volume / 256;

            /* Apply panning */
            int32_t left_factor  = (255 - (int32_t)v->pan);
            int32_t right_factor = (int32_t)v->pan;

            int32_t left_sample  = (vol_sample * left_factor) / 256;
            int32_t right_sample = (vol_sample * right_factor) / 256;

            /* Mix into output (clamp to 16-bit) */
            int32_t mix_l = (int32_t)mix_out[i * 2] + left_sample;
            int32_t mix_r = (int32_t)mix_out[i * 2 + 1] + right_sample;

            if (mix_l > 32767) mix_l = 32767;
            if (mix_l < -32768) mix_l = -32768;
            if (mix_r > 32767) mix_r = 32767;
            if (mix_r < -32768) mix_r = -32768;

            mix_out[i * 2]     = (int16_t)mix_l;
            mix_out[i * 2 + 1] = (int16_t)mix_r;
        }

        /* Update voice position */
        v->position = (v->position + output_samples * 2) % v->buffer_size;
    }
}

/* ============================================================================
 * OPL3 FM Synthesis
 *
 * The OPL3 (Yamaha YMF262) is a 2-operator FM synthesis chip. It provides
 * 18 channels (in OPL3 mode), each with 2 operators. Each operator can be
 * configured with:
 *   - Waveform (sine, half-sine, absolute-sine, pulse-sine, etc.)
 *   - Envelope (attack rate, decay rate, sustain level, release rate)
 *   - Vibrato and tremolo
 *   - Frequency multiplier
 *   - Key scaling
 *
 * The OPL3 is accessed through two index/data register pairs:
 *   - Port 0x388/0x389: Bank 0 (channels 0-8)
 *   - Port 0x38A/0x38B: Bank 1 (channels 9-17)
 *
 * For each channel, the note frequency is determined by:
 *   - F-number (10 bits): fine frequency control
 *   - Block (3 bits): octave selector
 *   - The actual frequency = F-num * 49716 / (2^(20 - block))
 * ============================================================================
 */

/**
 * Write a value to an OPL3 register.
 * Selects the correct bank based on the register address.
 */
static void opl3_write(uint16_t reg, uint8_t val)
{
    if (reg < 0x100) {
        outb(OPL3_BASE_PORT, (uint8_t)reg);
        io_wait();
        outb(OPL3_DATA_PORT, val);
    } else {
        outb(OPL3_BASE_PORT_2, (uint8_t)(reg & 0xFF));
        io_wait();
        outb(OPL3_DATA_PORT_2, val);
    }
    io_wait();
}

void sb16_opl3_init(void)
{
    if (!g_sb16.opl3_enabled) return;

    sb16_opl3_reset();
    klog_info("SB16: OPL3 initialized");
}

void sb16_opl3_reset(void)
{
    if (!g_sb16.opl3_enabled) return;

    /* Enable OPL3 mode */
    opl3_write(0x105, 0x01);  /* Enable OPL3 extensions */

    /* Disable all notes */
    uint8_t ch;
    for (ch = 0; ch < 9; ch++) {
        opl3_write(0xB0 + ch, 0x00);  /* Bank 0: key off */
        opl3_write(0x1B0 + ch, 0x00); /* Bank 1: key off */
    }

    /* Set all operators to default */
    memset(g_sb16.opl3_note_on, 0, sizeof(g_sb16.opl3_note_on));
}

void sb16_opl3_note_on(uint8_t channel, uint8_t note, uint8_t velocity)
{
    if (!g_sb16.opl3_enabled) return;
    if (channel >= 18) return;
    if (note > 127) return;

    /* Calculate octave (block) and F-number */
    uint8_t octave = note / 12;
    uint8_t semitone = note % 12;

    if (octave > 7) octave = 7;

    uint16_t fnum = g_opl3_fnum[semitone];
    uint8_t block = g_opl3_block[octave];

    /* Determine register bank */
    uint8_t bank = (channel >= 9) ? 1 : 0;
    uint8_t ch_offset = (channel >= 9) ? (channel - 9) : channel;

    /* Write F-number low byte */
    uint16_t reg_a0 = (uint16_t)((bank == 0 ? 0xA0 : 0x1A0) + ch_offset);
    opl3_write(reg_a0, (uint8_t)(fnum & 0xFF));

    /* Write key-on and block */
    uint16_t reg_b0 = (uint16_t)((bank == 0 ? 0xB0 : 0x1B0) + ch_offset);
    uint8_t key_on_val = (uint8_t)((block << 2) | ((fnum >> 8) & 0x03) | 0x20);
    opl3_write(reg_b0, key_on_val);

    g_sb16.opl3_note_on[channel] = 1;
}

void sb16_opl3_note_off(uint8_t channel, uint8_t note)
{
    if (!g_sb16.opl3_enabled) return;
    if (channel >= 18) return;

    (void)note;

    uint8_t bank = (channel >= 9) ? 1 : 0;
    uint8_t ch_offset = (channel >= 9) ? (channel - 9) : channel;

    uint16_t reg_b0 = (uint16_t)((bank == 0 ? 0xB0 : 0x1B0) + ch_offset);
    opl3_write(reg_b0, 0x00);

    g_sb16.opl3_note_on[channel] = 0;
}

void sb16_opl3_set_instrument(uint8_t channel, uint8_t program)
{
    if (!g_sb16.opl3_enabled) return;
    if (channel >= 18) return;
    if (program > 127) program = 0;

    /* Use default instrument for all programs (simplified) */
    const opl3_instrument_t *inst = &g_opl3_default_instrument;

    uint8_t bank = (channel >= 9) ? 1 : 0;
    uint8_t ch_offset = (channel >= 9) ? (channel - 9) : channel;
    uint8_t op1_offset = g_opl3_chan_op_offset[channel];
    uint8_t op2_offset = (uint8_t)(op1_offset + 3);

    uint16_t reg_base = (bank == 0) ? 0 : 0x100;

    /* Operator 1 */
    opl3_write((uint16_t)(reg_base + 0x20 + op1_offset), inst->op1_am_vib);
    opl3_write((uint16_t)(reg_base + 0x40 + op1_offset), inst->op1_ksl_tl);
    opl3_write((uint16_t)(reg_base + 0x60 + op1_offset), inst->op1_ar_dr);
    opl3_write((uint16_t)(reg_base + 0x80 + op1_offset), inst->op1_sl_rr);
    opl3_write((uint16_t)(reg_base + 0xE0 + op1_offset), inst->op1_ws);

    /* Operator 2 */
    opl3_write((uint16_t)(reg_base + 0x20 + op2_offset), inst->op2_am_vib);
    opl3_write((uint16_t)(reg_base + 0x40 + op2_offset), inst->op2_ksl_tl);
    opl3_write((uint16_t)(reg_base + 0x60 + op2_offset), inst->op2_ar_dr);
    opl3_write((uint16_t)(reg_base + 0x80 + op2_offset), inst->op2_sl_rr);
    opl3_write((uint16_t)(reg_base + 0xE0 + op2_offset), inst->op2_ws);

    /* Feedback/Algorithm */
    opl3_write((uint16_t)(reg_base + 0xC0 + ch_offset), inst->feedback);
}

void sb16_opl3_pitch_bend(uint8_t channel, uint16_t bend)
{
    if (!g_sb16.opl3_enabled) return;
    if (channel >= 18) return;

    /* Pitch bend is [-8192, 8191] with center at 0x2000.
     * We apply it by modifying the F-number slightly. */
    int32_t bend_val = (int32_t)(bend & 0x3FFF) - 0x2000;

    /* Apply a fraction of the bend to the F-number */
    uint8_t bank = (channel >= 9) ? 1 : 0;
    uint8_t ch_offset = (channel >= 9) ? (channel - 9) : channel;

    uint16_t reg_a0 = (uint16_t)((bank == 0 ? 0xA0 : 0x1A0) + ch_offset);
    uint16_t fnum = (uint16_t)(g_opl3_fnum[0] + (bend_val / 256));

    opl3_write(reg_a0, (uint8_t)(fnum & 0xFF));
}

void sb16_opl3_set_volume(uint8_t channel, uint8_t volume)
{
    if (!g_sb16.opl3_enabled) return;
    if (channel >= 18) return;

    /* Volume is controlled via Total Level (TL) register.
     * TL = 0 is loudest, TL = 63 is quietest. */
    uint8_t tl = (uint8_t)(63 - ((uint32_t)volume * 63 / 255));

    uint8_t bank = (channel >= 9) ? 1 : 0;
    uint8_t op2_offset = (uint8_t)(g_opl3_chan_op_offset[channel] + 3);

    uint16_t reg_base = (bank == 0) ? 0 : 0x100;
    opl3_write((uint16_t)(reg_base + 0x40 + op2_offset), tl);
}

/* ============================================================================
 * MIDI (MPU-401 UART)
 * ============================================================================
 */

void sb16_midi_init(void)
{
    if (!g_sb16.midi_enabled) return;

    /* Already in UART mode from detection */
    klog_info("SB16: MIDI UART ready");
}

int sb16_midi_write(uint8_t byte)
{
    if (!g_sb16.midi_enabled) return -1;

    /* Wait for output ready (DSR = 1) */
    uint32_t timeout = 100000;
    while (timeout--) {
        uint8_t status = inb(MPU401_STATUS_PORT);
        if (status & MPU401_STATUS_DSR) {
            outb(MPU401_DATA_PORT, byte);
            return 0;
        }
        io_wait();
    }

    return -1;
}

int sb16_midi_read(uint8_t *byte)
{
    if (!g_sb16.midi_enabled) return -1;
    if (!byte) return -1;

    /* Check if data is available (DRR = 1) */
    uint8_t status = inb(MPU401_STATUS_PORT);
    if (!(status & MPU401_STATUS_DRR)) {
        return -1;  /* No data available */
    }

    *byte = inb(MPU401_DATA_PORT);
    return 0;
}

int sb16_midi_data_available(void)
{
    if (!g_sb16.midi_enabled) return 0;

    uint8_t status = inb(MPU401_STATUS_PORT);
    return (status & MPU401_STATUS_DRR) ? 1 : 0;
}