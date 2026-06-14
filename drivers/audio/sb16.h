#ifndef SB16_H
#define SB16_H

#include "stdint.h"

/* ============================================================================
 * Sound Blaster 16 Driver - Complete Register and Command Definitions
 *
 * The Sound Blaster 16 (SB16) is Creative Labs' 16-bit ISA sound card.
 * This driver implements full DSP and mixer functionality:
 *   - DSP detection and version identification
 *   - Direct DAC and DMA-based playback
 *   - Auto-init DMA for continuous streaming
 *   - 8-bit and 16-bit DMA (ISA DMA controller programming)
 *   - Mixer chip access (CT1335/CT1345/CT1745)
 *   - Full mixer controls (Master, Voice, FM, CD, Line, Mic)
 *   - Input source selection and output gain/mute
 *   - Interrupt-driven playback
 *   - Multiple sample rates (5000-44100 Hz)
 *   - Multiple audio formats (8/16-bit, mono/stereo, signed/unsigned)
 *   - Software voice mixing (up to 8 simultaneous voices)
 *   - OPL3 FM synthesis (register access at 0x388/0x389)
 *   - 128 GM instrument patches mapped to OPL3 registers
 *   - MIDI port support (MPU-401 UART mode at 0x330)
 *   - ISA PnP detection for non-legacy addresses
 * ============================================================================
 */

/* --- ISA I/O Base Addresses (typical) --- */
#define SB16_BASE_MIN      0x210
#define SB16_BASE_MAX      0x260
#define SB16_BASE_STEP     0x10
#define SB16_DEFAULT_BASE  0x220

/* --- DSP Register Offsets from Base --- */
#define SB16_DSP_RESET          0x06  /* Write 1 then 0 to reset DSP */
#define SB16_DSP_READ           0x0A  /* Read data from DSP */
#define SB16_DSP_WRITE          0x0C  /* Write command/data to DSP */
#define SB16_DSP_STATUS         0x0C  /* Read: bit 7 = busy (not ready for write) */
#define SB16_DSP_DATA_AVAIL     0x0E  /* Read: bit 7 = data available */
#define SB16_DSP_ACK_16BIT      0x0F  /* Read: acknowledge 16-bit interrupt */

/* --- Mixer Register Offsets (accessed via base+0x04 index, base+0x05 data) --- */
#define SB16_MIXER_INDEX        0x04  /* Mixer register index port */
#define SB16_MIXER_DATA         0x05  /* Mixer register data port */

/* --- Mixer Register Numbers --- */
/* CT1335/CT1345/CT1745 compatible registers */
#define SB16_MIXER_RESET        0x00  /* Reset mixer (write 0x00) */
#define SB16_MIXER_VOICE_VOL    0x04  /* Voice (DAC) volume: left/right each 0-15 */
#define SB16_MIXER_FM_VOL       0x06  /* FM synthesis volume: left/right 0-15 */
#define SB16_MIXER_CD_VOL       0x08  /* CD audio volume: left/right 0-15 */
#define SB16_MIXER_LINE_VOL     0x0E  /* Line in volume: left/right 0-15 */
#define SB16_MIXER_MIC_VOL      0x1A  /* Mic volume: 0-15 (mono) */
#define SB16_MIXER_PC_BEEP_VOL  0x1C  /* PC speaker volume: 0-7 */
#define SB16_MIXER_MASTER_VOL   0x30  /* Master volume: left/right 0-15 (CT1745) */
#define SB16_MIXER_MASTER_VOL_CT1335 0x22  /* Master volume for CT1335 */
#define SB16_MIXER_BASS_VOL     0x32  /* Bass volume: left/right 0-15 (CT1745) */
#define SB16_MIXER_TREBLE_VOL   0x34  /* Treble volume: left/right 0-15 (CT1745) */
#define SB16_MIXER_AUX_VOL      0x36  /* Aux volume: left/right 0-15 (CT1745) */
#define SB16_MIXER_OUTPUT_SW    0x1E  /* Output switch (filter enable) */
#define SB16_MIXER_INPUT_SEL    0x1C  /* Input source selection */
#define SB16_MIXER_INPUT_GAIN   0x20  /* Input gain: left/right 0-15 */
#define SB16_MIXER_OUTPUT_GAIN  0x22  /* Output gain: left/right 0-15 */
#define SB16_MIXER_AGC          0x1E  /* Automatic Gain Control (bit 0) */
#define SB16_MIXER_TREBLE       0x44  /* Treble tone control */
#define SB16_MIXER_BASS         0x46  /* Bass tone control */
#define SB16_MIXER_IRQ          0x80  /* IRQ select: bit 0=IRQ2, 1=5, 2=7, 3=10 */
#define SB16_MIXER_DMA          0x81  /* DMA select: bits 0-1=8-bit DMA, bits 2-3=16-bit DMA */
#define SB16_MIXER_VOLUME_MASK  0x0F  /* Volume bits: 4 bits per channel */

/* --- Input Source Select Values --- */
#define SB16_INPUT_MIC           0x01  /* Microphone */
#define SB16_INPUT_CD            0x03  /* CD audio */
#define SB16_INPUT_LINE          0x07  /* Line in */
#define SB16_INPUT_FM            0x0B  /* FM synthesis */
#define SB16_INPUT_MUTE          0x00  /* Mute all inputs */

/* --- DSP Commands  --- */
/* Direct DAC (8-bit, no DMA) */
#define SB16_CMD_DIRECT_DAC_8BIT    0x10  /* Followed by 8-bit sample */

/* Single-cycle DMA (one-shot) */
#define SB16_CMD_DMA_8BIT           0x14  /* 8-bit PCM DMA: mode byte, len_low, len_high */
#define SB16_CMD_DMA_2BIT_ADPCM     0x16  /* 2-bit ADPCM DMA */
#define SB16_CMD_DMA_2_6BIT_ADPCM   0x17  /* 2.6-bit ADPCM DMA */
#define SB16_CMD_DMA_4BIT_ADPCM     0x16  /* 4-bit ADPCM DMA */
#define SB16_CMD_DMA_16BIT          0xB6  /* 16-bit PCM DMA */
#define SB16_CMD_DMA_16BIT_FIFO     0xB5  /* 16-bit FIFO DMA */

/* Auto-init DMA (continuous loop) */
#define SB16_CMD_AUTO_INIT_8BIT     0x1C  /* 8-bit auto-init DMA */
#define SB16_CMD_AUTO_INIT_16BIT    0xB6  /* 16-bit auto-init DMA (with auto-init bit set) */
#define SB16_CMD_AUTO_INIT_8BIT_HS  0x90  /* 8-bit high-speed auto-init DMA */

/* Transfer mode bits for 0xB6 command */
#define SB16_MODE_16BIT_MONO        0x00  /* 16-bit mono unsigned */
#define SB16_MODE_16BIT_MONO_SIGNED 0x10  /* 16-bit mono signed */
#define SB16_MODE_16BIT_STEREO      0x20  /* 16-bit stereo unsigned */
#define SB16_MODE_16BIT_STEREO_SIGNED 0x30  /* 16-bit stereo signed */
#define SB16_MODE_AUTO_INIT         0x04  /* Auto-init bit */
#define SB16_MODE_FIFO              0x02  /* FIFO mode */

/* Sample rate commands */
#define SB16_CMD_SET_RATE_OUTPUT    0x41  /* Set output sample rate: rate_high, rate_low */
#define SB16_CMD_SET_RATE_INPUT     0x42  /* Set input sample rate: rate_high, rate_low */

/* Control commands */
#define SB16_CMD_SPEAKER_ON         0xD1  /* Enable speaker */
#define SB16_CMD_SPEAKER_OFF        0xD3  /* Disable speaker */
#define SB16_CMD_STOP_8BIT          0xD0  /* Stop 8-bit DMA */
#define SB16_CMD_STOP_16BIT         0xD5  /* Stop 16-bit DMA */
#define SB16_CMD_CONTINUE_8BIT      0xD4  /* Continue 8-bit DMA */
#define SB16_CMD_CONTINUE_16BIT     0xD6  /* Continue 16-bit DMA */
#define SB16_CMD_PAUSE_8BIT         0xD0  /* Pause 8-bit DMA */
#define SB16_CMD_PAUSE_16BIT        0xD5  /* Pause 16-bit DMA */

/* DSP version */
#define SB16_CMD_GET_VERSION        0xE1  /* Get DSP version: returns major, minor */

/* DSP identification */
#define SB16_CMD_GET_ID             0xE7  /* Get DSP ID (SB16 returns 0x04) */

/* Block transfer size */
#define SB16_CMD_SET_BLOCK_SIZE     0x48  /* Set block transfer size */

/* DSP reset response */
#define SB16_DSP_RESET_OK           0xAA  /* DSP returns 0xAA after successful reset */

/* --- ISA DMA Controller Registers --- */
/* 8-bit DMA channels (0-3) */
#define ISA_DMA_ADDR_CH0     0x00  /* Channel 0 address */
#define ISA_DMA_COUNT_CH0    0x01  /* Channel 0 count */
#define ISA_DMA_ADDR_CH1     0x02  /* Channel 1 address */
#define ISA_DMA_COUNT_CH1    0x03  /* Channel 1 count */
#define ISA_DMA_ADDR_CH2     0x04  /* Channel 2 address */
#define ISA_DMA_COUNT_CH2    0x05  /* Channel 2 count */
#define ISA_DMA_ADDR_CH3     0x06  /* Channel 3 address */
#define ISA_DMA_COUNT_CH3    0x07  /* Channel 3 count */

/* 16-bit DMA channels (5-7) - word-aligned addresses */
#define ISA_DMA16_ADDR_CH5   0xC4  /* Channel 5 address */
#define ISA_DMA16_COUNT_CH5  0xC6  /* Channel 5 count */
#define ISA_DMA16_ADDR_CH6   0xC8  /* Channel 6 address */
#define ISA_DMA16_COUNT_CH6  0xCA  /* Channel 6 count */
#define ISA_DMA16_ADDR_CH7   0xCC  /* Channel 7 address */
#define ISA_DMA16_COUNT_CH7  0xCE  /* Channel 7 count */

/* DMA control registers */
#define ISA_DMA_CMD          0x08  /* DMA command register */
#define ISA_DMA_REQ          0x09  /* DMA request register */
#define ISA_DMA_MASK_SINGLE  0x0A  /* Single channel mask register */
#define ISA_DMA_MODE         0x0B  /* DMA mode register */
#define ISA_DMA_CLR_BYTE_FF  0x0C  /* Clear byte flip-flop */
#define ISA_DMA_TEMP         0x0D  /* Temporary register */
#define ISA_DMA_MASTER_CLEAR  0x0D /* Master clear */
#define ISA_DMA_CLR_MASK     0x0E  /* Clear mask register */
#define ISA_DMA_MASK_ALL     0x0F  /* Write mask register (all channels) */

/* 16-bit DMA extended registers */
#define ISA_DMA16_CMD        0xD0  /* 16-bit DMA command register */
#define ISA_DMA16_REQ        0xD2  /* 16-bit DMA request register */
#define ISA_DMA16_MASK_SINGLE 0xD4 /* 16-bit single channel mask */
#define ISA_DMA16_MODE       0xD6  /* 16-bit DMA mode register */
#define ISA_DMA16_CLR_BYTE_FF 0xD8 /* 16-bit clear byte flip-flop */
#define ISA_DMA16_MASTER_CLEAR 0xDA /* 16-bit master clear */
#define ISA_DMA16_CLR_MASK   0xDC  /* 16-bit clear mask register */
#define ISA_DMA16_MASK_ALL   0xDE  /* 16-bit write mask register */

/* DMA page registers (high 4 bits of address for 8-bit channels) */
#define ISA_DMA_PAGE_CH0     0x87  /* Page register for channel 0 */
#define ISA_DMA_PAGE_CH1     0x83  /* Page register for channel 1 */
#define ISA_DMA_PAGE_CH2     0x81  /* Page register for channel 2 */
#define ISA_DMA_PAGE_CH3     0x82  /* Page register for channel 3 */

/* DMA page registers for 16-bit channels */
#define ISA_DMA_PAGE_CH5     0x8B  /* Page register for channel 5 */
#define ISA_DMA_PAGE_CH6     0x89  /* Page register for channel 6 */
#define ISA_DMA_PAGE_CH7     0x8A  /* Page register for channel 7 */

/* DMA mode register bits */
#define ISA_DMA_MODE_DEMAND  0x00  /* Demand mode */
#define ISA_DMA_MODE_SINGLE  0x40  /* Single mode */
#define ISA_DMA_MODE_BLOCK   0x80  /* Block mode */
#define ISA_DMA_MODE_CASCADE 0xC0  /* Cascade mode */
#define ISA_DMA_MODE_INC     0x00  /* Address increment */
#define ISA_DMA_MODE_DEC     0x20  /* Address decrement */
#define ISA_DMA_MODE_AUTO    0x10  /* Auto-initialize */
#define ISA_DMA_MODE_VERIFY  0x00  /* Verify transfer */
#define ISA_DMA_MODE_WRITE   0x04  /* Write to memory (from device) */
#define ISA_DMA_MODE_READ    0x08  /* Read from memory (to device) */

/* --- DMA Channel Assignments --- */
#define SB16_DMA_8BIT_CHANNEL   1   /* Default 8-bit DMA channel */
#define SB16_DMA_16BIT_CHANNEL  5   /* Default 16-bit DMA channel */

/* --- IRQ Assignments --- */
#define SB16_IRQ_2  2
#define SB16_IRQ_5  5
#define SB16_IRQ_7  7
#define SB16_IRQ_10 10

/* --- Sample Rate Limits --- */
#define SB16_MIN_RATE   5000
#define SB16_MAX_RATE   44100

/* --- Audio Format Types --- */
#define SB16_FMT_U8_MONO      0  /* 8-bit unsigned, mono */
#define SB16_FMT_U8_STEREO    1  /* 8-bit unsigned, stereo */
#define SB16_FMT_S16_MONO     2  /* 16-bit signed, mono */
#define SB16_FMT_S16_STEREO   3  /* 16-bit signed, stereo */

/* --- Software Voice Mixing --- */
#define SB16_MAX_VOICES       8   /* Maximum simultaneous software voices */
#define SB16_VOICE_BUFFER_SIZE 16384  /* Per-voice buffer size in bytes */

/* --- OPL3 FM Synthesis --- */
#define OPL3_BASE_PORT        0x388  /* OPL3 register select port */
#define OPL3_DATA_PORT        0x389  /* OPL3 data port */
#define OPL3_BASE_PORT_2      0x38A  /* OPL3 register select port (bank 2) */
#define OPL3_DATA_PORT_2      0x38B  /* OPL3 data port (bank 2) */

/* OPL3 registers */
#define OPL3_REG_TEST         0x01  /* Test register */
#define OPL3_REG_TIMER1       0x02  /* Timer 1 */
#define OPL3_REG_TIMER2       0x03  /* Timer 2 */
#define OPL3_REG_TIMER_CTL    0x04  /* Timer control */
#define OPL3_REG_NOTE_SEL     0x08  /* Note select (keyboard split) */
#define OPL3_REG_FNUM_L       0xA0  /* F-number low byte */
#define OPL3_REG_KEYON_BLOCK  0xB0  /* Key on / block (octave) */
#define OPL3_REG_RHYTHM       0xBD  /* Rhythm mode */
#define OPL3_REG_FEEDBACK     0xC0  /* Feedback / algorithm */

/* Per-channel (operator) registers. 18 channels, 2 operators each.
 * Channel n: operator 1 base = offset(n), operator 2 base = offset(n) + 3 */
#define OPL3_REG_AM_VIB       0x20  /* AM/VIB/EG-TYP/KSR/MULT */
#define OPL3_REG_KSL_TL       0x40  /* KSL/Total Level */
#define OPL3_REG_AR_DR        0x60  /* Attack Rate / Decay Rate */
#define OPL3_REG_SL_RR        0x80  /* Sustain Level / Release Rate */
#define OPL3_REG_WS           0xE0  /* Wave Select */

/* --- MIDI (MPU-401 UART) --- */
#define MPU401_BASE_PORT      0x330  /* MPU-401 base port */
#define MPU401_DATA_PORT      0x330  /* MPU-401 data port */
#define MPU401_CMD_PORT       0x331  /* MPU-401 command/status port */
#define MPU401_STATUS_PORT    0x331  /* MPU-401 status port */

/* MPU-401 commands */
#define MPU401_CMD_UART_MODE  0x3F  /* Enter UART mode */
#define MPU401_CMD_RESET      0xFF  /* Reset MPU-401 */

/* MPU-401 status bits */
#define MPU401_STATUS_DSR     (1 << 7)  /* Data Set Ready (output ready) */
#define MPU401_STATUS_DRR     (1 << 6)  /* Data Read Ready (input ready) */

/* --- MIDI Messages --- */
#define MIDI_NOTE_OFF         0x80
#define MIDI_NOTE_ON          0x90
#define MIDI_POLY_PRESSURE    0xA0
#define MIDI_CONTROL_CHANGE   0xB0
#define MIDI_PROGRAM_CHANGE   0xC0
#define MIDI_CHANNEL_PRESSURE 0xD0
#define MIDI_PITCH_BEND       0xE0
#define MIDI_SYSTEM           0xF0

/* --- Voice Structure for Software Mixing --- */
typedef struct sb16_voice {
    uint8_t  active;           /* Voice is active */
    uint8_t  *buffer;          /* PCM data buffer */
    uint32_t buffer_size;      /* Buffer size in bytes */
    uint32_t position;         /* Current playback position */
    uint32_t sample_rate;      /* Voice sample rate */
    uint16_t channels;         /* 1=mono, 2=stereo */
    uint16_t bits;             /* 8 or 16 */
    uint8_t  volume;           /* 0-255 */
    uint8_t  pan;              /* 0=left, 128=center, 255=right */
    uint8_t  loop;             /* Loop playback */
} sb16_voice_t;

/* --- SB16 Device Structure --- */
typedef struct sb16_device {
    /* Hardware configuration */
    uint32_t base;              /* ISA I/O base address */
    uint8_t  irq;               /* IRQ line */
    uint8_t  dma8;              /* 8-bit DMA channel */
    uint8_t  dma16;             /* 16-bit DMA channel */

    /* DSP identification */
    uint8_t  dsp_major;         /* DSP major version */
    uint8_t  dsp_minor;         /* DSP minor version */
    uint8_t  dsp_id;            /* DSP ID (4 for SB16) */

    /* Mixer type */
    uint8_t  mixer_type;        /* 0=CT1335, 1=CT1345, 2=CT1745 */
    uint8_t  mixer_has_tone;    /* Mixer supports bass/treble */

    /* Current audio configuration */
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint16_t format;            /* SB16_FMT_* */

    /* DMA buffer management */
    uint8_t  *dma_buffer;       /* DMA buffer (physically contiguous) */
    uint32_t dma_buffer_size;   /* DMA buffer size */
    uint32_t dma_buffer_phys;   /* Physical address of DMA buffer */

    /* Playback state */
    uint8_t  playing;           /* Currently playing */
    uint8_t  auto_init;         /* Using auto-init mode */

    /* Software voice mixing */
    sb16_voice_t voices[SB16_MAX_VOICES];  /* Voice array */
    uint8_t  *mix_buffer;       /* Mixing buffer */
    uint32_t mix_buffer_size;   /* Mixing buffer size */

    /* OPL3 FM synthesis state */
    uint8_t  opl3_enabled;      /* OPL3 detected */
    uint8_t  opl3_note_on[18];  /* Note-on state for each channel */

    /* MIDI state */
    uint8_t  midi_enabled;      /* MPU-401 UART detected */
    uint8_t  midi_running;      /* MIDI byte being received */

    /* Mixer state */
    uint8_t  master_vol_left;
    uint8_t  master_vol_right;
    uint8_t  voice_vol_left;
    uint8_t  voice_vol_right;
    uint8_t  fm_vol_left;
    uint8_t  fm_vol_right;
    uint8_t  cd_vol_left;
    uint8_t  cd_vol_right;
    uint8_t  line_vol_left;
    uint8_t  line_vol_right;
    uint8_t  mic_vol;
    uint8_t  input_source;
    uint8_t  master_muted;
    uint8_t  irq_registered;

    /* Driver state */
    uint8_t  enabled;
    uint8_t  initialized;
} sb16_device_t;

/* --- Public API --- */
void sb16_init(void);
int  sb16_play(const void *data, uint32_t size, uint32_t rate, uint16_t channels);
void sb16_stop(void);
int  sb16_set_sample_rate(uint32_t rate);
int  sb16_set_volume(uint8_t left, uint8_t right);
int  sb16_get_volume(uint8_t *left, uint8_t *right);
int  sb16_set_mute(uint8_t mute);
int  sb16_get_mute(void);
int  sb16_set_master_volume(uint8_t left, uint8_t right);
int  sb16_set_voice_volume(uint8_t left, uint8_t right);
int  sb16_set_fm_volume(uint8_t left, uint8_t right);
int  sb16_set_cd_volume(uint8_t left, uint8_t right);
int  sb16_set_line_volume(uint8_t left, uint8_t right);
int  sb16_set_mic_volume(uint8_t volume);
int  sb16_set_input_source(uint8_t source);
int  sb16_set_format(uint16_t bits, uint16_t channels);
uint32_t sb16_get_buffer_position(void);
sb16_device_t *sb16_get_device(void);
uint8_t sb16_is_available(void);

/* --- Software Voice Mixing API --- */
int  sb16_voice_alloc(void);
void sb16_voice_free(int voice_id);
int  sb16_voice_play(int voice_id, const void *data, uint32_t size,
                     uint32_t rate, uint16_t channels, uint8_t loop);
void sb16_voice_stop(int voice_id);
int  sb16_voice_set_volume(int voice_id, uint8_t volume);
int  sb16_voice_set_pan(int voice_id, uint8_t pan);
void sb16_voice_mix_all(void);

/* --- OPL3 FM Synthesis API --- */
void sb16_opl3_init(void);
void sb16_opl3_reset(void);
void sb16_opl3_note_on(uint8_t channel, uint8_t note, uint8_t velocity);
void sb16_opl3_note_off(uint8_t channel, uint8_t note);
void sb16_opl3_set_instrument(uint8_t channel, uint8_t program);
void sb16_opl3_pitch_bend(uint8_t channel, uint16_t bend);
void sb16_opl3_set_volume(uint8_t channel, uint8_t volume);

/* --- MIDI API --- */
void sb16_midi_init(void);
int  sb16_midi_write(uint8_t byte);
int  sb16_midi_read(uint8_t *byte);
int  sb16_midi_data_available(void);

/* Internal - IRQ handler */
void sb16_irq_handler(void *regs);

#endif /* SB16_H */