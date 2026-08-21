/*
 * kernel/sha256.h - SHA-256 (FIPS 180-4) for kernel use.
 */
#ifndef SHA256_H
#define SHA256_H

#include "stdint.h"
#include "stddef.h"

typedef struct {
    uint8_t  data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} sha256_ctx_t;

void sha256_init(sha256_ctx_t *ctx);
void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len);
void sha256_final(sha256_ctx_t *ctx, uint8_t hash[32]);
void sha256(const uint8_t *data, size_t len, uint8_t hash[32]);
void sha256_hex(const uint8_t *data, size_t len, char out_hex[65]);

#endif
