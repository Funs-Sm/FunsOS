#ifndef FUNSOS_COMPRESS_H
#define FUNSOS_COMPRESS_H

/*
 * FUNSOS 压缩/解压 API
 * 提供 zlib 风格的压缩和解压功能。
 * 纯用户态实现，不依赖系统调用。
 */

#include "stdint.h"

/* 压缩级别 */
#define FUNSOS_Z_NO_COMPRESSION       0
#define FUNSOS_Z_BEST_SPEED           1
#define FUNSOS_Z_BEST_COMPRESSION     9
#define FUNSOS_Z_DEFAULT_COMPRESSION  (-1)

/* 压缩策略 */
#define FUNSOS_Z_FILTERED             1
#define FUNSOS_Z_HUFFMAN_ONLY         2
#define FUNSOS_Z_RLE                  3
#define FUNSOS_Z_FIXED                4
#define FUNSOS_Z_DEFAULT_STRATEGY     0

/* 返回码 */
#define FUNSOS_Z_OK                   0
#define FUNSOS_Z_STREAM_END           1
#define FUNSOS_Z_NEED_DICT            2
#define FUNSOS_Z_ERRNO               (-1)
#define FUNSOS_Z_STREAM_ERROR        (-2)
#define FUNSOS_Z_DATA_ERROR          (-3)
#define FUNSOS_Z_MEM_ERROR           (-4)
#define FUNSOS_Z_BUF_ERROR           (-5)
#define FUNSOS_Z_VERSION_ERROR       (-6)

/* 数据类型标识 */
#define FUNSOS_Z_BINARY               0
#define FUNSOS_Z_TEXT                 1
#define FUNSOS_Z_ASCII                FUNSOS_Z_TEXT
#define FUNSOS_Z_UNKNOWN              2

/* ---- 压缩流结构 ---- */

typedef struct {
    const uint8_t  *next_in;     /* 下一个输入字节 */
    uint32_t        avail_in;    /* 可用输入字节数 */
    uint64_t        total_in;    /* 已读取的输入字节总数 */

    uint8_t        *next_out;    /* 下一个输出字节位置 */
    uint32_t        avail_out;   /* 剩余输出空间 */
    uint64_t        total_out;   /* 已输出的字节总数 */

    char           *msg;         /* 错误消息 */
    void           *state;       /* 内部状态 */

    void          *(*zalloc)(void *opaque, uint32_t items, uint32_t size);
    void           (*zfree)(void *opaque, void *addr);
    void           *opaque;      /* zalloc/zfree 的私有数据 */

    int             data_type;   /* 数据类型猜测 */
    uint32_t        adler;       /* Adler-32 校验值 */
    uint32_t        reserved;    /* 保留字段 */
} funsos_z_stream_t;

/* ---- 压缩 API ---- */

/*
 * 初始化压缩流
 * 参数: strm - 压缩流结构; level - 压缩级别 (0-9)
 * 返回: FUNSOS_Z_OK 成功, 其他错误码
 */
int funsos_deflate_init(funsos_z_stream_t *strm, int level);

/*
 * 执行压缩
 * 参数: strm - 压缩流结构; flush - 刷新模式
 * 返回: FUNSOS_Z_OK 成功, FUNSOS_Z_STREAM_END 完成
 */
int funsos_deflate(funsos_z_stream_t *strm, int flush);

/* flush 参数值 */
#define FUNSOS_Z_NO_FLUSH            0
#define FUNSOS_Z_PARTIAL_FLUSH       1
#define FUNSOS_Z_SYNC_FLUSH          2
#define FUNSOS_Z_FULL_FLUSH          3
#define FUNSOS_Z_FINISH              4
#define FUNSOS_Z_BLOCK               5
#define FUNSOS_Z_TREES               6

/*
 * 结束压缩（释放资源）
 * 参数: strm - 压缩流结构
 * 返回: FUNSOS_Z_OK 成功
 */
int funsos_deflate_end(funsos_z_stream_t *strm);

/*
 * 重置压缩流（可重新开始压缩）
 * 参数: strm - 压缩流结构
 * 返回: FUNSOS_Z_OK 成功
 */
int funsos_deflate_reset(funsos_z_stream_t *strm);

/* ---- 解压 API ---- */

/*
 * 初始化解压流
 * 参数: strm - 压缩流结构
 * 返回: FUNSOS_Z_OK 成功
 */
int funsos_inflate_init(funsos_z_stream_t *strm);

/*
 * 执行解压
 * 参数: strm - 压缩流结构; flush - 刷新模式
 * 返回: FUNSOS_Z_OK 成功, FUNSOS_Z_STREAM_END 完成
 */
int funsos_inflate(funsos_z_stream_t *strm, int flush);

/*
 * 结束解压（释放资源）
 * 参数: strm - 压缩流结构
 * 返回: FUNSOS_Z_OK 成功
 */
int funsos_inflate_end(funsos_z_stream_t *strm);

/*
 * 重置解压流
 * 参数: strm - 压缩流结构
 * 返回: FUNSOS_Z_OK 成功
 */
int funsos_inflate_reset(funsos_z_stream_t *strm);

/* ---- 简易 API ---- */

/*
 * 压缩数据（一次性）
 * 参数: dest - 目标缓冲区; destLen - 目标大小(输入/输出)
 *       source - 源数据; sourceLen - 源数据长度
 *       level - 压缩级别
 * 返回: FUNSOS_Z_OK 成功
 */
int funsos_compress(uint8_t *dest, uint32_t *destLen,
                    const uint8_t *source, uint32_t sourceLen, int level);

/*
 * 解压数据（一次性）
 * 参数: dest - 目标缓冲区; destLen - 目标大小(输入/输出)
 *       source - 源数据; sourceLen - 源数据长度
 * 返回: FUNSOS_Z_OK 成功
 */
int funsos_uncompress(uint8_t *dest, uint32_t *destLen,
                      const uint8_t *source, uint32_t sourceLen);

/* ---- gzip 文件操作 ---- */

/*
 * 压缩文件到 gzip 格式
 * 参数: src_file - 源文件路径; dst_file - 目标文件路径; level - 压缩级别
 * 返回: 0 成功, -1 失败
 */
int funsos_gzip_compress_file(const char *src_file, const char *dst_file, int level);

/*
 * 解压 gzip 格式文件
 * 参数: src_file - 源文件路径; dst_file - 目标文件路径
 * 返回: 0 成功, -1 失败
 */
int funsos_gzip_decompress_file(const char *src_file, const char *dst_file);

/* ---- CRC32/Adler32 校验 ---- */

/*
 * 计算 CRC32 校验值
 * 参数: crc - 初始 CRC 值; buf - 数据; len - 数据长度
 * 返回: 更新后的 CRC32 值
 */
uint32_t funsos_crc32(uint32_t crc, const uint8_t *buf, uint32_t len);

/*
 * 计算 Adler-32 校验值
 * 参数: adler - 初始值; buf - 数据; len - 数据长度
 * 返回: 更新后的 Adler-32 值
 */
uint32_t funsos_adler32(uint32_t adler, const uint8_t *buf, uint32_t len);

#endif /* FUNSOS_COMPRESS_H */
