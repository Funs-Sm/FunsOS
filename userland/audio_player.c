#include "user_syscall.h"
#include "string.h"

static char buf[4096];

int main(int argc, char *argv[]) {
    if (argc < 2) {
        sys_write(1, "Usage: audio_player <file.wav>\n", 30);
        return 1;
    }

    int fd = sys_open(argv[1], 0);
    if (fd < 0) {
        sys_write(1, "audio_player: cannot open file\n", 30);
        return 1;
    }

    int n = sys_read(fd, buf, 44);
    if (n < 44) {
        sys_write(1, "audio_player: invalid WAV file\n", 30);
        sys_close(fd);
        return 1;
    }

    if (buf[0] != 'R' || buf[1] != 'I' || buf[2] != 'F' || buf[3] != 'F') {
        sys_write(1, "audio_player: not a RIFF file\n", 30);
        sys_close(fd);
        return 1;
    }

    if (buf[8] != 'W' || buf[9] != 'A' || buf[10] != 'V' || buf[11] != 'E') {
        sys_write(1, "audio_player: not a WAVE file\n", 30);
        sys_close(fd);
        return 1;
    }

    uint32_t fmt_offset = 12;
    uint32_t data_offset = 0;
    uint32_t data_size = 0;
    uint16_t audio_format = 0;
    uint16_t num_channels = 0;
    uint32_t sample_rate = 0;
    uint16_t bits_per_sample = 0;

    while (fmt_offset < 4096 - 8) {
        uint32_t chunk_id = *(uint32_t *)(buf + fmt_offset);
        uint32_t chunk_size = *(uint32_t *)(buf + fmt_offset + 4);

        if (chunk_id == 0x20746D66) {
            audio_format = *(uint16_t *)(buf + fmt_offset + 8);
            num_channels = *(uint16_t *)(buf + fmt_offset + 10);
            sample_rate = *(uint32_t *)(buf + fmt_offset + 12);
            bits_per_sample = *(uint16_t *)(buf + fmt_offset + 22);
        } else if (chunk_id == 0x61746164) {
            data_offset = fmt_offset + 8;
            data_size = chunk_size;
            break;
        }

        fmt_offset += 8 + chunk_size;
        if (chunk_size & 1) fmt_offset++;
    }

    if (data_offset == 0) {
        sys_write(1, "audio_player: no data chunk found\n", 34);
        sys_close(fd);
        return 1;
    }

    int dsp_fd = sys_open("/dev/dsp", 2);
    if (dsp_fd < 0) {
        sys_write(1, "audio_player: cannot open /dev/dsp\n", 35);
        sys_close(fd);
        return 1;
    }

    uint32_t offset = data_offset;
    uint32_t remaining = data_size;

    while (remaining > 0) {
        uint32_t to_read = remaining > 4096 ? 4096 : remaining;
        int rd = sys_read(fd, buf, to_read);
        if (rd <= 0) break;

        sys_write(dsp_fd, buf, rd);
        remaining -= rd;
        offset += rd;
    }

    sys_close(dsp_fd);
    sys_close(fd);

    sys_write(1, "audio_player: playback complete\n", 31);
    return 0;
}
