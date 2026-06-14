#include "png.h"
#include "kheap.h"
#include "string.h"

/* PNG signature */
static const uint8_t png_signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};

/* Chunk type constants */
#define PNG_IHDR 0x49484452
#define PNG_IDAT 0x49444154
#define PNG_IEND 0x49454E44

/* PNG color types */
#define PNG_COLOR_GRAY  0
#define PNG_COLOR_RGB   2
#define PNG_COLOR_RGBA  6

/* PNG filter types */
#define PNG_FILTER_NONE    0
#define PNG_FILTER_SUB     1
#define PNG_FILTER_UP      2
#define PNG_FILTER_AVG     3
#define PNG_FILTER_PAETH   4

/* ---- CRC32 ---- */

static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
    0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
    0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
    0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE,
    0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
    0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
    0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
    0x35B5A8FA, 0x42B2986C, 0xDBBBBBD6, 0xACBCCB40,
    0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116,
    0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
    0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
    0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A,
    0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818,
    0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
    0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
    0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C,
    0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2,
    0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
    0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7B49,
    0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086,
    0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4,
    0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
    0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
    0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
    0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE,
    0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
    0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
    0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252,
    0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60,
    0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
    0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
    0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04,
    0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
    0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
    0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
    0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E,
    0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C,
    0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
    0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
    0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0,
    0xA9BCAE53, 0xDEBB9EC5, 0x47D7897F, 0x30D0B8E9,
    0xBDDA9B7C, 0xCADD6CEA, 0x53D49050, 0x24D380C6,
    0xBA3C6B65, 0xCDBE3AF3, 0x54C5B549, 0x23C248DF,
    0xB3A56E9E, 0xC4B8A608, 0x5DB1BF12, 0x2AB68484,
    0xB966D481, 0xCE61E49F, 0x5EDEF90E, 0x29D9C998
};

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}

/* ---- Zlib / Deflate decompression ---- */

typedef struct {
    const uint8_t *data;
    uint32_t size;
    uint32_t pos;
    uint32_t bit_buf;
    int bit_count;
} inflate_state_t;

/* Length codes 257..285 base values and extra bits */
static const uint16_t len_base[29] = {
    3,4,5,6,7,8,9,10, 11,13,15,17, 19,23,27,31,
    35,43,51,59, 67,83,99,115, 131,163,195,227, 258
};
static const uint8_t len_extra[29] = {
    0,0,0,0,0,0,0,0, 1,1,1,1, 2,2,2,2,
    3,3,3,3, 4,4,4,4, 5,5,5,5, 0
};

/* Distance codes 0..29 base values and extra bits */
static const uint16_t dist_base[30] = {
    1,2,3,4, 5,7,9,13, 17,25,33,49, 65,97,129,193,
    257,385,513,769, 1025,1537,2049,3073, 4097,6145,8193,12289,
    16385,24577
};
static const uint8_t dist_extra[30] = {
    0,0,0,0, 1,1,2,2, 3,3,4,4, 5,5,6,6,
    7,7,8,8, 9,9,10,10, 11,11,12,12, 13,13
};

static void inflate_init(inflate_state_t *s, const uint8_t *data, uint32_t size)
{
    s->data = data;
    s->size = size;
    s->pos = 0;
    s->bit_buf = 0;
    s->bit_count = 0;
}

static int inflate_read_bits(inflate_state_t *s, int n)
{
    while (s->bit_count < n) {
        if (s->pos >= s->size) return -1;
        s->bit_buf |= (uint32_t)s->data[s->pos++] << s->bit_count;
        s->bit_count += 8;
    }
    int val = s->bit_buf & ((1 << n) - 1);
    s->bit_buf >>= n;
    s->bit_count -= n;
    return val;
}

static void inflate_align(inflate_state_t *s)
{
    s->bit_buf = 0;
    s->bit_count = 0;
}

/* Decode a fixed Huffman literal/length symbol.
 * Fixed Huffman code structure:
 *   7-bit codes: 0000000-0010111 -> symbols 256-279
 *   8-bit codes: 00110000-10111111 -> symbols 0-143
 *   8-bit codes: 11000000-11000111 -> symbols 280-287
 *   9-bit codes: 110010000-111111111 -> symbols 144-255
 */
static int inflate_decode_fixed_lit(inflate_state_t *s)
{
    int bits7 = inflate_read_bits(s, 7);
    if (bits7 < 0) return -1;

    if (bits7 <= 0x17) {
        return 256 + bits7;
    }

    int bit8 = inflate_read_bits(s, 1);
    if (bit8 < 0) return -1;
    int bits8 = (bits7 << 1) | bit8;

    if (bits8 >= 0x30 && bits8 <= 0xBF) {
        return bits8 - 0x30;
    }
    if (bits8 >= 0xC0 && bits8 <= 0xC7) {
        return 280 + (bits8 - 0xC0);
    }

    int bit9 = inflate_read_bits(s, 1);
    if (bit9 < 0) return -1;
    int bits9 = (bits8 << 1) | bit9;

    if (bits9 >= 0x190 && bits9 <= 0x1FF) {
        return 144 + (bits9 - 0x190);
    }

    return -1;
}

/* Decode a fixed Huffman distance symbol (5-bit codes, 0-29) */
static int inflate_decode_fixed_dist(inflate_state_t *s)
{
    return inflate_read_bits(s, 5);
}

static int inflate_block_stored(inflate_state_t *s, uint8_t *out, uint32_t *out_pos, uint32_t out_size)
{
    inflate_align(s);

    if (s->pos + 4 > s->size) return -1;
    uint16_t len = (uint16_t)s->data[s->pos] | ((uint16_t)s->data[s->pos + 1] << 8);
    uint16_t nlen = (uint16_t)s->data[s->pos + 2] | ((uint16_t)s->data[s->pos + 3] << 8);
    s->pos += 4;

    if (len != (uint16_t)(~nlen)) return -1;
    if (s->pos + len > s->size) return -1;
    if (*out_pos + len > out_size) return -1;

    memcpy(out + *out_pos, s->data + s->pos, len);
    s->pos += len;
    *out_pos += len;

    return 0;
}

static int inflate_block_fixed(inflate_state_t *s, uint8_t *out, uint32_t *out_pos, uint32_t out_size)
{
    for (;;) {
        int sym = inflate_decode_fixed_lit(s);
        if (sym < 0) return -1;

        if (sym < 256) {
            if (*out_pos >= out_size) return -1;
            out[*out_pos] = (uint8_t)sym;
            (*out_pos)++;
        } else if (sym == 256) {
            break;
        } else {
            int len_idx = sym - 257;
            if (len_idx < 0 || len_idx >= 29) return -1;

            uint16_t length = len_base[len_idx];
            if (len_extra[len_idx] > 0) {
                int extra = inflate_read_bits(s, len_extra[len_idx]);
                if (extra < 0) return -1;
                length += extra;
            }

            int dist_code = inflate_decode_fixed_dist(s);
            if (dist_code < 0 || dist_code >= 30) return -1;

            uint16_t dist = dist_base[dist_code];
            if (dist_extra[dist_code] > 0) {
                int extra = inflate_read_bits(s, dist_extra[dist_code]);
                if (extra < 0) return -1;
                dist += extra;
            }

            if (dist > *out_pos) return -1;
            if (*out_pos + length > out_size) return -1;

            for (uint16_t i = 0; i < length; i++) {
                out[*out_pos] = out[*out_pos - dist];
                (*out_pos)++;
            }
        }
    }
    return 0;
}

static int zlib_inflate(const uint8_t *data, uint32_t size, uint8_t **out, uint32_t *out_size)
{
    inflate_state_t s;
    inflate_init(&s, data, size);

    /* Read zlib header (2 bytes) */
    if (s.pos + 2 > s.size) return -1;
    s.pos += 2;

    /* Allocate initial output buffer */
    uint32_t buf_size = size * 4;
    if (buf_size < 4096) buf_size = 4096;
    uint8_t *buf = (uint8_t *)kmalloc(buf_size);
    if (!buf) return -1;

    uint32_t out_pos = 0;

    int bfinal = 0;
    while (!bfinal) {
        bfinal = inflate_read_bits(&s, 1);
        if (bfinal < 0) { kfree(buf); return -1; }

        int btype = inflate_read_bits(&s, 2);
        if (btype < 0) { kfree(buf); return -1; }

        if (btype == 0) {
            if (inflate_block_stored(&s, buf, &out_pos, buf_size) < 0) {
                kfree(buf);
                return -1;
            }
        } else if (btype == 1) {
            if (inflate_block_fixed(&s, buf, &out_pos, buf_size) < 0) {
                kfree(buf);
                return -1;
            }
        } else {
            kfree(buf);
            return -1;
        }
    }

    *out = buf;
    *out_size = out_pos;
    return 0;
}

/* ---- PNG chunk helpers ---- */

static uint32_t png_read32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* ---- Paeth predictor ---- */

static int paeth_predict(int a, int b, int c)
{
    int p = a + b - c;
    int pa = p - a; if (pa < 0) pa = -pa;
    int pb = p - b; if (pb < 0) pb = -pb;
    int pc = p - c; if (pc < 0) pc = -pc;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

/* ---- PNG row filter reconstruction ---- */

static void png_unfilter_row(uint8_t filter, uint8_t *row, const uint8_t *prev,
                              uint32_t row_size, uint8_t bpp)
{
    uint32_t i;
    switch (filter) {
    case PNG_FILTER_NONE:
        break;

    case PNG_FILTER_SUB:
        for (i = bpp; i < row_size; i++) {
            row[i] = row[i] + row[i - bpp];
        }
        break;

    case PNG_FILTER_UP:
        if (prev) {
            for (i = 0; i < row_size; i++) {
                row[i] = row[i] + prev[i];
            }
        }
        break;

    case PNG_FILTER_AVG:
        for (i = 0; i < row_size; i++) {
            int a = (i >= bpp) ? row[i - bpp] : 0;
            int b = prev ? prev[i] : 0;
            row[i] = (uint8_t)(row[i] + ((a + b) >> 1));
        }
        break;

    case PNG_FILTER_PAETH:
        for (i = 0; i < row_size; i++) {
            int a = (i >= bpp) ? row[i - bpp] : 0;
            int b = prev ? prev[i] : 0;
            int c = (i >= bpp && prev) ? prev[i - bpp] : 0;
            row[i] = (uint8_t)(row[i] + paeth_predict(a, b, c));
        }
        break;
    }
}

/* ---- Main decode function ---- */

int png_decode(const uint8_t *data, uint32_t size, uint8_t **out_rgb,
               uint32_t *out_width, uint32_t *out_height)
{
    if (!data || size < 8 || !out_rgb || !out_width || !out_height)
        return -1;

    /* Verify PNG signature */
    for (int i = 0; i < 8; i++) {
        if (data[i] != png_signature[i]) return -1;
    }

    uint32_t pos = 8;

    /* IHDR data */
    uint32_t width = 0, height = 0;
    uint8_t bit_depth = 0, color_type = 0;
    uint8_t compression = 0, filter_method = 0, interlace = 0;
    int ihdr_found = 0;

    /* IDAT collection */
    uint8_t *idat_buf = 0;
    uint32_t idat_size = 0;
    uint32_t idat_cap = 0;

    while (pos + 12 <= size) {
        /* Read chunk: length(4) + type(4) + data(length) + crc(4) */
        uint32_t chunk_len = png_read32(data + pos);
        pos += 4;

        if (pos + 4 > size) break;
        uint32_t chunk_type = png_read32(data + pos);
        pos += 4;

        if (pos + chunk_len > size) break;
        const uint8_t *chunk_data = data + pos;
        pos += chunk_len;

        if (pos + 4 > size) break;
        uint32_t chunk_crc = png_read32(data + pos);
        pos += 4;

        /* CRC verification: compute CRC over type + data */
        uint32_t computed_crc = 0xFFFFFFFF;
        computed_crc = crc32_update(computed_crc, data + pos - chunk_len - 8, 4 + chunk_len);
        computed_crc ^= 0xFFFFFFFF;

        if (computed_crc != chunk_crc) goto fail;

        switch (chunk_type) {
        case PNG_IHDR:
            if (chunk_len < 13) goto fail;
            width = png_read32(chunk_data);
            height = png_read32(chunk_data + 4);
            bit_depth = chunk_data[8];
            color_type = chunk_data[9];
            compression = chunk_data[10];
            filter_method = chunk_data[11];
            interlace = chunk_data[12];
            ihdr_found = 1;
            break;

        case PNG_IDAT:
            /* Append to IDAT buffer */
            if (idat_size + chunk_len > idat_cap) {
                uint32_t new_cap = idat_cap ? idat_cap * 2 : chunk_len + 1024;
                if (new_cap < idat_size + chunk_len) new_cap = idat_size + chunk_len;
                uint8_t *new_buf = (uint8_t *)kmalloc(new_cap);
                if (!new_buf) goto fail;
                if (idat_buf) {
                    memcpy(new_buf, idat_buf, idat_size);
                    kfree(idat_buf);
                }
                idat_buf = new_buf;
                idat_cap = new_cap;
            }
            memcpy(idat_buf + idat_size, chunk_data, chunk_len);
            idat_size += chunk_len;
            break;

        case PNG_IEND:
            goto done_parsing;

        default:
            break;
        }
    }

done_parsing:
    if (!ihdr_found || !idat_buf) goto fail;

    /* Validate IHDR */
    if (width == 0 || height == 0) goto fail;
    if (bit_depth != 8) goto fail;
    if (color_type != PNG_COLOR_GRAY && color_type != PNG_COLOR_RGB && color_type != PNG_COLOR_RGBA)
        goto fail;
    if (compression != 0 || filter_method != 0) goto fail;
    if (interlace != 0) goto fail;

    /* Decompress IDAT data */
    uint8_t *raw_data = 0;
    uint32_t raw_size = 0;
    if (zlib_inflate(idat_buf, idat_size, &raw_data, &raw_size) < 0) goto fail;

    kfree(idat_buf);
    idat_buf = 0;

    /* Calculate expected raw size */
    uint8_t bpp;
    if (color_type == PNG_COLOR_GRAY) bpp = 1;
    else if (color_type == PNG_COLOR_RGB) bpp = 3;
    else bpp = 4; /* RGBA */

    uint32_t row_bytes = width * bpp;
    uint32_t expected_size = height * (1 + row_bytes);
    if (raw_size < expected_size) { kfree(raw_data); return -1; }

    /* Allocate RGB output */
    uint32_t rgb_size = width * height * 3;
    uint8_t *rgb = (uint8_t *)kmalloc(rgb_size);
    if (!rgb) { kfree(raw_data); return -1; }

    /* Allocate two row buffers for filtering */
    uint8_t *cur_row = (uint8_t *)kmalloc(row_bytes);
    uint8_t *prev_row = (uint8_t *)kmalloc(row_bytes);
    if (!cur_row || !prev_row) {
        if (cur_row) kfree(cur_row);
        if (prev_row) kfree(prev_row);
        kfree(raw_data);
        kfree(rgb);
        return -1;
    }

    memset(prev_row, 0, row_bytes);

    uint32_t raw_pos = 0;
    for (uint32_t y = 0; y < height; y++) {
        if (raw_pos >= raw_size) break;
        uint8_t filter = raw_data[raw_pos++];
        if (filter > PNG_FILTER_PAETH) { goto cleanup_fail; }

        memcpy(cur_row, raw_data + raw_pos, row_bytes);
        raw_pos += row_bytes;

        png_unfilter_row(filter, cur_row, prev_row, row_bytes, bpp);

        /* Convert to RGB888 */
        for (uint32_t x = 0; x < width; x++) {
            uint8_t r, g, b;
            if (color_type == PNG_COLOR_GRAY) {
                r = g = b = cur_row[x];
            } else if (color_type == PNG_COLOR_RGB) {
                r = cur_row[x * 3 + 0];
                g = cur_row[x * 3 + 1];
                b = cur_row[x * 3 + 2];
            } else { /* RGBA */
                r = cur_row[x * 4 + 0];
                g = cur_row[x * 4 + 1];
                b = cur_row[x * 4 + 2];
            }
            rgb[(y * width + x) * 3 + 0] = r;
            rgb[(y * width + x) * 3 + 1] = g;
            rgb[(y * width + x) * 3 + 2] = b;
        }

        /* Swap row buffers */
        uint8_t *tmp = cur_row;
        cur_row = prev_row;
        prev_row = tmp;
    }

    kfree(cur_row);
    kfree(prev_row);
    kfree(raw_data);

    *out_rgb = rgb;
    *out_width = width;
    *out_height = height;
    return 0;

cleanup_fail:
    kfree(cur_row);
    kfree(prev_row);
    kfree(raw_data);
    kfree(rgb);
    return -1;

fail:
    if (idat_buf) kfree(idat_buf);
    return -1;
}

void png_free(uint8_t *rgb_data)
{
    if (rgb_data) kfree(rgb_data);
}
