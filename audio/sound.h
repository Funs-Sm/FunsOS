#ifndef SOUND_H
#define SOUND_H

#include "stdint.h"
#include "../kernel/sync.h"

/* ============================================================================
 * FunsOS Unified Audio Subsystem
 *
 * Provides a single interface for all audio hardware (AC'97, SB16, HDAudio).
 * Features:
 *   - Device abstraction with registration, lookup, and enumeration
 *   - Unified PCM API (playback, capture, pause/resume)
 *   - Full mixer API (master, PCM, mic, line, CD, FM, aux, record)
 *   - Buffer management with ring-buffer semantics
 *   - Format conversion (U8/S16/S24/S32, mono/stereo)
 *   - Software voice mixing (up to 8 simultaneous voices)
 *   - Spinlock-based thread safety
 *   - Automatic hardware detection and driver initialization
 *
 * Usage:
 *   sound_init()                    -- detect and init all hardware
 *   sound_play(0, buf, len, 44100, 2, 16) -- play on first device
 *   sound_set_volume(0, 200, 200)  -- set volume
 *   sound_stop(0)                   -- stop playback
 * ============================================================================
 */

/* --- Device Types --- */
#define SOUND_DEV_TYPE_NONE     0x00
#define SOUND_DEV_TYPE_AC97     0x01
#define SOUND_DEV_TYPE_SB16     0x02
#define SOUND_DEV_TYPE_HDA      0x03

/* --- Device Capabilities (bitmask) --- */
#define SOUND_CAP_PLAYBACK      (1 << 0)
#define SOUND_CAP_CAPTURE       (1 << 1)
#define SOUND_CAP_MIXER         (1 << 2)
#define SOUND_CAP_3D            (1 << 3)
#define SOUND_CAP_POWER_MGMT    (1 << 4)
#define SOUND_CAP_HIGH_RATE     (1 << 5)   /* Supports rates > 48 kHz */
#define SOUND_CAP_SOFTWARE_MIX  (1 << 6)   /* Supports software voice mixing */
#define SOUND_CAP_FM_SYNTH      (1 << 7)   /* OPL3 FM synthesis */
#define SOUND_CAP_MIDI          (1 << 8)   /* MIDI MPU-401 */
#define SOUND_CAP_HDMI          (1 << 9)   /* HDMI/DisplayPort output */
#define SOUND_CAP_24BIT         (1 << 10)  /* 24/32-bit support */
#define SOUND_CAP_JACK_SENSE    (1 << 11)  /* Jack presence detection */

/* --- PCM Format Constants --- */
#define SOUND_FMT_PCM_U8        0
#define SOUND_FMT_PCM_S16LE     1
#define SOUND_FMT_PCM_S24LE     2
#define SOUND_FMT_PCM_S32LE     3

/* --- Mixer Control Identifiers (matching OSS/ALSA conventions) --- */
#define SOUND_MIXER_MASTER       0x00  /* Master volume */
#define SOUND_MIXER_PCM          0x01  /* PCM/DAC volume */
#define SOUND_MIXER_MIC          0x02  /* Microphone volume */
#define SOUND_MIXER_LINE_IN      0x03  /* Line-in volume */
#define SOUND_MIXER_CD           0x04  /* CD audio volume */
#define SOUND_MIXER_FM           0x05  /* FM synthesis volume */
#define SOUND_MIXER_AUX          0x06  /* Aux volume */
#define SOUND_MIXER_BEEP         0x07  /* PC beep/speaker volume */
#define SOUND_MIXER_BASS         0x08  /* Bass tone control */
#define SOUND_MIXER_TREBLE       0x09  /* Treble tone control */
#define SOUND_MIXER_PHONE        0x0A  /* Phone volume */
#define SOUND_MIXER_VIDEO        0x0B  /* Video volume */
#define SOUND_MIXER_MONO_OUT     0x0C  /* Mono output volume */
#define SOUND_MIXER_REC_SRC      0x0D  /* Recording source selector */
#define SOUND_MIXER_REC_GAIN     0x0E  /* Recording gain */
#define SOUND_MIXER_3D_DEPTH     0x0F  /* 3D stereo enhancement depth */
#define SOUND_MIXER_INPUT_GAIN   0x10  /* Input gain */
#define SOUND_MIXER_OUTPUT_GAIN  0x11  /* Output gain */

/* --- Volume Range --- */
#define SOUND_VOLUME_MIN         0
#define SOUND_VOLUME_MAX         255
#define SOUND_VOLUME_MUTE        0
#define SOUND_VOLUME_0DB         128   /* Approximate 0dB on linear scale */

/* --- Common Sample Rates --- */
#define SOUND_RATE_8000          8000
#define SOUND_RATE_11025         11025
#define SOUND_RATE_16000         16000
#define SOUND_RATE_22050         22050
#define SOUND_RATE_32000         32000
#define SOUND_RATE_44100         44100
#define SOUND_RATE_48000         48000
#define SOUND_RATE_88200         88200
#define SOUND_RATE_96000         96000
#define SOUND_RATE_176400        176400
#define SOUND_RATE_192000        192000

/* --- PCM Stream States --- */
#define SOUND_PCM_STATE_STOPPED  0
#define SOUND_PCM_STATE_RUNNING  1
#define SOUND_PCM_STATE_PAUSED   2

/* --- Software Mixing --- */
#define SOUND_MAX_VOICES         8
#define SOUND_VOICE_FREE         0xFF

/* --- Device Limits --- */
#define SOUND_MAX_DEVICES        8
#define SOUND_MAX_NAME_LEN       64

/* ============================================================================
 * Sound Device Structure
 * ============================================================================
 */
typedef struct sound_device {
    /* Identification */
    char     name[SOUND_MAX_NAME_LEN];
    uint32_t type;              /* SOUND_DEV_TYPE_* */
    uint32_t caps;              /* Capability bitmask */

    /* Current audio configuration */
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint16_t format;            /* SOUND_FMT_* */

    /* Private data (points to driver-specific struct) */
    void    *private_data;

    /* ---- Core PCM Operations ---- */
    /* Playback: return 0 on success, negative on error */
    int (*play)(void *priv, const void *data, uint32_t size,
                uint32_t rate, uint16_t channels);
    /* Capture: return 0 on success, negative on error */
    int (*record)(void *priv, void *data, uint32_t size,
                  uint32_t rate, uint16_t channels);
    /* Stop all streams */
    void (*stop)(void *priv);
    /* Stop playback only */
    void (*stop_playback)(void *priv);
    /* Stop capture only */
    void (*stop_capture)(void *priv);

    /* ---- Volume / Mute Operations ---- */
    /* Set master volume: 0-255 per channel, return 0 on success */
    int (*set_volume)(void *priv, uint8_t left, uint8_t right);
    /* Get master volume, return 0 on success */
    int (*get_volume)(void *priv, uint8_t *left, uint8_t *right);
    /* Set mute: 1=mute, 0=unmute */
    int (*set_mute)(void *priv, uint8_t mute);
    /* Get mute: 1=muted, 0=unmuted */
    int (*get_mute)(void *priv);

    /* ---- Mixer Operations ---- */
    /* Set a mixer control level, return 0 on success */
    int (*set_mixer)(void *priv, uint32_t control, uint8_t left, uint8_t right);
    /* Get a mixer control level, return 0 on success */
    int (*get_mixer)(void *priv, uint32_t control, uint8_t *left, uint8_t *right);

    /* ---- Format Operations ---- */
    int (*set_format)(void *priv, uint16_t bits, uint16_t channels);
    int (*set_sample_rate)(void *priv, uint32_t rate);

    /* ---- Position / Status ---- */
    uint32_t (*get_position)(void *priv);

    /* Per-device lock for thread safety */
    spinlock_t lock;
} sound_device_t;

/* ============================================================================
 * Software Voice (for software mixing)
 * ============================================================================
 */
typedef struct sound_voice {
    uint8_t   active;
    uint8_t   *buffer;
    uint32_t  buffer_size;
    uint32_t  position;
    float     position_frac;       /* Fractional position for resampling */
    uint32_t  sample_rate;
    uint16_t  channels;
    uint16_t  bits;
    uint8_t   volume;              /* 0-255 */
    uint8_t   pan;                 /* 0=left, 128=center, 255=right */
    uint8_t   loop;
} sound_voice_t;

/* ============================================================================
 * Subsystem State
 * ============================================================================
 */
typedef struct sound_subsystem {
    sound_device_t *devices[SOUND_MAX_DEVICES];
    uint32_t        device_count;
    spinlock_t      lock;          /* Global subsystem lock */

    /* Software mixing */
    sound_voice_t   voices[SOUND_MAX_VOICES];
    uint8_t         *mix_buffer;
    uint32_t        mix_buffer_size;
    spinlock_t      mix_lock;

    /* Initialization state */
    uint8_t         initialized;
} sound_subsystem_t;

/* ============================================================================
 * Public API
 * ============================================================================
 */

/* --- Initialization --- */
/* Initialize the audio subsystem. Scans for hardware, initializes drivers,
 * and registers available devices. Call once at boot. */
void sound_init(void);

/* Manually register an audio device (called by drivers internally).
 * Returns device ID (>=0) on success, -1 on error. */
int sound_register_device(sound_device_t *dev);

/* Enumerate all hardware: probes PCI for AC'97/HDA, ISA for SB16,
 * initializes detected drivers, and registers devices.
 * Returns number of devices found. */
uint32_t sound_device_enumerate(void);

/* --- Device Access --- */
/* Get number of registered audio devices */
uint32_t sound_get_device_count(void);

/* Get device by index (0 = first / default device) */
sound_device_t *sound_get_device(uint32_t index);

/* Get first device of a given type */
sound_device_t *sound_get_device_by_type(uint32_t type);

/* Get the default (best available) device */
sound_device_t *sound_get_default_device(void);

/* Get device name */
const char *sound_get_device_name(uint32_t dev_id);

/* Get device type string (e.g. "HDA", "AC'97", "SB16") */
const char *sound_get_device_type_string(uint32_t dev_id);

/* --- PCM Playback --- */
/* Play audio data on a device.
 *   dev_id   : device index (0 = default)
 *   data     : PCM data buffer
 *   size     : data size in bytes
 *   rate     : sample rate in Hz (e.g. 44100)
 *   channels : 1 for mono, 2 for stereo
 *   bits     : bits per sample (8, 16, 24, 32)
 * Returns 0 on success, negative on error. */
int sound_play(uint32_t dev_id, const void *data, uint32_t size,
               uint32_t rate, uint16_t channels, uint16_t bits);

/* --- PCM Capture --- */
/* Record audio data from a device.
 * Returns 0 on success, negative on error. */
int sound_record(uint32_t dev_id, void *data, uint32_t size,
                 uint32_t rate, uint16_t channels, uint16_t bits);

/* --- Stream Control --- */
/* Stop all streams on a device */
int sound_stop(uint32_t dev_id);

/* Stop playback only */
int sound_stop_playback(uint32_t dev_id);

/* Stop capture only */
int sound_stop_capture(uint32_t dev_id);

/* Pause playback on a device */
int sound_pause(uint32_t dev_id);

/* Resume playback on a device */
int sound_resume(uint32_t dev_id);

/* --- Volume Control --- */
/* Set master volume (0-255 per channel). Returns 0 on success. */
int sound_set_volume(uint32_t dev_id, uint8_t left, uint8_t right);

/* Get master volume. Returns 0 on success. */
int sound_get_volume(uint32_t dev_id, uint8_t *left, uint8_t *right);

/* Mute/unmute a device */
int sound_set_mute(uint32_t dev_id, uint8_t mute);

/* Get mute state (1=muted, 0=unmuted) */
int sound_get_mute(uint32_t dev_id);

/* --- Mixer Controls --- */
/* Set a specific mixer control level.
 *   control: SOUND_MIXER_* identifier
 *   left, right: 0-255 volume (mono controls use average of left+right)
 * Returns 0 on success. */
int sound_set_mixer(uint32_t dev_id, uint32_t control, uint8_t left, uint8_t right);

/* Get a specific mixer control level. Returns 0 on success. */
int sound_get_mixer(uint32_t dev_id, uint32_t control, uint8_t *left, uint8_t *right);

/* --- Format Configuration --- */
/* Set audio format (bits per sample, number of channels).
 * Returns 0 on success. */
int sound_set_format(uint32_t dev_id, uint16_t bits, uint16_t channels);

/* Set sample rate. Returns 0 on success. */
int sound_set_sample_rate(uint32_t dev_id, uint32_t rate);

/* --- Status --- */
/* Get current playback buffer position in bytes */
uint32_t sound_get_buffer_position(uint32_t dev_id);

/* Check if a device is available (initialized and ready) */
int sound_is_available(uint32_t dev_id);

/* Get device capabilities bitmask */
uint32_t sound_get_capabilities(uint32_t dev_id);

/* --- Utility --- */
/* Map a 0-255 linear volume to the driver's internal range.
 * Useful for drivers that use non-linear scales (e.g. AC'97 attenuation). */
uint32_t sound_volume_map(uint8_t linear_vol, uint32_t max_val);

/* Convert between audio formats in-place or to a new buffer.
 * Copies data from src (in src_format, src_bits) to dst (in dst_format, dst_bits).
 * Returns number of bytes written to dst, or 0 on error. */
uint32_t sound_convert_format(const void *src, void *dst, uint32_t src_size,
                              uint16_t src_format, uint16_t src_chans,
                              uint16_t dst_format, uint16_t dst_chans,
                              uint16_t max_bits);

/* --- Software Voice Mixing --- */
/* Allocate a software voice. Returns voice ID (0-7) or -1 if none available. */
int sound_voice_alloc(void);

/* Free a software voice. */
void sound_voice_free(int voice_id);

/* Start playing a voice.
 *   voice_id : allocated voice ID
 *   data     : PCM data
 *   size     : data size in bytes
 *   rate     : sample rate
 *   channels : 1 or 2
 *   bits     : 8 or 16
 *   loop     : 1 to loop, 0 for one-shot
 * Returns 0 on success. */
int sound_voice_play(int voice_id, const void *data, uint32_t size,
                     uint32_t rate, uint16_t channels, uint16_t bits,
                     uint8_t loop);

/* Stop a voice. */
void sound_voice_stop(int voice_id);

/* Set voice volume (0-255). */
int sound_voice_set_volume(int voice_id, uint8_t volume);

/* Set voice stereo pan (0=left, 128=center, 255=right). */
int sound_voice_set_pan(int voice_id, uint8_t pan);

/* Mix all active voices into the output buffer and submit to the default device.
 * Called periodically (e.g. by timer interrupt or DMA callback). */
void sound_voice_mix_all(void);

/* --- Legacy Compatibility --- */
/* Direct DSP write (legacy SB16 compatibility) */
void sound_write_dsp(const void *data, uint32_t size);

#endif /* SOUND_H */