#ifndef AC97_H
#define AC97_H

#include "stdint.h"

/* ============================================================================
 * Intel AC'97 Audio Codec Driver - Complete Register Definitions
 *
 * AC'97 is the Audio Codec '97 standard defined by Intel. This driver
 * implements the full AC'97 specification including:
 *   - Native Audio Bus Mastering (NABM) controller interface
 *   - AC-link codec access (16-bit slot-based protocol)
 *   - Full mixer register set (0x00-0x7E)
 *   - Buffer Descriptor List (BDL) DMA engine
 *   - Multiple sample rates and audio formats
 *   - Power management (D0-D3)
 *   - 3D stereo enhancement
 *   - Jack sensing and auto-mute
 *   - Interrupt-driven playback and capture
 *   - ICH6/ICH7 HD Audio compatibility mode
 * ============================================================================
 */

/* --- PCI Device IDs for ICHx AC'97 Controllers --- */
#define PCI_VENDOR_INTEL        0x8086
#define PCI_DEV_ICH0_AC97       0x2415  /* ICH0 (82801AA) */
#define PCI_DEV_ICH1_AC97       0x2425  /* ICH1 (82801AB) */
#define PCI_DEV_ICH2_AC97       0x2445  /* ICH2 (82801BA) */
#define PCI_DEV_ICH3_AC97       0x2485  /* ICH3 (82801CA) */
#define PCI_DEV_ICH4_AC97       0x24C5  /* ICH4 (82801DB) */
#define PCI_DEV_ICH5_AC97       0x24D5  /* ICH5 (82801EB) */
#define PCI_DEV_ICH6_AC97       0x266E  /* ICH6 (82801FB) */
#define PCI_DEV_ICH7_AC97       0x27DE  /* ICH7 (82801GB) */
#define PCI_DEV_ICH6_AC97_MODEM 0x266D  /* ICH6 AC'97 Modem */
#define PCI_DEV_ICH7_AC97_MODEM 0x27DD  /* ICH7 AC'97 Modem */
#define PCI_DEV_ESB_AC97        0x25A6  /* Intel ESB (6300ESB) */
#define PCI_DEV_NFORCE_AC97     0x01B1  /* NVIDIA nForce AC'97 (0x10DE) */
#define PCI_DEV_NFORCE2_AC97    0x006A  /* NVIDIA nForce2 AC'97 (0x10DE) */
#define PCI_DEV_NFORCE3_AC97    0x00DA  /* NVIDIA nForce3 AC'97 (0x10DE) */
#define PCI_DEV_NFORCE4_AC97    0x0059  /* NVIDIA nForce4 AC'97 (0x10DE) */
#define PCI_DEV_SIS_AC97        0x7012  /* SiS 7012 AC'97 (0x1039) */
#define PCI_DEV_VIA_AC97        0x3058  /* VIA VT82C686 AC'97 (0x1106) */
#define PCI_DEV_VIA8233_AC97    0x3059  /* VIA VT8233 AC'97 (0x1106) */
#define PCI_DEV_VIA8235_AC97    0x3106  /* VIA VT8235 AC'97 (0x1106) */
#define PCI_DEV_VIA8237_AC97    0x3227  /* VIA VT8237 AC'97 (0x1106) */

/* PCI class codes */
#define PCI_CLASS_MULTIMEDIA    0x04
#define PCI_SUBCLASS_AUDIO      0x01

/* --- PCI BAR Layout --- */
#define AC97_BAR_NAMBAR         0       /* Native Audio Mixer BAR (BAR0) */
#define AC97_BAR_NABMBAR        1       /* Native Audio Bus Mastering BAR (BAR1) */

/* --- AC'97 Codec Mixer Register Map (accessed via NAMBAR) --- */
/* These are 16-bit registers accessed via inw/outw at NAMBAR+offset */
#define AC97_REG_RESET              0x00  /* Reset register (write 0x0000 to reset) */
#define AC97_REG_MASTER_VOLUME      0x02  /* Master Volume (0x8008=0dB, 0x1F1F=-46.5dB) */
#define AC97_REG_AUX_OUT_VOLUME     0x04  /* AUX Output Volume */
#define AC97_REG_MONO_VOLUME        0x06  /* Mono Volume (0x8000=0dB, 0x0000=mute) */
#define AC97_REG_MASTER_TONE        0x08  /* Master Tone (Bass/Treble) */
#define AC97_REG_PC_BEEP_VOLUME     0x0A  /* PC Beep Volume */
#define AC97_REG_PHONE_VOLUME       0x0C  /* Phone Volume */
#define AC97_REG_MIC_VOLUME         0x0E  /* Microphone Volume (+20dB boost) */
#define AC97_REG_LINE_IN_VOLUME     0x10  /* Line In Volume */
#define AC97_REG_CD_VOLUME          0x12  /* CD Audio Volume */
#define AC97_REG_VIDEO_VOLUME       0x14  /* Video Volume */
#define AC97_REG_AUX_VOLUME         0x16  /* Auxiliary Volume */
#define AC97_REG_PCM_OUT_VOLUME     0x18  /* PCM Output Volume */
#define AC97_REG_RECORD_SELECT      0x1A  /* Record Source Select */
#define AC97_REG_RECORD_GAIN        0x1C  /* Record Gain (stereo mic optional) */
#define AC97_REG_RECORD_GAIN_MIC    0x1E  /* Record Gain (mono mic) */
#define AC97_REG_GENERAL_PURPOSE    0x20  /* General Purpose */
#define AC97_REG_3D_CONTROL         0x22  /* 3D Stereo Enhancement Control */
#define AC97_REG_AUDIO_INT_PAGING   0x24  /* Audio Interrupt and Paging */
#define AC97_REG_POWERDOWN          0x26  /* Powerdown Control/Status */
#define AC97_REG_DAC_RATE           0x2C  /* DAC Sample Rate (write desired rate) */
#define AC97_REG_ADC_RATE           0x32  /* ADC Sample Rate (write desired rate) */
#define AC97_REG_EXTENDED_AUDIO_ID  0x28  /* Extended Audio ID */
#define AC97_REG_EXTENDED_AUDIO_CTL 0x2A  /* Extended Audio Control/Status */
#define AC97_REG_VENDOR_ID1         0x7C  /* Vendor ID1 */
#define AC97_REG_VENDOR_ID2         0x7E  /* Vendor ID2 */

/* --- AC'97 Codec Register bit definitions --- */

/* Master Volume: 0x8008 = 0dB (each channel: bit 5-0 = attenuation, bit 7 = mute) */
#define AC97_MUTE_BIT               0x8000
#define AC97_VOL_ATTEN_MASK         0x003F
#define AC97_VOL_ATTEN_0DB          0x0000
#define AC97_VOL_ATTEN_46_5DB       0x003F

/* Microphone Volume: bit 6 = +20dB boost */
#define AC97_MIC_BOOST_20DB         0x0040

/* Record Gain: bits 3-0 = gain in 1.5dB steps (0=0dB, 15=22.5dB) */
#define AC97_RECGAIN_MASK           0x0F0F
#define AC97_RECGAIN_LEFT_SHIFT     0
#define AC97_RECGAIN_RIGHT_SHIFT    8

/* Record Select: bits 2-0 = source */
#define AC97_RECSRC_MIC             0x0000
#define AC97_RECSRC_CD              0x0001
#define AC97_RECSRC_VIDEO           0x0002
#define AC97_RECSRC_AUX             0x0003
#define AC97_RECSRC_LINE_IN         0x0004
#define AC97_RECSRC_STEREO_MIX      0x0005
#define AC97_RECSRC_MONO_MIX        0x0006
#define AC97_RECSRC_PHONE           0x0007

/* 3D Control: bits 3-0 = depth (0=off, 15=max) */
#define AC97_3D_DEPTH_MASK          0x000F
#define AC97_3D_OFF                 0x0000
#define AC97_3D_MAX                 0x000F

/* Extended Audio ID */
#define AC97_EXT_ID_VRA             (1 << 0)   /* Variable Rate Audio supported */
#define AC97_EXT_ID_DRA             (1 << 1)   /* Double Rate Audio supported */
#define AC97_EXT_ID_SPDIF           (1 << 2)   /* S/PDIF supported */
#define AC97_EXT_ID_VRM             (1 << 3)   /* Variable Rate Mic supported */
#define AC97_EXT_ID_CDAC            (1 << 4)   /* Center DAC supported */
#define AC97_EXT_ID_SDAC            (1 << 5)   /* Surround DAC supported */
#define AC97_EXT_ID_LDAC            (1 << 6)   /* LFE DAC supported */
#define AC97_EXT_ID_AMAP            (1 << 7)   /* Slot/DAC mapping supported */
#define AC97_EXT_ID_REV_MASK        0x3C00     /* Revision ID */
#define AC97_EXT_ID_REV_21          0x0000     /* AC'97 2.1 */
#define AC97_EXT_ID_REV_22          0x0400     /* AC'97 2.2 */
#define AC97_EXT_ID_REV_23          0x0800     /* AC'97 2.3 */

/* Extended Audio Control */
#define AC97_EXT_CTL_VRA            (1 << 0)   /* Enable Variable Rate Audio */
#define AC97_EXT_CTL_DRA            (1 << 1)   /* Enable Double Rate Audio */
#define AC97_EXT_CTL_SPDIF          (1 << 2)   /* S/PDIF enable */
#define AC97_EXT_CTL_VRM            (1 << 3)   /* Variable Rate Mic enable */
#define AC97_EXT_CTL_CDAC           (1 << 4)   /* Center DAC enable */
#define AC97_EXT_CTL_SDAC           (1 << 5)   /* Surround DAC enable */
#define AC97_EXT_CTL_LDAC           (1 << 6)   /* LFE DAC enable */

/* Powerdown Control/Status */
#define AC97_PWR_ADC                (1 << 0)   /* ADC powerdown */
#define AC97_PWR_DAC                (1 << 1)   /* DAC powerdown */
#define AC97_PWR_ANALOG             (1 << 2)   /* Analog mixer powerdown (Vref off) */
#define AC97_PWR_REFOUT             (1 << 3)   /* Analog mixer powerdown (Vref on) */
#define AC97_PWR_ACLINK             (1 << 4)   /* AC-link powerdown */
#define AC97_PWR_CLK                (1 << 5)   /* Internal clock disable */
#define AC97_PWR_HP                 (1 << 6)   /* Headphone amplifier powerdown */
#define AC97_PWR_MODEM              (1 << 7)   /* Modem Line Codec ADC/DAC off */
#define AC97_PWR_MODEM_LINE         (1 << 8)   /* Modem Line Codec on */
#define AC97_PWR_HSET               (1 << 9)   /* Headset support */
#define AC97_PWR_MIXER_ADC          (1 << 10)  /* Mixer ADC input */
#define AC97_PWR_MIXER_DAC          (1 << 11)  /* Mixer DAC output */

/* General Purpose */
#define AC97_GP_POP                 0x0001  /* PCM Out Path (3D bypass) */
#define AC97_GP_3D                  0x0002  /* 3D mode */
#define AC97_GP_MIX                 0x0004  /* Mono output select */
#define AC97_GP_MIX_MSK             0x0008  /* Mono output mute */
#define AC97_GP_LPBK                0x0001  /* Loopback (ADC to DAC) */
#define AC97_GP_MS                  0x0001  /* Mic Select (0=Mic1, 1=Mic2) */
#define AC97_GP_LPBK2               0x0002  /* Loopback for codec 2 */

/* Audio Interrupt and Paging */
#define AC97_INT_IOC                0x0001  /* Interrupt on Completion */
#define AC97_INT_SENSE              0x0002  /* Sense interrupt */
#define AC97_INT_SENSE_STAT         0x0001  /* Sense status bit */
#define AC97_INT_PAGE_MASK          0x000F  /* Page select */
#define AC97_PAGE_VENDOR            0x0000  /* Vendor-specific page */

/* --- NABM (Native Audio Bus Mastering) Register Layout --- */
/* Three bus master engines: PI (PCM In), PO (PCM Out), MC (Mic In) */
/* Each engine occupies 0x10 bytes within the NABMBAR region */

#define AC97_BM_PI_BASE  0x00  /* PCM In (Capture) bus master base offset */
#define AC97_BM_PO_BASE  0x10  /* PCM Out (Playback) bus master base offset */
#define AC97_BM_MC_BASE  0x20  /* Mic In (Microphone) bus master base offset */

/* Offsets within each bus master engine's register block */
#define AC97_BM_REG_BDBAR  0x00  /* Buffer Descriptor Base Address (32-bit) */
#define AC97_BM_REG_CIV    0x04  /* Current Index Value (8-bit, read-only) */
#define AC97_BM_REG_LVI    0x05  /* Last Valid Index (8-bit, write-only) */
#define AC97_BM_REG_SR     0x06  /* Status Register (16-bit) */
#define AC97_BM_REG_PICB   0x08  /* Position in Current Buffer (16-bit) */
#define AC97_BM_REG_PIV    0x0A  /* Prefetched Index Value (8-bit) */
#define AC97_BM_REG_CR     0x0B  /* Control Register (8-bit) */

/* --- Global NABM Registers (offsets from NABMBAR) --- */
#define AC97_BM_REG_GLOB_CNT  0x2C  /* Global Control */
#define AC97_BM_REG_GLOB_STA  0x30  /* Global Status (32-bit) */

/* Global Control bits */
#define AC97_GLOB_CNT_GIE     (1 << 0)  /* Global Interrupt Enable */
#define AC97_GLOB_CNT_COLD    (1 << 1)  /* Cold Reset */
#define AC97_GLOB_CNT_WARM    (1 << 2)  /* Warm Reset */
#define AC97_GLOB_CNT_SHUT    (1 << 3)  /* Shutdown */
#define AC97_GLOB_CNT_ACLINK  (1 << 4)  /* AC-link disable */
#define AC97_GLOB_CNT_AC97    (1 << 5)  /* AC'97 mode (for ICH6/ICH7) */

/* Global Status bits */
#define AC97_GLOB_STA_PI      (1 << 0)   /* PCM In interrupt */
#define AC97_GLOB_STA_PO      (1 << 1)   /* PCM Out interrupt */
#define AC97_GLOB_STA_MC      (1 << 2)   /* Mic In interrupt */
#define AC97_GLOB_STA_PI2     (1 << 20)  /* PCM In 2 interrupt */
#define AC97_GLOB_STA_PO2     (1 << 21)  /* PCM Out 2 interrupt */
#define AC97_GLOB_STA_MC2     (1 << 22)  /* Mic In 2 interrupt */
#define AC97_GLOB_STA_SAMPLE  (1 << 24)  /* Sample rate change */
#define AC97_GLOB_STA_LAST    (1 << 25)  /* Last buffer processed */
#define AC97_GLOB_STA_MASK    0x03070007

/* --- Bus Master Control Register (CR) bits --- */
#define AC97_CR_RPBM   (1 << 0)  /* Run/Pause Bus Master */
#define AC97_CR_RR     (1 << 1)  /* Reset Register */
#define AC97_CR_LVBIE  (1 << 2)  /* Last Valid Buffer Interrupt Enable */
#define AC97_CR_FEIE   (1 << 3)  /* FIFO Error Interrupt Enable */
#define AC97_CR_IOCE   (1 << 4)  /* Interrupt On Completion Enable */

/* --- Bus Master Status Register (SR) bits --- */
#define AC97_SR_DCH    (1 << 0)  /* DMA Controller Halted */
#define AC97_SR_CELV   (1 << 1)  /* Current Equals Last Valid */
#define AC97_SR_LVBCI  (1 << 2)  /* Last Valid Buffer Completion Interrupt */
#define AC97_SR_BCIS   (1 << 3)  /* Buffer Completion Interrupt Status */
#define AC97_SR_FIFOE  (1 << 4)  /* FIFO Error */

/* --- Buffer Descriptor List (BDL) --- */
#define AC97_BDL_MAX_ENTRIES    32
#define AC97_BDL_ENTRY_SIZE     8   /* 8 bytes per BDL entry */
#define AC97_BDL_IOC            (1U << 31)  /* Interrupt on Completion flag */
#define AC97_BDL_BUP            (1U << 30)  /* Buffer Underrun Policy (0=send last, 1=zero) */
#define AC97_BDL_LEN_MASK       0x0000FFFF  /* Buffer length in samples */

/* BDL entry descriptor */
typedef struct {
    uint32_t addr;      /* Physical address of buffer (must be contiguous) */
    uint32_t len_ctrl;  /* Bits 15:0 = length in samples; Bit 31 = IOC; Bit 30 = BUP */
} ac97_bdl_entry_t;

/* --- Sample Rates --- */
#define AC97_RATE_8000   8000
#define AC97_RATE_11025  11025
#define AC97_RATE_16000  16000
#define AC97_RATE_22050  22050
#define AC97_RATE_32000  32000
#define AC97_RATE_44100  44100
#define AC97_RATE_48000  48000

/* Number of supported sample rates */
#define AC97_NUM_RATES   7

/* --- Audio Format Types --- */
#define AC97_FMT_U8_MONO     0   /* 8-bit unsigned, mono */
#define AC97_FMT_U8_STEREO   1   /* 8-bit unsigned, stereo */
#define AC97_FMT_S16_MONO    2   /* 16-bit signed, mono */
#define AC97_FMT_S16_STEREO  3   /* 16-bit signed, stereo */

/* --- Maximum buffer sizes --- */
#define AC97_MAX_BUFFER_SAMPLES  65536  /* Per BDL entry max */
#define AC97_DEFAULT_PERIOD_SIZE 4096   /* Default period in bytes */

/* --- Power States --- */
#define AC97_POWER_D0  0  /* Full power */
#define AC97_POWER_D1  1  /* Light sleep */
#define AC97_POWER_D2  2  /* Deep sleep */
#define AC97_POWER_D3  3  /* Cold (off) */

/* --- Jack Sensing --- */
#define AC97_JACK_NONE      0
#define AC97_JACK_PRESENT   1
#define AC97_JACK_UNKNOWN   2

/* --- AC'97 Controller Device Structure --- */
typedef struct ac97_device {
    /* PCI configuration */
    uint8_t  pci_bus;
    uint8_t  pci_dev;
    uint8_t  pci_func;
    uint16_t vendor_id;
    uint16_t device_id;

    /* I/O base addresses */
    uint32_t nambar;        /* Native Audio Mixer Base Address Register */
    uint32_t nabmbar;       /* Native Audio Bus Master Base Address Register */

    /* Interrupt */
    uint8_t  irq;
    uint8_t  irq_registered;

    /* Codec capabilities */
    uint16_t ext_audio_id;  /* Extended Audio ID register value */
    uint8_t  supports_vra;  /* Variable Rate Audio */
    uint8_t  supports_dra;  /* Double Rate Audio */
    uint8_t  supports_spdif;/* S/PDIF */
    uint8_t  supports_3d;   /* 3D stereo enhancement */

    /* Current audio configuration */
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint16_t format;         /* AC97_FMT_* */

    /* Playback engine state */
    uint8_t  po_active;     /* PCM Out engine active */
    uint8_t  po_stream;     /* PCM Out stream number */

    /* Capture engine state */
    uint8_t  pi_active;     /* PCM In engine active */
    uint8_t  pi_stream;     /* PCM In stream number */

    /* BDL pointers for playback */
    ac97_bdl_entry_t *po_bdl;       /* Virtual address of playback BDL */
    uint32_t          po_bdl_phys;  /* Physical address of playback BDL */
    uint8_t           po_bdl_count; /* Number of BDL entries used */

    /* BDL pointers for capture */
    ac97_bdl_entry_t *pi_bdl;       /* Virtual address of capture BDL */
    uint32_t          pi_bdl_phys;  /* Physical address of capture BDL */
    uint8_t           pi_bdl_count; /* Number of BDL entries used */

    /* Power state */
    uint8_t  power_state;

    /* Mixer state */
    uint8_t  master_muted;
    uint8_t  pcm_muted;
    uint8_t  line_in_muted;
    uint8_t  mic_muted;
    uint8_t  cd_muted;
    uint8_t  aux_muted;
    uint8_t  rec_source;
    uint8_t  rec_gain_left;
    uint8_t  rec_gain_right;
    uint8_t  mic_boost;
    uint8_t  _3d_depth;

    /* Jack sense */
    uint8_t  jack_present;

    /* Driver enabled flag */
    uint8_t  enabled;
    uint8_t  initialized;
} ac97_device_t;

/* --- Public API --- */
void ac97_init(void);
int  ac97_play(const void *data, uint32_t size, uint32_t rate, uint16_t channels);
int  ac97_record(void *data, uint32_t size, uint32_t rate, uint16_t channels);
void ac97_stop(void);
void ac97_stop_playback(void);
void ac97_stop_capture(void);
int  ac97_set_volume(uint8_t left, uint8_t right);
int  ac97_get_volume(uint8_t *left, uint8_t *right);
int  ac97_set_mute(uint8_t mute);
int  ac97_get_mute(void);
int  ac97_set_sample_rate(uint32_t rate);
int  ac97_set_format(uint16_t bits, uint16_t channels);
int  ac97_set_master_volume(uint8_t left, uint8_t right);
int  ac97_set_pcm_volume(uint8_t left, uint8_t right);
int  ac97_set_line_in_volume(uint8_t left, uint8_t right);
int  ac97_set_mic_volume(uint8_t volume, uint8_t boost);
int  ac97_set_cd_volume(uint8_t left, uint8_t right);
int  ac97_set_aux_volume(uint8_t left, uint8_t right);
int  ac97_set_recording_source(uint8_t source);
int  ac97_set_recording_gain(uint8_t left, uint8_t right);
int  ac97_set_3d_depth(uint8_t depth);
int  ac97_set_power_state(uint8_t state);
uint32_t ac97_get_buffer_position(void);
uint32_t ac97_get_playback_position(void);
ac97_device_t *ac97_get_device(void);
uint8_t ac97_is_available(void);

/* Internal - IRQ handler */
void ac97_irq_handler(void *regs);

#endif /* AC97_H */