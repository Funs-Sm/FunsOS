/* sound.c - FunsOS Unified Audio Subsystem Implementation
 *
 * Provides a single interface for all audio hardware (AC'97, SB16, HDAudio).
 * Handles device registration, mixer controls, PCM I/O, format conversion,
 * software voice mixing, and thread-safe access via spinlocks.
 */

#include "sound.h"
#include "sync.h"
#include "string.h"
#include "kheap.h"
#include "klog.h"
#include "ac97.h"
#include "sb16.h"
#include "hdaudio.h"

/* ==========================================================================
 * Static Subsystem State
 * ========================================================================== */

static sound_subsystem_t g_sound;

/* ==========================================================================
 * Internal: Device Lookup Helper
 * ========================================================================== */

/* Resolve a device ID. If dev_id >= device_count, use device 0.
 * Returns NULL if no devices available. Locks the global lock; caller
 * must not hold it. */
static sound_device_t *sound_resolve_device(uint32_t dev_id)
{
    sound_device_t *dev = NULL;

    spinlock_lock(&g_sound.lock);

    if (g_sound.device_count == 0) {
        spinlock_unlock(&g_sound.lock);
        return NULL;
    }

    if (dev_id < g_sound.device_count) {
        dev = g_sound.devices[dev_id];
    } else {
        dev = g_sound.devices[0];
    }

    spinlock_unlock(&g_sound.lock);
    return dev;
}

/* Determine PCM format from bits and channels */
static uint16_t sound_bits_channels_to_format(uint16_t bits, uint16_t channels)
{
    if (bits <= 8) {
        if (channels <= 1) return SOUND_FMT_PCM_U8;
        return SOUND_FMT_PCM_U8;
    } else if (bits <= 16) {
        return SOUND_FMT_PCM_S16LE;
    } else if (bits <= 24) {
        return SOUND_FMT_PCM_S24LE;
    }
    return SOUND_FMT_PCM_S32LE;
}

/* ==========================================================================
 * Wrapper Functions for AC'97 Driver
 * ========================================================================== */

static int ac97_wrap_play(void *priv, const void *data, uint32_t size,
                           uint32_t rate, uint16_t channels)
{
    (void)priv;
    return ac97_play(data, size, rate, channels);
}

static int ac97_wrap_record(void *priv, void *data, uint32_t size,
                             uint32_t rate, uint16_t channels)
{
    (void)priv;
    return ac97_record(data, size, rate, channels);
}

static void ac97_wrap_stop(void *priv)
{
    (void)priv;
    ac97_stop();
}

static void ac97_wrap_stop_playback(void *priv)
{
    (void)priv;
    ac97_stop_playback();
}

static void ac97_wrap_stop_capture(void *priv)
{
    (void)priv;
    ac97_stop_capture();
}

static int ac97_wrap_set_volume(void *priv, uint8_t left, uint8_t right)
{
    (void)priv;
    return ac97_set_master_volume(left, right);
}

static int ac97_wrap_get_volume(void *priv, uint8_t *left, uint8_t *right)
{
    (void)priv;
    if (!left || !right) return -1;

    ac97_device_t *ac97 = ac97_get_device();
    if (!ac97 || !ac97->initialized) return -1;

    /* AC'97 uses attenuation (0x3F = max attenuation, 0x00 = 0dB).
     * We invert: 0x3F → 0, 0x00 → 255 */
    /* Since we don't have direct get for AC'97 without reading regs,
     * return cached state from device structure is tricky.
     * We'll return 128 as reasonable default if we can't read. */
    *left = 128;
    *right = 128;
    return 0;
}

static int ac97_wrap_set_mute(void *priv, uint8_t mute)
{
    (void)priv;
    return ac97_set_mute(mute);
}

static int ac97_wrap_get_mute(void *priv)
{
    (void)priv;
    return ac97_get_mute();
}

static int ac97_wrap_set_mixer(void *priv, uint32_t control,
                                uint8_t left, uint8_t right)
{
    (void)priv;

    switch (control) {
    case SOUND_MIXER_MASTER:
        return ac97_set_master_volume(left, right);
    case SOUND_MIXER_PCM:
        return ac97_set_pcm_volume(left, right);
    case SOUND_MIXER_MIC:
        return ac97_set_mic_volume(left, 0);
    case SOUND_MIXER_LINE_IN:
        return ac97_set_line_in_volume(left, right);
    case SOUND_MIXER_CD:
        return ac97_set_cd_volume(left, right);
    case SOUND_MIXER_AUX:
        return ac97_set_aux_volume(left, right);
    case SOUND_MIXER_REC_SRC:
        return ac97_set_recording_source(left);
    case SOUND_MIXER_REC_GAIN:
        return ac97_set_recording_gain(left, right);
    case SOUND_MIXER_3D_DEPTH:
        return ac97_set_3d_depth(left);
    default:
        return -1;
    }
}

static int ac97_wrap_get_mixer(void *priv, uint32_t control,
                                uint8_t *left, uint8_t *right)
{
    (void)priv;
    if (!left || !right) return -1;

    /* AC'97 doesn't expose getters for all mixer controls in our API.
     * Return reasonable defaults. */
    *left = 128;
    *right = 128;
    (void)control;
    return 0;
}

static int ac97_wrap_set_format(void *priv, uint16_t bits, uint16_t channels)
{
    (void)priv;
    return ac97_set_format(bits, channels);
}

static int ac97_wrap_set_sample_rate(void *priv, uint32_t rate)
{
    (void)priv;
    return ac97_set_sample_rate(rate);
}

static uint32_t ac97_wrap_get_position(void *priv)
{
    (void)priv;
    return ac97_get_buffer_position();
}

/* ==========================================================================
 * Wrapper Functions for SB16 Driver
 * ========================================================================== */

static int sb16_wrap_play(void *priv, const void *data, uint32_t size,
                           uint32_t rate, uint16_t channels)
{
    (void)priv;
    return sb16_play(data, size, rate, channels);
}

static int sb16_wrap_record(void *priv, void *data, uint32_t size,
                             uint32_t rate, uint16_t channels)
{
    (void)priv;
    /* SB16 doesn't have a record API at the top level; play only */
    (void)data;
    (void)size;
    (void)rate;
    (void)channels;
    return -1;
}

static void sb16_wrap_stop(void *priv)
{
    (void)priv;
    sb16_stop();
}

static void sb16_wrap_stop_playback(void *priv)
{
    (void)priv;
    sb16_stop();
}

static void sb16_wrap_stop_capture(void *priv)
{
    (void)priv;
    /* SB16: no capture support at this level */
}

static int sb16_wrap_set_volume(void *priv, uint8_t left, uint8_t right)
{
    (void)priv;
    return sb16_set_master_volume(left, right);
}

static int sb16_wrap_get_volume(void *priv, uint8_t *left, uint8_t *right)
{
    (void)priv;
    return sb16_get_volume(left, right);
}

static int sb16_wrap_set_mute(void *priv, uint8_t mute)
{
    (void)priv;
    return sb16_set_mute(mute);
}

static int sb16_wrap_get_mute(void *priv)
{
    (void)priv;
    return sb16_get_mute();
}

static int sb16_wrap_set_mixer(void *priv, uint32_t control,
                                uint8_t left, uint8_t right)
{
    (void)priv;

    switch (control) {
    case SOUND_MIXER_MASTER:
        return sb16_set_master_volume(left, right);
    case SOUND_MIXER_PCM:
        return sb16_set_voice_volume(left, right);
    case SOUND_MIXER_MIC:
        return sb16_set_mic_volume(left);
    case SOUND_MIXER_LINE_IN:
        return sb16_set_line_volume(left, right);
    case SOUND_MIXER_CD:
        return sb16_set_cd_volume(left, right);
    case SOUND_MIXER_FM:
        return sb16_set_fm_volume(left, right);
    default:
        return -1;
    }
}

static int sb16_wrap_get_mixer(void *priv, uint32_t control,
                                uint8_t *left, uint8_t *right)
{
    (void)priv;
    if (!left || !right) return -1;

    sb16_device_t *sb16 = sb16_get_device();
    if (!sb16 || !sb16->initialized) return -1;

    switch (control) {
    case SOUND_MIXER_MASTER:
        *left = sb16->master_vol_left;
        *right = sb16->master_vol_right;
        break;
    case SOUND_MIXER_PCM:
        *left = sb16->voice_vol_left;
        *right = sb16->voice_vol_right;
        break;
    case SOUND_MIXER_MIC:
        *left = sb16->mic_vol;
        *right = sb16->mic_vol;
        break;
    case SOUND_MIXER_LINE_IN:
        *left = sb16->line_vol_left;
        *right = sb16->line_vol_right;
        break;
    case SOUND_MIXER_CD:
        *left = sb16->cd_vol_left;
        *right = sb16->cd_vol_right;
        break;
    case SOUND_MIXER_FM:
        *left = sb16->fm_vol_left;
        *right = sb16->fm_vol_right;
        break;
    default:
        *left = 128;
        *right = 128;
    }
    return 0;
}

static int sb16_wrap_set_format(void *priv, uint16_t bits, uint16_t channels)
{
    (void)priv;
    return sb16_set_format(bits, channels);
}

static int sb16_wrap_set_sample_rate(void *priv, uint32_t rate)
{
    (void)priv;
    return sb16_set_sample_rate(rate);
}

static uint32_t sb16_wrap_get_position(void *priv)
{
    (void)priv;
    return sb16_get_buffer_position();
}

/* ==========================================================================
 * Wrapper Functions for HDA Driver
 * ========================================================================== */

static int hda_wrap_play(void *priv, const void *data, uint32_t size,
                          uint32_t rate, uint16_t channels)
{
    (void)priv;
    return hdaudio_play(data, size, rate, channels);
}

static int hda_wrap_record(void *priv, void *data, uint32_t size,
                            uint32_t rate, uint16_t channels)
{
    (void)priv;
    return hdaudio_record(data, size, rate, channels);
}

static void hda_wrap_stop(void *priv)
{
    (void)priv;
    hdaudio_stop();
}

static void hda_wrap_stop_playback(void *priv)
{
    (void)priv;
    hdaudio_stop();
}

static void hda_wrap_stop_capture(void *priv)
{
    (void)priv;
    hdaudio_stop();
}

static int hda_wrap_set_volume(void *priv, uint8_t left, uint8_t right)
{
    (void)priv;
    return hdaudio_set_volume(left, right);
}

static int hda_wrap_get_volume(void *priv, uint8_t *left, uint8_t *right)
{
    (void)priv;
    return hdaudio_get_volume(left, right);
}

static int hda_wrap_set_mute(void *priv, uint8_t mute)
{
    (void)priv;
    return hdaudio_set_mute(mute);
}

static int hda_wrap_get_mute(void *priv)
{
    (void)priv;
    return hdaudio_get_mute();
}

static int hda_wrap_set_mixer(void *priv, uint32_t control,
                               uint8_t left, uint8_t right)
{
    (void)priv;

    switch (control) {
    case SOUND_MIXER_MASTER:
    case SOUND_MIXER_PCM:
        return hdaudio_set_volume(left, right);
    default:
        return -1;
    }
}

static int hda_wrap_get_mixer(void *priv, uint32_t control,
                               uint8_t *left, uint8_t *right)
{
    (void)priv;
    if (!left || !right) return -1;

    switch (control) {
    case SOUND_MIXER_MASTER:
    case SOUND_MIXER_PCM:
        return hdaudio_get_volume(left, right);
    default:
        *left = 128;
        *right = 128;
        return 0;
    }
}

static int hda_wrap_set_format(void *priv, uint16_t bits, uint16_t channels)
{
    (void)priv;
    return hdaudio_set_format(bits, channels);
}

static int hda_wrap_set_sample_rate(void *priv, uint32_t rate)
{
    (void)priv;
    return hdaudio_set_sample_rate(rate);
}

static uint32_t hda_wrap_get_position(void *priv)
{
    (void)priv;
    return hdaudio_get_buffer_position();
}

/* ==========================================================================
 * Device Registration Helpers
 * ========================================================================== */

/* Register the AC'97 device if available */
static void sound_register_ac97(void)
{
    if (!ac97_is_available()) return;

    ac97_device_t *ac97 = ac97_get_device();
    if (!ac97) return;

    sound_device_t *dev = (sound_device_t *)kmalloc(sizeof(sound_device_t));
    if (!dev) return;

    memset(dev, 0, sizeof(sound_device_t));

    /* Set device name based on vendor */
    if (ac97->vendor_id == 0x8086) {
        memcpy(dev->name, "Intel AC'97 Audio", 18);
    } else if (ac97->vendor_id == 0x10DE) {
        memcpy(dev->name, "NVIDIA nForce AC'97 Audio", 26);
    } else if (ac97->vendor_id == 0x1039) {
        memcpy(dev->name, "SiS AC'97 Audio", 16);
    } else if (ac97->vendor_id == 0x1106) {
        memcpy(dev->name, "VIA AC'97 Audio", 16);
    } else {
        memcpy(dev->name, "AC'97 Audio Device", 18);
    }

    dev->type           = SOUND_DEV_TYPE_AC97;
    dev->sample_rate    = ac97->sample_rate;
    dev->channels       = ac97->channels;
    dev->bits_per_sample = ac97->bits_per_sample;
    dev->format         = SOUND_FMT_PCM_S16LE;
    dev->private_data   = (void *)ac97;

    /* Capabilities */
    dev->caps = SOUND_CAP_PLAYBACK | SOUND_CAP_CAPTURE | SOUND_CAP_MIXER
              | SOUND_CAP_POWER_MGMT | SOUND_CAP_JACK_SENSE;
    if (ac97->supports_3d) dev->caps |= SOUND_CAP_3D;

    /* PCM operations */
    dev->play           = ac97_wrap_play;
    dev->record         = ac97_wrap_record;
    dev->stop           = ac97_wrap_stop;
    dev->stop_playback  = ac97_wrap_stop_playback;
    dev->stop_capture   = ac97_wrap_stop_capture;

    /* Volume/Mute operations */
    dev->set_volume     = ac97_wrap_set_volume;
    dev->get_volume     = ac97_wrap_get_volume;
    dev->set_mute       = ac97_wrap_set_mute;
    dev->get_mute       = ac97_wrap_get_mute;

    /* Mixer operations */
    dev->set_mixer      = ac97_wrap_set_mixer;
    dev->get_mixer      = ac97_wrap_get_mixer;

    /* Format operations */
    dev->set_format     = ac97_wrap_set_format;
    dev->set_sample_rate = ac97_wrap_set_sample_rate;

    /* Position */
    dev->get_position   = ac97_wrap_get_position;

    spinlock_init(&dev->lock);

    sound_register_device(dev);
    klog_info("sound: registered AC'97 device (%s)", dev->name);
}

/* Register the SB16 device if available */
static void sound_register_sb16(void)
{
    if (!sb16_is_available()) return;

    sb16_device_t *sb16 = sb16_get_device();
    if (!sb16) return;

    sound_device_t *dev = (sound_device_t *)kmalloc(sizeof(sound_device_t));
    if (!dev) return;

    memset(dev, 0, sizeof(sound_device_t));

    memcpy(dev->name, "Creative Sound Blaster 16", 26);

    dev->type           = SOUND_DEV_TYPE_SB16;
    dev->sample_rate    = sb16->sample_rate;
    dev->channels       = sb16->channels;
    dev->bits_per_sample = sb16->bits_per_sample;
    dev->format         = SOUND_FMT_PCM_S16LE;
    dev->private_data   = (void *)sb16;

    /* Capabilities */
    dev->caps = SOUND_CAP_PLAYBACK | SOUND_CAP_MIXER
              | SOUND_CAP_SOFTWARE_MIX;
    if (sb16->opl3_enabled) dev->caps |= SOUND_CAP_FM_SYNTH;
    if (sb16->midi_enabled) dev->caps |= SOUND_CAP_MIDI;

    /* PCM operations */
    dev->play           = sb16_wrap_play;
    dev->record         = sb16_wrap_record;
    dev->stop           = sb16_wrap_stop;
    dev->stop_playback  = sb16_wrap_stop_playback;
    dev->stop_capture   = sb16_wrap_stop_capture;

    /* Volume/Mute operations */
    dev->set_volume     = sb16_wrap_set_volume;
    dev->get_volume     = sb16_wrap_get_volume;
    dev->set_mute       = sb16_wrap_set_mute;
    dev->get_mute       = sb16_wrap_get_mute;

    /* Mixer operations */
    dev->set_mixer      = sb16_wrap_set_mixer;
    dev->get_mixer      = sb16_wrap_get_mixer;

    /* Format operations */
    dev->set_format     = sb16_wrap_set_format;
    dev->set_sample_rate = sb16_wrap_set_sample_rate;

    /* Position */
    dev->get_position   = sb16_wrap_get_position;

    spinlock_init(&dev->lock);

    sound_register_device(dev);
    klog_info("sound: registered SB16 device (%s)", dev->name);
}

/* Register the HDA device if available */
static void sound_register_hda(void)
{
    if (!hdaudio_is_available()) return;

    hda_controller_t *hda = hdaudio_get_controller();
    if (!hda) return;

    sound_device_t *dev = (sound_device_t *)kmalloc(sizeof(sound_device_t));
    if (!dev) return;

    memset(dev, 0, sizeof(sound_device_t));

    memcpy(dev->name, "Intel HD Audio", 15);

    dev->type           = SOUND_DEV_TYPE_HDA;
    dev->sample_rate    = 44100;
    dev->channels       = 2;
    dev->bits_per_sample = 16;
    dev->format         = SOUND_FMT_PCM_S16LE;
    dev->private_data   = (void *)hda;

    /* Capabilities */
    dev->caps = SOUND_CAP_PLAYBACK | SOUND_CAP_CAPTURE | SOUND_CAP_MIXER
              | SOUND_CAP_POWER_MGMT | SOUND_CAP_HIGH_RATE
              | SOUND_CAP_24BIT | SOUND_CAP_JACK_SENSE;
    if (hda->supports_64bit) {
        /* 64-bit support implies modern chipset */
    }

    /* PCM operations */
    dev->play           = hda_wrap_play;
    dev->record         = hda_wrap_record;
    dev->stop           = hda_wrap_stop;
    dev->stop_playback  = hda_wrap_stop_playback;
    dev->stop_capture   = hda_wrap_stop_capture;

    /* Volume/Mute operations */
    dev->set_volume     = hda_wrap_set_volume;
    dev->get_volume     = hda_wrap_get_volume;
    dev->set_mute       = hda_wrap_set_mute;
    dev->get_mute       = hda_wrap_get_mute;

    /* Mixer operations */
    dev->set_mixer      = hda_wrap_set_mixer;
    dev->get_mixer      = hda_wrap_get_mixer;

    /* Format operations */
    dev->set_format     = hda_wrap_set_format;
    dev->set_sample_rate = hda_wrap_set_sample_rate;

    /* Position */
    dev->get_position   = hda_wrap_get_position;

    spinlock_init(&dev->lock);

    sound_register_device(dev);
    klog_info("sound: registered HDA device (%s)", dev->name);
}

/* ==========================================================================
 * Initialization
 * ========================================================================== */

void sound_init(void)
{
    memset(&g_sound, 0, sizeof(sound_subsystem_t));
    spinlock_init(&g_sound.lock);
    spinlock_init(&g_sound.mix_lock);

    /* Allocate mix buffer for software voice mixing (4KB is a reasonable
     * default period; it will be resized as needed in sound_voice_mix_all) */
    g_sound.mix_buffer_size = 4096;
    g_sound.mix_buffer = (uint8_t *)kmalloc(g_sound.mix_buffer_size);
    if (!g_sound.mix_buffer) {
        g_sound.mix_buffer_size = 0;
    }

    g_sound.initialized = 1;
    klog_info("sound: subsystem initialized");
}

uint32_t sound_device_enumerate(void)
{
    uint32_t count = 0;

    klog_info("sound: starting device enumeration...");

    /* Initialize and register AC'97 */
    ac97_init();
    spinlock_lock(&g_sound.lock);
    count = g_sound.device_count;
    spinlock_unlock(&g_sound.lock);
    sound_register_ac97();

    /* Initialize and register SB16 */
    sb16_init();
    spinlock_lock(&g_sound.lock);
    count = g_sound.device_count;
    spinlock_unlock(&g_sound.lock);
    sound_register_sb16();

    /* Initialize and register HDA */
    hdaudio_init();
    spinlock_lock(&g_sound.lock);
    count = g_sound.device_count;
    spinlock_unlock(&g_sound.lock);
    sound_register_hda();

    spinlock_lock(&g_sound.lock);
    count = g_sound.device_count;
    spinlock_unlock(&g_sound.lock);

    klog_info("sound: enumeration complete, %u device(s) found", count);
    return count;
}

/* ==========================================================================
 * Device Registration
 * ========================================================================== */

int sound_register_device(sound_device_t *dev)
{
    int result = -1;

    spinlock_lock(&g_sound.lock);

    if (!dev) {
        spinlock_unlock(&g_sound.lock);
        return -1;
    }

    if (g_sound.device_count >= SOUND_MAX_DEVICES) {
        klog_warn("sound: cannot register device '%s': max devices (%u) reached",
                  dev->name, SOUND_MAX_DEVICES);
        spinlock_unlock(&g_sound.lock);
        return -1;
    }

    /* Check for duplicate */
    uint32_t i;
    for (i = 0; i < g_sound.device_count; i++) {
        if (g_sound.devices[i] == dev) {
            spinlock_unlock(&g_sound.lock);
            return (int)i;  /* Already registered */
        }
    }

    g_sound.devices[g_sound.device_count] = dev;
    result = (int)g_sound.device_count;
    g_sound.device_count++;

    spinlock_unlock(&g_sound.lock);
    return result;
}

/* ==========================================================================
 * Device Access
 * ========================================================================== */

uint32_t sound_get_device_count(void)
{
    uint32_t count;
    spinlock_lock(&g_sound.lock);
    count = g_sound.device_count;
    spinlock_unlock(&g_sound.lock);
    return count;
}

sound_device_t *sound_get_device(uint32_t index)
{
    return sound_resolve_device(index);
}

sound_device_t *sound_get_device_by_type(uint32_t type)
{
    sound_device_t *dev = NULL;
    uint32_t i;

    spinlock_lock(&g_sound.lock);

    for (i = 0; i < g_sound.device_count; i++) {
        if (g_sound.devices[i]->type == type) {
            dev = g_sound.devices[i];
            break;
        }
    }

    spinlock_unlock(&g_sound.lock);
    return dev;
}

sound_device_t *sound_get_default_device(void)
{
    sound_device_t *dev = NULL;

    spinlock_lock(&g_sound.lock);

    if (g_sound.device_count == 0) {
        spinlock_unlock(&g_sound.lock);
        return NULL;
    }

    /* Prefer HDA over AC'97 over SB16 */
    uint32_t i;
    /* First pass: look for HDA */
    for (i = 0; i < g_sound.device_count; i++) {
        if (g_sound.devices[i]->type == SOUND_DEV_TYPE_HDA) {
            dev = g_sound.devices[i];
            spinlock_unlock(&g_sound.lock);
            return dev;
        }
    }
    /* Second pass: AC'97 */
    for (i = 0; i < g_sound.device_count; i++) {
        if (g_sound.devices[i]->type == SOUND_DEV_TYPE_AC97) {
            dev = g_sound.devices[i];
            spinlock_unlock(&g_sound.lock);
            return dev;
        }
    }
    /* Fallback: first available */
    dev = g_sound.devices[0];

    spinlock_unlock(&g_sound.lock);
    return dev;
}

const char *sound_get_device_name(uint32_t dev_id)
{
    sound_device_t *dev = sound_resolve_device(dev_id);
    if (!dev) return "none";
    return dev->name;
}

const char *sound_get_device_type_string(uint32_t dev_id)
{
    sound_device_t *dev = sound_resolve_device(dev_id);
    if (!dev) return "none";

    switch (dev->type) {
    case SOUND_DEV_TYPE_AC97: return "AC'97";
    case SOUND_DEV_TYPE_SB16: return "SB16";
    case SOUND_DEV_TYPE_HDA:  return "HDA";
    default:                  return "unknown";
    }
}

/* ==========================================================================
 * PCM Playback / Capture
 * ========================================================================== */

int sound_play(uint32_t dev_id, const void *data, uint32_t size,
               uint32_t rate, uint16_t channels, uint16_t bits)
{
    if (!data || size == 0) return -1;

    sound_device_t *dev = sound_resolve_device(dev_id);
    if (!dev) return -1;

    if (!dev->play) return -1;

    /* Validate and set format */
    if (bits < 8) bits = 8;
    if (bits > 32) bits = 32;
    if (channels < 1) channels = 1;
    if (channels > 2) channels = 2;

    spinlock_lock(&dev->lock);

    dev->sample_rate    = rate;
    dev->channels       = channels;
    dev->bits_per_sample = bits;
    dev->format         = sound_bits_channels_to_format(bits, channels);

    int result = dev->play(dev->private_data, data, size, rate, channels);

    spinlock_unlock(&dev->lock);
    return result;
}

int sound_record(uint32_t dev_id, void *data, uint32_t size,
                 uint32_t rate, uint16_t channels, uint16_t bits)
{
    if (!data || size == 0) return -1;

    sound_device_t *dev = sound_resolve_device(dev_id);
    if (!dev) return -1;

    if (!dev->record) return -1;

    if (bits < 8) bits = 8;
    if (bits > 32) bits = 32;
    if (channels < 1) channels = 1;
    if (channels > 2) channels = 2;

    spinlock_lock(&dev->lock);

    dev->sample_rate    = rate;
    dev->channels       = channels;
    dev->bits_per_sample = bits;
    dev->format         = sound_bits_channels_to_format(bits, channels);

    int result = dev->record(dev->private_data, data, size, rate, channels);

    spinlock_unlock(&dev->lock);
    return result;
}

/* ==========================================================================
 * Stream Control
 * ========================================================================== */

int sound_stop(uint32_t dev_id)
{
    sound_device_t *dev = sound_resolve_device(dev_id);
    if (!dev) return -1;

    spinlock_lock(&dev->lock);
    if (dev->stop) dev->stop(dev->private_data);
    spinlock_unlock(&dev->lock);

    return 0;
}

int sound_stop_playback(uint32_t dev_id)
{
    sound_device_t *dev = sound_resolve_device(dev_id);
    if (!dev) return -1;

    spinlock_lock(&dev->lock);
    if (dev->stop_playback) dev->stop_playback(dev->private_data);
    else if (dev->stop) dev->stop(dev->private_data);
    spinlock_unlock(&dev->lock);

    return 0;
}

int sound_stop_capture(uint32_t dev_id)
{
    sound_device_t *dev = sound_resolve_device(dev_id);
    if (!dev) return -1;

    spinlock_lock(&dev->lock);
    if (dev->stop_capture) dev->stop_capture(dev->private_data);
    else if (dev->stop) dev->stop(dev->private_data);
    spinlock_unlock(&dev->lock);

    return 0;
}

int sound_pause(uint32_t dev_id)
{
    /* Pause is equivalent to stopping - drivers can restart from position */
    return sound_stop_playback(dev_id);
}

int sound_resume(uint32_t dev_id)
{
    /* Resume is device-specific; for now, stop is sufficient.
     * The driver will be re-fed data on the next play() call. */
    (void)dev_id;
    return 0;
}

/* ==========================================================================
 * Volume Control
 * ========================================================================== */

int sound_set_volume(uint32_t dev_id, uint8_t left, uint8_t right)
{
    sound_device_t *dev = sound_resolve_device(dev_id);
    if (!dev) return -1;
    if (!dev->set_volume) return -1;

    spinlock_lock(&dev->lock);
    int result = dev->set_volume(dev->private_data, left, right);
    spinlock_unlock(&dev->lock);

    return result;
}

int sound_get_volume(uint32_t dev_id, uint8_t *left, uint8_t *right)
{
    if (!left || !right) return -1;

    sound_device_t *dev = sound_resolve_device(dev_id);
    if (!dev) return -1;
    if (!dev->get_volume) return -1;

    spinlock_lock(&dev->lock);
    int result = dev->get_volume(dev->private_data, left, right);
    spinlock_unlock(&dev->lock);

    return result;
}

int sound_set_mute(uint32_t dev_id, uint8_t mute)
{
    sound_device_t *dev = sound_resolve_device(dev_id);
    if (!dev) return -1;
    if (!dev->set_mute) return -1;

    spinlock_lock(&dev->lock);
    int result = dev->set_mute(dev->private_data, mute);
    spinlock_unlock(&dev->lock);

    return result;
}

int sound_get_mute(uint32_t dev_id)
{
    sound_device_t *dev = sound_resolve_device(dev_id);
    if (!dev) return -1;
    if (!dev->get_mute) return -1;

    spinlock_lock(&dev->lock);
    int result = dev->get_mute(dev->private_data);
    spinlock_unlock(&dev->lock);

    return result;
}

/* ==========================================================================
 * Mixer Controls
 * ========================================================================== */

int sound_set_mixer(uint32_t dev_id, uint32_t control, uint8_t left, uint8_t right)
{
    sound_device_t *dev = sound_resolve_device(dev_id);
    if (!dev) return -1;
    if (!dev->set_mixer) return -1;

    spinlock_lock(&dev->lock);
    int result = dev->set_mixer(dev->private_data, control, left, right);
    spinlock_unlock(&dev->lock);

    return result;
}

int sound_get_mixer(uint32_t dev_id, uint32_t control, uint8_t *left, uint8_t *right)
{
    if (!left || !right) return -1;

    sound_device_t *dev = sound_resolve_device(dev_id);
    if (!dev) return -1;
    if (!dev->get_mixer) return -1;

    spinlock_lock(&dev->lock);
    int result = dev->get_mixer(dev->private_data, control, left, right);
    spinlock_unlock(&dev->lock);

    return result;
}

/* ==========================================================================
 * Format Configuration
 * ========================================================================== */

int sound_set_format(uint32_t dev_id, uint16_t bits, uint16_t channels)
{
    sound_device_t *dev = sound_resolve_device(dev_id);
    if (!dev) return -1;
    if (!dev->set_format) return -1;

    spinlock_lock(&dev->lock);

    dev->bits_per_sample = bits;
    dev->channels        = channels;
    dev->format          = sound_bits_channels_to_format(bits, channels);

    int result = dev->set_format(dev->private_data, bits, channels);

    spinlock_unlock(&dev->lock);
    return result;
}

int sound_set_sample_rate(uint32_t dev_id, uint32_t rate)
{
    sound_device_t *dev = sound_resolve_device(dev_id);
    if (!dev) return -1;
    if (!dev->set_sample_rate) return -1;

    spinlock_lock(&dev->lock);

    dev->sample_rate = rate;

    int result = dev->set_sample_rate(dev->private_data, rate);

    spinlock_unlock(&dev->lock);
    return result;
}

/* ==========================================================================
 * Status
 * ========================================================================== */

uint32_t sound_get_buffer_position(uint32_t dev_id)
{
    sound_device_t *dev = sound_resolve_device(dev_id);
    if (!dev) return 0;
    if (!dev->get_position) return 0;

    /* Don't lock for a read-only position query */
    return dev->get_position(dev->private_data);
}

int sound_is_available(uint32_t dev_id)
{
    sound_device_t *dev = sound_resolve_device(dev_id);
    return (dev != NULL) ? 1 : 0;
}

uint32_t sound_get_capabilities(uint32_t dev_id)
{
    sound_device_t *dev = sound_resolve_device(dev_id);
    if (!dev) return 0;
    return dev->caps;
}

/* ==========================================================================
 * Utility: Volume Mapping
 * ========================================================================== */

uint32_t sound_volume_map(uint8_t linear_vol, uint32_t max_val)
{
    if (linear_vol == 0) return 0;
    if (linear_vol >= 255) return max_val;

    /* Linear scaling: (linear_vol / 255) * max_val */
    return ((uint32_t)linear_vol * max_val) / 255;
}

/* ==========================================================================
 * Utility: Format Conversion
 * ========================================================================== */

uint32_t sound_convert_format(const void *src, void *dst, uint32_t src_size,
                              uint16_t src_format, uint16_t src_channels,
                              uint16_t dst_format, uint16_t dst_channels,
                              uint16_t max_bits)
{
    if (!src || !dst || src_size == 0) return 0;
    (void)max_bits;  /* Reserved for future use */

    /* Fast path: same format, same channels */
    if (src_format == dst_format && src_channels == dst_channels) {
        memcpy(dst, src, src_size);
        return src_size;
    }

    uint32_t bytes_per_src_sample = 1;
    uint32_t bytes_per_dst_sample = 1;

    if (src_format == SOUND_FMT_PCM_S16LE) bytes_per_src_sample = 2;
    else if (src_format == SOUND_FMT_PCM_S24LE) bytes_per_src_sample = 3;
    else if (src_format == SOUND_FMT_PCM_S32LE) bytes_per_src_sample = 4;

    if (dst_format == SOUND_FMT_PCM_S16LE) bytes_per_dst_sample = 2;
    else if (dst_format == SOUND_FMT_PCM_S24LE) bytes_per_dst_sample = 3;
    else if (dst_format == SOUND_FMT_PCM_S32LE) bytes_per_dst_sample = 4;

    uint32_t src_frame_size = bytes_per_src_sample * src_channels;
    uint32_t dst_frame_size = bytes_per_dst_sample * dst_channels;
    uint32_t num_frames     = src_size / src_frame_size;
    uint32_t dst_total      = num_frames * dst_frame_size;

    /* Simplify: if channels differ but format is the same, do channel conv */
    if (src_format == dst_format && src_channels != dst_channels) {
        uint32_t f;
        if (src_channels == 2 && dst_channels == 1) {
            /* Stereo → Mono: average left and right */
            if (bytes_per_src_sample == 1) {
                /* U8 */
                const uint8_t *sp = (const uint8_t *)src;
                uint8_t *dp = (uint8_t *)dst;
                for (f = 0; f < num_frames; f++) {
                    int32_t sum = (int32_t)sp[0] + (int32_t)sp[1];
                    dp[0] = (uint8_t)(sum / 2);
                    sp += 2;
                    dp += 1;
                }
            } else if (bytes_per_src_sample == 2) {
                /* S16 */
                const int16_t *sp = (const int16_t *)src;
                int16_t *dp = (int16_t *)dst;
                for (f = 0; f < num_frames; f++) {
                    int32_t sum = (int32_t)sp[0] + (int32_t)sp[1];
                    dp[0] = (int16_t)(sum / 2);
                    sp += 2;
                    dp += 1;
                }
            }
        } else if (src_channels == 1 && dst_channels == 2) {
            /* Mono → Stereo: duplicate sample to both channels */
            if (bytes_per_src_sample == 1) {
                const uint8_t *sp = (const uint8_t *)src;
                uint8_t *dp = (uint8_t *)dst;
                for (f = 0; f < num_frames; f++) {
                    dp[0] = sp[0];
                    dp[1] = sp[0];
                    sp += 1;
                    dp += 2;
                }
            } else if (bytes_per_src_sample == 2) {
                const int16_t *sp = (const int16_t *)src;
                int16_t *dp = (int16_t *)dst;
                for (f = 0; f < num_frames; f++) {
                    dp[0] = sp[0];
                    dp[1] = sp[0];
                    sp += 1;
                    dp += 2;
                }
            }
        }
        return dst_total;
    }

    /* Full conversion: different format AND possibly different channels */
    uint32_t f;
    for (f = 0; f < num_frames; f++) {
        int32_t left  = 0;
        int32_t right = 0;

        /* --- Read source sample(s) --- */
        if (src_channels == 2) {
            /* Stereo source */
            if (bytes_per_src_sample == 1) {
                const uint8_t *sp = (const uint8_t *)src + f * 2;
                left  = ((int32_t)sp[0] - 128) << 8;
                right = ((int32_t)sp[1] - 128) << 8;
            } else if (bytes_per_src_sample == 2) {
                const int16_t *sp = (const int16_t *)src + f * 2;
                left  = (int32_t)sp[0];
                right = (int32_t)sp[1];
            } else if (bytes_per_src_sample == 3) {
                const uint8_t *sp = (const uint8_t *)src + f * 6;
                left  = (int32_t)((sp[0] | (sp[1] << 8) | (sp[2] << 16)) << 8) >> 8;
                right = (int32_t)((sp[3] | (sp[4] << 8) | (sp[5] << 16)) << 8) >> 8;
            } else {
                const int32_t *sp = (const int32_t *)src + f * 2;
                left  = sp[0];
                right = sp[1];
            }
        } else {
            /* Mono source */
            if (bytes_per_src_sample == 1) {
                const uint8_t *sp = (const uint8_t *)src + f;
                left = right = ((int32_t)sp[0] - 128) << 8;
            } else if (bytes_per_src_sample == 2) {
                const int16_t *sp = (const int16_t *)src + f;
                left = right = (int32_t)sp[0];
            } else if (bytes_per_src_sample == 3) {
                const uint8_t *sp = (const uint8_t *)src + f * 3;
                left = right = (int32_t)((sp[0] | (sp[1] << 8) | (sp[2] << 16)) << 8) >> 8;
            } else {
                const int32_t *sp = (const int32_t *)src + f;
                left = right = sp[0];
            }
        }

        /* If converting from stereo to mono, average */
        if (src_channels == 2 && dst_channels == 1) {
            left = (left + right) / 2;
            right = left;
        }
        /* If converting from mono to stereo, duplicate */
        if (src_channels == 1 && dst_channels == 2) {
            right = left;
        }

        /* --- Write destination sample(s) --- */
        if (dst_channels == 2) {
            if (bytes_per_dst_sample == 1) {
                uint8_t *dp = (uint8_t *)dst + f * 2;
                dp[0] = (uint8_t)(((left  >> 8) + 128) & 0xFF);
                dp[1] = (uint8_t)(((right >> 8) + 128) & 0xFF);
            } else if (bytes_per_dst_sample == 2) {
                int16_t *dp = (int16_t *)dst + f * 2;
                dp[0] = (int16_t)left;
                dp[1] = (int16_t)right;
            } else if (bytes_per_dst_sample == 3) {
                uint8_t *dp = (uint8_t *)dst + f * 6;
                dp[0] = (uint8_t)(left & 0xFF);
                dp[1] = (uint8_t)((left >> 8) & 0xFF);
                dp[2] = (uint8_t)((left >> 16) & 0xFF);
                dp[3] = (uint8_t)(right & 0xFF);
                dp[4] = (uint8_t)((right >> 8) & 0xFF);
                dp[5] = (uint8_t)((right >> 16) & 0xFF);
            } else {
                int32_t *dp = (int32_t *)dst + f * 2;
                dp[0] = left;
                dp[1] = right;
            }
        } else {
            if (bytes_per_dst_sample == 1) {
                uint8_t *dp = (uint8_t *)dst + f;
                dp[0] = (uint8_t)(((left >> 8) + 128) & 0xFF);
            } else if (bytes_per_dst_sample == 2) {
                int16_t *dp = (int16_t *)dst + f;
                dp[0] = (int16_t)left;
            } else if (bytes_per_dst_sample == 3) {
                uint8_t *dp = (uint8_t *)dst + f * 3;
                dp[0] = (uint8_t)(left & 0xFF);
                dp[1] = (uint8_t)((left >> 8) & 0xFF);
                dp[2] = (uint8_t)((left >> 16) & 0xFF);
            } else {
                int32_t *dp = (int32_t *)dst + f;
                dp[0] = left;
            }
        }
    }

    return dst_total;
}

/* ==========================================================================
 * Software Voice Mixing
 * ========================================================================== */

int sound_voice_alloc(void)
{
    int i;

    spinlock_lock(&g_sound.mix_lock);

    for (i = 0; i < SOUND_MAX_VOICES; i++) {
        if (!g_sound.voices[i].active) {
            memset(&g_sound.voices[i], 0, sizeof(sound_voice_t));
            g_sound.voices[i].active   = 1;
            g_sound.voices[i].volume   = 255;
            g_sound.voices[i].pan      = 128;
            g_sound.voices[i].loop     = 0;
            spinlock_unlock(&g_sound.mix_lock);
            return i;
        }
    }

    spinlock_unlock(&g_sound.mix_lock);
    return -1;
}

void sound_voice_free(int voice_id)
{
    if (voice_id < 0 || voice_id >= SOUND_MAX_VOICES) return;

    spinlock_lock(&g_sound.mix_lock);
    sound_voice_stop(voice_id);
    memset(&g_sound.voices[voice_id], 0, sizeof(sound_voice_t));
    spinlock_unlock(&g_sound.mix_lock);
}

int sound_voice_play(int voice_id, const void *data, uint32_t size,
                     uint32_t rate, uint16_t channels, uint16_t bits,
                     uint8_t loop)
{
    if (voice_id < 0 || voice_id >= SOUND_MAX_VOICES) return -1;
    if (!data || size == 0) return -1;

    spinlock_lock(&g_sound.mix_lock);

    sound_voice_t *v = &g_sound.voices[voice_id];
    if (!v->active) {
        spinlock_unlock(&g_sound.mix_lock);
        return -1;
    }

    v->buffer        = (uint8_t *)data;
    v->buffer_size   = size;
    v->position      = 0;
    v->position_frac = 0.0f;
    v->sample_rate   = rate;
    v->channels      = channels;
    v->bits          = bits;
    v->loop          = loop;

    spinlock_unlock(&g_sound.mix_lock);
    return 0;
}

void sound_voice_stop(int voice_id)
{
    if (voice_id < 0 || voice_id >= SOUND_MAX_VOICES) return;

    spinlock_lock(&g_sound.mix_lock);
    g_sound.voices[voice_id].buffer      = NULL;
    g_sound.voices[voice_id].buffer_size = 0;
    g_sound.voices[voice_id].position    = 0;
    g_sound.voices[voice_id].position_frac = 0.0f;
    spinlock_unlock(&g_sound.mix_lock);
}

int sound_voice_set_volume(int voice_id, uint8_t volume)
{
    if (voice_id < 0 || voice_id >= SOUND_MAX_VOICES) return -1;

    spinlock_lock(&g_sound.mix_lock);
    g_sound.voices[voice_id].volume = volume;
    spinlock_unlock(&g_sound.mix_lock);
    return 0;
}

int sound_voice_set_pan(int voice_id, uint8_t pan)
{
    if (voice_id < 0 || voice_id >= SOUND_MAX_VOICES) return -1;

    spinlock_lock(&g_sound.mix_lock);
    g_sound.voices[voice_id].pan = pan;
    spinlock_unlock(&g_sound.mix_lock);
    return 0;
}

/* Mix all active voices into the mix buffer and submit to the default device.
 * This mixes one period (mix_buffer_size bytes) of S16LE stereo data.
 * The output device is assumed to be configured for 44100 Hz stereo 16-bit. */
void sound_voice_mix_all(void)
{
    sound_device_t *dev = sound_get_default_device();
    if (!dev) return;

    /* Allocate or resize mix buffer for S16LE stereo @ 44100 Hz, one period */
    uint32_t needed = 4096;  /* 4KB period = 1024 samples stereo S16 */
    spinlock_lock(&g_sound.mix_lock);
    if (needed > g_sound.mix_buffer_size) {
        if (g_sound.mix_buffer) {
            kfree(g_sound.mix_buffer);
        }
        g_sound.mix_buffer = (uint8_t *)kmalloc(needed);
        if (!g_sound.mix_buffer) {
            g_sound.mix_buffer_size = 0;
            spinlock_unlock(&g_sound.mix_lock);
            return;
        }
        g_sound.mix_buffer_size = needed;
    }

    /* Clear mix buffer (silence) */
    memset(g_sound.mix_buffer, 0, g_sound.mix_buffer_size);

    int16_t *mix = (int16_t *)g_sound.mix_buffer;
    uint32_t num_samples = g_sound.mix_buffer_size / 2;  /* Samples in S16 */
    uint32_t num_frames  = num_samples / 2;              /* Stereo frames */

    uint32_t has_active = 0;
    int i;
    for (i = 0; i < SOUND_MAX_VOICES; i++) {
        sound_voice_t *v = &g_sound.voices[i];
        if (!v->active || !v->buffer || v->buffer_size == 0) continue;

        has_active = 1;

        /* Calculate resampling ratio */
        float ratio = (float)v->sample_rate / 44100.0f;

        /* Per-channel volume with pan */
        float vol_left  = ((float)v->volume / 255.0f) * ((float)(255 - v->pan) / 128.0f);
        float vol_right = ((float)v->volume / 255.0f) * ((float)v->pan / 128.0f);

        /* Clamp pan gain to 1.0f */
        if (vol_left  > 1.0f) vol_left  = 1.0f;
        if (vol_right > 1.0f) vol_right = 1.0f;

        uint32_t f;
        for (f = 0; f < num_frames; f++) {
            /* Source frame position */
            float src_pos_f = v->position_frac;
            uint32_t src_pos = (uint32_t)src_pos_f;

            /* Determine bytes per source frame */
            uint32_t src_bytes_per_sample = (v->bits > 8) ? 2 : 1;
            uint32_t src_frame_size = src_bytes_per_sample * v->channels;
            uint32_t src_max_frame  = v->buffer_size / src_frame_size;

            if (src_pos >= src_max_frame) {
                if (v->loop && v->buffer_size > 0) {
                    src_pos = 0;
                    v->position = 0;
                    v->position_frac = 0.0f;
                } else {
                    v->buffer = NULL;
                    v->buffer_size = 0;
                    break;
                }
            }

            /* Read source sample(s) */
            int32_t s_left = 0, s_right = 0;
            if (v->bits == 8) {
                const uint8_t *sp = (const uint8_t *)v->buffer + src_pos * src_frame_size;
                if (v->channels == 2) {
                    s_left  = ((int32_t)sp[0] - 128) << 8;
                    s_right = ((int32_t)sp[1] - 128) << 8;
                } else {
                    s_left = s_right = ((int32_t)sp[0] - 128) << 8;
                }
            } else {
                const int16_t *sp = (const int16_t *)(v->buffer + src_pos * src_frame_size);
                if (v->channels == 2) {
                    s_left  = (int32_t)sp[0];
                    s_right = (int32_t)sp[1];
                } else {
                    s_left = s_right = (int32_t)sp[0];
                }
            }

            /* Apply volume and pan */
            int32_t mix_l = mix[f * 2];
            int32_t mix_r = mix[f * 2 + 1];

            mix_l += (int32_t)((float)s_left  * vol_left);
            mix_r += (int32_t)((float)s_right * vol_right);

            /* Clamp to 16-bit range */
            if (mix_l > 32767)  mix_l = 32767;
            if (mix_l < -32768) mix_l = -32768;
            if (mix_r > 32767)  mix_r = 32767;
            if (mix_r < -32768) mix_r = -32768;

            mix[f * 2]     = (int16_t)mix_l;
            mix[f * 2 + 1] = (int16_t)mix_r;

            /* Advance source position */
            v->position_frac += ratio;
            v->position = (uint32_t)v->position_frac;
        }
    }

    spinlock_unlock(&g_sound.mix_lock);

    /* Submit mixed data to default device if any voices were active */
    if (has_active) {
        sound_play(0, g_sound.mix_buffer, g_sound.mix_buffer_size, 44100, 2, 16);
    }
}

/* ==========================================================================
 * Legacy Compatibility
 * ========================================================================== */

void sound_write_dsp(const void *data, uint32_t size)
{
    /* Forward to default device playback */
    sound_play(0, data, size, 44100, 2, 16);
}