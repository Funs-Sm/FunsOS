#ifndef I2S_PCM_H
#define I2S_PCM_H

#include "stdint.h"

/* I2S / PCM audio controller driver.
 *
 * Implements a generic I2S/PCM controller found on modern SoCs and
 * some PCI audio cards. Provides:
 *   - 16/24/32-bit sample depth
 *   - Mono / stereo
 *   - 8..192 kHz sample rates via internal MCLK division
 *   - Master / slave mode
 *   - DMA descriptors (BDL-like) for buffer list playback
 *   - Codec I2C control for external DAC/ADC
 */

#define I2S_PCM_REG_VERSION    0x00
#define I2S_PCM_REG_CONTROL    0x04
#define I2S_PCM_REG_FMT        0x08
#define I2S_PCM_REG_DIV        0x0C
#define I2S_PCM_REG_BDL_BASE   0x10
#define I2S_PCM_REG_BDL_COUNT  0x14
#define I2S_PCM_REG_LPIB       0x18
#define I2S_PCM_REG_INT_MASK   0x1C
#define I2S_PCM_REG_INT_STATUS 0x20
#define I2S_PCM_REG_POSITION   0x24

/* Control register bits */
#define I2S_CTRL_RESET         (1 << 0)
#define I2S_CTRL_ENABLE        (1 << 1)
#define I2S_CTRL_MASTER        (1 << 2)
#define I2S_CTRL_LOOPBACK      (1 << 3)
#define I2S_CTRL_TX_START      (1 << 4)
#define I2S_CTRL_RX_START      (1 << 5)
#define I2S_CTRL_FLUSH         (1 << 6)

/* Format register fields (each 4 bits) */
#define I2S_FMT_DATA_LEN_SHIFT 0
#define I2S_FMT_DATA_LEN_MASK  0xF
#define I2S_FMT_SLOT_LEN_SHIFT 4
#define I2S_FMT_SLOT_LEN_MASK  0xF
#define I2S_FMT_CHANNELS_SHIFT 8
#define I2S_FMT_CHANNELS_MASK  0xF
#define I2S_FMT_LRCK_POL       (1 << 12)
#define I2S_FMT_BCLK_POL       (1 << 13)

#define I2S_FMT_PROTO_I2S      0
#define I2S_FMT_PROTO_LSB      1
#define I2S_FMT_PROTO_MSB      2

/* Sample rate encoding into divisor register (BCLK = MCLK / (bclk_div)) */
#define I2S_PCM_DEFAULT_MCLK_HZ 24576000

#define I2S_PCM_MAX_BDL_ENTRIES   32
#define I2S_PCM_SAMPLE_MAX_FRAMES (16 * 1024 * 1024)

/* DMA descriptor (one per contiguous buffer in the BDL). */
typedef struct __attribute__((packed)) {
    uint32_t addr;
    uint32_t length;
    uint32_t flags;     /* bit0 = IOC, bit1 = last */
    uint32_t reserved;
} i2s_pcm_bdl_entry_t;

/* Public codec descriptor (used by codec drivers to register control). */
typedef struct {
    const char *name;
    uint8_t  i2c_addr;
    uint16_t default_rate;
    uint8_t  channels;
    /* Codec control callbacks. */
    int (*set_format)(uint32_t sample_rate, uint8_t channels,
                      uint8_t bits_per_sample);
    int (*set_power)(int on);
} i2s_pcm_codec_t;

int i2s_pcm_init(volatile uint32_t *regs, uint32_t mclk_hz);
int i2s_pcm_register_codec(const i2s_pcm_codec_t *codec);
int i2s_pcm_play(const int32_t *samples, uint32_t frames,
                 uint32_t sample_rate, uint8_t channels,
                 uint8_t bits_per_sample);
void i2s_pcm_stop(void);
int  i2s_pcm_is_playing(void);

#endif