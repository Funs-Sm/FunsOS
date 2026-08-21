#ifndef FUNSOS_CHECKSUM_H
#define FUNSOS_CHECKSUM_H

/*
 * FUNSOS 校验和 API
 * 提供 CRC32、MD5、SHA1、SHA256 等校验和计算功能。
 * 纯用户态实现，不依赖系统调用。
 */

#include "stdint.h"

/* ---- CRC32 ---- */

typedef struct {
    uint32_t crc;
} funsos_crc32_ctx_t;

/*
 * 初始化 CRC32 上下文
 * 参数: ctx - 上下文指针
 */
void funsos_crc32_init(funsos_crc32_ctx_t *ctx);

/*
 * 更新 CRC32 计算
 * 参数: ctx - 上下文指针; data - 数据; len - 数据长度
 */
void funsos_crc32_update(funsos_crc32_ctx_t *ctx, const uint8_t *data, uint32_t len);

/*
 * 完成 CRC32 计算
 * 参数: ctx - 上下文指针
 * 返回: CRC32 校验值
 */
uint32_t funsos_crc32_final(funsos_crc32_ctx_t *ctx);

/*
 * 一次性计算 CRC32
 * 参数: data - 数据; len - 数据长度
 * 返回: CRC32 校验值
 */
uint32_t funsos_crc32_calc(const uint8_t *data, uint32_t len);

/* ---- CRC16 ---- */

typedef struct {
    uint16_t crc;
} funsos_crc16_ctx_t;

/*
 * 初始化 CRC16 上下文
 * 参数: ctx - 上下文指针
 */
void funsos_crc16_init(funsos_crc16_ctx_t *ctx);

/*
 * 更新 CRC16 计算
 * 参数: ctx - 上下文指针; data - 数据; len - 数据长度
 */
void funsos_crc16_update(funsos_crc16_ctx_t *ctx, const uint8_t *data, uint32_t len);

/*
 * 完成 CRC16 计算
 * 参数: ctx - 上下文指针
 * 返回: CRC16 校验值
 */
uint16_t funsos_crc16_final(funsos_crc16_ctx_t *ctx);

/*
 * 一次性计算 CRC16
 * 参数: data - 数据; len - 数据长度
 * 返回: CRC16 校验值
 */
uint16_t funsos_crc16_calc(const uint8_t *data, uint32_t len);

/* ---- MD5 ---- */

#define FUNSOS_MD5_DIGEST_LENGTH   16
#define FUNSOS_MD5_BLOCK_LENGTH    64

typedef struct {
    uint32_t state[4];       /* 状态 (A, B, C, D) */
    uint64_t count;          /* 总位数 */
    uint8_t  buffer[FUNSOS_MD5_BLOCK_LENGTH];  /* 输入缓冲区 */
} funsos_md5_ctx_t;

/*
 * 初始化 MD5 上下文
 * 参数: ctx - 上下文指针
 */
void funsos_md5_init(funsos_md5_ctx_t *ctx);

/*
 * 更新 MD5 计算
 * 参数: ctx - 上下文指针; data - 数据; len - 数据长度
 */
void funsos_md5_update(funsos_md5_ctx_t *ctx, const uint8_t *data, uint32_t len);

/*
 * 完成 MD5 计算
 * 参数: ctx - 上下文指针; digest - 接收摘要的缓冲区 (16字节)
 */
void funsos_md5_final(funsos_md5_ctx_t *ctx, uint8_t digest[FUNSOS_MD5_DIGEST_LENGTH]);

/*
 * 一次性计算 MD5
 * 参数: data - 数据; len - 数据长度; digest - 接收摘要的缓冲区 (16字节)
 */
void funsos_md5_calc(const uint8_t *data, uint32_t len,
                     uint8_t digest[FUNSOS_MD5_DIGEST_LENGTH]);

/*
 * MD5 转为十六进制字符串
 * 参数: digest - MD5 摘要 (16字节); hex - 接收十六进制字符串的缓冲区 (33字节)
 */
void funsos_md5_hex(const uint8_t digest[FUNSOS_MD5_DIGEST_LENGTH], char hex[33]);

/* ---- SHA-1 ---- */

#define FUNSOS_SHA1_DIGEST_LENGTH  20
#define FUNSOS_SHA1_BLOCK_LENGTH   64

typedef struct {
    uint32_t state[5];       /* 状态 (H0-H4) */
    uint64_t count;          /* 总位数 */
    uint8_t  buffer[FUNSOS_SHA1_BLOCK_LENGTH];  /* 输入缓冲区 */
} funsos_sha1_ctx_t;

/*
 * 初始化 SHA1 上下文
 * 参数: ctx - 上下文指针
 */
void funsos_sha1_init(funsos_sha1_ctx_t *ctx);

/*
 * 更新 SHA1 计算
 * 参数: ctx - 上下文指针; data - 数据; len - 数据长度
 */
void funsos_sha1_update(funsos_sha1_ctx_t *ctx, const uint8_t *data, uint32_t len);

/*
 * 完成 SHA1 计算
 * 参数: ctx - 上下文指针; digest - 接收摘要的缓冲区 (20字节)
 */
void funsos_sha1_final(funsos_sha1_ctx_t *ctx, uint8_t digest[FUNSOS_SHA1_DIGEST_LENGTH]);

/*
 * 一次性计算 SHA1
 * 参数: data - 数据; len - 数据长度; digest - 接收摘要的缓冲区 (20字节)
 */
void funsos_sha1_calc(const uint8_t *data, uint32_t len,
                      uint8_t digest[FUNSOS_SHA1_DIGEST_LENGTH]);

/*
 * SHA1 转为十六进制字符串
 * 参数: digest - SHA1 摘要 (20字节); hex - 接收十六进制字符串的缓冲区 (41字节)
 */
void funsos_sha1_hex(const uint8_t digest[FUNSOS_SHA1_DIGEST_LENGTH], char hex[41]);

/* ---- SHA-256 ---- */

#define FUNSOS_SHA256_DIGEST_LENGTH  32
#define FUNSOS_SHA256_BLOCK_LENGTH   64

typedef struct {
    uint32_t state[8];       /* 状态 (H0-H7) */
    uint64_t count;          /* 总位数 */
    uint8_t  buffer[FUNSOS_SHA256_BLOCK_LENGTH];  /* 输入缓冲区 */
} funsos_sha256_ctx_t;

/*
 * 初始化 SHA256 上下文
 * 参数: ctx - 上下文指针
 */
void funsos_sha256_init(funsos_sha256_ctx_t *ctx);

/*
 * 更新 SHA256 计算
 * 参数: ctx - 上下文指针; data - 数据; len - 数据长度
 */
void funsos_sha256_update(funsos_sha256_ctx_t *ctx, const uint8_t *data, uint32_t len);

/*
 * 完成 SHA256 计算
 * 参数: ctx - 上下文指针; digest - 接收摘要的缓冲区 (32字节)
 */
void funsos_sha256_final(funsos_sha256_ctx_t *ctx, uint8_t digest[FUNSOS_SHA256_DIGEST_LENGTH]);

/*
 * 一次性计算 SHA256
 * 参数: data - 数据; len - 数据长度; digest - 接收摘要的缓冲区 (32字节)
 */
void funsos_sha256_calc(const uint8_t *data, uint32_t len,
                        uint8_t digest[FUNSOS_SHA256_DIGEST_LENGTH]);

/*
 * SHA256 转为十六进制字符串
 * 参数: digest - SHA256 摘要 (32字节); hex - 接收十六进制字符串的缓冲区 (65字节)
 */
void funsos_sha256_hex(const uint8_t digest[FUNSOS_SHA256_DIGEST_LENGTH], char hex[65]);

/* ---- 文件校验和 ---- */

/*
 * 计算文件的 MD5
 * 参数: filepath - 文件路径; digest - 接收摘要的缓冲区 (16字节)
 * 返回: 0 成功, -1 失败
 */
int funsos_md5_file(const char *filepath, uint8_t digest[FUNSOS_MD5_DIGEST_LENGTH]);

/*
 * 计算文件的 SHA1
 * 参数: filepath - 文件路径; digest - 接收摘要的缓冲区 (20字节)
 * 返回: 0 成功, -1 失败
 */
int funsos_sha1_file(const char *filepath, uint8_t digest[FUNSOS_SHA1_DIGEST_LENGTH]);

/*
 * 计算文件的 SHA256
 * 参数: filepath - 文件路径; digest - 接收摘要的缓冲区 (32字节)
 * 返回: 0 成功, -1 失败
 */
int funsos_sha256_file(const char *filepath, uint8_t digest[FUNSOS_SHA256_DIGEST_LENGTH]);

/* ---- Adler-32 ---- */

/*
 * 计算 Adler-32 校验和
 * 参数: adler - 初始值; data - 数据; len - 数据长度
 * 返回: 更新后的 Adler-32 值
 */
uint32_t funsos_adler32(uint32_t adler, const uint8_t *data, uint32_t len);

/* ---- XOR 校验和 ---- */

/*
 * 计算 XOR 校验和
 * 参数: data - 数据; len - 数据长度
 * 返回: XOR 校验值
 */
uint8_t funsos_xor_checksum(const uint8_t *data, uint32_t len);

#endif /* FUNSOS_CHECKSUM_H */
