#ifndef ES1370_H
#define ES1370_H

#include "stdint.h"

/* ESS ES1370 / Ensoniq AudioPCI driver.
 *
 * The ES1370 is a PCI-based AC'97-compatible audio controller found on
 * many late-1990s and early-2000s motherboards (Ensoniq AudioPCI, ESS
 * Maestro-1 derivatives, etc.). It exposes a small register set for
 * codec control plus three DMA channels for PCM playback/capture.
 *
 * This driver supports:
 *   - PCI device probe (vendor 0x1274, device 0x5000)
 *   - AC'97 codec cold/warm reset
 *   - PCM playback via DMA channel 0 (16-bit stereo, 48 kHz native)
 *   - Sample rate conversion via the SRC (Sample Rate Converter)
 *   - Per-channel volume / mute
 */

#define ES1370_VENDOR_ID    0x1274
#define ES1370_DEVICE_ID    0x5000

/* PCI config registers */
#define ES1370_PCI_SUBSYS   0x2C
#define ES1370_PCI_ROM      0x30
#define ES1370_PCI_LATENCY  0x0D
#define ES1370_PCI_CMD      0x04

/* Control / status register block (offsets from BAR0) */
#define ES1370_REG_CONTROL     0x00  /* Control/Status */
#define ES1370_REG_STATUS      0x04  /* Status */
#define ES1370_REG_UART_DATA   0x08
#define ES1370_REG_UART_STATUS 0x09
#define ES1370_REG_TIMING      0x0C
#define ES1370_REG_SERIAL_CTRL 0x10
#define ES1370_REG_CODEC       0x14  /* AC'97 codec write */
#define ES1370_REG_CODEC_STAT  0x18  /* AC'97 codec read / status */
#define ES1370_REG_SRC_ROUTE   0x1C  /* SRC slot routing */
#define ES1370_REG_SRC_VOL_L   0x20  /* SRC left volume */
#define ES1370_REG_SRC_VOL_R   0x24  /* SRC right volume */

/* DMA channel 0 registers */
#define ES1370_REG_DAC0_COUNT  0x28
#define ES1370_REG_DAC0_FRAME  0x2C
#define ES1370_REG_DAC0_ADDR   0x30

/* Control register bits */
#define ES1370_CTRL_EN_IRQ0    (1 << 0)
#define ES1370_CTRL_RESET      (1 << 1)
#define ES1370_CTRL_AUTORESET  (1 << 2)

/* AC'97 codec registers (subset) */
#define AC97_REG_RESET         0x00
#define AC97_REG_MASTER_VOL    0x02
#define AC97_REG_HEAD_VOL    0x04
#define AC97_REG_MASTER_MONO   0x06
#define AC97_REG_PCM_VOL       0x18
#define AC97_REG_RECORD_SEL    0x1A
#define AC97_REG_EXT_AUDIO     0x2A
#define AC97_REG_EXT_CTRL      0x2C

#define AC97_RESET_COMPLETE    0x8000

/* Maximum supported DMA buffer length (in 16-bit samples per channel). */
#define ES1370_DAC0_MAX_SAMPLES  65536

/* PCM format constants */
#define ES1370_SAMPLE_RATE_DEFAULT  48000
#define ES1370_CHANNELS_DEFAULT    2

/* Public API */
int es1370_init(uint8_t bus, uint8_t dev, uint8_t func);
int es1370_reset(void);
int es1370_play(const int16_t *samples, uint32_t sample_count,
                uint32_t sample_rate, uint8_t channels);
void es1370_stop(void);
int es1370_is_playing(void);

#endif