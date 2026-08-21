#ifndef FUNSOS_CRYPTO_H
#define FUNSOS_CRYPTO_H

#include "stdint.h"
#include "stddef.h"

/*
 * FUNSOS SDK - 加密与哈希子模块
 *
 * 提供 CRC32/MD5/SHA 摘要、对称加密、随机数生成等安全相关 API
 * 桥接到内核 crypto 子系统
 *
 * 版本: 1.0.0 (FunsCore v0.8 / SDK 1.4.0 新增)
 */

#define FUNSOS_CRYPTO_API_VERSION  0x0100

/* ---- 哈希算法类型 ---- */
#define FUNSOS_HASH_CRC32    1
#define FUNSOS_HASH_CRC32C   2
#define FUNSOS_HASH_MD5      3
#define FUNSOS_HASH_SHA1     4
#define FUNSOS_HASH_SHA256   5

/* ---- 对称加密算法 ---- */
#define FUNSOS_CIPHER_AES128  16
#define FUNSOS_CIPHER_AES256  17
#define FUNSOS_CIPHER_DES     18
#define FUNSOS_CIPHER_XOR     19

/* ---- 哈希摘要大小 (字节) ---- */
#define FUNSOS_CRC32_SIZE    4
#define FUNSOS_MD5_SIZE      16
#define FUNSOS_SHA1_SIZE     20
#define FUNSOS_SHA256_SIZE   32

/* ---- 错误码 ---- */
#define FUNSOS_CRYPTO_OK             0
#define FUNSOS_CRYPTO_ERR_INVAL     -1
#define FUNSOS_CRYPTO_ERR_NOMEM     -2
#define FUNSOS_CRYPTO_ERR_UNSUPPORTED -3
#define FUNSOS_CRYPTO_ERR_KEY       -4

#ifdef __cplusplus
extern "C" {
#endif

/* ---- CRC32 ---- */

/*
 * 计算 CRC32 (IEEE 802.3)
 * 参数: crc - 初始值(0或前一分块结果); data - 数据缓冲区; len - 长度
 * 返回: CRC32 值
 */
uint32_t funsos_crypto_crc32(uint32_t crc, const uint8_t *data, uint32_t len);

/*
 * 计算 CRC32C (Castagnoli)
 */
uint32_t funsos_crypto_crc32c(uint32_t crc, const uint8_t *data, uint32_t len);

/* ---- 哈希摘要 ---- */

/*
 * 计算数据的哈希摘要
 * 参数: alg - FUNSOS_HASH_*; data - 输入数据; len - 数据长度;
 *       out - 接收摘要的缓冲区 (至少 32 字节); out_len - 输入缓冲区大小,输出实际写入长度
 * 返回: 0 成功, <0 失败
 */
int funsos_crypto_hash(int alg, const uint8_t *data, uint32_t len,
                       uint8_t *out, uint32_t *out_len);

/*
 * 获取指定哈希算法的摘要大小
 */
uint32_t funsos_crypto_hash_size(int alg);

/*
 * 计算文件的哈希摘要
 * 参数: alg - FUNSOS_HASH_*; path - 文件路径;
 *       out - 接收摘要; out_len - 缓冲区大小,输出实际长度
 * 返回: 0 成功, <0 失败
 */
int funsos_crypto_hash_file(int alg, const char *path,
                            uint8_t *out, uint32_t *out_len);

/* ---- 对称加密 ---- */

/*
 * 加密数据块
 * 参数: alg - FUNSOS_CIPHER_*; key - 密钥; key_len - 密钥长度;
 *       src - 明文; dst - 密文; len - 数据长度 (必须为块大小的倍数)
 * 返回: 0 成功, <0 失败
 */
int funsos_crypto_encrypt(int alg, const uint8_t *key, uint32_t key_len,
                          const uint8_t *src, uint8_t *dst, uint32_t len);

/*
 * 解密数据块
 */
int funsos_crypto_decrypt(int alg, const uint8_t *key, uint32_t key_len,
                          const uint8_t *src, uint8_t *dst, uint32_t len);

/*
 * 获取指定加密算法的块大小
 */
uint32_t funsos_crypto_block_size(int alg);

/*
 * 获取指定加密算法的密钥长度 (0 表示变长)
 */
uint32_t funsos_crypto_key_size(int alg);

/* ---- 随机数 ---- */

/*
 * 获取一个随机字节
 */
uint8_t funsos_crypto_random_byte(void);

/*
 * 获取一个随机 32 位整数
 */
uint32_t funsos_crypto_random_u32(void);

/*
 * 填充缓冲区为随机字节
 * 参数: buf - 缓冲区; len - 长度
 */
void funsos_crypto_random_bytes(uint8_t *buf, uint32_t len);

/* ---- 编码/解码 ---- */

/*
 * Base64 编码
 * 参数: src - 输入; src_len - 长度; dst - 输出缓冲区; dst_max - 缓冲区大小
 * 返回: 编码后长度, <0 失败
 */
int funsos_crypto_base64_encode(const uint8_t *src, uint32_t src_len,
                                char *dst, uint32_t dst_max);

/*
 * Base64 解码
 * 参数: src - 输入字符串; dst - 输出缓冲区; dst_max - 缓冲区大小
 * 返回: 解码后长度, <0 失败
 */
int funsos_crypto_base64_decode(const char *src, uint8_t *dst, uint32_t dst_max);

/*
 * 十六进制编码
 */
int funsos_crypto_hex_encode(const uint8_t *src, uint32_t src_len,
                             char *dst, uint32_t dst_max);

/*
 * 十六进制解码
 */
int funsos_crypto_hex_decode(const char *src, uint8_t *dst, uint32_t dst_max);

#ifdef __cplusplus
}
#endif

#endif /* FUNSOS_CRYPTO_H */
