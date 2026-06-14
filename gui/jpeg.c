#include "jpeg.h"
#include "kheap.h"
#include "string.h"

/* JPEG markers */
#define JPEG_SOI   0xD8
#define JPEG_SOF0  0xC0
#define JPEG_SOF2  0xC2
#define JPEG_DHT   0xC4
#define JPEG_DQT   0xDB
#define JPEG_SOS   0xDA
#define JPEG_EOI   0xD9
#define JPEG_RST0  0xD0
#define JPEG_RST7  0xD7
#define JPEG_APP0  0xE0
#define JPEG_COM   0xFE

/* AAN scaled IDCT constants (fixed-point, 13-bit fractional) */
#define AAN_C1  4096   /* 1.0      << 12 */
#define AAN_C2  5681   /* 1.387039 << 12 */
#define AAN_C3  3406   /* 0.831470 << 12 */
#define AAN_C4  2276   /* 0.555568 << 12 */
#define AAN_C5  1567   /* 0.382683 << 12 */
#define AAN_C6  3996   /* 0.975412 << 12 */
#define AAN_C7  2824   /* 0.689416 << 12 */

#define IDCT_SHIFT 12
#define IDCT_ROUND (1 << (IDCT_SHIFT - 1))

/* Huffman table */
typedef struct {
    uint8_t bits[17];    /* bits[1..16] = number of codes of each length */
    uint8_t vals[256];   /* symbol values in order */
    uint16_t count[17];  /* cumulative count for each length */
    uint16_t code[17];   /* first code for each length */
} jpeg_huff_table_t;

/* Quantization table */
typedef struct {
    uint8_t quant[64];
} jpeg_quant_table_t;

/* Component info */
typedef struct {
    uint8_t id;
    uint8_t h_samp;
    uint8_t v_samp;
    uint8_t quant_id;
    uint8_t dc_table;
    uint8_t ac_table;
    int16_t dc_pred;
} jpeg_component_t;

/* Decoder state */
typedef struct {
    const uint8_t *data;
    uint32_t size;
    uint32_t pos;

    uint32_t width;
    uint32_t height;
    uint8_t num_components;
    uint8_t precision;

    jpeg_component_t components[4];
    jpeg_huff_table_t dc_tables[4];
    jpeg_huff_table_t ac_tables[4];
    jpeg_quant_table_t quant_tables[4];

    int max_h_samp;
    int max_v_samp;

    /* Bitstream reader */
    uint32_t bit_buf;
    int bit_count;
} jpeg_decoder_t;

/* ---- Bitstream reader ---- */

static int jpeg_fill_bits(jpeg_decoder_t *d)
{
    while (d->bit_count <= 24) {
        if (d->pos >= d->size) return -1;
        uint8_t byte = d->data[d->pos++];
        if (byte == 0xFF) {
            if (d->pos >= d->size) return -1;
            uint8_t marker = d->data[d->pos++];
            if (marker != 0x00) {
                /* stuffed marker, put it back */
                d->pos -= 2;
                return 0;
            }
        }
        d->bit_buf = (d->bit_buf << 8) | byte;
        d->bit_count += 8;
    }
    return 0;
}

static int jpeg_read_bit(jpeg_decoder_t *d)
{
    if (d->bit_count == 0) {
        if (jpeg_fill_bits(d) < 0) return -1;
    }
    d->bit_count--;
    return (d->bit_buf >> d->bit_count) & 1;
}

static int jpeg_read_bits(jpeg_decoder_t *d, int n)
{
    int val = 0;
    for (int i = 0; i < n; i++) {
        int bit = jpeg_read_bit(d);
        if (bit < 0) return -1;
        val = (val << 1) | bit;
    }
    return val;
}

/* ---- Huffman decoding ---- */

static void jpeg_build_huff_table(jpeg_huff_table_t *t)
{
    int code = 0;
    int sym_idx = 0;
    for (int len = 1; len <= 16; len++) {
        t->count[len] = t->bits[len];
        t->code[len] = (uint16_t)code;
        code += t->bits[len];
        code <<= 1;
    }
    (void)sym_idx;
}

static int jpeg_decode_huff(jpeg_decoder_t *d, jpeg_huff_table_t *t)
{
    int code = 0;
    int sym_idx = 0;
    for (int len = 1; len <= 16; len++) {
        int bit = jpeg_read_bit(d);
        if (bit < 0) return -1;
        code = (code << 1) | bit;
        if (code < (int)t->code[len] + (int)t->bits[len]) {
            int idx = sym_idx + code - t->code[len];
            return t->vals[idx];
        }
        sym_idx += t->bits[len];
    }
    return -1;
}

/* Receive and extend a value from the bitstream */
static int jpeg_extend(int val, int bits)
{
    if (bits == 0) return 0;
    if (val < (1 << (bits - 1)))
        val -= (1 << bits);
    return val;
}

/* ---- IDCT (AAN fast IDCT) ---- */

static void jpeg_idct(int16_t *block)
{
    int z1, z3, z4, z5, tmp0, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7;
    int tmp10, tmp11, tmp12, tmp13;
    int16_t *blk = block;

    /* Pass 1: process columns */
    for (int i = 0; i < 8; i++) {
        tmp0 = blk[0 * 8 + i];
        tmp1 = blk[1 * 8 + i];
        tmp2 = blk[2 * 8 + i];
        tmp3 = blk[3 * 8 + i];
        tmp4 = blk[4 * 8 + i];
        tmp5 = blk[5 * 8 + i];
        tmp6 = blk[6 * 8 + i];
        tmp7 = blk[7 * 8 + i];

        tmp10 = tmp0 + tmp7;
        tmp13 = tmp0 - tmp7;
        tmp11 = tmp1 + tmp6;
        tmp12 = tmp1 - tmp6;

        tmp0 = tmp10 + tmp11;
        tmp1 = tmp10 - tmp11;

        z1 = (AAN_C6 * (tmp12 + tmp13) + IDCT_ROUND) >> IDCT_SHIFT;
        tmp2 = z1 - ((AAN_C2 * tmp12 + IDCT_ROUND) >> IDCT_SHIFT);
        tmp3 = z1 - ((AAN_C4 * tmp13 + IDCT_ROUND) >> IDCT_SHIFT);

        z1 = tmp4 + tmp5;
        z3 = tmp3 + tmp6;
        z4 = tmp2 + tmp7;

        z5 = (AAN_C3 * z3 + AAN_C7 * z4 + IDCT_ROUND) >> IDCT_SHIFT;
        tmp4 = (AAN_C1 * z1 + IDCT_ROUND) >> IDCT_SHIFT;
        tmp5 = (AAN_C5 * (z1 - z3 - z4) + IDCT_ROUND) >> IDCT_SHIFT;
        tmp6 = z5 - ((AAN_C7 * z3 + IDCT_ROUND) >> IDCT_SHIFT);
        tmp7 = z5 - ((AAN_C3 * z4 + IDCT_ROUND) >> IDCT_SHIFT);

        blk[0 * 8 + i] = (int16_t)(tmp0 + tmp4);
        blk[7 * 8 + i] = (int16_t)(tmp0 - tmp4);
        blk[1 * 8 + i] = (int16_t)(tmp1 + tmp7);
        blk[6 * 8 + i] = (int16_t)(tmp1 - tmp7);
        blk[2 * 8 + i] = (int16_t)(tmp2 + tmp6);
        blk[5 * 8 + i] = (int16_t)(tmp2 - tmp6);
        blk[3 * 8 + i] = (int16_t)(tmp3 + tmp5);
        blk[4 * 8 + i] = (int16_t)(tmp3 - tmp5);
    }

    /* Pass 2: process rows */
    for (int i = 0; i < 8; i++) {
        tmp0 = blk[i * 8 + 0];
        tmp1 = blk[i * 8 + 1];
        tmp2 = blk[i * 8 + 2];
        tmp3 = blk[i * 8 + 3];
        tmp4 = blk[i * 8 + 4];
        tmp5 = blk[i * 8 + 5];
        tmp6 = blk[i * 8 + 6];
        tmp7 = blk[i * 8 + 7];

        tmp10 = tmp0 + tmp7;
        tmp13 = tmp0 - tmp7;
        tmp11 = tmp1 + tmp6;
        tmp12 = tmp1 - tmp6;

        tmp0 = tmp10 + tmp11;
        tmp1 = tmp10 - tmp11;

        z1 = (AAN_C6 * (tmp12 + tmp13) + IDCT_ROUND) >> IDCT_SHIFT;
        tmp2 = z1 - ((AAN_C2 * tmp12 + IDCT_ROUND) >> IDCT_SHIFT);
        tmp3 = z1 - ((AAN_C4 * tmp13 + IDCT_ROUND) >> IDCT_SHIFT);

        z1 = tmp4 + tmp5;
        z3 = tmp3 + tmp6;
        z4 = tmp2 + tmp7;

        z5 = (AAN_C3 * z3 + AAN_C7 * z4 + IDCT_ROUND) >> IDCT_SHIFT;
        tmp4 = (AAN_C1 * z1 + IDCT_ROUND) >> IDCT_SHIFT;
        tmp5 = (AAN_C5 * (z1 - z3 - z4) + IDCT_ROUND) >> IDCT_SHIFT;
        tmp6 = z5 - ((AAN_C7 * z3 + IDCT_ROUND) >> IDCT_SHIFT);
        tmp7 = z5 - ((AAN_C3 * z4 + IDCT_ROUND) >> IDCT_SHIFT);

        blk[i * 8 + 0] = (int16_t)((tmp0 + tmp4 + IDCT_ROUND) >> IDCT_SHIFT);
        blk[i * 8 + 7] = (int16_t)((tmp0 - tmp4 + IDCT_ROUND) >> IDCT_SHIFT);
        blk[i * 8 + 1] = (int16_t)((tmp1 + tmp7 + IDCT_ROUND) >> IDCT_SHIFT);
        blk[i * 8 + 6] = (int16_t)((tmp1 - tmp7 + IDCT_ROUND) >> IDCT_SHIFT);
        blk[i * 8 + 2] = (int16_t)((tmp2 + tmp6 + IDCT_ROUND) >> IDCT_SHIFT);
        blk[i * 8 + 5] = (int16_t)((tmp2 - tmp6 + IDCT_ROUND) >> IDCT_SHIFT);
        blk[i * 8 + 3] = (int16_t)((tmp3 + tmp5 + IDCT_ROUND) >> IDCT_SHIFT);
        blk[i * 8 + 4] = (int16_t)((tmp3 - tmp5 + IDCT_ROUND) >> IDCT_SHIFT);
    }
}

/* ---- Zigzag order ---- */

static const uint8_t jpeg_zigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

/* ---- Marker parsing helpers ---- */

static uint16_t jpeg_read16(jpeg_decoder_t *d)
{
    if (d->pos + 2 > d->size) return 0;
    uint16_t val = ((uint16_t)d->data[d->pos] << 8) | d->data[d->pos + 1];
    d->pos += 2;
    return val;
}

static uint8_t jpeg_read8(jpeg_decoder_t *d)
{
    if (d->pos >= d->size) return 0;
    return d->data[d->pos++];
}

static int jpeg_parse_sof0(jpeg_decoder_t *d, uint32_t len)
{
    if (len < 8) return -1;

    d->precision = jpeg_read8(d);
    d->height = jpeg_read16(d);
    d->width = jpeg_read16(d);
    d->num_components = jpeg_read8(d);

    if (d->precision != 8) return -1;
    if (d->num_components != 1 && d->num_components != 3) return -1;
    if (len < (uint32_t)(8 + d->num_components * 3)) return -1;

    d->max_h_samp = 1;
    d->max_v_samp = 1;

    for (int i = 0; i < d->num_components; i++) {
        d->components[i].id = jpeg_read8(d);
        uint8_t samp = jpeg_read8(d);
        d->components[i].h_samp = (samp >> 4) & 0x0F;
        d->components[i].v_samp = samp & 0x0F;
        d->components[i].quant_id = jpeg_read8(d);
        d->components[i].dc_pred = 0;

        if (d->components[i].h_samp > d->max_h_samp)
            d->max_h_samp = d->components[i].h_samp;
        if (d->components[i].v_samp > d->max_v_samp)
            d->max_v_samp = d->components[i].v_samp;
    }

    return 0;
}

static int jpeg_parse_dht(jpeg_decoder_t *d, uint32_t len)
{
    uint32_t end = d->pos + len - 2;
    while (d->pos < end) {
        uint8_t info = jpeg_read8(d);
        int table_class = (info >> 4) & 0x01; /* 0=DC, 1=AC */
        int table_id = info & 0x0F;
        if (table_id > 3) return -1;

        jpeg_huff_table_t *t;
        if (table_class == 0)
            t = &d->dc_tables[table_id];
        else
            t = &d->ac_tables[table_id];

        memset(t->bits, 0, sizeof(t->bits));
        int total = 0;
        for (int i = 1; i <= 16; i++) {
            t->bits[i] = jpeg_read8(d);
            total += t->bits[i];
        }
        if (total > 256) return -1;

        for (int i = 0; i < total; i++) {
            t->vals[i] = jpeg_read8(d);
        }

        jpeg_build_huff_table(t);
    }
    return 0;
}

static int jpeg_parse_dqt(jpeg_decoder_t *d, uint32_t len)
{
    uint32_t end = d->pos + len - 2;
    while (d->pos < end) {
        uint8_t info = jpeg_read8(d);
        int precision = (info >> 4) & 0x0F; /* 0=8bit, 1=16bit */
        int table_id = info & 0x0F;
        if (table_id > 3) return -1;

        jpeg_quant_table_t *t = &d->quant_tables[table_id];

        if (precision == 0) {
            /* 8-bit precision */
            for (int i = 0; i < 64; i++) {
                t->quant[jpeg_zigzag[i]] = jpeg_read8(d);
            }
        } else {
            /* 16-bit precision - store lower 8 bits */
            for (int i = 0; i < 64; i++) {
                uint16_t val = jpeg_read16(d);
                t->quant[jpeg_zigzag[i]] = (uint8_t)(val >> (val > 255 ? 8 : 0));
            }
        }
    }
    return 0;
}

static int jpeg_parse_sos(jpeg_decoder_t *d, uint32_t len)
{
    if (len < 6) return -1;

    uint8_t num_comp = jpeg_read8(d);
    if (num_comp != d->num_components) return -1;
    if (len < (uint32_t)(4 + num_comp * 2 + 3)) return -1;

    for (int i = 0; i < num_comp; i++) {
        uint8_t comp_id = jpeg_read8(d);
        uint8_t table_info = jpeg_read8(d);

        /* Find matching component */
        int found = 0;
        for (int j = 0; j < d->num_components; j++) {
            if (d->components[j].id == comp_id) {
                d->components[j].dc_table = (table_info >> 4) & 0x0F;
                d->components[j].ac_table = table_info & 0x0F;
                found = 1;
                break;
            }
        }
        if (!found) return -1;
    }

    /* Skip spectral selection and successive approximation */
    jpeg_read8(d); /* Ss */
    jpeg_read8(d); /* Se */
    jpeg_read8(d); /* Ah/Al */

    return 0;
}

/* ---- Decode one 8x8 block ---- */

static int jpeg_decode_block(jpeg_decoder_t *d, int comp_idx, int16_t *block)
{
    jpeg_component_t *c = &d->components[comp_idx];
    jpeg_huff_table_t *dc_table = &d->dc_tables[c->dc_table];
    jpeg_huff_table_t *ac_table = &d->ac_tables[c->ac_table];
    jpeg_quant_table_t *qt = &d->quant_tables[c->quant_id];

    memset(block, 0, 64 * sizeof(int16_t));

    /* Decode DC coefficient */
    int dc_bits = jpeg_decode_huff(d, dc_table);
    if (dc_bits < 0) return -1;
    if (dc_bits > 11) return -1;

    int dc_val = 0;
    if (dc_bits > 0) {
        dc_val = jpeg_read_bits(d, dc_bits);
        if (dc_val < 0) return -1;
        dc_val = jpeg_extend(dc_val, dc_bits);
    }

    c->dc_pred += dc_val;
    block[0] = (int16_t)(c->dc_pred * (int16_t)qt->quant[0]);

    /* Decode AC coefficients */
    int k = 1;
    while (k < 64) {
        int ac_sym = jpeg_decode_huff(d, ac_table);
        if (ac_sym < 0) return -1;

        if (ac_sym == 0) break; /* EOB */

        int run = (ac_sym >> 4) & 0x0F;
        int bits = ac_sym & 0x0F;

        k += run;
        if (k >= 64) return -1;

        if (bits > 0) {
            int ac_val = jpeg_read_bits(d, bits);
            if (ac_val < 0) return -1;
            ac_val = jpeg_extend(ac_val, bits);
            block[jpeg_zigzag[k]] = (int16_t)(ac_val * (int16_t)qt->quant[jpeg_zigzag[k]]);
            k++;
        } else {
            /* ZRL - 16 zero run */
            k += 15;
        }
    }

    /* IDCT */
    jpeg_idct(block);

    return 0;
}

/* ---- YCbCr to RGB conversion ---- */

static void jpeg_ycbcr_to_rgb(int y, int cb, int cr, uint8_t *r, uint8_t *g, uint8_t *b)
{
    /* Fixed-point YCbCr -> RGB: scale by 1024 */
    int rr = ((y << 10) + 1436 * (cr - 128) + 512) >> 10;
    int gg = ((y << 10) - 352 * (cb - 128) - 731 * (cr - 128) + 512) >> 10;
    int bb = ((y << 10) + 1815 * (cb - 128) + 512) >> 10;

    if (rr < 0) rr = 0; else if (rr > 255) rr = 255;
    if (gg < 0) gg = 0; else if (gg > 255) gg = 255;
    if (bb < 0) bb = 0; else if (bb > 255) bb = 255;

    *r = (uint8_t)rr;
    *g = (uint8_t)gg;
    *b = (uint8_t)bb;
}

/* ---- Decode MCU ---- */

static int jpeg_decode_mcu(jpeg_decoder_t *d, int16_t *blocks[4],
                           int mcu_x, int mcu_y, uint8_t *rgb_out,
                           uint32_t img_w, uint32_t img_h)
{
    int max_h = d->max_h_samp;
    int max_v = d->max_v_samp;

    /* Decode all blocks for each component */
    int block_idx = 0;
    for (int c = 0; c < d->num_components; c++) {
        int h_samp = d->components[c].h_samp;
        int v_samp = d->components[c].v_samp;
        int num_blocks = h_samp * v_samp;

        for (int b = 0; b < num_blocks; b++) {
            if (jpeg_decode_block(d, c, blocks[block_idx + b]) < 0)
                return -1;
        }
        block_idx += num_blocks;
    }

    /* Write pixels */
    int mcu_w = max_h * 8;
    int mcu_h = max_v * 8;
    int base_x = mcu_x * mcu_w;
    int base_y = mcu_y * mcu_h;

    for (int py = 0; py < mcu_h; py++) {
        int y = base_y + py;
        if (y >= (int)img_h) continue;

        for (int px = 0; px < mcu_w; px++) {
            int x = base_x + px;
            if (x >= (int)img_w) continue;

            if (d->num_components == 1) {
                /* Grayscale */
                int h_samp = d->components[0].h_samp;
                int v_samp = d->components[0].v_samp;
                int bx = px / (max_h * 8 / (h_samp * 8));
                int by = py / (max_v * 8 / (v_samp * 8));
                int ix = (px * h_samp / max_h) % 8;
                int iy = (py * v_samp / max_v) % 8;
                int16_t val = blocks[0][by * 8 * h_samp * 8 + bx * 64 + iy * 8 + ix];
                if (val < 0) val = 0; else if (val > 255) val = 255;
                uint8_t v = (uint8_t)val;
                rgb_out[(y * img_w + x) * 3 + 0] = v;
                rgb_out[(y * img_w + x) * 3 + 1] = v;
                rgb_out[(y * img_w + x) * 3 + 2] = v;
            } else {
                /* YCbCr - find Y block */
                int y_comp = -1;
                for (int c = 0; c < d->num_components; c++) {
                    if (d->components[c].id == 1) { y_comp = c; break; }
                }
                /* If component order is not standard, use first as Y */
                if (y_comp < 0) y_comp = 0;

                int y_h = d->components[y_comp].h_samp;
                int y_v = d->components[y_comp].v_samp;

                int y_block_x = (px * y_h / max_h) / 8;
                int y_block_y = (py * y_v / max_v) / 8;
                int y_ix = (px * y_h / max_h) % 8;
                int y_iy = (py * y_v / max_v) % 8;

                int y_block_idx = 0;
                for (int c = 0; c < y_comp; c++) {
                    y_block_idx += d->components[c].h_samp * d->components[c].v_samp;
                }
                y_block_idx += y_block_y * y_h + y_block_x;

                int16_t y_val = blocks[y_block_idx][y_iy * 8 + y_ix];

                /* Cb block */
                int cb_comp = -1;
                for (int c = 0; c < d->num_components; c++) {
                    if (d->components[c].id == 2) { cb_comp = c; break; }
                }
                if (cb_comp < 0) cb_comp = 1;

                int cb_h = d->components[cb_comp].h_samp;
                int cb_v = d->components[cb_comp].v_samp;
                int cb_block_x = (px * cb_h / max_h) / 8;
                int cb_block_y = (py * cb_v / max_v) / 8;
                int cb_ix = (px * cb_h / max_h) % 8;
                int cb_iy = (py * cb_v / max_v) % 8;

                int cb_block_idx = 0;
                for (int c = 0; c < cb_comp; c++) {
                    cb_block_idx += d->components[c].h_samp * d->components[c].v_samp;
                }
                cb_block_idx += cb_block_y * cb_h + cb_block_x;

                int16_t cb_val = blocks[cb_block_idx][cb_iy * 8 + cb_ix];

                /* Cr block */
                int cr_comp = -1;
                for (int c = 0; c < d->num_components; c++) {
                    if (d->components[c].id == 3) { cr_comp = c; break; }
                }
                if (cr_comp < 0) cr_comp = 2;

                int cr_h = d->components[cr_comp].h_samp;
                int cr_v = d->components[cr_comp].v_samp;
                int cr_block_x = (px * cr_h / max_h) / 8;
                int cr_block_y = (py * cr_v / max_v) / 8;
                int cr_ix = (px * cr_h / max_h) % 8;
                int cr_iy = (py * cr_v / max_v) % 8;

                int cr_block_idx = 0;
                for (int c = 0; c < cr_comp; c++) {
                    cr_block_idx += d->components[c].h_samp * d->components[c].v_samp;
                }
                cr_block_idx += cr_block_y * cr_h + cr_block_x;

                int16_t cr_val = blocks[cr_block_idx][cr_iy * 8 + cr_ix];

                uint8_t r, g, b;
                jpeg_ycbcr_to_rgb(y_val, cb_val, cr_val, &r, &g, &b);
                rgb_out[(y * img_w + x) * 3 + 0] = r;
                rgb_out[(y * img_w + x) * 3 + 1] = g;
                rgb_out[(y * img_w + x) * 3 + 2] = b;
            }
        }
    }

    return 0;
}

/* ---- Main decode function ---- */

int jpeg_decode(const uint8_t *data, uint32_t size, uint8_t **out_rgb,
                uint32_t *out_width, uint32_t *out_height)
{
    if (!data || size < 4 || !out_rgb || !out_width || !out_height)
        return -1;

    jpeg_decoder_t dec;
    memset(&dec, 0, sizeof(dec));
    dec.data = data;
    dec.size = size;
    dec.pos = 0;

    /* Check SOI */
    if (jpeg_read16(&dec) != ((0xFF << 8) | JPEG_SOI))
        return -1;

    /* Parse markers */
    int found_sof = 0;
    int found_sos = 0;

    while (dec.pos < dec.size) {
        uint8_t marker_hi = jpeg_read8(&dec);
        if (marker_hi != 0xFF) continue;

        uint8_t marker_lo;
        do {
            marker_lo = jpeg_read8(&dec);
        } while (marker_lo == 0xFF && dec.pos < dec.size);

        /* Skip restart markers and fill bytes */
        if (marker_lo == 0x00 || (marker_lo >= JPEG_RST0 && marker_lo <= JPEG_RST7))
            continue;

        if (marker_lo == JPEG_EOI) break;

        uint16_t seg_len = jpeg_read16(&dec);
        if (seg_len < 2) return -1;
        uint32_t payload_len = seg_len - 2;

        switch (marker_lo) {
        case JPEG_SOF0:
            if (jpeg_parse_sof0(&dec, seg_len) < 0) return -1;
            found_sof = 1;
            break;

        case JPEG_DHT:
            if (jpeg_parse_dht(&dec, seg_len) < 0) return -1;
            break;

        case JPEG_DQT:
            if (jpeg_parse_dqt(&dec, seg_len) < 0) return -1;
            break;

        case JPEG_SOS:
            if (jpeg_parse_sos(&dec, seg_len) < 0) return -1;
            found_sos = 1;
            break;

        case JPEG_SOF2:
            /* Progressive JPEG not supported */
            return -1;

        default:
            /* Skip unknown/app markers */
            dec.pos += payload_len;
            break;
        }

        if (found_sos) break;
    }

    if (!found_sof || !found_sos) return -1;
    if (dec.width == 0 || dec.height == 0) return -1;

    /* Allocate output buffer */
    uint32_t rgb_size = dec.width * dec.height * 3;
    uint8_t *rgb = (uint8_t *)kmalloc(rgb_size);
    if (!rgb) return -1;
    memset(rgb, 0, rgb_size);

    /* Allocate block buffers - max 10 blocks per MCU (Y:2x2=4, Cb:1, Cr:1 for 4:2:0) */
    int total_blocks = 0;
    for (int i = 0; i < dec.num_components; i++) {
        total_blocks += dec.components[i].h_samp * dec.components[i].v_samp;
    }
    if (total_blocks > 10) {
        kfree(rgb);
        return -1;
    }

    int16_t *block_buf = (int16_t *)kmalloc(total_blocks * 64 * sizeof(int16_t));
    if (!block_buf) {
        kfree(rgb);
        return -1;
    }

    int16_t *block_ptrs[10];
    for (int i = 0; i < total_blocks; i++) {
        block_ptrs[i] = block_buf + i * 64;
    }

    /* Calculate MCU grid */
    int mcu_w = dec.max_h_samp * 8;
    int mcu_h = dec.max_v_samp * 8;
    int mcus_x = (dec.width + mcu_w - 1) / mcu_w;
    int mcus_y = (dec.height + mcu_h - 1) / mcu_h;

    /* Initialize bitstream reader */
    dec.bit_buf = 0;
    dec.bit_count = 0;

    /* Decode all MCUs */
    for (int my = 0; my < mcus_y; my++) {
        for (int mx = 0; mx < mcus_x; mx++) {
            if (jpeg_decode_mcu(&dec, block_ptrs, mx, my, rgb,
                                dec.width, dec.height) < 0) {
                kfree(rgb);
                kfree(block_buf);
                return -1;
            }
        }
    }

    kfree(block_buf);

    *out_rgb = rgb;
    *out_width = dec.width;
    *out_height = dec.height;
    return 0;
}

void jpeg_free(uint8_t *rgb_data)
{
    if (rgb_data) kfree(rgb_data);
}
