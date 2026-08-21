/* i2s_pcm.c - I2S / PCM audio controller driver.
 *
 * Programs a generic I2S/PCM controller to play a PCM stream by:
 *   1. Computing the BCLK divider for the requested sample rate
 *   2. Configuring the format register for sample width, slot width,
 *      channel count, polarity, and protocol (I2S/left/right justified)
 *   3. Building a DMA descriptor list pointing at the host buffer
 *   4. Programming the BDL base and length registers
 *   5. Starting the DMA engine and waiting for completion
 *
 * A separate codec (DAC/ADC) is controlled via I2C and is registered at
 * runtime through i2s_pcm_register_codec().
 */

#include "i2s_pcm.h"
#include "i2c.h"
#include "kheap.h"
#include "string.h"

static volatile uint32_t *g_regs;
static uint32_t g_mclk;
static int g_inited;
static int g_playing;

/* Host DMA buffer (single contiguous buffer; BDL can describe multiple
 * segments but for simplicity we use one entry). */
static void     *g_buf_virt;
static uint32_t  g_buf_paddr;
static uint32_t  g_buf_size;

/* BDL entry - one entry for the entire host buffer. */
static i2s_pcm_bdl_entry_t g_bdl[1];

static const i2s_pcm_codec_t *g_codec;

/* Compute BCLK divider for a desired sample rate.
 *   bclk = sample_rate * channels * sample_width_in_bytes * 2 (phases)
 * We approximate by using BCLK = MCLK / bclk_div where bclk_div is
 * chosen such that the rate matches as closely as possible. */
static uint32_t compute_div(uint32_t sample_rate, uint8_t channels,
                             uint8_t bits_per_sample)
{
    if (sample_rate == 0) sample_rate = 48000;
    if (channels < 1) channels = 2;
    if (bits_per_sample < 16) bits_per_sample = 16;

    uint32_t bclk = sample_rate * channels * bits_per_sample;
    if (bclk == 0) return 1;
    uint32_t div = g_mclk / bclk;
    if (div == 0) div = 1;
    /* The actual LRCK divider is BCLK/2. */
    return div;
}

static uint32_t pack_format(uint8_t bits, uint8_t slot_bits,
                              uint8_t channels, int lrck_pol, int bclk_pol)
{
    uint32_t v = 0;
    v |= ((uint32_t)bits & 0xF) << I2S_FMT_DATA_LEN_SHIFT;
    v |= ((uint32_t)slot_bits & 0xF) << I2S_FMT_SLOT_LEN_SHIFT;
    v |= ((uint32_t)channels & 0xF) << I2S_FMT_CHANNELS_SHIFT;
    if (lrck_pol) v |= I2S_FMT_LRCK_POL;
    if (bclk_pol) v |= I2S_FMT_BCLK_POL;
    v |= I2S_FMT_PROTO_I2S << 14;
    return v;
}

int i2s_pcm_init(volatile uint32_t *regs, uint32_t mclk_hz) {
    if (g_inited) return 0;
    g_regs = regs;
    g_mclk = mclk_hz ? mclk_hz : I2S_PCM_DEFAULT_MCLK_HZ;

    /* Soft-reset. */
    uint32_t ctl = g_regs[I2S_PCM_REG_CONTROL / 4];
    g_regs[I2S_PCM_REG_CONTROL / 4] = ctl | I2S_CTRL_RESET;
    for (volatile int i = 0; i < 10000; i++);
    g_regs[I2S_PCM_REG_CONTROL / 4] = 0;

    /* Default to master mode + enable. */
    g_regs[I2S_PCM_REG_CONTROL / 4] = I2S_CTRL_MASTER;
    g_inited = 1;
    return 0;
}

int i2s_pcm_register_codec(const i2s_pcm_codec_t *codec) {
    if (!codec) return -1;
    g_codec = codec;
    /* Bring the codec out of reset. */
    if (codec->set_power) codec->set_power(1);
    return 0;
}

int i2s_pcm_play(const int32_t *samples, uint32_t frames,
                 uint32_t sample_rate, uint8_t channels,
                 uint8_t bits_per_sample)
{
    if (!g_inited) return -1;
    if (!samples || frames == 0) return -1;
    if (bits_per_sample < 16) bits_per_sample = 16;
    if (bits_per_sample > 32) bits_per_sample = 32;

    /* Copy samples into DMA-safe memory. */
    uint32_t bytes = frames * (bits_per_sample / 8) * channels;
    if (bytes > I2S_PCM_SAMPLE_MAX_FRAMES) bytes = I2S_PCM_SAMPLE_MAX_FRAMES;
    g_buf_size = bytes;
    g_buf_virt = kmalloc(bytes);
    if (!g_buf_virt) return -1;
    memcpy(g_buf_virt, samples, bytes);
    g_buf_paddr = (uint32_t)g_buf_virt;

    /* Build BDL entry. */
    g_bdl[0].addr = g_buf_paddr;
    g_bdl[0].length = bytes;
    g_bdl[0].flags = 0x03;  /* IOC + last */
    g_bdl[0].reserved = 0;

    /* Configure format + divisor. */
    uint32_t div = compute_div(sample_rate, channels, bits_per_sample);
    g_regs[I2S_PCM_REG_DIV / 4] = div;
    g_regs[I2S_PCM_REG_FMT / 4] = pack_format(bits_per_sample, 32,
                                                channels, 0, 0);
    g_regs[I2S_PCM_REG_BDL_BASE / 4] = (uint32_t)g_bdl;
    g_regs[I2S_PCM_REG_BDL_COUNT / 4] = 1;

    /* Tell codec about the format if registered. */
    if (g_codec && g_codec->set_format) {
        g_codec->set_format(sample_rate, channels, bits_per_sample);
    }

    /* Start playback. */
    uint32_t ctl = g_regs[I2S_PCM_REG_CONTROL / 4];
    ctl &= ~(I2S_CTRL_FLUSH);
    ctl |= I2S_CTRL_ENABLE | I2S_CTRL_TX_START;
    g_regs[I2S_PCM_REG_CONTROL / 4] = ctl;

    g_playing = 1;
    return 0;
}

void i2s_pcm_stop(void) {
    if (!g_inited) return;
    uint32_t ctl = g_regs[I2S_PCM_REG_CONTROL / 4];
    ctl &= ~(I2S_CTRL_TX_START | I2S_CTRL_RX_START);
    ctl |= I2S_CTRL_FLUSH;
    g_regs[I2S_PCM_REG_CONTROL / 4] = ctl;
    for (volatile int i = 0; i < 1000; i++);
    ctl &= ~I2S_CTRL_FLUSH;
    g_regs[I2S_PCM_REG_CONTROL / 4] = ctl;
    g_playing = 0;
}

int i2s_pcm_is_playing(void) { return g_playing; }