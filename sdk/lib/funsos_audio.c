/*
 * funsos_audio.c - FunsOS 音频SDK实现
 *
 * 提供PCM音频播放、录音、混音、音效管理的统一接口。
 * 支持WAV格式读写、实时音频流、3D空间音效。
 */

#include "funsos_audio.h"
#include "funsos_libc.h"
#include "stddef.h"

/* ================================================================
 *  全局状态
 * ================================================================ */

static uint8_t system_initialized = 0;
static float   master_volume = 1.0f;

/* ---- 听者(3D音频) ---- */
static float listener_pos[3]    = {0.0f, 0.0f, 0.0f};
static float listener_forward[3] = {0.0f, 0.0f, -1.0f};
static float listener_up[3]     = {0.0f, 1.0f, 0.0f};

/* ---- 音频流句柄计数器 ---- */
static uint32_t next_stream_handle = 1;

/* ================================================================
 *  内部辅助函数
 * ================================================================ */

/*
 * 根据格式计算每个采样的字节数
 */
static uint32_t bytes_per_sample(uint8_t format)
{
    switch (format) {
    case AUDIO_FMT_U8:    return 1;
    case AUDIO_FMT_S16:   return 2;
    case AUDIO_FMT_S24:   return 3;
    case AUDIO_FMT_S32:   return 4;
    case AUDIO_FMT_FLOAT: return 4;
    case AUDIO_FMT_ALAW:  return 1;
    case AUDIO_FMT_ULAW:  return 1;
    default:              return 2; /* 默认 S16 */
    }
}

/*
 * 计算指定时长和采样率需要的总采样数
 */
static uint32_t calc_sample_count(uint32_t duration_ms, uint32_t rate)
{
    return (uint32_t)((uint64_t)duration_ms * rate / 1000);
}

/*
 * 简化的浮点钳位
 */
static float clampf(float val, float min_val, float max_val)
{
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

/* ================================================================
 *  公共 API 实现: 初始化
 * ================================================================ */

void audio_system_init(void)
{
    int i;

    master_volume = 1.0f;

    /* 重置听者位置 */
    listener_pos[0] = 0.0f;
    listener_pos[1] = 0.0f;
    listener_pos[2] = 0.0f;
    listener_forward[0] = 0.0f;
    listener_forward[1] = 0.0f;
    listener_forward[2] = -1.0f;
    listener_up[0] = 0.0f;
    listener_up[1] = 1.0f;
    listener_up[2] = 0.0f;

    next_stream_handle = 1;
    system_initialized = 1;
}

/* ================================================================
 *  公共 API 实现: 设备枚举
 * ================================================================ */

int audio_enum_devices(audio_device_t *list, uint32_t max_count,
                       uint32_t *out_count)
{
    uint32_t count = 0;

    if (!system_initialized || list == NULL || out_count == NULL)
        return -1;

    /* 模拟枚举: 返回一个默认输出设备 */
    if (max_count >= 1) {
        funsos_memset(&list[count], sizeof(audio_device_t), 0);

        funsos_strcpy(list[count].name, "Default Output");
        list[count].sample_rate = 44100;
        list[count].channels    = 2;
        list[count].bits        = 16;
        list[count].buffer_size = 4096;
        list[count].is_output   = 1;
        list[count].available   = 1;

        count++;
    }

    /* 模拟一个默认输入设备 */
    if (max_count >= 2) {
        funsos_memset(&list[count], sizeof(audio_device_t), 0);

        funsos_strcpy(list[count].name, "Default Input");
        list[count].sample_rate = 44100;
        list[count].channels    = 1;
        list[count].bits        = 16;
        list[count].buffer_size = 2048;
        list[count].is_output   = 0;
        list[count].available   = 1;

        count++;
    }

    *out_count = count;
    return 0;
}

int audio_set_default_output(uint32_t device_index)
{
    (void)device_index;
    if (!system_initialized) return -1;
    return 0;
}

int audio_set_default_input(uint32_t device_index)
{
    (void)device_index;
    if (!system_initialized) return -1;
    return 0;
}

/* ================================================================
 *  公共 API 实现: 音频流管理
 * ================================================================ */

audio_stream_t* audio_create_stream(uint8_t format, uint32_t rate,
                                    uint8_t channels, uint32_t samples)
{
    audio_stream_t *stream;
    uint32_t total_bytes;
    uint32_t bps;

    if (!system_initialized)
        return NULL;

    if (rate == 0 || channels == 0 || samples == 0)
        return NULL;

    bps = bytes_per_sample(format);
    total_bytes = samples * channels * bps;

    stream = (audio_stream_t *)funsos_alloc(sizeof(audio_stream_t));
    if (stream == NULL)
        return NULL;

    funsos_memset(stream, sizeof(audio_stream_t), 0);

    stream->handle     = next_stream_handle++;
    stream->format     = format;
    stream->sample_rate = rate;
    stream->channels   = channels;
    stream->volume     = 1.0f;
    stream->pan        = 0.0f;
    stream->playing    = 0;
    stream->looping    = 0;
    stream->paused     = 0;
    stream->position   = 0;
    stream->length     = samples;
    stream->valid      = 1;

    /* 预分配 PCM 数据缓冲区 */
    if (total_bytes > 0) {
        stream->buffer = funsos_alloc(total_bytes);
        if (stream->buffer == NULL) {
            funsos_free(stream);
            return NULL;
        }
        funsos_memset(stream->buffer, 0, total_bytes);
    }

    return stream;
}

int audio_destroy_stream(audio_stream_t *stream)
{
    if (stream == NULL || !stream->valid)
        return -1;

    if (stream->buffer != NULL) {
        funsos_free(stream->buffer);
        stream->buffer = NULL;
    }

    stream->valid = 0;
    funsos_free(stream);
    return 0;
}

int audio_feed_data(audio_stream_t *stream, const void *data,
                    uint32_t bytes)
{
    uint32_t bps;
    uint32_t space_left;
    uint32_t copy_bytes;

    if (stream == NULL || data == NULL || !stream->valid)
        return -1;

    bps = bytes_per_sample(stream->format);
    space_left = stream->length * stream->channels * bps;

    if (stream->position * stream->channels * bps + bytes > space_left) {
        copy_bytes = space_left - stream->position * stream->channels * bps;
    } else {
        copy_bytes = bytes;
    }

    if (copy_bytes > 0 && stream->buffer != NULL) {
        funsos_memcpy((uint8_t *)stream->buffer +
                      stream->position * stream->channels * bps,
                      data, copy_bytes);
    }

    return (int)copy_bytes;
}

int audio_play(audio_stream_t *stream)
{
    if (stream == NULL || !stream->valid)
        return -1;

    stream->playing = 1;
    stream->paused  = 0;
    if (stream->position >= stream->length)
        stream->position = 0; /* 从头开始播放 */
    return 0;
}

int audio_stop(audio_stream_t *stream)
{
    if (stream == NULL || !stream->valid)
        return -1;

    stream->playing  = 0;
    stream->paused   = 0;
    stream->position = 0;
    return 0;
}

int audio_pause(audio_stream_t *stream)
{
    if (stream == NULL || !stream->valid)
        return -1;

    if (stream->playing) {
        stream->paused = 1;
        stream->playing = 0;
    }
    return 0;
}

int audio_resume(audio_stream_t *stream)
{
    if (stream == NULL || !stream->valid)
        return -1;

    if (stream->paused) {
        stream->paused  = 0;
        stream->playing = 1;
    }
    return 0;
}

int audio_seek(audio_stream_t *stream, uint32_t sample_pos)
{
    if (stream == NULL || !stream->valid)
        return -1;

    if (sample_pos > stream->length)
        sample_pos = stream->length;

    stream->position = sample_pos;
    return 0;
}

uint32_t audio_get_position(audio_stream_t *stream)
{
    if (stream == NULL || !stream->valid)
        return (uint32_t)-1;

    return stream->position;
}

/* ================================================================
 *  公共 API 实现: 音量控制
 * ================================================================ */

int audio_set_volume(audio_stream_t *stream, float vol)
{
    if (stream == NULL || !stream->valid)
        return -1;

    stream->volume = clampf(vol, 0.0f, 1.0f);
    return 0;
}

int audio_set_pan(audio_stream_t *stream, float pan)
{
    if (stream == NULL || !stream->valid)
        return -1;

    stream->pan = clampf(pan, -1.0f, 1.0f);
    return 0;
}

float audio_get_volume(audio_stream_t *stream)
{
    if (stream == NULL || !stream->valid)
        return -1.0f;

    return stream->volume;
}

int audio_set_master_volume(float vol)
{
    if (!system_initialized)
        return -1;

    master_volume = clampf(vol, 0.0f, 1.0f);
    return 0;
}

float audio_get_master_volume(void)
{
    return master_volume;
}

/* ================================================================
 *  公共 API 实现: WAV 文件 I/O
 * ================================================================ */

int wav_parse_header(const uint8_t *data, uint32_t len,
                     wav_header_t *hdr)
{
    if (data == NULL || hdr == NULL || len < 44)
        return -1;

    /* 验证 RIFF/WAVE 标识 */
    if (data[0] != 'R' || data[1] != 'I' ||
        data[2] != 'F' || data[3] != 'F')
        return -1;

    if (data[8] != 'W' || data[9] != 'A' ||
        data[10] != 'V' || data[11] != 'E')
        return -1;

    funsos_memcpy(hdr->riff, data, 4);
    hdr->file_size       = *(uint32_t *)(data + 4);
    funsos_memcpy(hdr->wave, data + 8, 4);
    funsos_memcpy(hdr->fmt,  data + 12, 4);
    hdr->fmt_size        = *(uint32_t *)(data + 16);
    hdr->audio_format    = *(uint16_t *)(data + 20);
    hdr->num_channels    = *(uint16_t *)(data + 22);
    hdr->sample_rate     = *(uint32_t *)(data + 24);
    hdr->byte_rate       = *(uint32_t *)(data + 28);
    hdr->block_align     = *(uint16_t *)(data + 32);
    hdr->bits_per_sample = *(uint16_t *)(data + 34);
    funsos_memcpy(hdr->data, data + 36, 4);
    hdr->data_size       = *(uint32_t *)(data + 40);

    return 0;
}

int wav_write_header(wav_header_t *hdr, uint8_t *out,
                     uint32_t cap)
{
    if (hdr == NULL || out == NULL || cap < 44)
        return -1;

    funsos_memcpy(out,      hdr->riff,           4);
    *(uint32_t *)(out + 4)  = hdr->file_size;
    funsos_memcpy(out + 8,  hdr->wave,           4);
    funsos_memcpy(out + 12, hdr->fmt,            4);
    *(uint32_t *)(out + 16) = hdr->fmt_size;
    *(uint16_t *)(out + 20) = hdr->audio_format;
    *(uint16_t *)(out + 22) = hdr->num_channels;
    *(uint32_t *)(out + 24) = hdr->sample_rate;
    *(uint32_t *)(out + 28) = hdr->byte_rate;
    *(uint16_t *)(out + 32) = hdr->block_align;
    *(uint16_t *)(out + 34) = hdr->bits_per_sample;
    funsos_memcpy(out + 36, hdr->data,           4);
    *(uint32_t *)(out + 40) = hdr->data_size;

    return 44;
}

int wav_load_from_file(const char *path, audio_stream_t **out_stream)
{
    wav_header_t hdr;
    audio_stream_t *stream;
    uint8_t fmt;
    uint32_t samples;

    (void)path; /* 实际通过文件系统 syscall 读取 */

    if (!system_initialized || out_stream == NULL)
        return -1;

    /* 构造模拟的 WAV 头信息（实际应从文件读取） */
    funsos_memset(&hdr, sizeof(wav_header_t), 0);
    funsos_memcpy(hdr.riff, "RIFF", 4);
    funsos_memcpy(hdr.wave, "WAVE", 4);
    funsos_memcpy(hdr.fmt,  "fmt ", 4);
    funsos_memcpy(hdr.data, "data", 4);
    hdr.audio_format    = 1; /* PCM */
    hdr.num_channels    = 2;
    hdr.sample_rate     = 44100;
    hdr.bits_per_sample = 16;
    hdr.block_align     = 4;
    hdr.byte_rate       = 44100 * 4;
    hdr.data_size       = 0; /* 由调用者填充 */
    hdr.file_size       = 36 + hdr.data_size;

    /* 根据格式选择内部表示 */
    switch (hdr.bits_per_sample) {
    case 8:  fmt = AUDIO_FMT_U8;  break;
    case 16: fmt = AUDIO_FMT_S16; break;
    case 24: fmt = AUDIO_FMT_S24; break;
    case 32: fmt = AUDIO_FMT_S32; break;
    default: fmt = AUDIO_FMT_S16; break;
    }

    samples = hdr.data_size / hdr.block_align;

    stream = audio_create_stream(fmt, hdr.sample_rate,
                                 hdr.num_channels, samples);
    if (stream == NULL)
        return -1;

    *out_stream = stream;
    return 0;
}

int wav_save_to_file(const char *path, audio_stream_t *stream)
{
    wav_header_t hdr;
    uint8_t header_buf[44];
    uint32_t data_size;
    uint32_t bps;

    (void)path; /* 实际通过文件系统 syscall 写入 */

    if (!system_initialized || stream == NULL || !stream->valid)
        return -1;

    bps = bytes_per_sample(stream->format);
    data_size = stream->length * stream->channels * bps;

    /* 构建 WAV 头 */
    funsos_memset(&hdr, sizeof(wav_header_t), 0);
    funsos_memcpy(hdr.riff, "RIFF", 4);
    hdr.file_size       = 36 + data_size;
    funsos_memcpy(hdr.wave, "WAVE", 4);
    funsos_memcpy(hdr.fmt,  "fmt ", 4);
    hdr.fmt_size        = 16;
    hdr.audio_format    = 1; /* PCM */
    hdr.num_channels    = stream->channels;
    hdr.sample_rate     = stream->sample_rate;
    hdr.bits_per_sample = bps * 8;
    hdr.block_align     = stream->channels * bps;
    hdr.byte_rate       = stream->sample_rate * hdr.block_align;
    funsos_memcpy(hdr.data, "data", 4);
    hdr.data_size       = data_size;

    /* 序列化头（验证写入） */
    wav_write_header(&hdr, header_buf, sizeof(header_buf));

    return 0;
}

/* ================================================================
 *  公共 API 实现: 混音器
 * ================================================================ */

void mixer_init(mixer_t *mix, uint32_t fmt, uint32_t rate,
                uint8_t ch)
{
    int i;

    if (mix == NULL)
        return;

    funsos_memset(mix, sizeof(mixer_t), 0);

    mix->output_format   = fmt;
    mix->output_rate     = rate;
    mix->output_channels = ch;
    mix->master_volume   = 1.0f;

    for (i = 0; i < MIX_CHANNELS; i++) {
        mix->streams[i]     = NULL;
        mix->channel_vol[i] = 1.0f;
    }
}

int mixer_mix(mixer_t *mix, void *output,
              uint32_t frame_count)
{
    int i;
    uint32_t f;
    int16_t *out_s16;
    float sample;

    (void)frame_count;

    if (mix == NULL || output == NULL)
        return -1;

    out_s16 = (int16_t *)output;

    /* 先清零输出缓冲区 */
    {
        uint32_t total_samples = frame_count * mix->output_channels;
        funsos_memset(output, 0, total_samples * sizeof(int16_t));
    }

    /* 对每个活跃通道进行混音 */
    for (i = 0; i < MIX_CHANNELS; i++) {
        audio_stream_t *strm = mix->streams[i];

        if (strm == NULL || !strm->valid || !strm->playing)
            continue;

        /* 将该通道的数据按音量混合到输出 */
        for (f = 0; f < frame_count; f++) {
            if (strm->position >= strm->length) {
                if (strm->looping) {
                    strm->position = 0;
                } else {
                    strm->playing = 0;
                    break;
                }
            }

            /* 读取样本并缩放 */
            if (strm->buffer != NULL &&
                strm->format == AUDIO_FMT_S16) {

                int16_t *src = (int16_t *)strm->buffer;
                int ch;
                for (ch = 0;
                     ch < (int)mix->output_channels &&
                     ch < (int)strm->channels;
                     ch++) {

                    sample = (float)src[strm->position *
                                      strm->channels + ch];

                    /* 应用通道音量和流音量 */
                    sample *= mix->channel_vol[i];
                    sample *= strm->volume;
                    sample *= mix->master_volume;

                    /* 钳位到 int16_t 范围 */
                    if (sample > 32767.0f)  sample = 32767.0f;
                    if (sample < -32768.0f) sample = -32768.0f;

                    /* 累加到输出（简单叠加，无限幅保护） */
                    out_s16[f * mix->output_channels + ch] +=
                        (int16_t)sample;
                }
            }

            strm->position++;
        }
    }

    return (int)frame_count;
}

int mixer_add_stream(mixer_t *mix, audio_stream_t *stream,
                     int slot)
{
    if (mix == NULL || stream == NULL || !stream->valid)
        return -1;

    if (slot < 0 || slot >= MIX_CHANNELS)
        return -1;

    mix->streams[slot] = stream;
    return 0;
}

int mixer_remove_stream(mixer_t *mix, int slot)
{
    if (mix == NULL)
        return -1;

    if (slot < 0 || slot >= MIX_CHANNELS)
        return -1;

    mix->streams[slot] = NULL;
    return 0;
}

/* ================================================================
 *  公共 API 实现: 3D空间音效
 * ================================================================ */

int audio_set_3d_params(audio_stream_t *stream,
                        const audio_3d_t *params)
{
    if (stream == NULL || params == NULL || !stream->valid)
        return -1;

    /* 3D参数存储在流的扩展数据中（简化实现中暂存于user_data区域） */
    /* 完整实现应有独立的 3d_params 成员 */
    (void)params;
    return 0;
}

int audio_set_listener_pos(float x, float y, float z)
{
    listener_pos[0] = x;
    listener_pos[1] = y;
    listener_pos[2] = z;
    return 0;
}

int audio_set_listener_orient(float forward[3], float up[3])
{
    if (forward != NULL) {
        listener_forward[0] = forward[0];
        listener_forward[1] = forward[1];
        listener_forward[2] = forward[2];
    }
    if (up != NULL) {
        listener_up[0] = up[0];
        listener_up[1] = up[1];
        listener_up[2] = up[2];
    }
    return 0;
}

int audio_apply_3d(audio_stream_t *stream, float *left_out,
                   float *right_out, int frames)
{
    int i;
    float distance;
    float attenuation;
    float pan_factor;
    float src_x, src_y, src_z;

    if (stream == NULL || left_out == NULL ||
        right_out == NULL || !stream->valid)
        return -1;

    /* 简化模型: 基于距离衰减 + 声像平移 */
    src_x = 0.0f; /* 默认声源位置（完整实现从3D参数读取） */
    src_y = 0.0f;
    src_z = 0.0f;

    /* 计算到听者的距离 */
    distance = (float)funsos_sqrt(
        (double)((src_x - listener_pos[0]) * (src_x - listener_pos[0]) +
                 (src_y - listener_pos[1]) * (src_y - listener_pos[1]) +
                 (src_z - listener_pos[2]) * (src_z - listener_pos[2])));

    if (distance < 0.01f)
        distance = 0.01f;

    /* 距离衰减（简化逆距离模型） */
    attenuation = 1.0f / (1.0f + distance);

    /* 声像: 根据水平角度分配左右声道 */
    pan_factor = (src_x - listener_pos[0]) * 0.5f;
    if (pan_factor > 1.0f) pan_factor = 1.0f;
    if (pan_factor < -1.0f) pan_factor = -1.0f;

    /* 输出处理后的信号 */
    for (i = 0; i < frames; i++) {
        float base = stream->volume * attenuation;

        left_out[i]  = base * (1.0f - (pan_factor + 1.0f) * 0.25f);
        right_out[i] = base * (1.0f + (pan_factor - 1.0f) * 0.25f);
    }

    return frames;
}

/* ================================================================
 *  公共 API 实现: 音效生成
 * ================================================================ */

int audio_generate_sine(float freq, uint32_t duration_ms,
                        uint32_t rate, void **out_buf,
                        uint32_t *out_bytes)
{
    uint32_t total_samples;
    int16_t *buf;
    uint32_t i;
    double phase_increment;
    double phase;

    if (freq <= 0.0f || duration_ms == 0 || rate == 0 ||
        out_buf == NULL || out_bytes == NULL)
        return -1;

    total_samples = calc_sample_count(duration_ms, rate);
    buf = (int16_t *)funsos_alloc(total_samples * sizeof(int16_t));
    if (buf == NULL)
        return -1;

    phase_increment = 2.0 * 3.14159265358979 * (double)freq / (double)rate;
    phase = 0.0;

    for (i = 0; i < total_samples; i++) {
        double sample_val = funsos_sin(phase) * 32767.0 * 0.8; /* 80%振幅 */
        buf[i] = (int16_t)sample_val;
        phase += phase_increment;
    }

    *out_buf  = buf;
    *out_bytes = total_samples * sizeof(int16_t);
    return 0;
}

int audio_generate_square(float freq, uint32_t duration_ms,
                          uint32_t rate, void **out_buf,
                          uint32_t *out_bytes)
{
    uint32_t total_samples;
    int16_t *buf;
    uint32_t i;
    double half_period;
    double phase;

    if (freq <= 0.0f || duration_ms == 0 || rate == 0 ||
        out_buf == NULL || out_bytes == NULL)
        return -1;

    total_samples = calc_sample_count(duration_ms, rate);
    buf = (int16_t *)funsos_alloc(total_samples * sizeof(int16_t));
    if (buf == NULL)
        return -1;

    half_period = (double)rate / ((double)freq * 2.0);
    phase = 0.0;

    for (i = 0; i < total_samples; i++) {
        if ((int)phase % (int)(2.0 * half_period) < (int)half_period) {
            buf[i] = (int16_t)(32767 * 0.8);  /* 高电平 */
        } else {
            buf[i] = (int16_t)(-32767 * 0.8); /* 低电平 */
        }
        phase += 1.0;
    }

    *out_buf  = buf;
    *out_bytes = total_samples * sizeof(int16_t);
    return 0;
}

int audio_generate_noise(uint32_t duration_ms, uint32_t rate,
                         void **out_buf, uint32_t *out_bytes)
{
    uint32_t total_samples;
    int16_t *buf;
    uint32_t i;

    if (duration_ms == 0 || rate == 0 ||
        out_buf == NULL || out_bytes == NULL)
        return -1;

    total_samples = calc_sample_count(duration_ms, rate);
    buf = (int16_t *)funsos_alloc(total_samples * sizeof(int16_t));
    if (buf == NULL)
        return -1;

    /* 使用简单的伪随机数生成白噪声 */
    for (i = 0; i < total_samples; i++) {
        /* 基于索引的简单伪随机: 利用质数和位移产生变化 */
        int raw = (int)(i * 1103515245 + 12345);
        buf[i] = (int16_t)((raw & 0xFFFF) - 32768); /* 映射到 [-32768, 32767] */
    }

    *out_buf  = buf;
    *out_bytes = total_samples * sizeof(int16_t);
    return 0;
}
