#ifndef CS4281_H
#define CS4281_H

#include "stdint.h"

/* Cirrus Logic CS4281 / CrystalClear SoundFusion PCI audio driver.
 *
 * The CS4281 is a 4-speaker AC'97-compatible controller that supports
 * parallel digital output via its DMA engines. The driver programs the
 * host DMA for two PCM streams (front and rear) and configures the
 * AC'97 codec via the CS4281's codec interface.
 *
 * Capabilities:
 *   - 16-bit stereo playback up to 48 kHz
 *   - Independent volume / mute per stream
 *   - Hardware sample rate conversion (no SRC - source must match rate)
 */

#define CS4281_VENDOR_ID    0x1013
#define CS4281_DEVICE_ID    0x6001

/* PCI config registers */
#define CS4281_PCI_CMD      0x04
#define CS4281_PCI_BAR0     0x10   /* Native registers */
#define CS4281_PCI_BAR1     0x14   /* AC97 I/O */

/* Native (BA0) registers (offsets from BAR0) */
#define CS4281_REG_CFG      0x0000
#define CS4281_REG_PPLLC    0x0004
#define CS4281_REG_PPLLCC   0x0008
#define CS4281_REG_FMLO     0x0010
#define CS4281_REG_FMHI     0x0014
#define CS4281_REG_IAC      0x0018
#define CS4281_REG_IAS      0x001C
#define CS4281_REG_HIMR     0x0020
#define CS4281_REG_HISR     0x0024
#define CS4281_REG_HICR     0x0028
#define CS4281_REG_DMACC    0x0030

/* Playback DMA channel 0 registers (offsets from BA0) */
#define CS4281_D0CC         0x0100  /* Channel control */
#define CS4281_D0CL         0x0104  /* Count low */
#define CS4281_D0CA         0x0108  /* Address (physical, 32-bit) */
#define CS4281_D0CS         0x010C  /* Count high / status */
#define CS4281_D0CV         0x0110  /* Current value */
#define CS4281_D0RES        0x0114

/* Playback DMA channel 1 (rear speakers) */
#define CS4281_D1CC         0x0118
#define CS4281_D1CL         0x011C
#define CS4281_D1CA         0x0120
#define CS4281_D1CS         0x0124
#define CS4281_D1CV         0x0128

/* AC'97 registers (offsets from BAR1) */
#define CS4281_AC97_RESET   0x00
#define CS4281_AC97_CTRL    0x02
#define CS4281_AC97_INDEX   0x04
#define CS4281_AC97_DATA    0x06

/* Channel control bits (D0CC/D1CC) */
#define CS4281_DMA_ENABLE   (1 << 0)
#define CS4281_DMA_RESET    (1 << 1)
#define CS4281_DMA_PAUSE    (1 << 2)
#define CS4281_DMA_INT      (1 << 3)
#define CS4281_DMA_FMT_16   (1 << 4)
#define CS4281_DMA_STEREO   (1 << 5)

/* Configuration register bits */
#define CS4281_CFG_SLOW     (1 << 0)
#define CS4281_CFG_HF1      (1 << 1)
#define CS4281_CFG_HF2      (1 << 2)
#define CS4281_CFG_HF0      (1 << 3)

/* Public API */
int cs4281_init(uint8_t bus, uint8_t dev, uint8_t func);
int cs4281_reset(void);
int cs4281_play(const int16_t *samples, uint32_t frames,
                uint32_t sample_rate, uint8_t channels);
void cs4281_stop(void);

#endif