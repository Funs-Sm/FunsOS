#include "crypto.h"
#include "kheap.h"
#include "string.h"
#include "klog.h"
#include "sync.h"
#include "stdio.h"

static struct crypto_alg *alg_list;
static uint8_t crypto_initialized = 0;
static uint32_t alg_count = 0;
static spinlock_t crypto_lock;
static uint32_t crc32_table[256];
static uint8_t crc32_table_init_done = 0;

static const uint32_t crc32c_poly = 0x82F63B78;

static void crc32_init_tables(void) {
    if (crc32_table_init_done) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ crc32c_poly;
            else
                crc >>= 1;
        }
        crc32_table[i] = crc;
    }
    crc32_table_init_done = 1;
}

void crypto_crc32_init(void) {
    crc32_init_tables();
}

uint32_t crypto_crc32c(uint32_t crc, const uint8_t *buf, uint32_t len) {
    if (!crc32_table_init_done) crc32_init_tables();
    crc ^= 0xFFFFFFFF;
    while (len--) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ *buf++) & 0xFF];
    }
    return crc ^ 0xFFFFFFFF;
}

uint32_t crypto_crc32(uint32_t crc, const uint8_t *buf, uint32_t len) {
    return crypto_crc32c(crc, buf, len);
}

static int null_cipher_setkey(struct crypto_tfm *tfm, const uint8_t *key, uint32_t keylen) {
    (void)tfm; (void)key; (void)keylen;
    return 0;
}

static int null_cipher_encrypt(struct crypto_tfm *tfm, const uint8_t *src, uint8_t *dst, uint32_t len) {
    (void)tfm;
    if (src != dst) memcpy(dst, src, len);
    return 0;
}

static int null_cipher_decrypt(struct crypto_tfm *tfm, const uint8_t *src, uint8_t *dst, uint32_t len) {
    (void)tfm;
    if (src != dst) memcpy(dst, src, len);
    return 0;
}

static struct crypto_alg null_cipher_alg = {
    .cra_name = "ecb(cipher_null)",
    .cra_driver_name = "cipher_null-generic",
    .cra_type = CRYPTO_ALG_TYPE_CIPHER,
    .cra_blocksize = 1,
    .cra_ctxsize = 0,
    .cra_priority = 0,
    .cra_flags = 0,
    .cra_init = NULL,
    .cra_exit = NULL,
    .cra_u = {
        .cipher = {
            .setkey = null_cipher_setkey,
            .encrypt = null_cipher_encrypt,
            .decrypt = null_cipher_decrypt
        }
    }
};

int crypto_init(void) {
    if (crypto_initialized) return 0;
    spinlock_init(&crypto_lock);
    alg_list = NULL;
    alg_count = 0;
    crc32_init_tables();

    crypto_register_alg(&null_cipher_alg);

    crypto_initialized = 1;
    klog_info("Crypto API framework initialized (%u algorithms)", alg_count);
    return 0;
}

int crypto_register_alg(struct crypto_alg *alg) {
    if (!alg) return -1;
    spinlock_lock(&crypto_lock);
    alg->next = alg_list;
    alg_list = alg;
    alg_count++;
    spinlock_unlock(&crypto_lock);
    klog_debug("crypto: registered algorithm '%s'", alg->cra_name);
    return 0;
}

int crypto_unregister_alg(struct crypto_alg *alg) {
    if (!alg) return -1;
    spinlock_lock(&crypto_lock);
    struct crypto_alg **prev = &alg_list;
    while (*prev && *prev != alg) prev = &(*prev)->next;
    if (*prev) {
        *prev = alg->next;
        alg_count--;
    }
    spinlock_unlock(&crypto_lock);
    return 0;
}

struct crypto_alg *crypto_find_alg(const char *name, crypto_alg_type_t type) {
    if (!name) return NULL;
    spinlock_lock(&crypto_lock);
    for (struct crypto_alg *a = alg_list; a; a = a->next) {
        if (a->cra_type == type && strcmp(a->cra_name, name) == 0) {
            spinlock_unlock(&crypto_lock);
            return a;
        }
    }
    spinlock_unlock(&crypto_lock);
    return NULL;
}

struct crypto_tfm *crypto_alloc_tfm(const char *name, crypto_alg_type_t type) {
    struct crypto_alg *alg = crypto_find_alg(name, type);
    if (!alg) return NULL;

    struct crypto_tfm *tfm = (struct crypto_tfm *)kmalloc(sizeof(struct crypto_tfm) + alg->cra_ctxsize);
    if (!tfm) return NULL;
    memset(tfm, 0, sizeof(struct crypto_tfm) + alg->cra_ctxsize);
    tfm->alg = alg;
    if (alg->cra_init) alg->cra_init(tfm);
    return tfm;
}

void crypto_free_tfm(struct crypto_tfm *tfm) {
    if (!tfm) return;
    if (tfm->alg && tfm->alg->cra_exit) tfm->alg->cra_exit(tfm);
    kfree(tfm);
}

int crypto_cipher_setkey(struct crypto_tfm *tfm, const uint8_t *key, uint32_t keylen) {
    if (!tfm || !tfm->alg || tfm->alg->cra_type != CRYPTO_ALG_TYPE_CIPHER) return -1;
    if (tfm->alg->cra_u.cipher.setkey)
        return tfm->alg->cra_u.cipher.setkey(tfm, key, keylen);
    return -1;
}

int crypto_cipher_encrypt(struct crypto_tfm *tfm, const uint8_t *src, uint8_t *dst, uint32_t len) {
    if (!tfm || !tfm->alg || tfm->alg->cra_type != CRYPTO_ALG_TYPE_CIPHER) return -1;
    if (tfm->alg->cra_u.cipher.encrypt)
        return tfm->alg->cra_u.cipher.encrypt(tfm, src, dst, len);
    return -1;
}

int crypto_cipher_decrypt(struct crypto_tfm *tfm, const uint8_t *src, uint8_t *dst, uint32_t len) {
    if (!tfm || !tfm->alg || tfm->alg->cra_type != CRYPTO_ALG_TYPE_CIPHER) return -1;
    if (tfm->alg->cra_u.cipher.decrypt)
        return tfm->alg->cra_u.cipher.decrypt(tfm, src, dst, len);
    return -1;
}

int crypto_hash_init(struct crypto_tfm *tfm) {
    if (!tfm || !tfm->alg || tfm->alg->cra_type != CRYPTO_ALG_TYPE_SHASH) return -1;
    if (tfm->alg->cra_u.hash.init)
        return tfm->alg->cra_u.hash.init(tfm);
    return -1;
}

int crypto_hash_update(struct crypto_tfm *tfm, const uint8_t *data, uint32_t len) {
    if (!tfm || !tfm->alg || tfm->alg->cra_type != CRYPTO_ALG_TYPE_SHASH) return -1;
    if (tfm->alg->cra_u.hash.update)
        return tfm->alg->cra_u.hash.update(tfm, data, len);
    return -1;
}

int crypto_hash_final(struct crypto_tfm *tfm, uint8_t *out) {
    if (!tfm || !tfm->alg || tfm->alg->cra_type != CRYPTO_ALG_TYPE_SHASH) return -1;
    if (tfm->alg->cra_u.hash.final)
        return tfm->alg->cra_u.hash.final(tfm, out);
    return -1;
}

uint32_t crypto_hash_digestsize(struct crypto_tfm *tfm) {
    if (!tfm || !tfm->alg) return 0;
    if (tfm->alg->cra_type == CRYPTO_ALG_TYPE_SHASH || tfm->alg->cra_type == CRYPTO_ALG_TYPE_DIGEST)
        return tfm->alg->cra_u.hash.digestsize;
    return 0;
}

void crypto_print_stats(void) {
    klog_info("=== Crypto Framework ===");
    klog_info("Registered algorithms: %u", alg_count);
    klog_info("CRC32 table initialized: %s", crc32_table_init_done ? "yes" : "no");
    for (struct crypto_alg *a = alg_list; a; a = a->next) {
        const char *type_str = "unknown";
        switch (a->cra_type) {
            case CRYPTO_ALG_TYPE_CIPHER: type_str = "cipher"; break;
            case CRYPTO_ALG_TYPE_COMPRESS: type_str = "compress"; break;
            case CRYPTO_ALG_TYPE_DIGEST: type_str = "digest"; break;
            case CRYPTO_ALG_TYPE_SHASH: type_str = "shash"; break;
            case CRYPTO_ALG_TYPE_AEAD: type_str = "aead"; break;
        }
        klog_info("  [%s] type=%s blocksize=%u priority=%u",
                 a->cra_name, type_str, a->cra_blocksize, a->cra_priority);
    }
}
