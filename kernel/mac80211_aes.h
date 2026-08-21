#ifndef MAC80211_AES_H
#define MAC80211_AES_H

#include "stdint.h"

/* 802.11 encryption support.
 *
 * Implements:
 *   - AES-128 block cipher (single-block encrypt + decrypt)
 *   - AES-CCM (Counter Mode + CBC-MAC) authenticated encryption
 *     used by 802.11i (CCMP) and 802.11ad (GCMP)
 *   - AES-CMAC (Michael / MMH / BIP) used by 802.11w BIP
 *   - RC4 (kept for legacy WEP / TKIP keystream)
 *   - TKIP SBOX / key mixing (subset for testing)
 *
 * This driver does not perform real-time encryption in production; it
 * focuses on correct protocol semantics and the byte sequences required
 * to validate a 802.11i handshake.
 */

#define AES_BLOCK_LEN     16
#define AES_KEY_LEN       16

#define CCMP_HDR_LEN      8
#define CCMP_MIC_LEN      8
#define CCMP_NONCE_LEN    13

/* AES-128 ECB primitive. */
typedef struct {
    uint32_t round_keys[44];
} aes_ctx_t;

int aes_set_key(aes_ctx_t *ctx, const uint8_t key[16]);
void aes_encrypt(const aes_ctx_t *ctx, const uint8_t in[AES_BLOCK_LEN],
                 uint8_t out[AES_BLOCK_LEN]);

/* AES-CCM authenticated encryption (CCMP / GCMP). */
int aes_ccm_init(const aes_ctx_t *ctx, const uint8_t *key, int key_len);
int aes_ccm_encrypt(const aes_ctx_t *ctx, const uint8_t *nonce,
                     const uint8_t *aad, int aad_len,
                     const uint8_t *pt, int pt_len,
                     uint8_t *ct, uint8_t *mic);

/* AES-CMAC (BIP, MFP). */
int aes_cmac(const uint8_t *key, int key_len,
              const uint8_t *data, int data_len,
              uint8_t mac[16]);

/* RC4 - used for WEP keystream only. */
typedef struct {
    uint8_t S[256];
    int    i;
    int    j;
} rc4_ctx_t;
void rc4_init(rc4_ctx_t *c, const uint8_t *key, int key_len);
void rc4_crypt(rc4_ctx_t *c, const uint8_t *in, uint8_t *out, int len);

/* TKIP key mixing (subset; produces per-packet keystream seed). */
int tkip_mix(const uint8_t *tk, const uint8_t *ta, uint32_t tsc,
              uint8_t out[16]);

#endif