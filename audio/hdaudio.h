#ifndef HDAUDIO_H
#define HDAUDIO_H

#include "stdint.h"

/* ============================================================================
 * Intel High Definition Audio (HDA) Driver - Complete Register Definitions
 *
 * Intel HD Audio is the successor to AC'97, defined in the Intel High
 * Definition Audio Specification. This driver implements the full spec:
 *   - PCI HDA controller detection (vendor 0x8086, class 0x0403)
 *   - CORB (Command Output Ring Buffer) and RIRB (Response Input Ring Buffer)
 *   - Stream Descriptor management (up to 4 output streams, 4 input streams)
 *   - Codec enumeration and widget discovery
 *   - Audio path setup (DAC → Mixer → Pin complex)
 *   - Volume controls (amplifier gain with 0.25dB steps, mute)
 *   - Power management (D0-D3 widget states, link power management)
 *   - Interrupt handling (stream completion, unsolicited responses)
 *   - HDMI/DisplayPort audio
 *   - Full verb definitions
 *   - Multi-format support (44.1/48/88.2/96/192 kHz, 16/20/24/32-bit, 2-8 ch)
 *
 * Architecture:
 *   The HDA controller communicates with codecs through a serial link.
 *   Commands are sent via the CORB (up to 256 entries) and responses are
 *   received via the RIRB (up to 256 entries). Each codec is selected by
 *   its codec address (bits 31:28 of the command).
 *
 *   Audio data flows through Stream Descriptors (SD), each with its own
 *   Buffer Descriptor List (BDL) for DMA. The controller can have multiple
 *   input and output streams.
 *
 *   Codec widgets form an audio function graph. Key widget types:
 *     Audio Output (0x0): DAC - converts digital to analog
 *     Audio Input (0x1): ADC - converts analog to digital
 *     Audio Mixer (0x2): Sums multiple inputs
 *     Audio Selector (0x3): Selects one of multiple inputs
 *     Pin Complex (0x4): Physical jack connection
 *     Power (0x5): Power management widget
 *     Volume Knob (0x6): Physical volume knob
 * ============================================================================
 */

/* --- PCI Constants --- */
#define HDA_PCI_CLASS           0x040300  /* Class 0x04 (multimedia), subclass 0x03 (HDA) */
#define HDA_PCI_CLASS_MASK      0xFFFF00
#define HDA_INTEL_VENDOR        0x8086
#define HDA_MAX_CODECS          15
#define HDA_MAX_STREAMS         16
#define HDA_MAX_INPUT_STREAMS   4
#define HDA_MAX_OUTPUT_STREAMS  4
#define HDA_MAX_BDL_ENTRIES     256
#define HDA_MAX_CONNECTIONS     32

/* --- Controller Register Map (offsets from MMIO base) --- */
#define HDA_REG_GCAP            0x00  /* Global Capabilities (16-bit) */
#define HDA_REG_VMIN            0x02  /* Minor Version (8-bit) */
#define HDA_REG_VMAJ            0x03  /* Major Version (8-bit) */
#define HDA_REG_OUTPAY          0x04  /* Output Payload Capability (16-bit) */
#define HDA_REG_INPAY           0x06  /* Input Payload Capability (16-bit) */
#define HDA_REG_GCTL            0x08  /* Global Control (32-bit) */
#define HDA_REG_WAKEEN          0x0C  /* Wake Enable (16-bit) */
#define HDA_REG_STATESTS        0x0E  /* State Change Status (16-bit) */
#define HDA_REG_GSTS            0x10  /* Global Status (16-bit) */
#define HDA_REG_OUTSTRMPAY      0x18  /* Output Stream Payload Capability */
#define HDA_REG_INSTRMPAY       0x1A  /* Input Stream Payload Capability */
#define HDA_REG_INTCTL          0x20  /* Interrupt Control (32-bit) */
#define HDA_REG_INTSTS          0x24  /* Interrupt Status (32-bit) */
#define HDA_REG_WALCLK          0x30  /* Wall Clock Counter (32-bit) */
#define HDA_REG_SSYNC           0x38  /* Stream Synchronization (32-bit) */
#define HDA_REG_CORBLBASE       0x40  /* CORB Lower Base Address (32-bit) */
#define HDA_REG_CORBUBASE       0x44  /* CORB Upper Base Address (32-bit) */
#define HDA_REG_CORBWP          0x48  /* CORB Write Pointer (16-bit) */
#define HDA_REG_CORBRP          0x4A  /* CORB Read Pointer (16-bit) - reset with 0x8000 */
#define HDA_REG_CORBCTL         0x4C  /* CORB Control (8-bit) */
#define HDA_REG_CORBSTS         0x4D  /* CORB Status (8-bit) */
#define HDA_REG_CORBSIZE        0x4E  /* CORB Size (8-bit) */
#define HDA_REG_RIRBLBASE       0x50  /* RIRB Lower Base Address (32-bit) */
#define HDA_REG_RIRBUBASE       0x54  /* RIRB Upper Base Address (32-bit) */
#define HDA_REG_RIRBWP          0x58  /* RIRB Write Pointer (16-bit) - reset with 0x8000 */
#define HDA_REG_RIRBCNT         0x5C  /* RIRB Response Interrupt Count (16-bit) */
#define HDA_REG_RIRBCTL         0x5A  /* RIRB Control (8-bit) */
#define HDA_REG_RIRBSTS         0x5D  /* RIRB Status (8-bit) */
#define HDA_REG_RIRBSIZE        0x5E  /* RIRB Size (8-bit) */
#define HDA_REG_ICOI            0x60  /* Immediate Command Output Interface */
#define HDA_REG_ICII            0x64  /* Immediate Command Input Interface */
#define HDA_REG_ICIS            0x68  /* Immediate Command Status */
#define HDA_REG_DPIBLBASE       0x70  /* DMA Position Buffer Lower Base */
#define HDA_REG_DPIBUBASE       0x74  /* DMA Position Buffer Upper Base */

/* GCTL bits */
#define HDA_GCTL_CRST           (1 << 0)   /* Controller Reset */
#define HDA_GCTL_FCNTRL         (1 << 1)   /* Flush Control */
#define HDA_GCTL_UNSOL          (1 << 8)   /* Unsolicited Response Enable */

/* INTCTL bits */
#define HDA_INTCTL_GIE          (1U << 31)  /* Global Interrupt Enable */
#define HDA_INTCTL_CIE          (1 << 30)   /* Controller Interrupt Enable */
#define HDA_INTCTL_SIE_MASK     0x3FFFFFFF  /* Stream Interrupt Enable mask */

/* INTSTS bits */
#define HDA_INTSTS_GIS          (1U << 31)  /* Global Interrupt Status */
#define HDA_INTSTS_CIS          (1 << 30)   /* Controller Interrupt Status */
#define HDA_INTSTS_SIS_MASK     0x3FFFFFFF  /* Stream Interrupt Status mask */

/* STATESTS bits - one bit per codec */
#define HDA_STATESTS_SDI_MASK   0x7FFF

/* GCAP fields */
#define HDA_GCAP_ISS_SHIFT      8
#define HDA_GCAP_OSS_SHIFT      12
#define HDA_GCAP_BSS_SHIFT      3
#define HDA_GCAP_64OK           (1 << 0)  /* 64-bit address support */

/* CORB control bits */
#define HDA_CORBCTL_CMEIE       (1 << 0)  /* CORB Memory Error Interrupt Enable */
#define HDA_CORBCTL_RUN         (1 << 1)  /* CORB Run */

/* CORB status bits */
#define HDA_CORBSTS_CMEI        (1 << 0)  /* CORB Memory Error Interrupt */

/* CORB size encoding */
#define HDA_CORBSIZE_2ENTRIES   0x00  /* 2 entries */
#define HDA_CORBSIZE_16ENTRIES  0x01  /* 16 entries */
#define HDA_CORBSIZE_256ENTRIES 0x02  /* 256 entries */

/* RIRB control bits */
#define HDA_RIRBCTL_RINTFL      (1 << 0)  /* Response Interrupt Flag Clear */
#define HDA_RIRBCTL_RUN         (1 << 1)  /* RIRB Run */
#define HDA_RIRBCTL_ROIEC       (1 << 2)  /* Response Overrun Interrupt Enable */

/* RIRB status bits */
#define HDA_RIRBSTS_RINTFL      (1 << 0)  /* Response Interrupt */
#define HDA_RIRBSTS_ROIS        (1 << 2)  /* Response Overrun Interrupt Status */

/* RIRB size encoding */
#define HDA_RIRBSIZE_2ENTRIES   0x00
#define HDA_RIRBSIZE_16ENTRIES  0x01
#define HDA_RIRBSIZE_256ENTRIES 0x02

/* CORBRP reset value */
#define HDA_CORBRP_RST          (1 << 15)  /* Reset CORB Read Pointer */

/* RIRBWP reset value */
#define HDA_RIRBWP_RST          (1 << 15)  /* Reset RIRB Write Pointer */

/* --- Stream Descriptor Register Map --- */
/* Each stream descriptor occupies 0x20 bytes starting at HDA_SD_BASE */
#define HDA_SD_BASE             0x80
#define HDA_SD_SIZE             0x20

/* Stream Descriptor register offsets */
#define HDA_SD_REG_CTL          0x00  /* Control (24 bits, byte0-2) */
#define HDA_SD_REG_STS          0x03  /* Status (8-bit, byte3) */
#define HDA_SD_REG_LPIB         0x04  /* Link Position in Buffer (32-bit) */
#define HDA_SD_REG_CBL          0x08  /* Cyclic Buffer Length (32-bit) */
#define HDA_SD_REG_LVI          0x0C  /* Last Valid Index (16-bit) */
#define HDA_SD_REG_FIFOW        0x0E  /* FIFO Watermark (16-bit) */
#define HDA_SD_REG_FIFOS        0x10  /* FIFO Size (16-bit) */
#define HDA_SD_REG_FMT          0x12  /* Stream Format (16-bit) */
#define HDA_SD_REG_BDPL         0x18  /* BDL Pointer Lower (32-bit) */
#define HDA_SD_REG_BDPU         0x1C  /* BDL Pointer Upper (32-bit) */

/* Stream Control bits (bits 0-23) */
#define HDA_SD_CTL_SRST         (1 << 0)    /* Stream Reset */
#define HDA_SD_CTL_RUN          (1 << 1)    /* Run */
#define HDA_SD_CTL_IOCE         (1 << 2)    /* Interrupt on Completion Enable */
#define HDA_SD_CTL_FEIE         (1 << 3)    /* FIFO Error Interrupt Enable */
#define HDA_SD_CTL_DEIE         (1 << 4)    /* Descriptor Error Interrupt Enable */
#define HDA_SD_CTL_STRIPE_SHIFT 5           /* Stripe control shift */
#define HDA_SD_CTL_STRIPE_MASK  0x000000E0
#define HDA_SD_CTL_TP           (1 << 8)    /* Traffic Priority */
#define HDA_SD_CTL_DIR          (1 << 9)    /* Direction: 0=output, 1=input */
#define HDA_SD_CTL_STRM_SHIFT   12           /* Stream tag shift */
#define HDA_SD_CTL_STRM_MASK    0x0000F000

/* Stream Status bits */
#define HDA_SD_STS_BCIS         (1 << 2)    /* Buffer Completion Interrupt Status */
#define HDA_SD_STS_FIFOE        (1 << 3)    /* FIFO Error */
#define HDA_SD_STS_DESE         (1 << 4)    /* Descriptor Error */
#define HDA_SD_STS_FIFORDY      (1 << 5)    /* FIFO Ready */

/* --- Stream Format encoding --- */
/* Bits per sample field */
#define HDA_FMT_BITS_8          0x0000  /* 8 bit */
#define HDA_FMT_BITS_16         0x4000  /* 16 bit */
#define HDA_FMT_BITS_20         0x8000  /* 20 bit */
#define HDA_FMT_BITS_24         0xC000  /* 24 bit */
#define HDA_FMT_BITS_32         0xE000  /* 32 bit */

/* Number of channels field (bits 3:0) */
#define HDA_FMT_CHAN_MASK       0x000F  /* Actual channels = value + 1 */

/* Sample rate divider/multiplier field (bits 10:8) */
#define HDA_FMT_RATE_48KHZ      0x0000  /* 48 kHz */
#define HDA_FMT_RATE_44KHZ      0x0800  /* 44.1 kHz */
#define HDA_FMT_RATE_MULT_2     0x0400  /* 2x multiplier */
#define HDA_FMT_RATE_MULT_4     0x0300  /* 4x multiplier */
#define HDA_FMT_RATE_DIV_2      0x0100  /* Divide by 2 (24 kHz) */
#define HDA_FMT_RATE_DIV_3      0x0200  /* Divide by 3 (16 kHz) */
#define HDA_FMT_RATE_DIV_4      0x0500  /* Divide by 4 (12 kHz) */
#define HDA_FMT_RATE_DIV_6      0x0600  /* Divide by 6 (8 kHz) */
#define HDA_FMT_RATE_32KHZ      0x0F00  /* 32 kHz */

/* Combined sample rate constants */
#define HDA_RATE_8000           8000
#define HDA_RATE_11025          11025
#define HDA_RATE_16000          16000
#define HDA_RATE_22050          22050
#define HDA_RATE_32000          32000
#define HDA_RATE_44100          44100
#define HDA_RATE_48000          48000
#define HDA_RATE_88200          88200
#define HDA_RATE_96000          96000
#define HDA_RATE_176400         176400
#define HDA_RATE_192000         192000

/* --- BDL Entry --- */
typedef struct {
    uint32_t addr_low;   /* Buffer address low 32 bits */
    uint32_t addr_high;  /* Buffer address high 32 bits */
    uint32_t length;     /* Buffer length in bytes (bits 31:1 for IOC) */
    uint32_t flags;      /* bit 0 = IOC (Interrupt on Completion) */
} hda_bdl_entry_t;

#define HDA_BDL_IOC             (1U << 0)   /* Interrupt on Completion */

/* --- HDA Verb Commands --- */
/* Command format: bits 31:28 = codec address, bits 27:20 = node ID, bits 19:0 = verb + payload */

/* Get Parameter verb (0xF00) - sub-parameters: */
#define HDA_VERB_GET_PARAM              0xF00
#define HDA_VERB_GET_CONN_LIST_ENTRY    0xF02  /* Get connection list entry */

/* Set/Get verbs */
#define HDA_VERB_SET_CONV_FMT           0x200  /* Set converter format */
#define HDA_VERB_GET_CONV_FMT           0xA00  /* Get converter format */
#define HDA_VERB_SET_AMP_GAIN_MUTE      0x300  /* Set amplifier gain/mute */
#define HDA_VERB_GET_AMP_GAIN_MUTE      0xB00  /* Get amplifier gain/mute */
#define HDA_VERB_SET_PIN_WIDGET_CTL     0x707  /* Set pin widget control */
#define HDA_VERB_GET_PIN_WIDGET_CTL     0xF07  /* Get pin widget control */
#define HDA_VERB_SET_UNSOL_ENABLE       0x708  /* Set unsolicited response enable */
#define HDA_VERB_GET_UNSOL_ENABLE       0xF08  /* Get unsolicited response enable */
#define HDA_VERB_SET_POWER_STATE        0x705  /* Set power state */
#define HDA_VERB_GET_POWER_STATE        0xF05  /* Get power state */
#define HDA_VERB_SET_CONN_SELECT        0x701  /* Set connection select */
#define HDA_VERB_GET_CONN_SELECT        0xF01  /* Get connection select */
#define HDA_VERB_SET_EAPD_BTL_ENABLE    0x70C  /* Set EAPD/BTL enable */
#define HDA_VERB_GET_EAPD_BTL_ENABLE    0xF0C  /* Get EAPD/BTL enable */
#define HDA_VERB_SET_DIGITAL_CONV_CTL1  0x70D  /* Set digital converter control 1 */
#define HDA_VERB_SET_DIGITAL_CONV_CTL2  0x70E  /* Set digital converter control 2 */
#define HDA_VERB_SET_WIDGET_CTL         0x700  /* Set widget control */
#define HDA_VERB_SET_CODEC_RESET        0x7FF  /* Function reset */

/* Verb for setting stream on converter */
#define HDA_VERB_SET_STREAMCHAN         0x706  /* Set stream and channel */

/* Execution verbs */
#define HDA_VERB_EXEC_PIN_SENSE         0xF09  /* Execute pin sense */
#define HDA_VERB_EXEC_VOLUME_KNOB       0xF0F  /* Execute volume knob */

/* Verb Payload: Amplifier Gain/Mute
 * Set AMP Gain/Mute (0x300):
 *   Bits 15:0 = payload
 *     Bit 15 = Set Output (0) or Input (1) amplifier
 *     Bit 14 = Set Left amplifier (1=enable)
 *     Bit 13 = Set Right amplifier (1=enable)
 *     Bit 12 = Mute (1=mute)
 *     Bits 6:0 = Gain (step count, 0.25dB per step)
 * Get AMP Gain/Mute (0xB00):
 *   Bits 15:0 = same payload to select which amp to get
 *   Response bits 6:0 = current gain
 *   Response bit 7 = mute state
 */
#define HDA_AMP_OUTPUT          0x0000
#define HDA_AMP_INPUT           0x8000
#define HDA_AMP_LEFT_CHAN       0x4000
#define HDA_AMP_RIGHT_CHAN      0x2000
#define HDA_AMP_MUTE            0x1000
#define HDA_AMP_GAIN_MASK       0x007F
#define HDA_AMP_GAIN_OFFSET     0
#define HDA_AMP_GET_LEFT        0x4000
#define HDA_AMP_GET_RIGHT       0x2000
#define HDA_AMP_GET_MUTE        0x0080

/* Power State bits */
#define HDA_PWR_STATE_D0        0x00
#define HDA_PWR_STATE_D1        0x01
#define HDA_PWR_STATE_D2        0x02
#define HDA_PWR_STATE_D3        0x03

/* Pin Widget Control bits */
#define HDA_PIN_CTL_HPHN_ENABLE (1 << 7)  /* Headphone amp enable */
#define HDA_PIN_CTL_OUT_ENABLE  (1 << 6)  /* Output enable */
#define HDA_PIN_CTL_IN_ENABLE   (1 << 5)  /* Input enable */
#define HDA_PIN_CTL_VREF_ENABLE(x) ((x) << 0)  /* VRef enable (0-7) */

/* Pin Sense response bits */
#define HDA_PIN_SENSE_PRESENCE  (1U << 31)  /* Presence detect */

/* EAPD/BTL bits */
#define HDA_EAPD_BTL_ENABLE     (1 << 1)  /* EAPD enable */
#define HDA_EAPD_BTL_LR_SWAP    (1 << 0)  /* L/R swap */

/* Unsolicited response enable bits */
#define HDA_UNSOL_ENABLE        (1 << 7)  /* Enable unsolicited responses */

/* --- Get Parameter IDs --- */
#define HDA_PARAM_VENDOR_ID             0x00  /* Vendor/Device ID */
#define HDA_PARAM_REVISION_ID           0x02  /* Revision ID */
#define HDA_PARAM_SUBORDINATE_NODE_COUNT 0x04 /* Subordinate Node Count */
#define HDA_PARAM_FUNCTION_GROUP_TYPE   0x05  /* Function Group Type */
#define HDA_PARAM_AUDIO_FG_CAP          0x08  /* Audio Function Group Capabilities */
#define HDA_PARAM_AUDIO_WIDGET_CAP      0x09  /* Audio Widget Capabilities */
#define HDA_PARAM_PCM_SUPPORT           0x0A  /* Supported PCM Sizes/Rates */
#define HDA_PARAM_STREAM_SUPPORT        0x0B  /* Supported Stream Formats */
#define HDA_PARAM_PIN_CAP               0x0C  /* Pin Capabilities */
#define HDA_PARAM_INPUT_AMP_CAP         0x0D  /* Input Amplifier Capabilities */
#define HDA_PARAM_CONN_LIST_LENGTH      0x0E  /* Connection List Length */
#define HDA_PARAM_OUTPUT_AMP_CAP        0x12  /* Output Amplifier Capabilities */
#define HDA_PARAM_POWER_STATE           0x0F  /* Supported Power States */
#define HDA_PARAM_PROCESSING_CAP        0x10  /* Processing Capabilities */
#define HDA_PARAM_GPIO_COUNT            0x11  /* GPIO Count */
#define HDA_PARAM_VOLUME_KNOB_CAP       0x13  /* Volume Knob Capabilities */

/* --- Widget Capabilities Flags --- */
#define HDA_WIDGET_CAP_TYPE_MASK        0x00F00000
#define HDA_WIDGET_CAP_TYPE_SHIFT       20
#define HDA_WIDGET_CAP_DELAY_MASK       0x000F0000
#define HDA_WIDGET_CAP_DELAY_SHIFT      16
#define HDA_WIDGET_CAP_CHAN_COUNT_MASK  0x0000FF00
#define HDA_WIDGET_CAP_CHAN_COUNT_SHIFT 8
#define HDA_WIDGET_CAP_CP               (1 << 1)    /* Connection List Present */
#define HDA_WIDGET_CAP_PWR_CTRL         (1 << 4)    /* Power Control */
#define HDA_WIDGET_CAP_DIGITAL          (1 << 6)    /* Digital */
#define HDA_WIDGET_CAP_UNSOL_CAP        (1 << 7)    /* Unsolicited Response Capable */
#define HDA_WIDGET_CAP_PROC_WIDGET      (1 << 8)    /* Processing Widget */
#define HDA_WIDGET_CAP_STRIPE           (1 << 9)    /* Stripe */
#define HDA_WIDGET_CAP_FORMAT_OVERRIDE  (1 << 10)   /* Format Override */
#define HDA_WIDGET_CAP_AMP_OVERRIDE     (1 << 11)   /* Amp Param Override */
#define HDA_WIDGET_CAP_OUT_AMP_PRESENT  (1 << 12)   /* Output Amplifier Present */
#define HDA_WIDGET_CAP_IN_AMP_PRESENT   (1 << 13)   /* Input Amplifier Present */
#define HDA_WIDGET_CAP_LR_SWAP          (1 << 14)   /* Left/Right Swap */

/* --- Widget Types --- */
#define HDA_WTYPE_AUDIO_OUT     0x0  /* Audio Output (DAC) */
#define HDA_WTYPE_AUDIO_IN      0x1  /* Audio Input (ADC) */
#define HDA_WTYPE_AUDIO_MIXER   0x2  /* Audio Mixer */
#define HDA_WTYPE_AUDIO_SELECTOR 0x3 /* Audio Selector */
#define HDA_WTYPE_PIN_COMPLEX   0x4  /* Pin Complex */
#define HDA_WTYPE_POWER         0x5  /* Power Widget */
#define HDA_WTYPE_VOLUME_KNOB   0x6  /* Volume Knob Widget */
#define HDA_WTYPE_BEEP_GEN      0x7  /* Beep Generator */
#define HDA_WTYPE_VENDOR_DEF    0xF  /* Vendor Defined */

/* --- Pin Capabilities Flags --- */
#define HDA_PIN_CAP_IMPEDANCE    (1 << 0)    /* Impedance Sense Capable */
#define HDA_PIN_CAP_TRIGGER      (1 << 1)    /* Trigger Required */
#define HDA_PIN_CAP_PRESENCE     (1 << 2)    /* Presence Detect Capable */
#define HDA_PIN_CAP_HP_DRIVE     (1 << 3)    /* Headphone Drive Capable */
#define HDA_PIN_CAP_OUTPUT       (1 << 4)    /* Output Capable */
#define HDA_PIN_CAP_INPUT        (1 << 5)    /* Input Capable */
#define HDA_PIN_CAP_BALANCED     (1 << 6)    /* Balanced I/O */
#define HDA_PIN_CAP_HDMI         (1 << 7)    /* HDMI */
#define HDA_PIN_CAP_VREF_CTRL    (1 << 8)    /* VRef Control */
#define HDA_PIN_CAP_EAPD         (1 << 16)   /* EAPD Capable */
#define HDA_PIN_CAP_DP           (1 << 24)   /* DisplayPort */
#define HDA_PIN_CAP_HBR          (1 << 27)   /* High Bit Rate */

/* --- Pin Configuration Default Register (0x71C) --- */
#define HDA_CONFIG_DEFAULT_PORT_CONN_MASK   0x30000000
#define HDA_CONFIG_DEFAULT_LOCATION_MASK    0x3F000000
#define HDA_CONFIG_DEFAULT_DEVICE_MASK      0x00F00000
#define HDA_CONFIG_DEFAULT_CONN_TYPE_MASK   0x000F0000
#define HDA_CONFIG_DEFAULT_COLOR_MASK       0x0000F000
#define HDA_CONFIG_DEFAULT_MISC_MASK        0x00000F00
#define HDA_CONFIG_DEFAULT_ASSOCIATION_MASK 0x000000F0
#define HDA_CONFIG_DEFAULT_SEQUENCE_MASK    0x0000000F

/* Port Connectivity */
#define HDA_PORT_CONN_JACK          0x00000000  /* Jack */
#define HDA_PORT_CONN_NONE          0x10000000  /* No physical connection */
#define HDA_PORT_CONN_FIXED         0x20000000  /* Fixed function device */
#define HDA_PORT_CONN_BOTH          0x30000000  /* Both jack and internal */

/* Location (gross) */
#define HDA_LOC_REAR                0x02000000  /* Rear panel */
#define HDA_LOC_FRONT               0x04000000  /* Front panel */
#define HDA_LOC_LEFT                0x08000000  /* Left side */
#define HDA_LOC_RIGHT               0x0A000000  /* Right side */
#define HDA_LOC_TOP                 0x0C000000  /* Top */
#define HDA_LOC_BOTTOM              0x0E000000  /* Bottom */

/* Default Device */
#define HDA_DEV_LINE_OUT            0x00000000  /* Line Out */
#define HDA_DEV_SPEAKER             0x00100000  /* Speaker */
#define HDA_DEV_HP_OUT              0x00200000  /* Headphone Out */
#define HDA_DEV_CD                  0x00300000  /* CD */
#define HDA_DEV_SPDIF_OUT           0x00400000  /* S/PDIF Out */
#define HDA_DEV_DIGITAL_OTHER_OUT   0x00500000  /* Digital Other Out */
#define HDA_DEV_MODEM_LINE          0x00600000  /* Modem Line Side */
#define HDA_DEV_MODEM_HANDSET       0x00700000  /* Modem Handset Side */
#define HDA_DEV_LINE_IN             0x00800000  /* Line In */
#define HDA_DEV_AUX                 0x00900000  /* AUX */
#define HDA_DEV_MIC_IN              0x00A00000  /* Mic In */
#define HDA_DEV_TELEPHONY           0x00B00000  /* Telephony */
#define HDA_DEV_SPDIF_IN            0x00C00000  /* S/PDIF In */
#define HDA_DEV_DIGITAL_OTHER_IN    0x00D00000  /* Digital Other In */
#define HDA_DEV_HDMI_OUT            0x00E00000  /* HDMI/DisplayPort Out */

/* --- Audio Function Group Capabilities --- */
#define HDA_AFG_CAP_OUTPUT_DELAY_SHIFT    0
#define HDA_AFG_CAP_INPUT_DELAY_SHIFT     4

/* --- Power State Supported flags --- */
#define HDA_PWR_SUPPORT_D0          (1 << 0)
#define HDA_PWR_SUPPORT_D1          (1 << 1)
#define HDA_PWR_SUPPORT_D2          (1 << 2)
#define HDA_PWR_SUPPORT_D3          (1 << 3)
#define HDA_PWR_SUPPORT_D3COLD      (1 << 4)
#define HDA_PWR_SUPPORT_S3D3COLD    (1 << 7)
#define HDA_PWR_SUPPORT_CLKSTOP     (1 << 8)
#define HDA_PWR_SUPPORT_EPSS        (1 << 30)

/* --- Amplifier Capabilities --- */
#define HDA_AMP_CAP_OFFSET_SHIFT    0
#define HDA_AMP_CAP_OFFSET_MASK     0x0000007F
#define HDA_AMP_CAP_NUM_STEPS_SHIFT 7
#define HDA_AMP_CAP_NUM_STEPS_MASK  0x00007F00
#define HDA_AMP_CAP_STEP_SIZE_SHIFT 14
#define HDA_AMP_CAP_STEP_SIZE_MASK  0x000FC000
#define HDA_AMP_CAP_MUTE_CAP        (1 << 18)  /* Mute Capable */

/* --- PCM Support Flags --- */
#define HDA_PCM_BITS_8              (1 << 16)
#define HDA_PCM_BITS_16             (1 << 17)
#define HDA_PCM_BITS_20             (1 << 18)
#define HDA_PCM_BITS_24             (1 << 19)
#define HDA_PCM_BITS_32             (1 << 20)
#define HDA_PCM_RATE_8KHZ           (1 << 0)
#define HDA_PCM_RATE_11KHZ          (1 << 1)
#define HDA_PCM_RATE_16KHZ          (1 << 2)
#define HDA_PCM_RATE_22KHZ          (1 << 3)
#define HDA_PCM_RATE_32KHZ          (1 << 4)
#define HDA_PCM_RATE_44KHZ          (1 << 5)
#define HDA_PCM_RATE_48KHZ          (1 << 6)
#define HDA_PCM_RATE_88KHZ          (1 << 7)
#define HDA_PCM_RATE_96KHZ          (1 << 8)
#define HDA_PCM_RATE_176KHZ         (1 << 9)
#define HDA_PCM_RATE_192KHZ         (1 << 10)
#define HDA_PCM_RATE_384KHZ         (1 << 11)  /* Actually 384 = 0 */

/* --- Codec Node Info --- */
typedef struct hda_widget {
    uint32_t nid;               /* Node ID */
    uint8_t  type;              /* Widget type (HDA_WTYPE_*) */
    uint32_t capabilities;      /* Widget capabilities */
    uint32_t pcm_support;       /* PCM format support */
    uint32_t pin_cap;           /* Pin capabilities (for pin widgets) */
    uint32_t pin_config;        /* Default pin configuration */
    uint32_t out_amp_cap;       /* Output amplifier capabilities */
    uint32_t in_amp_cap;        /* Input amplifier capabilities */
    uint32_t pwr_support;       /* Supported power states */
    uint32_t conn_list_len;     /* Connection list length */
    uint16_t conn_list[HDA_MAX_CONNECTIONS];  /* Connection list */
    uint8_t  conn_select;       /* Current connection select */
} hda_widget_t;

typedef struct hda_codec {
    uint32_t   addr;            /* Codec address (0-14) */
    uint32_t   vendor_id;       /* Vendor ID (VID:DID) */
    uint32_t   revision_id;
    uint32_t   afg_nid;         /* Audio Function Group node ID */
    uint32_t   afg_start;       /* First widget node ID */
    uint32_t   afg_count;       /* Number of widget nodes */
    uint32_t   widget_count;    /* Actual widgets allocated */
    hda_widget_t *widgets;      /* Array of widget descriptors */
    uint32_t   output_dac_nid;  /* Output DAC node */
    uint32_t   output_pin_nid;  /* Output pin node */
    uint32_t   input_adc_nid;   /* Input ADC node */
    uint32_t   input_pin_nid;   /* Input pin node */
    uint32_t   mixer_nid;       /* Mixer node */
    uint32_t   volume_knob_nid; /* Volume knob node */
    uint32_t   out_amp_vol;     /* Current output amp volume (0-127) */
    uint8_t    out_amp_muted;   /* Output amp muted */
    uint32_t   cur_sample_rate;
    uint16_t   cur_channels;
    uint16_t   cur_bits;
} hda_codec_t;

typedef struct hda_stream {
    uint8_t   stream_num;       /* Stream descriptor number */
    uint8_t   stream_tag;       /* Stream tag (1-15) */
    uint8_t   direction;        /* 0=output, 1=input */
    uint8_t   active;           /* Stream is running */
    uint16_t  fmt;              /* Format register value */
    uint32_t  cbl;              /* Cyclic Buffer Length */
    uint8_t   bdl_count;        /* Number of BDL entries */
    hda_bdl_entry_t *bdl;       /* Virtual address of BDL */
    uint32_t  bdl_phys;         /* Physical address of BDL */
    uint32_t  cpu_buffer_size;  /* Total buffer size */
} hda_stream_t;

typedef struct hda_controller {
    /* PCI config */
    uint32_t pci_bus;
    uint32_t pci_dev;
    uint32_t pci_func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  irq;

    /* MMIO base (mapped virtual address) */
    volatile uint8_t *reg_base;

    /* Controller capabilities */
    uint8_t  num_input_streams;
    uint8_t  num_output_streams;
    uint8_t  num_bidir_streams;
    uint8_t  supports_64bit;

    /* CORB/RIRB */
    volatile uint32_t *corb;
    uint32_t  corb_entries;
    uint32_t  corb_phys;
    volatile uint64_t *rirb;
    uint32_t  rirb_entries;
    uint32_t  rirb_phys;

    /* Codecs */
    hda_codec_t codecs[HDA_MAX_CODECS];
    uint32_t  num_codecs;
    uint32_t  states_ts;

    /* Streams */
    hda_stream_t streams[HDA_MAX_STREAMS];
    uint8_t  stream_alloc_mask;     /* Bitmask of allocated stream tags */

    /* DMA Position Buffer */
    volatile uint32_t *dma_pos_buf;
    uint32_t  dma_pos_buf_phys;

    /* State */
    uint32_t initialized;
} hda_controller_t;

/* --- Public API --- */
void hdaudio_init(void);
int  hdaudio_play(const void *data, uint32_t size, uint32_t rate, uint16_t channels);
int  hdaudio_record(void *data, uint32_t size, uint32_t rate, uint16_t channels);
void hdaudio_stop(void);
void hdaudio_stop_stream(uint8_t stream_num);
int  hdaudio_set_volume(uint8_t left, uint8_t right);
int  hdaudio_get_volume(uint8_t *left, uint8_t *right);
int  hdaudio_set_mute(uint8_t mute);
int  hdaudio_get_mute(void);
int  hdaudio_set_format(uint16_t bits, uint16_t channels);
int  hdaudio_set_sample_rate(uint32_t rate);
int  hdaudio_set_power_state(uint8_t state);
uint32_t hdaudio_get_buffer_position(void);
hda_controller_t *hdaudio_get_controller(void);
uint8_t hdaudio_is_available(void);
void hdaudio_send_command(uint8_t codec, uint32_t cmd);
int  hdaudio_read_response(uint8_t codec, uint32_t *response);

/* Internal - IRQ handler */
void hdaudio_irq_handler(void *regs);

#endif /* HDAUDIO_H */