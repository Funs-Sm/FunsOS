#ifndef CRYPTO_H
#define CRYPTO_H

#include "stdint.h"

#define CRYPTO_MAX_ALG_NAME 64
#define CRYPTO_MAX_ALGS 32

typedef enum {
    CRYPTO_ALG_TYPE_CIPHER = 0,
    CRYPTO_ALG_TYPE_COMPRESS = 1,
    CRYPTO_ALG_TYPE_DIGEST = 2,
    CRYPTO_ALG_TYPE_SHASH = 3,
    CRYPTO_ALG_TYPE_AEAD = 4
} crypto_alg_type_t;

struct crypto_tfm;

struct crypto_alg {
    char cra_name[CRYPTO_MAX_ALG_NAME];
    char cra_driver_name[CRYPTO_MAX_ALG_NAME];
    crypto_alg_type_t cra_type;
    uint32_t cra_blocksize;
    uint32_t cra_ctxsize;
    uint32_t cra_priority;
    uint32_t cra_flags;
    int (*cra_init)(struct crypto_tfm *tfm);
    void (*cra_exit)(struct crypto_tfm *tfm);
    union {
        struct {
            int (*setkey)(struct crypto_tfm *tfm, const uint8_t *key, uint32_t keylen);
            int (*encrypt)(struct crypto_tfm *tfm, const uint8_t *src, uint8_t *dst, uint32_t len);
            int (*decrypt)(struct crypto_tfm *tfm, const uint8_t *src, uint8_t *dst, uint32_t len);
        } cipher;
        struct {
            int (*init)(struct crypto_tfm *tfm);
            int (*update)(struct crypto_tfm *tfm, const uint8_t *data, uint32_t len);
            int (*final)(struct crypto_tfm *tfm, uint8_t *out);
            uint32_t digestsize;
        } hash;
        struct {
            int (*compress)(struct crypto_tfm *tfm, const uint8_t *src, uint32_t slen,
                           uint8_t *dst, uint32_t *dlen);
            int (*decompress)(struct crypto_tfm *tfm, const uint8_t *src, uint32_t slen,
                             uint8_t *dst, uint32_t *dlen);
        } compress;
    } cra_u;
    struct crypto_alg *next;
};

struct crypto_tfm {
    struct crypto_alg *alg;
    void *__crt_ctx[];
};

int crypto_init(void);

int crypto_register_alg(struct crypto_alg *alg);
int crypto_unregister_alg(struct crypto_alg *alg);
struct crypto_alg *crypto_find_alg(const char *name, crypto_alg_type_t type);

struct crypto_tfm *crypto_alloc_tfm(const char *name, crypto_alg_type_t type);
void crypto_free_tfm(struct crypto_tfm *tfm);

int crypto_cipher_setkey(struct crypto_tfm *tfm, const uint8_t *key, uint32_t keylen);
int crypto_cipher_encrypt(struct crypto_tfm *tfm, const uint8_t *src, uint8_t *dst, uint32_t len);
int crypto_cipher_decrypt(struct crypto_tfm *tfm, const uint8_t *src, uint8_t *dst, uint32_t len);

int crypto_hash_init(struct crypto_tfm *tfm);
int crypto_hash_update(struct crypto_tfm *tfm, const uint8_t *data, uint32_t len);
int crypto_hash_final(struct crypto_tfm *tfm, uint8_t *out);
uint32_t crypto_hash_digestsize(struct crypto_tfm *tfm);

void crypto_crc32_init(void);
uint32_t crypto_crc32(uint32_t crc, const uint8_t *buf, uint32_t len);
uint32_t crypto_crc32c(uint32_t crc, const uint8_t *buf, uint32_t len);

void crypto_print_stats(void);

#endif
