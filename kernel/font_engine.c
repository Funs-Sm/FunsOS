#include "font_engine.h"
#include "font.h"
#include "freetype_mini.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"
#include "vfs.h"
#include "path.h"

/* Glyph cache */
#define FE_CACHE_SIZE 512

typedef struct {
    uint32_t codepoint;
    uint32_t face_id;
    glyph_t glyph;
    uint8_t in_use;
} cache_entry_t;

static cache_entry_t fe_cache[FE_CACHE_SIZE];
static uint32_t fe_cache_next;

/* Active font faces */
#define FE_MAX_FACES 8
static font_face_t *fe_faces[FE_MAX_FACES];
static uint32_t fe_face_count;

/* Default built-in font face */
static font_face_t fe_builtin_face;

/* Cyrillic supplement (U+0400-U+04FF) - basic block characters */
/* We map common Cyrillic letters to approximate Latin equivalents for the bitmap font */
static uint32_t cyrillic_to_latin(uint32_t cp) {
    /* Map Cyrillic uppercase to Latin look-alikes */
    switch (cp) {
        case 0x0410: return 'A';  /* A */
        case 0x0411: return 'B';  /* B (approx) */
        case 0x0412: return 'B';  /* B (approx) */
        case 0x0413: return 'T';  /* Gamma ~ T */
        case 0x0414: return 'D';  /* D (approx) */
        case 0x0415: return 'E';  /* E */
        case 0x0416: return 'X';  /* Zhe ~ X */
        case 0x0417: return '3';  /* Z (approx) */
        case 0x0418: return 'N';  /* I ~ N */
        case 0x0419: return 'N';  /* I short */
        case 0x041A: return 'K';  /* K */
        case 0x041B: return 'N';  /* L (approx) */
        case 0x041C: return 'M';  /* M */
        case 0x041D: return 'H';  /* N ~ H */
        case 0x041E: return 'O';  /* O */
        case 0x041F: return 'N';  /* P (approx) */
        case 0x0420: return 'P';  /* R */
        case 0x0421: return 'C';  /* S ~ C */
        case 0x0422: return 'T';  /* T */
        case 0x0423: return 'Y';  /* U ~ Y */
        case 0x0424: return 'O';  /* F (approx) */
        case 0x0425: return 'X';  /* Kh ~ X */
        case 0x0426: return 'U';  /* Ts (approx) */
        case 0x0427: return '4';  /* Ch (approx) */
        case 0x0428: return 'W';  /* Sha ~ W */
        case 0x0429: return 'W';  /* Shcha ~ W */
        case 0x042A: return 'b';  /* Hard sign */
        case 0x042B: return 'b';  /* Y (approx) */
        case 0x042C: return 'b';  /* Soft sign */
        case 0x042D: return '3';  /* E rev */
        case 0x042E: return 'O';  /* Yu (approx) */
        case 0x042F: return 'R';  /* Ya (approx) */
        /* Lowercase */
        case 0x0430: return 'a';
        case 0x0431: return 'b';
        case 0x0432: return 'b';
        case 0x0433: return 'r';
        case 0x0434: return 'd';
        case 0x0435: return 'e';
        case 0x0436: return 'x';
        case 0x0437: return '3';
        case 0x0438: return 'n';
        case 0x0439: return 'n';
        case 0x043A: return 'k';
        case 0x043B: return 'n';
        case 0x043C: return 'm';
        case 0x043D: return 'n';
        case 0x043E: return 'o';
        case 0x043F: return 'n';
        case 0x0440: return 'p';
        case 0x0441: return 'c';
        case 0x0442: return 't';
        case 0x0443: return 'y';
        case 0x0444: return 'o';
        case 0x0445: return 'x';
        case 0x0446: return 'u';
        case 0x0447: return '4';
        case 0x0448: return 'w';
        case 0x0449: return 'w';
        case 0x044A: return 'b';
        case 0x044B: return 'b';
        case 0x044C: return 'b';
        case 0x044D: return '3';
        case 0x044E: return 'o';
        case 0x044F: return 'r';
        default: return 0;
    }
}

/* Simple CJK box glyph - a filled rectangle placeholder */
/* (Rendered dynamically in fe_render_builtin_glyph) */

static int is_cjk_codepoint(uint32_t cp) {
    /* CJK Unified Ideographs: U+4E00-U+9FFF */
    if (cp >= 0x4E00 && cp <= 0x9FFF) return 1;
    /* CJK Extension A: U+3400-U+4DBF */
    if (cp >= 0x3400 && cp <= 0x4DBF) return 1;
    /* CJK Compatibility: U+F900-U+FAFF */
    if (cp >= 0xF900 && cp <= 0xFAFF) return 1;
    /* Hiragana: U+3040-U+309F */
    if (cp >= 0x3040 && cp <= 0x309F) return 1;
    /* Katakana: U+30A0-U+30FF */
    if (cp >= 0x30A0 && cp <= 0x30FF) return 1;
    /* CJK Symbols and Punctuation: U+3000-U+303F */
    if (cp >= 0x3000 && cp <= 0x303F) return 1;
    /* Fullwidth Forms: U+FF00-U+FFEF */
    if (cp >= 0xFF00 && cp <= 0xFFEF) return 1;
    return 0;
}

static int is_cyrillic_codepoint(uint32_t cp) {
    return (cp >= 0x0400 && cp <= 0x04FF);
}

void font_engine_init(void) {
    memset(fe_cache, 0, sizeof(fe_cache));
    fe_cache_next = 0;
    fe_face_count = 0;

    /* Initialize built-in font face */
    memset(&fe_builtin_face, 0, sizeof(fe_builtin_face));
    strcpy(fe_builtin_face.family, "Builtin");
    strcpy(fe_builtin_face.style, "Regular");
    fe_builtin_face.size = 16;
    fe_builtin_face.ascent = 12;
    fe_builtin_face.descent = 4;
    fe_builtin_face.line_gap = 2;
    fe_builtin_face.units_per_em = 16;
    fe_builtin_face.font_data = 0;

    fe_faces[0] = &fe_builtin_face;
    fe_face_count = 1;
}

static glyph_t *fe_cache_lookup(uint32_t face_id, uint32_t codepoint) {
    for (uint32_t i = 0; i < FE_CACHE_SIZE; i++) {
        if (fe_cache[i].in_use && fe_cache[i].face_id == face_id &&
            fe_cache[i].codepoint == codepoint) {
            return &fe_cache[i].glyph;
        }
    }
    return 0;
}

static glyph_t *fe_cache_store(uint32_t face_id, uint32_t codepoint) {
    /* Find free slot or evict oldest */
    uint32_t idx = fe_cache_next;
    fe_cache_next = (fe_cache_next + 1) % FE_CACHE_SIZE;

    /* Free old bitmap if any */
    if (fe_cache[idx].in_use && fe_cache[idx].glyph.bitmap) {
        kfree(fe_cache[idx].glyph.bitmap);
    }

    memset(&fe_cache[idx], 0, sizeof(cache_entry_t));
    fe_cache[idx].face_id = face_id;
    fe_cache[idx].codepoint = codepoint;
    fe_cache[idx].in_use = 1;

    return &fe_cache[idx].glyph;
}

static glyph_t *fe_render_builtin_glyph(uint32_t codepoint) {
    glyph_t *cached = fe_cache_lookup(0, codepoint);
    if (cached) return cached;

    glyph_t *g = fe_cache_store(0, codepoint);

    /* Handle CJK characters with box glyph */
    if (is_cjk_codepoint(codepoint)) {
        g->codepoint = codepoint;
        g->width = 16;
        g->height = 16;
        g->bearing_x = 0;
        g->bearing_y = 12;
        g->advance = 16;
        g->bitmap = (uint8_t *)kmalloc(16 * 16);
        if (g->bitmap) {
            /* Create a double-width box for CJK */
            for (int row = 0; row < 16; row++) {
                for (int col = 0; col < 16; col++) {
                    if (row == 0 || row == 15 || col == 0 || col == 15) {
                        g->bitmap[row * 16 + col] = 0xFF;
                    } else {
                        g->bitmap[row * 16 + col] = 0x00;
                    }
                }
            }
        }
        return g;
    }

    /* Handle Cyrillic by mapping to Latin look-alikes */
    if (is_cyrillic_codepoint(codepoint)) {
        uint32_t latin_cp = cyrillic_to_latin(codepoint);
        if (latin_cp >= 32 && latin_cp <= 127) {
            codepoint = latin_cp;
        } else {
            codepoint = '?';
        }
    }

    /* Standard ASCII bitmap font */
    if (codepoint < 32 || codepoint > 127) {
        codepoint = 32; /* space */
    }

    g->codepoint = codepoint;
    g->width = FONT_GLYPH_WIDTH;
    g->height = FONT_GLYPH_HEIGHT;
    g->bearing_x = 0;
    g->bearing_y = (int32_t)fe_builtin_face.ascent;
    g->advance = FONT_GLYPH_WIDTH;

    /* Convert 1-bit bitmap to 8-bit alpha */
    g->bitmap = (uint8_t *)kmalloc(FONT_GLYPH_WIDTH * FONT_GLYPH_HEIGHT);
    if (g->bitmap) {
        const uint8_t *src = font_data[codepoint - 32];
        for (int row = 0; row < FONT_GLYPH_HEIGHT; row++) {
            uint8_t bits = src[row];
            for (int col = 0; col < FONT_GLYPH_WIDTH; col++) {
                if (bits & (0x80 >> col)) {
                    g->bitmap[row * FONT_GLYPH_WIDTH + col] = 0xFF;
                } else {
                    g->bitmap[row * FONT_GLYPH_WIDTH + col] = 0x00;
                }
            }
        }
    }

    return g;
}

static glyph_t *fe_render_ttf_glyph(font_face_t *face, uint32_t codepoint) {
    ttf_font_t *ttf = (ttf_font_t *)face->font_data;
    if (!ttf) return fe_render_builtin_glyph(codepoint);

    /* Check cache */
    uint32_t face_id = 0;
    for (uint32_t i = 0; i < fe_face_count; i++) {
        if (fe_faces[i] == face) { face_id = i + 1; break; }
    }

    glyph_t *cached = fe_cache_lookup(face_id, codepoint);
    if (cached) return cached;

    /* Get glyph index */
    uint16_t glyph_idx = ttf_get_glyph_index(ttf, codepoint);
    if (glyph_idx == 0) {
        /* Try built-in as fallback */
        return fe_render_builtin_glyph(codepoint);
    }

    glyph_t *g = fe_cache_store(face_id, codepoint);
    g->codepoint = codepoint;

    /* For TTF, we render to an offscreen buffer and extract the bitmap */
    float scale = (float)face->size / ttf->units_per_em;

    /* Get glyph metrics */
    uint32_t glyph_off = 0;
    if (ttf->index_to_loc_format == 0) {
        uint16_t off16 = (uint16_t)((ttf->data[ttf->loca_offset + glyph_idx * 2] << 8) |
                                      ttf->data[ttf->loca_offset + glyph_idx * 2 + 1]);
        glyph_off = off16 * 2;
    } else {
        glyph_off = (uint32_t)((ttf->data[ttf->loca_offset + glyph_idx * 4] << 24) |
                                (ttf->data[ttf->loca_offset + glyph_idx * 4 + 1] << 16) |
                                (ttf->data[ttf->loca_offset + glyph_idx * 4 + 2] << 8) |
                                ttf->data[ttf->loca_offset + glyph_idx * 4 + 3]);
    }

    uint32_t abs_off = ttf->glyf_offset + glyph_off;
    int16_t num_contours = (int16_t)((ttf->data[abs_off] << 8) | ttf->data[abs_off + 1]);
    int16_t x_min = (int16_t)((ttf->data[abs_off + 2] << 8) | ttf->data[abs_off + 3]);
    int16_t y_max = (int16_t)((ttf->data[abs_off + 8] << 8) | ttf->data[abs_off + 9]);

    /* Get advance width */
    uint16_t num_long_hor_metrics = (uint16_t)((ttf->data[ttf->hhea_offset + 34] << 8) |
                                                 ttf->data[ttf->hhea_offset + 35]);
    uint16_t advance_width = 0;
    if (glyph_idx < num_long_hor_metrics) {
        advance_width = (uint16_t)((ttf->data[ttf->hmtx_offset + glyph_idx * 4] << 8) |
                                    ttf->data[ttf->hmtx_offset + glyph_idx * 4 + 1]);
    } else if (num_long_hor_metrics > 0) {
        advance_width = (uint16_t)((ttf->data[ttf->hmtx_offset + (num_long_hor_metrics - 1) * 4] << 8) |
                                    ttf->data[ttf->hmtx_offset + (num_long_hor_metrics - 1) * 4 + 1]);
    }

    int32_t lsb = 0;
    if (glyph_idx < num_long_hor_metrics) {
        lsb = (int16_t)((ttf->data[ttf->hmtx_offset + glyph_idx * 4 + 2] << 8) |
                         ttf->data[ttf->hmtx_offset + glyph_idx * 4 + 3]);
    }

    g->advance = (int32_t)(advance_width * scale);
    g->bearing_x = (int32_t)(lsb * scale);
    g->bearing_y = (int32_t)(y_max * scale);

    /* Estimate glyph dimensions */
    int16_t x_max_val = (int16_t)((ttf->data[abs_off + 6] << 8) | ttf->data[abs_off + 7]);
    int16_t y_min_val = (int16_t)((ttf->data[abs_off + 4] << 8) | ttf->data[abs_off + 5]);

    uint32_t gw = (uint32_t)((x_max_val - x_min) * scale) + 2;
    uint32_t gh = (uint32_t)((y_max - y_min_val) * scale) + 2;
    if (gw == 0) gw = 1;
    if (gh == 0) gh = 1;
    if (gw > 128) gw = 128;
    if (gh > 128) gh = 128;

    g->width = gw;
    g->height = gh;

    /* For simple glyphs (no compound), render to a temporary buffer */
    if (num_contours > 0) {
        /* Create temporary rendering context */
        uint32_t *tmp_buf = (uint32_t *)kmalloc(gw * gh * 4);
        if (tmp_buf) {
            memset(tmp_buf, 0, gw * gh * 4);
            gfx_context_t tmp_ctx;
            gfx_init(&tmp_ctx, tmp_buf, gw, gh, gw * 4, 32);

            /* Render glyph using existing TTF renderer */
            ttf_render_glyph(ttf, glyph_idx, (int32_t)(-x_min * scale),
                             (int32_t)(y_max * scale), face->size, 0xFFFFFF, &tmp_ctx);

            /* Extract alpha channel from rendered pixels */
            g->bitmap = (uint8_t *)kmalloc(gw * gh);
            if (g->bitmap) {
                for (uint32_t py = 0; py < gh; py++) {
                    for (uint32_t px = 0; px < gw; px++) {
                        uint32_t pixel = tmp_buf[py * gw + px];
                        /* Use brightness as alpha */
                        uint8_t r = (uint8_t)((pixel >> 16) & 0xFF);
                        uint8_t gr = (uint8_t)((pixel >> 8) & 0xFF);
                        uint8_t b = (uint8_t)(pixel & 0xFF);
                        g->bitmap[py * gw + px] = (r + gr + b) / 3;
                    }
                }
            }

            kfree(tmp_buf);
        }
    } else {
        /* Empty glyph (space, etc.) */
        g->bitmap = (uint8_t *)kmalloc(gw * gh);
        if (g->bitmap) {
            memset(g->bitmap, 0, gw * gh);
        }
    }

    return g;
}

font_face_t *font_load(const char *path, uint32_t size) {
    if (fe_face_count >= FE_MAX_FACES) return 0;

    /* If no path, return built-in font */
    if (!path || !*path) {
        font_face_t *face = (font_face_t *)kmalloc(sizeof(font_face_t));
        if (!face) return 0;
        *face = fe_builtin_face;
        face->size = size;
        fe_faces[fe_face_count++] = face;
        return face;
    }

    /* Try to load TTF from VFS */
    file_t *f = 0;
    if (vfs_open(path, FILE_MODE_READ, &f) != 0 || !f) {
        /* Fall back to built-in font */
        return font_load(0, size);
    }

    /* Read file contents */
    uint32_t file_size = 0;
    uint32_t cap = 65536;
    uint8_t *data = (uint8_t *)kmalloc(cap);
    if (!data) {
        vfs_close(f);
        return font_load(0, size);
    }

    char rbuf[256];
    int32_t nr;
    while ((nr = vfs_read(f, rbuf, 255)) > 0) {
        if (file_size + nr >= cap) {
            cap *= 2;
            uint8_t *new_data = (uint8_t *)krealloc(data, cap);
            if (!new_data) {
                kfree(data);
                vfs_close(f);
                return font_load(0, size);
            }
            data = new_data;
        }
        memcpy(data + file_size, rbuf, nr);
        file_size += nr;
    }
    vfs_close(f);

    /* Try to parse as TTF */
    ttf_font_t *ttf = (ttf_font_t *)kmalloc(sizeof(ttf_font_t));
    if (!ttf) {
        kfree(data);
        return font_load(0, size);
    }

    if (ttf_load(data, file_size, ttf) != 0) {
        kfree(ttf);
        kfree(data);
        return font_load(0, size);
    }

    /* Create font face */
    font_face_t *face = (font_face_t *)kmalloc(sizeof(font_face_t));
    if (!face) {
        kfree(ttf);
        kfree(data);
        return font_load(0, size);
    }

    memset(face, 0, sizeof(font_face_t));
    strcpy(face->family, "TTF");
    strcpy(face->style, "Regular");
    face->size = size;
    face->ascent = (uint32_t)(ttf->ascender * (float)size / ttf->units_per_em);
    face->descent = (uint32_t)(-ttf->descender * (float)size / ttf->units_per_em);
    face->line_gap = (uint32_t)(ttf->line_gap * (float)size / ttf->units_per_em);
    face->units_per_em = ttf->units_per_em;
    face->font_data = ttf;

    fe_faces[fe_face_count++] = face;
    return face;
}

void font_free(font_face_t *face) {
    if (!face) return;
    if (face == &fe_builtin_face) return;

    if (face->font_data) {
        ttf_font_t *ttf = (ttf_font_t *)face->font_data;
        if (ttf->data) {
            kfree(ttf->data);
        }
        kfree(ttf);
    }

    /* Remove from faces list */
    for (uint32_t i = 0; i < fe_face_count; i++) {
        if (fe_faces[i] == face) {
            for (uint32_t j = i; j < fe_face_count - 1; j++) {
                fe_faces[j] = fe_faces[j + 1];
            }
            fe_face_count--;
            break;
        }
    }

    kfree(face);
}

glyph_t *font_get_glyph(font_face_t *face, uint32_t codepoint) {
    if (!face) face = &fe_builtin_face;

    if (face == &fe_builtin_face || !face->font_data) {
        return fe_render_builtin_glyph(codepoint);
    }

    return fe_render_ttf_glyph(face, codepoint);
}

void font_draw_glyph(gfx_context_t *ctx, glyph_t *glyph, int32_t x, int32_t y, uint32_t fg) {
    if (!ctx || !glyph || !glyph->bitmap) return;

    /* Extract foreground color components */
    uint32_t fg_r = (fg >> 16) & 0xFF;
    uint32_t fg_g = (fg >> 8) & 0xFF;
    uint32_t fg_b = fg & 0xFF;

    int32_t draw_x = x + glyph->bearing_x;
    int32_t draw_y = y - glyph->bearing_y + (int32_t)glyph->height;

    for (uint32_t row = 0; row < glyph->height; row++) {
        for (uint32_t col = 0; col < glyph->width; col++) {
            int32_t px = draw_x + (int32_t)col;
            int32_t py = draw_y + (int32_t)row;

            if (px < 0 || px >= (int32_t)ctx->width ||
                py < 0 || py >= (int32_t)ctx->height) continue;

            uint8_t alpha = glyph->bitmap[row * glyph->width + col];
            if (alpha == 0) continue;

            if (alpha == 255) {
                gfx_set_pixel(ctx, px, py, fg);
            } else {
                /* Alpha blending */
                gfx_color_t dst = gfx_get_pixel(ctx, px, py);
                uint32_t dst_r = (dst >> 16) & 0xFF;
                uint32_t dst_g = (dst >> 8) & 0xFF;
                uint32_t dst_b = dst & 0xFF;

                uint32_t inv = 255 - alpha;
                uint32_t r = (fg_r * alpha + dst_r * inv) / 255;
                uint32_t g = (fg_g * alpha + dst_g * inv) / 255;
                uint32_t b = (fg_b * alpha + dst_b * inv) / 255;

                gfx_set_pixel(ctx, px, py, (r << 16) | (g << 8) | b);
            }
        }
    }
}

/* Parse a single UTF-8 codepoint, return number of bytes consumed */
static int utf8_decode(const char *str, uint32_t *codepoint) {
    uint8_t c = (uint8_t)str[0];

    if (c < 0x80) {
        *codepoint = c;
        return 1;
    } else if ((c & 0xE0) == 0xC0) {
        *codepoint = (uint32_t)((c & 0x1F) << 6);
        if ((uint8_t)str[1] & 0xC0) *codepoint |= (uint32_t)((uint8_t)str[1] & 0x3F);
        return 2;
    } else if ((c & 0xF0) == 0xE0) {
        *codepoint = (uint32_t)((c & 0x0F) << 12);
        if ((uint8_t)str[1] & 0xC0) *codepoint |= (uint32_t)((uint8_t)str[1] & 0x3F) << 6;
        if ((uint8_t)str[2] & 0xC0) *codepoint |= (uint32_t)((uint8_t)str[2] & 0x3F);
        return 3;
    } else if ((c & 0xF8) == 0xF0) {
        *codepoint = (uint32_t)((c & 0x07) << 18);
        if ((uint8_t)str[1] & 0xC0) *codepoint |= (uint32_t)((uint8_t)str[1] & 0x3F) << 12;
        if ((uint8_t)str[2] & 0xC0) *codepoint |= (uint32_t)((uint8_t)str[2] & 0x3F) << 6;
        if ((uint8_t)str[3] & 0xC0) *codepoint |= (uint32_t)((uint8_t)str[3] & 0x3F);
        return 4;
    }

    *codepoint = c;
    return 1;
}

uint32_t font_draw_text_utf8(font_face_t *face, gfx_context_t *ctx, const char *text, int32_t x, int32_t y, uint32_t fg) {
    if (!text) return 0;

    int32_t cur_x = x;
    const char *p = text;

    while (*p) {
        uint32_t cp;
        int bytes = utf8_decode(p, &cp);
        p += bytes;

        if (cp == 0) continue;

        glyph_t *glyph = font_get_glyph(face, cp);
        if (glyph) {
            font_draw_glyph(ctx, glyph, cur_x, y, fg);
            cur_x += glyph->advance;
        } else {
            cur_x += 8;
        }
    }

    return (uint32_t)(cur_x - x);
}

uint32_t font_text_width(font_face_t *face, const char *text) {
    if (!text) return 0;

    uint32_t width = 0;
    const char *p = text;

    while (*p) {
        uint32_t cp;
        int bytes = utf8_decode(p, &cp);
        p += bytes;

        if (cp == 0) continue;

        glyph_t *glyph = font_get_glyph(face, cp);
        if (glyph) {
            width += (uint32_t)glyph->advance;
        } else {
            width += 8;
        }
    }

    return width;
}
