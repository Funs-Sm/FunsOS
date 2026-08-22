/*
 * kernel/cmd_hash.c
 * sha256sum, md5sum commands
 */

#include "cmd_hash.h"
#include "shell.h"
#include "shell_error.h"
#include "string.h"
#include "stdio.h"
#include "../fs/vfs.h"
#include "sha256.h"

/* ============================================================
 * MD5 implementation (RFC 1321)
 * ============================================================ */

typedef struct {
    uint32_t state[4];
    uint32_t count[2];
    uint8_t  buffer[64];
} md5_ctx_t;

static void md5_init(md5_ctx_t *ctx);
static void md5_update(md5_ctx_t *ctx, const uint8_t *data, size_t len);
static void md5_final(md5_ctx_t *ctx, uint8_t digest[16]);
static void md5_hex(const uint8_t digest[16], char out[33]);

#define MD5_F(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))
#define MD5_G(x, y, z) ((y) ^ ((z) & ((x) ^ (y))))
#define MD5_H(x, y, z) ((x) ^ (y) ^ (z))
#define MD5_I(x, y, z) ((y) ^ ((x) | ~(z)))

#define ROTL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

#define FF(a, b, c, d, x, s, ac) do { \
    (a) += MD5_F((b),(c),(d)) + (x) + (uint32_t)(ac); \
    (a) = ROTL32((a), (s)); \
    (a) += (b); \
} while(0)

#define GG(a, b, c, d, x, s, ac) do { \
    (a) += MD5_G((b),(c),(d)) + (x) + (uint32_t)(ac); \
    (a) = ROTL32((a), (s)); \
    (a) += (b); \
} while(0)

#define HH(a, b, c, d, x, s, ac) do { \
    (a) += MD5_H((b),(c),(d)) + (x) + (uint32_t)(ac); \
    (a) = ROTL32((a), (s)); \
    (a) += (b); \
} while(0)

#define II(a, b, c, d, x, s, ac) do { \
    (a) += MD5_I((b),(c),(d)) + (x) + (uint32_t)(ac); \
    (a) = ROTL32((a), (s)); \
    (a) += (b); \
} while(0)

static const uint32_t S[4][4] = {
    { 7, 12, 17, 22 },
    { 5,  9, 14, 20 },
    { 4, 11, 16, 23 },
    { 6, 10, 15, 21 }
};

static const uint32_t K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
    0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};

static void md5_encode(const uint8_t *input, uint32_t output[16], uint32_t len) {
    for (uint32_t i = 0, j = 0; j < len; i++, j += 4) {
        output[i] = ((uint32_t)input[j]) |
                    (((uint32_t)input[j + 1]) << 8) |
                    (((uint32_t)input[j + 2]) << 16) |
                    (((uint32_t)input[j + 3]) << 24);
    }
}

static void md5_decode(const uint32_t input[16], uint8_t output[64]) {
    for (uint32_t i = 0, j = 0; i < 16; i++, j += 4) {
        output[j]     = (uint8_t)(input[i] & 0xff);
        output[j + 1] = (uint8_t)((input[i] >> 8) & 0xff);
        output[j + 2] = (uint8_t)((input[i] >> 16) & 0xff);
        output[j + 3] = (uint8_t)((input[i] >> 24) & 0xff);
    }
}

static void md5_transform(md5_ctx_t *ctx, const uint8_t block[64]) {
    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t x[16];
    md5_encode(block, x, 64);

    /* Round 1 */
    FF(a, b, c, d, x[ 0], S[0][0], K[ 0]);
    FF(d, a, b, c, x[ 1], S[0][1], K[ 1]);
    FF(c, d, a, b, x[ 2], S[0][2], K[ 2]);
    FF(b, c, d, a, x[ 3], S[0][3], K[ 3]);
    FF(a, b, c, d, x[ 4], S[0][0], K[ 4]);
    FF(d, a, b, c, x[ 5], S[0][1], K[ 5]);
    FF(c, d, a, b, x[ 6], S[0][2], K[ 6]);
    FF(b, c, d, a, x[ 7], S[0][3], K[ 7]);
    FF(a, b, c, d, x[ 8], S[0][0], K[ 8]);
    FF(d, a, b, c, x[ 9], S[0][1], K[ 9]);
    FF(c, d, a, b, x[10], S[0][2], K[10]);
    FF(b, c, d, a, x[11], S[0][3], K[11]);
    FF(a, b, c, d, x[12], S[0][0], K[12]);
    FF(d, a, b, c, x[13], S[0][1], K[13]);
    FF(c, d, a, b, x[14], S[0][2], K[14]);
    FF(b, c, d, a, x[15], S[0][3], K[15]);

    /* Round 2 */
    GG(a, b, c, d, x[ 1], S[1][0], K[16]);
    GG(d, a, b, c, x[ 6], S[1][1], K[17]);
    GG(c, d, a, b, x[11], S[1][2], K[18]);
    GG(b, c, d, a, x[ 0], S[1][3], K[19]);
    GG(a, b, c, d, x[ 5], S[1][0], K[20]);
    GG(d, a, b, c, x[10], S[1][1], K[21]);
    GG(c, d, a, b, x[15], S[1][2], K[22]);
    GG(b, c, d, a, x[ 4], S[1][3], K[23]);
    GG(a, b, c, d, x[ 9], S[1][0], K[24]);
    GG(d, a, b, c, x[14], S[1][1], K[25]);
    GG(c, d, a, b, x[ 3], S[1][2], K[26]);
    GG(b, c, d, a, x[ 8], S[1][3], K[27]);
    GG(a, b, c, d, x[13], S[1][0], K[28]);
    GG(d, a, b, c, x[ 2], S[1][1], K[29]);
    GG(c, d, a, b, x[ 7], S[1][2], K[30]);
    GG(b, c, d, a, x[12], S[1][3], K[31]);

    /* Round 3 */
    HH(a, b, c, d, x[ 5], S[2][0], K[32]);
    HH(d, a, b, c, x[ 8], S[2][1], K[33]);
    HH(c, d, a, b, x[11], S[2][2], K[34]);
    HH(b, c, d, a, x[14], S[2][3], K[35]);
    HH(a, b, c, d, x[ 1], S[2][0], K[36]);
    HH(d, a, b, c, x[ 4], S[2][1], K[37]);
    HH(c, d, a, b, x[ 7], S[2][2], K[38]);
    HH(b, c, d, a, x[10], S[2][3], K[39]);
    HH(a, b, c, d, x[13], S[2][0], K[40]);
    HH(d, a, b, c, x[ 0], S[2][1], K[41]);
    HH(c, d, a, b, x[ 3], S[2][2], K[42]);
    HH(b, c, d, a, x[ 6], S[2][3], K[43]);
    HH(a, b, c, d, x[ 9], S[2][0], K[44]);
    HH(d, a, b, c, x[12], S[2][1], K[45]);
    HH(c, d, a, b, x[15], S[2][2], K[46]);
    HH(b, c, d, a, x[ 2], S[2][3], K[47]);

    /* Round 4 */
    II(a, b, c, d, x[ 0], S[3][0], K[48]);
    II(d, a, b, c, x[ 7], S[3][1], K[49]);
    II(c, d, a, b, x[14], S[3][2], K[50]);
    II(b, c, d, a, x[ 5], S[3][3], K[51]);
    II(a, b, c, d, x[12], S[3][0], K[52]);
    II(d, a, b, c, x[ 3], S[3][1], K[53]);
    II(c, d, a, b, x[10], S[3][2], K[54]);
    II(b, c, d, a, x[ 1], S[3][3], K[55]);
    II(a, b, c, d, x[ 8], S[3][0], K[56]);
    II(d, a, b, c, x[15], S[3][1], K[57]);
    II(c, d, a, b, x[ 6], S[3][2], K[58]);
    II(b, c, d, a, x[13], S[3][3], K[59]);
    II(a, b, c, d, x[ 4], S[3][0], K[60]);
    II(d, a, b, c, x[11], S[3][1], K[61]);
    II(c, d, a, b, x[ 2], S[3][2], K[62]);
    II(b, c, d, a, x[ 9], S[3][3], K[63]);

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
}

static void md5_init(md5_ctx_t *ctx) {
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->count[0] = 0;
    ctx->count[1] = 0;
}

static void md5_update(md5_ctx_t *ctx, const uint8_t *data, size_t len) {
    uint32_t i, idx, part_len;

    idx = (uint32_t)((ctx->count[0] >> 3) & 0x3F);
    ctx->count[0] += (uint32_t)(len << 3);
    if (ctx->count[0] < ((uint32_t)len << 3))
        ctx->count[1]++;
    ctx->count[1] += (uint32_t)(len >> 29);

    part_len = 64 - idx;
    if ((uint32_t)len >= part_len) {
        memcpy(ctx->buffer + idx, data, part_len);
        md5_transform(ctx, ctx->buffer);
        for (i = part_len; i + 63 < (uint32_t)len; i += 64)
            md5_transform(ctx, data + i);
        idx = 0;
    } else {
        i = 0;
    }
    memcpy(ctx->buffer + idx, data + i, (uint32_t)len - i);
}

static void md5_final(md5_ctx_t *ctx, uint8_t digest[16]) {
    uint8_t bits[8];
    uint32_t idx, pad_len;
    static const uint8_t padding[64] = { 0x80 };

    md5_decode(ctx->count, bits);
    idx = (uint32_t)((ctx->count[0] >> 3) & 0x3F);
    pad_len = (idx < 56) ? (56 - idx) : (120 - idx);
    md5_update(ctx, padding, pad_len);
    md5_update(ctx, bits, 8);
    md5_decode(ctx->state, digest);
}

static void md5_hex(const uint8_t digest[16], char out[33]) {
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 16; i++) {
        out[i * 2]     = hex[(digest[i] >> 4) & 0xF];
        out[i * 2 + 1] = hex[digest[i] & 0xF];
    }
    out[32] = '\0';
}

/* ============================================================
 * Shared helpers
 * ============================================================ */

static void print_sha256_file(const char *path) {
    file_t *file = NULL;
    uint8_t buf[1024];
    uint8_t hash[32];
    sha256_ctx_t ctx;
    int32_t r;

    int32_t ret = vfs_open(path, FILE_MODE_READ, &file);
    if (ret < 0) {
        shell_error(SHELL_ERR_FILE_NOT_FOUND, path);
        shell_last_exit_code = 1;
        return;
    }

    sha256_init(&ctx);
    while (1) {
        r = vfs_read(file, buf, sizeof(buf));
        if (r <= 0) break;
        sha256_update(&ctx, buf, (size_t)r);
    }
    sha256_final(&ctx, hash);

    vfs_close(file);

    static const char hex[] = "0123456789abcdef";
    char line[320];
    int pos = 0;
    for (int i = 0; i < 32; i++) {
        line[pos++] = hex[(hash[i] >> 4) & 0xF];
        line[pos++] = hex[hash[i] & 0xF];
    }
    line[pos++] = ' ';
    line[pos++] = ' ';
    /* Append filename */
    int plen = strlen(path);
    if (plen > (int)(sizeof(line) - pos - 2)) plen = (int)(sizeof(line) - pos - 2);
    memcpy(line + pos, path, (size_t)plen);
    pos += plen;
    line[pos++] = '\n';
    line[pos] = '\0';
    shell_print(line);
}

static void print_md5_file(const char *path) {
    file_t *file = NULL;
    uint8_t buf[1024];
    uint8_t digest[16];
    md5_ctx_t ctx;
    int32_t r;
    char hex[33];

    int32_t ret = vfs_open(path, FILE_MODE_READ, &file);
    if (ret < 0) {
        shell_error(SHELL_ERR_FILE_NOT_FOUND, path);
        shell_last_exit_code = 1;
        return;
    }

    md5_init(&ctx);
    while (1) {
        r = vfs_read(file, buf, sizeof(buf));
        if (r <= 0) break;
        md5_update(&ctx, buf, (size_t)r);
    }
    md5_final(&ctx, digest);

    vfs_close(file);

    md5_hex(digest, hex);

    char line[320];
    snprintf(line, sizeof(line), "%s  %s\n", hex, path);
    shell_print(line);
}

/* ============================================================
 * cmd_sha256sum
 * ============================================================ */
void cmd_sha256sum(const char *args) {
    if (!args || !*args) {
        shell_print("sha256sum - compute SHA-256 checksum\n");
        shell_print("Usage: sha256sum [FILE]...\n");
        shell_print("       sha256sum -c CHECKFILE\n");
        shell_print("  -c  Verify checksums from a file\n");
        shell_last_exit_code = 1;
        return;
    }

    /* Check for -c mode */
    if (args[0] == '-' && args[1] == 'c' && (args[2] == ' ' || args[2] == '\0')) {
        const char *check_file = NULL;
        if (args[2] == ' ') {
            check_file = args + 3;
            while (*check_file == ' ') check_file++;
        }
        if (!check_file || !*check_file) {
            shell_print("sha256sum: -c requires a file argument\n");
            shell_last_exit_code = 1;
            return;
        }

        file_t *cf = NULL;
        int32_t ret = vfs_open(check_file, FILE_MODE_READ, &cf);
        if (ret < 0) {
            shell_error(SHELL_ERR_FILE_NOT_FOUND, check_file);
            shell_last_exit_code = 1;
            return;
        }

        char line[512];
        int lineno = 0;
        shell_last_exit_code = 0;

        while (1) {
            int i = 0;
            int ch;
            /* Read one line from check file */
            while (i < (int)sizeof(line) - 1) {
                int r = vfs_read(cf, &ch, 1);
                if (r <= 0) { line[i] = '\0'; break; }
                if (ch == '\n') { line[i] = '\0'; break; }
                line[i++] = (char)ch;
            }
            if (i == 0) break;
            lineno++;

            /* Parse: HASH  filename  (two spaces between) */
            /* Skip empty lines or lines starting with # */
            while (line[0] == ' ' || line[0] == '\t') {
                int j = 0;
                while (line[j + 1]) { line[j] = line[j + 1]; j++; }
                line[j] = '\0';
            }
            if (line[0] == '\0' || line[0] == '#') continue;

            /* Find first space (end of hash) */
            char *sp = line;
            while (*sp && *sp != ' ' && *sp != '\t') sp++;
            if (*sp == '\0') continue;
            int hash_len = (int)(sp - line);
            if (hash_len != 64) continue;
            *sp = '\0';

            /* Skip whitespace */
            while (*sp == ' ' || *sp == '\t') sp++;
            /* Next space or end terminates filename */
            char *fname_start = sp;
            while (*sp && *sp != ' ' && *sp != '\t') sp++;
            *sp = '\0';

            if (!*fname_start) continue;

            /* Compute actual hash */
            file_t *tf = NULL;
            int32_t r2 = vfs_open(fname_start, FILE_MODE_READ, &tf);
            char status[64];
            if (r2 < 0) {
                snprintf(status, sizeof(status), "%s: FAILED (file not found)\n", fname_start);
                shell_last_exit_code = 1;
            } else {
                uint8_t hash2[32];
                sha256_ctx_t ctx;
                uint8_t buf[1024];
                sha256_init(&ctx);
                while (1) {
                    int32_t rd = vfs_read(tf, buf, sizeof(buf));
                    if (rd <= 0) break;
                    sha256_update(&ctx, buf, (size_t)rd);
                }
                sha256_final(&ctx, hash2);
                vfs_close(tf);

                static const char hex[] = "0123456789abcdef";
                char computed[65];
                for (int hi = 0; hi < 32; hi++) {
                    computed[hi * 2]     = hex[(hash2[hi] >> 4) & 0xF];
                    computed[hi * 2 + 1] = hex[hash2[hi] & 0xF];
                }
                computed[64] = '\0';

                if (memcmp(line, computed, 64) == 0) {
                    snprintf(status, sizeof(status), "%s: OK\n", fname_start);
                } else {
                    snprintf(status, sizeof(status), "%s: FAILED\n", fname_start);
                    shell_last_exit_code = 1;
                }
            }
            shell_print(status);
        }
        vfs_close(cf);
        return;
    }

    /* Normal mode: one or more files */
    const char *files[64];
    int count = 0;
    const char *p = args;
    while (*p == ' ') p++;
    while (*p && count < 64) {
        files[count++] = p;
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
    }

    shell_last_exit_code = 0;
    for (int i = 0; i < count; i++) {
        print_sha256_file(files[i]);
    }
}

/* ============================================================
 * cmd_md5sum
 * ============================================================ */
void cmd_md5sum(const char *args) {
    if (!args || !*args) {
        shell_print("md5sum - compute MD5 checksum\n");
        shell_print("Usage: md5sum [FILE]...\n");
        shell_print("       md5sum -c CHECKFILE\n");
        shell_print("  -c  Verify checksums from a file\n");
        shell_last_exit_code = 1;
        return;
    }

    /* Check for -c mode */
    if (args[0] == '-' && args[1] == 'c' && (args[2] == ' ' || args[2] == '\0')) {
        const char *check_file = NULL;
        if (args[2] == ' ') {
            check_file = args + 3;
            while (*check_file == ' ') check_file++;
        }
        if (!check_file || !*check_file) {
            shell_print("md5sum: -c requires a file argument\n");
            shell_last_exit_code = 1;
            return;
        }

        file_t *cf = NULL;
        int32_t ret = vfs_open(check_file, FILE_MODE_READ, &cf);
        if (ret < 0) {
            shell_error(SHELL_ERR_FILE_NOT_FOUND, check_file);
            shell_last_exit_code = 1;
            return;
        }

        char line[512];
        shell_last_exit_code = 0;

        while (1) {
            int i = 0;
            /* Read one line */
            while (i < (int)sizeof(line) - 1) {
                int r = vfs_read(cf, &line[i], 1);
                if (r <= 0) { line[i] = '\0'; break; }
                if (line[i] == '\n') { line[i] = '\0'; break; }
                i++;
            }
            if (i == 0) break;

            while (line[0] == ' ' || line[0] == '\t') {
                int j = 0;
                while (line[j + 1]) { line[j] = line[j + 1]; j++; }
                line[j] = '\0';
            }
            if (line[0] == '\0' || line[0] == '#') continue;

            char *sp = line;
            while (*sp && *sp != ' ' && *sp != '\t') sp++;
            if (*sp == '\0') continue;
            int hash_len = (int)(sp - line);
            if (hash_len != 32) continue;
            *sp = '\0';

            while (*sp == ' ' || *sp == '\t') sp++;
            char *fname_start = sp;
            while (*sp && *sp != ' ' && *sp != '\t') sp++;
            *sp = '\0';

            if (!*fname_start) continue;

            file_t *tf = NULL;
            int32_t r2 = vfs_open(fname_start, FILE_MODE_READ, &tf);
            char status[64];
            if (r2 < 0) {
                snprintf(status, sizeof(status), "%s: FAILED (file not found)\n", fname_start);
                shell_last_exit_code = 1;
            } else {
                md5_ctx_t ctx;
                uint8_t digest[16];
                uint8_t buf[1024];
                md5_init(&ctx);
                while (1) {
                    int32_t rd = vfs_read(tf, buf, sizeof(buf));
                    if (rd <= 0) break;
                    md5_update(&ctx, buf, (size_t)rd);
                }
                md5_final(&ctx, digest);
                vfs_close(tf);

                char computed[33];
                md5_hex(digest, computed);

                if (memcmp(line, computed, 32) == 0) {
                    snprintf(status, sizeof(status), "%s: OK\n", fname_start);
                } else {
                    snprintf(status, sizeof(status), "%s: FAILED\n", fname_start);
                    shell_last_exit_code = 1;
                }
            }
            shell_print(status);
        }
        vfs_close(cf);
        return;
    }

    /* Normal mode */
    const char *files[64];
    int count = 0;
    const char *p = args;
    while (*p == ' ') p++;
    while (*p && count < 64) {
        files[count++] = p;
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
    }

    shell_last_exit_code = 0;
    for (int i = 0; i < count; i++) {
        print_md5_file(files[i]);
    }
}

/*
 * cmd_hash - dispatch to sha256sum/md5sum based on subcommand.
 *   hash sha256sum <files...>
 *   hash md5sum    <files...>
 *   hash help
 */
void cmd_hash(const char *args)
{
    while (args && *args == ' ') args++;

    if (!args || !*args || strncmp(args, "help", 4) == 0) {
        shell_print("hash - dispatch to checksum tools\n");
        shell_print("Usage:\n");
        shell_print("  hash sha256sum <file>...     Compute SHA-256 digests\n");
        shell_print("  hash sha256sum -c <sumfile>  Verify SHA-256 digests\n");
        shell_print("  hash md5sum <file>...        Compute MD5 digests\n");
        shell_print("  hash md5sum -c <sumfile>     Verify MD5 digests\n");
        shell_print("Aliases: sha256sum, md5sum\n");
        shell_last_exit_code = 0;
        return;
    }

    if (strncmp(args, "sha256", 6) == 0 || strncmp(args, "sha256sum", 9) == 0) {
        args += (args[3] == '2') ? 9 : 6;
        cmd_sha256sum(args);
        return;
    }
    if (strncmp(args, "md5", 3) == 0) {
        args += 3;
        if (*args == 's') {
            while (*args == 's' || *args == 'u' || *args == 'm') args++;
        }
        cmd_md5sum(args);
        return;
    }

    shell_print("hash: unknown subcommand '");
    {
        const char *q = args;
        while (*q && *q != ' ') {
            char ch = *q++;
            if (ch >= ' ' && ch < 0x7F) {
                char tmp[2] = { ch, 0 };
                shell_print(tmp);
            }
        }
    }
    shell_print("'. Try 'hash help'.\n");
    shell_last_exit_code = 1;
}
