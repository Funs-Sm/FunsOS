#ifndef FUNSOS_AUDIO_SDK_H
#define FUNSOS_AUDIO_SDK_H

/*
 * funsos_audio.h - FunsOS 音频SDK头文件
 *
 * 提供PCM音频播放、录音、混音、音效管理的统一接口。
 * 支持WAV格式读写、实时音频流、3D空间音效。
 */

#include "stdint.h"
#include "stddef.h"

/* ---- 音频采样格式 ---- */
#define AUDIO_FMT_U8       0   /* 无符号8位 */
#define AUDIO_FMT_S16      1   /* 有符号16位小端 */
#define AUDIO_FMT_S24      2   /* 有符号24位 */
#define AUDIO_FMT_S32      3   /* 有符号32位 */
#define AUDIO_FMT_FLOAT    4   /* 32位浮点 */
#define AUDIO_FMT_ALAW     8   /* A律压缩 */
#define AUDIO_FMT_ULAW     9   /* u律压缩 */

/* ---- WAV 文件头结构体 ---- */
typedef struct wav_header {
    char     riff[4];       /* "RIFF" 标识 */
    uint32_t file_size;     /* 文件大小-8 */
    char     wave[4];       /* "WAVE" 标识 */
    char     fmt[4];        /* "fmt " 标识 */
    uint32_t fmt_size;      /* fmt块大小(PCM=16) */
    uint16_t audio_format;  /* 音频格式(1=PCM) */
    uint16_t num_channels;  /* 声道数 */
    uint32_t sample_rate;   /* 采样率(Hz) */
    uint32_t byte_rate;     /* 字节率 */
    uint16_t block_align;   /* 块对齐 */
    uint16_t bits_per_sample; /* 每样本位数 */
    char     data[4];       /* "data" 标识 */
    uint32_t data_size;     /* 数据大小(字节) */
} wav_header_t;

/* ---- 音频设备信息 ---- */
typedef struct audio_device {
    char     name[64];        /* 设备名称 */
    uint32_t sample_rate;     /* 采样率 */
    uint8_t  channels;        /* 声道数 */
    uint8_t  bits;            /* 位深度 */
    uint32_t buffer_size;     /* 硬件缓冲大小(字节) */
    uint8_t  is_output;       /* 是否为输出设备 */
    uint8_t  available;       /* 设备是否可用 */
} audio_device_t;

/* ---- 音频流结构体 ---- */
#define AUDIO_STREAM_MAX 16

typedef struct audio_stream {
    uint32_t handle;          /* 流句柄 */
    uint8_t  format;          /* 采样格式 (AUDIO_FMT_*) */
    uint32_t sample_rate;     /* 采样率 */
    uint8_t  channels;        /* 声道数 */
    float    volume;          /* 音量 0.0~1.0 */
    float    pan;             /* 声像 -1.0(左) ~ 1.0(右) */
    uint8_t  playing;         /* 是否正在播放 */
    uint8_t  looping;         /* 是否循环 */
    uint8_t  paused;          /* 是否暂停 */
    uint32_t position;        /* 当前采样位置 */
    uint32_t length;          /* 总采样数 */
    void    *buffer;          /* PCM数据缓冲区指针 */
    uint8_t  valid;           /* 结构体是否有效 */
} audio_stream_t;

/* ---- 3D音效参数 ---- */
typedef struct audio_3d_params {
    float position[3];      /* 声源位置 (x, y, z) */
    float velocity[3];      /* 声源速度向量 */
    float direction[3];     /* 声源方向向量 */
    float min_distance;     /* 最小距离(最大音量处) */
    float max_distance;     /* 最大距离(静音处) */
    float rolloff_factor;   /* 距离衰减系数 */
} audio_3d_t;

/* ---- 混音器结构体 ---- */
#define MIX_CHANNELS 32

typedef struct mixer_state {
    audio_stream_t *streams[MIX_CHANNELS]; /* 混音通道上的流 */
    float channel_vol[MIX_CHANNELS];       /* 各通道音量 */
    float master_volume;                   /* 主音量 0.0~1.0 */
    uint32_t output_format;                /* 输出格式 */
    uint32_t output_rate;                  /* 输出采样率 */
    uint8_t  output_channels;              /* 输出声道数 */
} mixer_t;

/* ================================================================
 *  核心初始化
 * ================================================================ */

/*
 * 初始化音频子系统（枚举设备，准备混音器）
 */
void audio_system_init(void);

/* ================================================================
 *  设备枚举与管理 API
 * ================================================================ */

/*
 * 枚举所有可用的音频设备
 * 参数: list - 接收设备列表的数组; max_count - 数组最大容量;
 *       out_count - 实际输出的设备数量
 * 返回: 0 成功, -1 失败
 */
int audio_enum_devices(audio_device_t *list, uint32_t max_count,
                       uint32_t *out_count);

/*
 * 设置默认输出设备
 * 参数: device_index - 设备索引（来自枚举结果）
 * 返回: 0 成功, -1 失败
 */
int audio_set_default_output(uint32_t device_index);

/*
 * 设置默认输入设备（录音）
 * 返回: 0 成功, -1 失败
 */
int audio_set_default_input(uint32_t device_index);

/* ================================================================
 *  音频流管理 API
 * ================================================================ */

/*
 * 创建新的音频流
 * 参数: format - 采样格式; rate - 采样率;
 *       channels - 声道数; samples - 预分配采样数
 * 返回: 流指针, NULL 表示失败
 */
audio_stream_t* audio_create_stream(uint8_t format, uint32_t rate,
                                    uint8_t channels, uint32_t samples);

/*
 * 销毁音频流并释放资源
 * 返回: 0 成功, -1 失败
 */
int audio_destroy_stream(audio_stream_t *stream);

/*
 * 向音频流喂入PCM数据
 * 参数: stream - 目标流; data - PCM数据指针; bytes - 数据字节数
 * 返回: 实际写入的字节数, -1 表示错误
 */
int audio_feed_data(audio_stream_t *stream, const void *data,
                    uint32_t bytes);

/*
 * 开始播放音频流
 * 返回: 0 成功, -1 失败
 */
int audio_play(audio_stream_t *stream);

/*
 * 停止播放（重置到开头）
 * 返回: 0 成功, -1 失败
 */
int audio_stop(audio_stream_t *stream);

/*
 * 暂停播放
 * 返回: 0 成功, -1 失败
 */
int audio_pause(audio_stream_t *stream);

/*
 * 恢复播放
 * 返回: 0 成功, -1 失败
 */
int audio_resume(audio_stream_t *stream);

/*
 * 跳转到指定采样位置
 * 返回: 0 成功, -1 失败
 */
int audio_seek(audio_stream_t *stream, uint32_t sample_pos);

/*
 * 获取当前播放位置
 * 返回: 当前采样位置, (uint32_t)-1 表示无效
 */
uint32_t audio_get_position(audio_stream_t *stream);

/* ================================================================
 *  音量控制 API
 * ================================================================ */

/*
 * 设置单个流的音量
 * 参数: stream - 目标流; vol - 音量值 0.0~1.0
 * 返回: 0 成功, -1 失败
 */
int audio_set_volume(audio_stream_t *stream, float vol);

/*
 * 设置单个流的声像(立体声平衡)
 * 参数: stream - 目标流; pan - 声像 -1.0(左) ~ 0.0(中) ~ 1.0(右)
 * 返回: 0 成功, -1 失败
 */
int audio_set_pan(audio_stream_t *stream, float pan);

/*
 * 获取单个流的当前音量
 * 返回: 音量值 0.0~1.0, -1.0 表示错误
 */
float audio_get_volume(audio_stream_t *stream);

/*
 * 设置全局主音量
 * 参数: vol - 主音量值 0.0~1.0
 * 返回: 0 成功, -1 失败
 */
int audio_set_master_volume(float vol);

/*
 * 获取全局主音量
 * 返回: 主音量值 0.0~1.0
 */
float audio_get_master_volume(void);

/* ================================================================
 *  WAV 文件 I/O API
 * ================================================================ */

/*
 * 从WAV文件加载并创建音频流
 * 参数: path - 文件路径; out_stream - 输出的流指针
 * 返回: 0 成功, -1 失败
 */
int wav_load_from_file(const char *path, audio_stream_t **out_stream);

/*
 * 将音频流保存为WAV文件
 * 返回: 0 成功, -1 失败
 */
int wav_save_to_file(const char *path, audio_stream_t *stream);

/*
 * 解析内存中的WAV文件头
 * 参数: data - WAV数据指针; len - 数据长度; hdr - 输出头信息
 * 返回: 0 成功, -1 格式无效
 */
int wav_parse_header(const uint8_t *data, uint32_t len,
                     wav_header_t *hdr);

/*
 * 将WAV头信息序列化为字节数组
 * 参数: hdr - 头信息; out - 输出缓冲区; cap - 缓冲区容量
 * 返回: 写入的字节数, -1 表示缓冲区不足
 */
int wav_write_header(wav_header_t *hdr, uint8_t *out,
                     uint32_t cap);

/* ================================================================
 *  混音器 API
 * ================================================================ */

/*
 * 初始化混音器
 * 参数: mix - 混音器实例; fmt - 输出格式; rate - 采样率;
 *       ch - 输出声道数
 */
void mixer_init(mixer_t *mix, uint32_t fmt, uint32_t rate,
                uint8_t ch);

/*
 * 执行一帧混音（混合所有活跃通道到输出缓冲区）
 * 参数: mix - 混音器; output - 输出缓冲区;
 *       frame_count - 要混合的帧数
 * 返回: 实际混合的帧数, -1 表示错误
 */
int mixer_mix(mixer_t *mix, void *output,
              uint32_t frame_count);

/*
 * 将音频流添加到指定混音通道
 * 参数: mix - 混音器; stream - 音频流; slot - 通道号(0~MIX_CHANNELS-1)
 * 返回: 0 成功, -1 失败
 */
int mixer_add_stream(mixer_t *mix, audio_stream_t *stream,
                     int slot);

/*
 * 从指定混音通道移除音频流
 * 返回: 0 成功, -1 失败
 */
int mixer_remove_stream(mixer_t *mix, int slot);

/* ================================================================
 *  3D空间音效 API
 * ================================================================ */

/*
 * 为音频流设置3D空间参数
 * 返回: 0 成功, -1 失败
 */
int audio_set_3d_params(audio_stream_t *stream,
                        const audio_3d_t *params);

/*
 * 设置听者(麦克风)位置
 */
int audio_set_listener_pos(float x, float y, float z);

/*
 * 设置听者朝向
 * 参数: forward - 前方向向量; up - 上方向向量
 */
int audio_set_listener_orient(float forward[3], float up[3]);

/*
 * 应用3D空间效果并输出左右声道数据
 * 参数: stream - 音频流; left_out - 左声道输出;
 *       right_out - 右声道输出; frames - 帧数
 * 返回: 实际处理的帧数, -1 表示错误
 */
int audio_apply_3d(audio_stream_t *stream, float *left_out,
                   float *right_out, int frames);

/* ================================================================
 *  简单音效生成 API
 * ================================================================ */

/*
 * 生成正弦波音频数据
 * 参数: freq - 频率(Hz); duration_ms - 时长(毫秒);
 *       rate - 采样率; out_buf - 输出数据指针;
 *       out_bytes - 输出数据大小
 * 返回: 0 成功, -1 失败
 */
int audio_generate_sine(float freq, uint32_t duration_ms,
                        uint32_t rate, void **out_buf,
                        uint32_t *out_bytes);

/*
 * 生成方波音频数据
 * 返回: 0 成功, -1 失败
 */
int audio_generate_square(float freq, uint32_t duration_ms,
                          uint32_t rate, void **out_buf,
                          uint32_t *out_bytes);

/*
 * 生成白噪声音频数据
 * 返回: 0 成功, -1 失败
 */
int audio_generate_noise(uint32_t duration_ms, uint32_t rate,
                         void **out_buf, uint32_t *out_bytes);

#endif /* FUNSOS_AUDIO_SDK_H */
