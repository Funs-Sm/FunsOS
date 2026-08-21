/* mac80211_aes.c - 802.11 crypto primitives (AES, RC4, TKIP).
 *
 * Implements the encryption primitives required for:
 *   - AES-128 (ECB) - used for CCMP / GCMP / BIP key scheduling
 *   - AES-CCM (CCMP) - 802.11i
 *   - AES-CMAC (BIP) - 802.11w management frame protection
 *   - RC4 - legacy WEP keystream
 *   - TKIP key mixing (subset) - legacy TKIP
 *
 * This is a clean-room implementation. The performance is not the focus
 * - on production hardware it would be replaced with hardware AES-NI
 * intrinsics or assembly. For kernel self-tests this is sufficient.
 */

#include "mac80211_aes.h"
#include "string.h"

#define AES_ROUND 10

static const uint8_t sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16,
};
static const uint8_t rcon[11] = {
    0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1b,0x36
};

static uint8_t xtime(uint8_t v) {
    return (uint8_t)((v << 1) ^ (((v >> 7) & 1) * 0x1b));
}

int aes_set_key(aes_ctx_t *ctx, const uint8_t key[16]) {
    if (!ctx || !key) return -1;
    memset(ctx, 0, sizeof(*ctx));
    for (int i = 0; i < 4; i++) {
        ctx->round_keys[i] = (uint32_t)key[4*i]
                            | ((uint32_t)key[4*i + 1] << 8)
                            | ((uint32_t)key[4*i + 2] << 16)
                            | ((uint32_t)key[4*i + 3] << 24);
    }
    for (int i = 4; i < 44; i++) {
        uint32_t t = ctx->round_keys[i - 1];
        if (i % 4 == 0) {
            uint8_t b0 = (uint8_t)(t >> 24);
            uint8_t b1 = (uint8_t)(t >> 16);
            uint8_t b2 = (uint8_t)(t >> 8);
            uint8_t b3 = (uint8_t)(t);
            uint8_t s0 = sbox[b1];
            uint8_t s1 = sbox[b2];
            uint8_t s2 = sbox[b3];
            uint8_t s3 = sbox[b0];
            t = ((uint32_t)s0 << 24) | ((uint32_t)s1 << 16)
              | ((uint32_t)s2 << 8) | s3;
            t ^= (uint32_t)rcon[i / 4] << 24;
        }
        ctx->round_keys[i] = ctx->round_keys[i - 4] ^ t;
    }
    return 0;
}

static void aes_encrypt_block(const aes_ctx_t *ctx, const uint8_t in[16],
                                uint8_t out[16])
{
    uint8_t s[16];
    memcpy(s, in, 16);
    /* AddRoundKey (round 0). */
    for (int i = 0; i < 16; i++) {
        s[i] ^= (uint8_t)(ctx->round_keys[i / 4] >> (8 * (i % 4)));
    }
    for (int r = 1; r <= AES_ROUND; r++) {
        /* SubBytes */
        for (int i = 0; i < 16; i++) s[i] = sbox[s[i]];
        /* ShiftRows */
        uint8_t t;
        t = s[1]; s[1] = s[5]; s[5] = s[9]; s[9] = s[13]; s[13] = t;
        t = s[2]; s[2] = s[10]; s[10] = t;
        t = s[6]; s[6] = s[14]; s[14] = t;
        t = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = s[3]; s[3] = t;
        /* MixColumns */
        if (r != AES_ROUND) {
            for (int c = 0; c < 4; c++) {
                int j = c * 4;
                uint8_t a0 = s[j], a1 = s[j + 1], a2 = s[j + 2], a3 = s[j + 3];
                uint8_t t0 = a0 ^ a1 ^ a2 ^ a3;
                s[j]     ^= t0 ^ xtime(a0 ^ a1);
                s[j + 1] ^= t0 ^ xtime(a1 ^ a2);
                s[j + 2] ^= t0 ^ xtime(a2 ^ a3);
                s[j + 3] ^= t0 ^ xtime(a3 ^ a0);
            }
        }
        /* AddRoundKey */
        for (int i = 0; i < 16; i++) {
            s[i] ^= (uint8_t)(ctx->round_keys[r * 4 + i / 4]
                              >> (8 * (i % 4)));
        }
    }
    memcpy(out, s, 16);
}

void aes_encrypt(const aes_ctx_t *ctx, const uint8_t in[16], uint8_t out[16]) {
    if (!ctx) return;
    if (!out) out = (uint8_t *)in;
    aes_encrypt_block(ctx, in, out);
}

/* ---- AES-CCM (RFC 3610) ---- */

static void xor_block(uint8_t *a, const uint8_t *b) {
    for (int i = 0; i < 16; i++) a[i] ^= b[i];
}

static void inc_ctr(uint8_t *ctr) {
    for (int i = 15; i >= 12; i--) {
        if (++ctr[i]) return;
    }
}

int aes_ccm_encrypt(const aes_ctx_t *ctx, const uint8_t *nonce,
                     const uint8_t *aad, int aad_len,
                     const uint8_t *pt, int pt_len,
                     uint8_t *ct, uint8_t *mic)
{
    if (!ctx || !nonce || !pt || !ct || !mic) return -1;
    uint8_t B[16];
    uint8_t X[16];
    uint8_t A[16] = {0};
    int L = 2;  /* octets for length field */
    int M = 8;  /* auth tag length (CCMP) */
    /* B0 = Flags | Nonce | Length */
    B[0] = (uint8_t)((6 << 4) | ((M - 2) / 2) << 3 | (L - 1));
    memcpy(B + 1, nonce, 13);
    B[14] = (uint8_t)(pt_len >> 8);
    B[15] = (uint8_t)(pt_len);
    aes_encrypt_block(ctx, B, X);

    /* AAD encoding: 2 bytes length + AAD. */
    if (aad && aad_len > 0) {
        int len_field_size = (aad_len < 0xFF00) ? 2 : 6;
        if (len_field_size == 2) {
            A[0] = (uint8_t)(aad_len >> 8);
            A[1] = (uint8_t)(aad_len);
        } else {
            A[0] = 0xFF; A[1] = 0xFE;
            A[2] = (uint8_t)(aad_len >> 24);
            A[3] = (uint8_t)(aad_len >> 16);
            A[4] = (uint8_t)(aad_len >> 8);
            A[5] = (uint8_t)(aad_len);
        }
        memcpy(A + len_field_size, aad, (aad_len < 16 - len_field_size)
                                        ? aad_len : 16 - len_field_size);
    }
    xor_block(X, A);
    aes_encrypt_block(ctx, X, X);

    int off = 0;
    while (off < pt_len) {
        int chunk = (pt_len - off < 16) ? (pt_len - off) : 16;
        memset(B, 0, 16);
        memcpy(B, pt + off, chunk);
        xor_block(X, B);
        aes_encrypt_block(ctx, X, X);
        off += chunk;
    }
    /* Compute CTR0 */
    uint8_t ctr[16] = {0};
    ctr[0] = (uint8_t)(L - 1);
    memcpy(ctr + 1, nonce, 13);
    uint8_t S[16];
    aes_encrypt_block(ctx, ctr, S);
    memcpy(mic, X, M);

    /* Encrypt plaintext in CTR mode. */
    inc_ctr(ctr);
    off = 0;
    while (off < pt_len) {
        int chunk = (pt_len - off < 16) ? (pt_len - off) : 16;
        aes_encrypt_block(ctx, ctr, B);
        for (int i = 0; i < chunk; i++) {
            ct[off + i] = (uint8_t)(pt[off + i] ^ B[i]);
        }
        off += chunk;
        inc_ctr(ctr);
    }
    return 0;
    (void)pt_len;
}

/* ---- AES-CMAC (RFC 4493) ---- */

static void shift_left(uint8_t *out, const uint8_t *in, int n) {
    for (int i = 0; i < 16; i++) out[i] = in[i];
    for (int i = 0; i < n; i++) {
        int carry = 0;
        for (int j = 0; j < 16; j++) {
            int b = out[j] & 0x80;
            out[j] = (uint8_t)((out[j] << 1) | carry);
            carry = b ? 1 : 0;
        }
    }
}

int aes_cmac(const uint8_t *key, int key_len, const uint8_t *data,
              int data_len, uint8_t mac[16])
{
    if (!key || key_len != 16 || !mac) return -1;
    aes_ctx_t ctx;
    aes_set_key(&ctx, key);
    /* Derive K1/K2: Encrypt 0 with key, derive constant by X */
    uint8_t zero[16] = {0};
    uint8_t L[16];
    aes_encrypt_block(&ctx, zero, L);
    uint8_t K1[16], K2[16];
    uint8_t Rb = 0x87;
    int msb = L[0] & 0x80;
    shift_left(K1, L, 1);
    if (msb) K1[15] ^= Rb;
    msb = K1[0] & 0x80;
    shift_left(K2, K1, 1);
    if (msb) K2[15] ^= Rb;

    int n = (data_len + 15 - 1) / 16;
    if (n == 0) n = 1;
    uint8_t X[16] = {0};
    uint8_t Y[16];
    int flag = 0;
    if (data_len % 16 == 0) flag = 1;

    int off = 0;
    for (int i = 0; i < n - 1; i++) {
        for (int j = 0; j < 16; j++) Y[j] = (uint8_t)(X[j] ^ data[off + j]);
        aes_encrypt_block(&ctx, Y, X);
        off += 16;
    }
    /* Last block. */
    uint8_t M_last[16] = {0};
    int last = data_len - off;
    if (last > 0 && last < 16) memcpy(M_last, data + off, last);
    if (flag) {
        for (int j = 0; j < 16; j++) M_last[j] = (uint8_t)(K1[j] ^ M_last[j]);
    } else {
        for (int j = 0; j < 16; j++) M_last[j] = (uint8_t)(K2[j] ^ M_last[j]);
    }
    for (int j = 0; j < 16; j++) Y[j] = (uint8_t)(X[j] ^ M_last[j]);
    aes_encrypt_block(&ctx, Y, X);
    memcpy(mac, X, 16);
    return 0;
}

/* ---- RC4 ---- */

void rc4_init(rc4_ctx_t *c, const uint8_t *key, int key_len) {
    if (!c || !key) return;
    for (int i = 0; i < 256; i++) c->S[i] = (uint8_t)i;
    int j = 0;
    for (int i = 0; i < 256; i++) {
        j = (j + c->S[i] + key[i % key_len]) & 0xFF;
        uint8_t t = c->S[i]; c->S[i] = c->S[j]; c->S[j] = t;
    }
    c->i = c->j = 0;
}

void rc4_crypt(rc4_ctx_t *c, const uint8_t *in, uint8_t *out, int len) {
    if (!c) return;
    for (int k = 0; k < len; k++) {
        c->i = (c->i + 1) & 0xFF;
        c->j = (c->j + c->S[c->i]) & 0xFF;
        uint8_t t = c->S[c->i]; c->S[c->i] = c->S[c->j]; c->S[c->j] = t;
        uint8_t k_idx = c->S[(c->S[c->i] + c->S[c->j]) & 0xFF];
        out[k] = (uint8_t)(in[k] ^ k_idx);
    }
}

/* ---- TKIP key mixing (subset) ---- */

int tkip_mix(const uint8_t *tk, const uint8_t *ta, uint32_t tsc,
              uint8_t out[16])
{
    /* Simplified TKIP mix: rotate by tsc. Production implementation
     * requires SBOX lookups; this subset produces a deterministic seed
     * suitable for unit testing. */
    if (!tk || !ta || !out) return -1;
    for (int i = 0; i < 16; i++) out[i] = tk[i];
    out[0] ^= (uint8_t)(tsc);
    out[1] ^= (uint8_t)(tsc >> 8);
    out[2] ^= (uint8_t)(tsc >> 16);
    out[3] ^= (uint8_t)(tsc >> 24);
    for (int i = 0; i < 6; i++) out[4 + i] ^= ta[i];
    return 0;
}

int aes_ccm_init(const aes_ctx_t *ctx, const uint8_t *key, int key_len) {
    (void)ctx; (void)key; (void)key_len;
    return 0;
}