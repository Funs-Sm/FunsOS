#ifndef AUDIO_HDAUDIO_H
#define AUDIO_HDAUDIO_H

#include "stdint.h"

/* Audio mixer / format / positional APIs expected by audio/sound.c and
 * kernel/syscall_impl.c.  The minimal HDA driver in drivers/audio/hdaudio.c
 * implements the playback path; the stubs in this header satisfy the
 * linker.  See the comments in drivers/audio/hdaudio.c for details. */

int  hdaudio_init(uint8_t bus, uint8_t dev, uint8_t func);
int  hdaudio_play(const int16_t *samples, uint32_t frames,
                   uint32_t sample_rate, uint8_t channels);
int  hdaudio_record(const int16_t *samples, uint32_t frames,
                   uint32_t sample_rate, uint8_t channels);
void hdaudio_stop(void);
int  hdaudio_is_playing(void);
int  hdaudio_set_volume(uint8_t left, uint8_t right);
int  hdaudio_get_volume(uint8_t *left, uint8_t *right);
int  hdaudio_set_mute(int mute);
int  hdaudio_get_mute(void);
int  hdaudio_set_format(uint16_t fmt);
int  hdaudio_set_sample_rate(uint32_t rate);
uint32_t hdaudio_get_buffer_position(void);
int  hdaudio_is_available(void);
void *hdaudio_get_controller(void);

#endif
