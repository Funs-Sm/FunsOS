#ifndef HDAUDIO_H
#define HDAUDIO_H

#include "stdint.h"

/* Intel High Definition Audio (HDA) controller driver.
 *
 * HDA is the standard PCI audio interface for almost all systems
 * produced after ~2005. It uses a memory-mapped register ring to
 * communicate with the controller and a stream descriptor DMA model
 * to transfer audio buffers.
 *
 * This driver implements the minimum subset needed to play a single
 * PCM stream via output converter 0 on the codec. Multi-stream,
 * 3D-audio paths, and jack detection are out of scope.
 */

#define HDAUDIO_VENDOR_INTEL   0x8086

/* PCI class code: 0x04 (multimedia), subclass 0x03 (HDA), prog-if 0x00. */
#define HDAUDIO_PCI_CLASS      0x04
#define HDAUDIO_PCI_SUBCLASS   0x03

/* HDA controller registers (offsets from PCI BAR0, 32-bit memory). */
#define HDA_REG_GCAP          0x00    /* Global Capabilities */
#define HDA_REG_VMIN          0x02
#define HDA_REG_VMAJ          0x03
#define HDA_REG_GCTL          0x08    /* Global Control */
#define HDA_REG_WAKEEN        0x0C
#define HDA_REG_STATESTS      0x0E
#define HDA_REG_INTCTL        0x20
#define HDA_REG_INTSTS        0x24
#define HDA_REG_CORBLBASE     0x40
#define HDA_REG_CORBUBASE     0x44
#define HDA_REG_CORBWP        0x48
#define HDA_REG_CORBRP        0x4A
#define HDA_REG_CORBCTL       0x4C
#define HDA_REG_CORBSIZE      0x4E
#define HDA_REG_RIRBLBASE     0x50
#define HDA_REG_RIRBUBASE     0x54
#define HDA_REG_RIRBWP        0x58
#define HDA_REG_RINTCNT       0x5A
#define HDA_REG_RIRBCTL       0x5C
#define HDA_REG_RIRBSIZE      0x5E

/* Streams (offset = 0x80 + stream_idx * 0x20) */
#define HDA_REG_SD_BASE       0x80
#define HDA_REG_SD_STRIDE     0x20

#define HDA_SD_REG_CTL        0x00
#define HDA_SD_REG_STS        0x03
#define HDA_SD_REG_LPIB       0x04
#define HDA_SD_REG_CBL        0x08
#define HDA_SD_REG_LVI        0x0C
#define HDA_SD_REG_FMT        0x10
#define HDA_SD_REG_BDLPL      0x18
#define HDA_SD_REG_BDLPU      0x1C

/* SD CTL bits */
#define HDA_SD_CTL_SRST       (1 << 0)
#define HDA_SD_CTL_RUN        (1 << 1)
#define HDA_SD_CTL_INTEN      (1 << 2)
#define HDA_SD_CTL_FMT_MASK   0xF
#define HDA_SD_CTL_STRM_MASK  (0xF << 20)

/* GCTL bits */
#define HDA_GCTL_CRST         (1 << 0)

/* CORB/RIRB commands */
#define HDA_CMD_GET_PARAMETER 0xF00
#define HDA_PARAM_VENDOR_ID   0x00
#define HDA_PARAM_REV_ID      0x02
#define HDA_PARAM_NID_ROOT    0x05
#define HDA_PARAM_NODE_COUNT  0x04

#define HDA_CMD_GET_CONN_LIST 0xF02
#define HDA_CMD_SET_CONV_FMT  0x2
#define HDA_CMD_SET_STREAM_CH 0x706
#define HDA_CMD_GET_CONV_FMT  0xA

/* Codec verbs (command field construction). */
#define HDA_VERB_GET_PARAMETER(param)   ((HDA_CMD_GET_PARAMETER << 8) | (param))
#define HDA_VERB_GET_CONV_FMT(nid)      ((HDA_CMD_GET_CONV_FMT << 8) | (nid))

/* Stream descriptor (one per active stream). Must be 8-byte aligned. */
typedef struct __attribute__((packed)) {
    uint32_t addr;
    uint32_t len;
    uint32_t flags;
} hda_bdl_entry_t;

#define HDA_BDL_MAX_ENTRIES  32
#define HDA_SAMPLE_RATE_DEFAULT 48000
#define HDA_FMT_48K_16B_STEREO 0x0011   /* 48 kHz, 16-bit, 2 ch */

/* Public API */
int hdaudio_init(uint8_t bus, uint8_t dev, uint8_t func);
int hdaudio_play(const int16_t *samples, uint32_t frames,
                 uint32_t sample_rate, uint8_t channels);
void hdaudio_stop(void);
int hdaudio_is_playing(void);

#endif