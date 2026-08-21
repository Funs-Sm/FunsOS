/* funsos_crypto.c - 加密与哈希子模块实现
 *
 * 桥接 SDK 加密 API 到内核 crypto/krng 子系统
 */

#include "funsos.h"
#include "funsos_crypto.h"
#include "crypto.h"
#include "krng.h"
#include "kheap.h"
#include "string.h"
#include "vfs.h"

/* ---- CRC32 ---- */

uint32_t funsos_crypto_crc32(uint32_t crc, const uint8_t *data, uint32_t len) {
    return crypto_crc32(crc, data, len);
}

uint32_t funsos_crypto_crc32c(uint32_t crc, const uint8_t *data, uint32_t len) {
    return crypto_crc32c(crc, data, len);
}

/* ---- 哈希摘要 ---- */

uint32_t funsos_crypto_hash_size(int alg) {
    switch (alg) {
        case FUNSOS_HASH_CRC32:    return 4;
        case FUNSOS_HASH_CRC32C:   return 4;
        case FUNSOS_HASH_MD5:      return 16;
        case FUNSOS_HASH_SHA1:     return 20;
        case FUNSOS_HASH_SHA256:   return 32;
        default: return 0;
    }
}

int funsos_crypto_hash(int alg, const uint8_t *data, uint32_t len,
                       uint8_t *out, uint32_t *out_len) {
    if (!data || !out || !out_len) return FUNSOS_CRYPTO_ERR_INVAL;

    uint32_t need = funsos_crypto_hash_size(alg);
    if (need == 0) return FUNSOS_CRYPTO_ERR_UNSUPPORTED;
    if (*out_len < need) { *out_len = need; return FUNSOS_CRYPTO_ERR_NOMEM; }
    *out_len = need;

    switch (alg) {
        case FUNSOS_HASH_CRC32:
        case FUNSOS_HASH_CRC32C: {
            uint32_t init = 0xFFFFFFFFu;
            uint32_t crc = (alg == FUNSOS_HASH_CRC32)
                ? crypto_crc32(init, data, len)
                : crypto_crc32c(init, data, len);
            crc ^= 0xFFFFFFFFu;
            /* 写入小端序 */
            out[0] = (uint8_t)(crc & 0xFF);
            out[1] = (uint8_t)((crc >> 8) & 0xFF);
            out[2] = (uint8_t)((crc >> 16) & 0xFF);
            out[3] = (uint8_t)((crc >> 24) & 0xFF);
            return FUNSOS_CRYPTO_OK;
        }
        case FUNSOS_HASH_MD5:
        case FUNSOS_HASH_SHA1:
        case FUNSOS_HASH_SHA256: {
            /* 通过内核 crypto 框架调用 hash 算法 */
            const char *alg_name = (alg == FUNSOS_HASH_MD5)   ? "md5"   :
                                   (alg == FUNSOS_HASH_SHA1)  ? "sha1"  :
                                                                "sha256";
            struct crypto_tfm *tfm = crypto_alloc_tfm(alg_name, CRYPTO_ALG_TYPE_SHASH);
            if (!tfm) return FUNSOS_CRYPTO_ERR_UNSUPPORTED;
            if (crypto_hash_init(tfm) != 0) {
                crypto_free_tfm(tfm);
                return FUNSOS_CRYPTO_ERR_INVAL;
            }
            if (crypto_hash_update(tfm, data, len) != 0) {
                crypto_free_tfm(tfm);
                return FUNSOS_CRYPTO_ERR_INVAL;
            }
            if (crypto_hash_final(tfm, out) != 0) {
                crypto_free_tfm(tfm);
                return FUNSOS_CRYPTO_ERR_INVAL;
            }
            crypto_free_tfm(tfm);
            return FUNSOS_CRYPTO_OK;
        }
        default:
            return FUNSOS_CRYPTO_ERR_UNSUPPORTED;
    }
}

int funsos_crypto_hash_file(int alg, const char *path,
                            uint8_t *out, uint32_t *out_len) {
    if (!path || !out || !out_len) return FUNSOS_CRYPTO_ERR_INVAL;

    file_t *f = NULL;
    if (vfs_open(path, FILE_MODE_READ, &f) != 0 || !f) {
        return FUNSOS_CRYPTO_ERR_INVAL;
    }

    /* 读取整个文件到缓冲区（简化实现） */
    uint32_t max_size = 65536;  /* 64KB 上限 */
    uint8_t *buf = (uint8_t *)kmalloc(max_size);
    if (!buf) {
        vfs_close(f);
        return FUNSOS_CRYPTO_ERR_NOMEM;
    }

    int32_t n = vfs_read(f, buf, max_size);
    vfs_close(f);
    if (n < 0) {
        kfree(buf);
        return FUNSOS_CRYPTO_ERR_INVAL;
    }

    int ret = funsos_crypto_hash(alg, buf, (uint32_t)n, out, out_len);
    kfree(buf);
    return ret;
}

/* ---- 对称加密 ---- */

uint32_t funsos_crypto_block_size(int alg) {
    switch (alg) {
        case FUNSOS_CIPHER_AES128:
        case FUNSOS_CIPHER_AES256: return 16;
        case FUNSOS_CIPHER_DES:    return 8;
        case FUNSOS_CIPHER_XOR:    return 1;
        default: return 0;
    }
}

uint32_t funsos_crypto_key_size(int alg) {
    switch (alg) {
        case FUNSOS_CIPHER_AES128: return 16;
        case FUNSOS_CIPHER_AES256: return 32;
        case FUNSOS_CIPHER_DES:    return 8;
        case FUNSOS_CIPHER_XOR:    return 0;  /* 变长 */
        default: return 0;
    }
}

int funsos_crypto_encrypt(int alg, const uint8_t *key, uint32_t key_len,
                          const uint8_t *src, uint8_t *dst, uint32_t len) {
    if (!key || !src || !dst) return FUNSOS_CRYPTO_ERR_INVAL;
    uint32_t bs = funsos_crypto_block_size(alg);
    if (bs == 0) return FUNSOS_CRYPTO_ERR_UNSUPPORTED;
    if (alg != FUNSOS_CIPHER_XOR && (len % bs) != 0) {
        return FUNSOS_CRYPTO_ERR_INVAL;
    }

    switch (alg) {
        case FUNSOS_CIPHER_XOR: {
            /* 简单异或加密 */
            for (uint32_t i = 0; i < len; i++) {
                dst[i] = src[i] ^ key[i % key_len];
            }
            return FUNSOS_CRYPTO_OK;
        }
        case FUNSOS_CIPHER_AES128:
        case FUNSOS_CIPHER_AES256:
        case FUNSOS_CIPHER_DES: {
            const char *alg_name = (alg == FUNSOS_CIPHER_AES128) ? "aes" :
                                   (alg == FUNSOS_CIPHER_AES256) ? "aes" :
                                                                    "des";
            struct crypto_tfm *tfm = crypto_alloc_tfm(alg_name, CRYPTO_ALG_TYPE_CIPHER);
            if (!tfm) return FUNSOS_CRYPTO_ERR_UNSUPPORTED;
            if (crypto_cipher_setkey(tfm, key, key_len) != 0) {
                crypto_free_tfm(tfm);
                return FUNSOS_CRYPTO_ERR_KEY;
            }
            if (crypto_cipher_encrypt(tfm, src, dst, len) != 0) {
                crypto_free_tfm(tfm);
                return FUNSOS_CRYPTO_ERR_INVAL;
            }
            crypto_free_tfm(tfm);
            return FUNSOS_CRYPTO_OK;
        }
        default:
            return FUNSOS_CRYPTO_ERR_UNSUPPORTED;
    }
}

int funsos_crypto_decrypt(int alg, const uint8_t *key, uint32_t key_len,
                          const uint8_t *src, uint8_t *dst, uint32_t len) {
    if (!key || !src || !dst) return FUNSOS_CRYPTO_ERR_INVAL;
    uint32_t bs = funsos_crypto_block_size(alg);
    if (bs == 0) return FUNSOS_CRYPTO_ERR_UNSUPPORTED;
    if (alg != FUNSOS_CIPHER_XOR && (len % bs) != 0) {
        return FUNSOS_CRYPTO_ERR_INVAL;
    }

    switch (alg) {
        case FUNSOS_CIPHER_XOR: {
            /* 异或解密等于加密 */
            for (uint32_t i = 0; i < len; i++) {
                dst[i] = src[i] ^ key[i % key_len];
            }
            return FUNSOS_CRYPTO_OK;
        }
        case FUNSOS_CIPHER_AES128:
        case FUNSOS_CIPHER_AES256:
        case FUNSOS_CIPHER_DES: {
            const char *alg_name = (alg == FUNSOS_CIPHER_AES128) ? "aes" :
                                   (alg == FUNSOS_CIPHER_AES256) ? "aes" :
                                                                    "des";
            struct crypto_tfm *tfm = crypto_alloc_tfm(alg_name, CRYPTO_ALG_TYPE_CIPHER);
            if (!tfm) return FUNSOS_CRYPTO_ERR_UNSUPPORTED;
            if (crypto_cipher_setkey(tfm, key, key_len) != 0) {
                crypto_free_tfm(tfm);
                return FUNSOS_CRYPTO_ERR_KEY;
            }
            if (crypto_cipher_decrypt(tfm, src, dst, len) != 0) {
                crypto_free_tfm(tfm);
                return FUNSOS_CRYPTO_ERR_INVAL;
            }
            crypto_free_tfm(tfm);
            return FUNSOS_CRYPTO_OK;
        }
        default:
            return FUNSOS_CRYPTO_ERR_UNSUPPORTED;
    }
}

/* ---- 随机数 ---- */

uint8_t funsos_crypto_random_byte(void) {
    return (uint8_t)(krng_next32() & 0xFF);
}

uint32_t funsos_crypto_random_u32(void) {
    return krng_next32();
}

void funsos_crypto_random_bytes(uint8_t *buf, uint32_t len) {
    if (!buf) return;
    uint32_t i = 0;
    while (i + 4 <= len) {
        uint32_t v = krng_next32();
        buf[i++] = (uint8_t)(v & 0xFF);
        buf[i++] = (uint8_t)((v >> 8) & 0xFF);
        buf[i++] = (uint8_t)((v >> 16) & 0xFF);
        buf[i++] = (uint8_t)((v >> 24) & 0xFF);
    }
    if (i < len) {
        uint32_t v = krng_next32();
        while (i < len) {
            buf[i++] = (uint8_t)(v & 0xFF);
            v >>= 8;
        }
    }
}

/* ---- Base64 编码 ---- */

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int funsos_crypto_base64_encode(const uint8_t *src, uint32_t src_len,
                                char *dst, uint32_t dst_max) {
    if (!src || !dst) return -1;
    uint32_t need = ((src_len + 2) / 3) * 4;
    if (dst_max < need + 1) return -1;

    uint32_t i = 0, j = 0;
    while (i + 2 < src_len) {
        uint32_t v = ((uint32_t)src[i] << 16) |
                     ((uint32_t)src[i + 1] << 8) |
                     ((uint32_t)src[i + 2]);
        dst[j++] = b64_table[(v >> 18) & 0x3F];
        dst[j++] = b64_table[(v >> 12) & 0x3F];
        dst[j++] = b64_table[(v >> 6) & 0x3F];
        dst[j++] = b64_table[v & 0x3F];
        i += 3;
    }
    if (i < src_len) {
        uint32_t v = (uint32_t)src[i] << 16;
        if (i + 1 < src_len) v |= (uint32_t)src[i + 1] << 8;
        dst[j++] = b64_table[(v >> 18) & 0x3F];
        dst[j++] = b64_table[(v >> 12) & 0x3F];
        dst[j++] = (i + 1 < src_len) ? b64_table[(v >> 6) & 0x3F] : '=';
        dst[j++] = '=';
    }
    dst[j] = '\0';
    return (int)j;
}

static int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int funsos_crypto_base64_decode(const char *src, uint8_t *dst, uint32_t dst_max) {
    if (!src || !dst) return -1;
    uint32_t i = 0, j = 0;
    while (src[i] && src[i] != '=') {
        int v0 = b64_val(src[i++]);
        if (v0 < 0) break;
        while (src[i] == '\n' || src[i] == '\r' || src[i] == ' ') i++;
        int v1 = (src[i] && src[i] != '=') ? b64_val(src[i++]) : -1;
        if (v1 < 0) break;
        if (j < dst_max) dst[j++] = (uint8_t)((v0 << 2) | (v1 >> 4));

        while (src[i] == '\n' || src[i] == '\r' || src[i] == ' ') i++;
        int v2 = (src[i] && src[i] != '=') ? b64_val(src[i++]) : -1;
        if (v2 < 0) break;
        if (j < dst_max) dst[j++] = (uint8_t)(((v1 & 0xF) << 4) | (v2 >> 2));

        while (src[i] == '\n' || src[i] == '\r' || src[i] == ' ') i++;
        int v3 = (src[i] && src[i] != '=') ? b64_val(src[i++]) : -1;
        if (v3 < 0) break;
        if (j < dst_max) dst[j++] = (uint8_t)(((v2 & 0x3) << 6) | v3);
    }
    return (int)j;
}

/* ---- 十六进制编解码 ---- */

int funsos_crypto_hex_encode(const uint8_t *src, uint32_t src_len,
                             char *dst, uint32_t dst_max) {
    if (!src || !dst) return -1;
    if (dst_max < src_len * 2 + 1) return -1;
    static const char hex[] = "0123456789abcdef";
    for (uint32_t i = 0; i < src_len; i++) {
        dst[i * 2]     = hex[(src[i] >> 4) & 0xF];
        dst[i * 2 + 1] = hex[src[i] & 0xF];
    }
    dst[src_len * 2] = '\0';
    return (int)(src_len * 2);
}

int funsos_crypto_hex_decode(const char *src, uint8_t *dst, uint32_t dst_max) {
    if (!src || !dst) return -1;
    uint32_t i = 0, j = 0;
    while (src[i] && src[i + 1] && j < dst_max) {
        int hi = -1, lo = -1;
        char c = src[i];
        if (c >= '0' && c <= '9') hi = c - '0';
        else if (c >= 'a' && c <= 'f') hi = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') hi = c - 'A' + 10;
        c = src[i + 1];
        if (c >= '0' && c <= '9') lo = c - '0';
        else if (c >= 'a' && c <= 'f') lo = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') lo = c - 'A' + 10;
        if (hi < 0 || lo < 0) break;
        dst[j++] = (uint8_t)((hi << 4) | lo);
        i += 2;
    }
    return (int)j;
}
