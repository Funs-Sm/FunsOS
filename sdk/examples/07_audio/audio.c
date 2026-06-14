/* audio.c - Audio playback example
 * Demonstrates playing audio on FUNSOS.
 */

#include "funsos.h"

int main(void)
{
    funsos_window_t win = funsos_create_window(150, 100, 500, 350, "Audio Demo");
    funsos_fill_window(win, 0x2D2D2D);

    funsos_color_t white = {0xFF, 0xFF, 0xFF, 0xFF};
    funsos_color_t green = {0x00, 0xFF, 0x00, 0xFF};
    funsos_color_t blue  = {0x00, 0x78, 0xD4, 0xFF};

    funsos_draw_text(win, 20, 20, "FUNSOS Audio Demo", white);

    /* Initialize audio system */
    int audio_ok = funsos_audio_init();
    if (audio_ok == 0) {
        funsos_draw_text(win, 20, 60, "Audio initialized", green);

        /* Get device info */
        funsos_audio_device_t dev;
        if (funsos_audio_get_device(0, &dev) == 0) {
            funsos_draw_text(win, 20, 90, "Audio device found", green);
        }

        /* Set volume */
        funsos_audio_set_volume(0, 75, 75);
        funsos_draw_text(win, 20, 120, "Volume set to 75%", white);

        /* Play a WAV file */
        funsos_draw_text(win, 20, 160, "Press SPACE to play audio", blue);
        funsos_draw_text(win, 20, 190, "Press S to stop", blue);
        funsos_draw_text(win, 20, 220, "Press UP/DOWN to adjust volume", blue);
    } else {
        funsos_draw_text(win, 20, 60, "Audio initialization failed", white);
    }

    /* Event loop */
    funsos_event_t event;
    uint8_t volume = 75;

    while (1) {
        if (funsos_wait_event(&event) != 0)
            continue;

        if (event.type == FUNSOS_EVENT_KEY_PRESS) {
            switch (event.key) {
            case 0x1B:  /* Escape */
                funsos_destroy_window(win);
                return 0;
            case ' ':   /* Play */
                funsos_audio_play_wav("/usr/share/sounds/test.wav");
                funsos_draw_text(win, 20, 260, "Playing...", green);
                break;
            case 's':   /* Stop */
                funsos_audio_stop(0);
                funsos_draw_text(win, 20, 260, "Stopped.       ", white);
                break;
            case 0x26:  /* Up arrow */
                volume = volume < 95 ? volume + 5 : 100;
                funsos_audio_set_volume(0, volume, volume);
                break;
            case 0x28:  /* Down arrow */
                volume = volume > 5 ? volume - 5 : 0;
                funsos_audio_set_volume(0, volume, volume);
                break;
            }
        }
    }
}
