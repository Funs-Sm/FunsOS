#ifndef FUNSOS_AUDIO_H
#define FUNSOS_AUDIO_H

/*
 * FUNSOS 音频 API
 * 提供 PCM/WAV 音频播放功能。
 * 基于 audio/sound.h 的音频设备接口。
 */

#include "stdint.h"

/* ---- 音频格式 ---- */
#define FUNSOS_AUDIO_FMT_PCM_S16LE  0  /* 有符号 16 位小端 */
#define FUNSOS_AUDIO_FMT_PCM_U8     1  /* 无符号 8 位 */

/* ---- 音频设备信息 ---- */
typedef struct {
    char name[32];              /* 设备名称 */
    uint32_t sample_rate;       /* 采样率 */
    uint16_t channels;          /* 声道数 */
    uint16_t bits_per_sample;   /* 每样本位数 */
} funsos_audio_device_t;

/* ---- WAV 文件头 ---- */
typedef struct {
    char     riff_tag[4];       /* "RIFF" */
    uint32_t riff_size;         /* 文件大小 - 8 */
    char     wave_tag[4];       /* "WAVE" */
    char     fmt_tag[4];        /* "fmt " */
    uint32_t fmt_size;          /* 格式块大小 */
    uint16_t audio_format;      /* 音频格式 (1=PCM) */
    uint16_t channels;          /* 声道数 */
    uint32_t sample_rate;       /* 采样率 */
    uint32_t byte_rate;         /* 字节率 */
    uint16_t block_align;       /* 块对齐 */
    uint16_t bits_per_sample;   /* 每样本位数 */
    char     data_tag[4];       /* "data" */
    uint32_t data_size;         /* 数据大小 */
} funsos_wav_header_t;

/*
 * 初始化音频子系统
 * 返回: 0 成功, -1 失败
 */
int funsos_audio_init(void);

/*
 * 获取音频设备信息
 * 参数: index - 设备索引; info - 接收设备信息
 * 返回: 0 成功, -1 失败
 */
int funsos_audio_get_device(uint32_t index, funsos_audio_device_t *info);

/*
 * 播放音频数据
 * 参数: dev_id - 设备 ID; data - 音频数据; size - 数据大小
 * 返回: 0 成功, -1 失败
 */
int funsos_audio_play(uint32_t dev_id, const void *data, uint32_t size);

/*
 * 停止播放
 * 参数: dev_id - 设备 ID
 * 返回: 0 成功, -1 失败
 */
int funsos_audio_stop(uint32_t dev_id);

/*
 * 设置音量
 * 参数: dev_id - 设备 ID; left - 左声道音量 (0-255); right - 右声道音量 (0-255)
 * 返回: 0 成功, -1 失败
 */
int funsos_audio_set_volume(uint32_t dev_id, uint8_t left, uint8_t right);

/*
 * 获取当前音量
 * 参数: dev_id - 设备 ID; left - 接收左声道音量; right - 接收右声道音量
 * 返回: 0 成功, -1 失败
 */
int funsos_audio_get_volume(uint32_t dev_id, uint8_t *left, uint8_t *right);

/*
 * 播放 WAV 文件
 * 参数: path - WAV 文件路径
 * 返回: 0 成功, -1 失败
 */
int funsos_audio_play_wav(const char *path);

#endif /* FUNSOS_AUDIO_H */
