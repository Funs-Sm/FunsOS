#include "unicode.h"
#include "string.h"

/* ---- UTF-8 decode ---- */
/* Parse a UTF-8 multibyte sequence starting at str.
 * Returns the codepoint, and sets *bytes_read to the number of bytes consumed.
 * For invalid sequences, returns the replacement character U+FFFD. */
uint32_t utf8_decode(const char *str, int *bytes_read) {
    if (!str || !*str) {
        if (bytes_read) *bytes_read = 0;
        return 0;
    }

    uint8_t b0 = (uint8_t)str[0];

    /* ASCII (0xxxxxxx) */
    if (b0 < 0x80) {
        if (bytes_read) *bytes_read = 1;
        return b0;
    }

    /* Continuation byte alone (10xxxxxx) - invalid */
    if ((b0 & 0xC0) == 0x80) {
        if (bytes_read) *bytes_read = 1;
        return 0xFFFD;
    }

    /* 2-byte sequence (110xxxxx 10xxxxxx) */
    if ((b0 & 0xE0) == 0xC0) {
        if ((uint8_t)str[1] == 0) { if (bytes_read) *bytes_read = 1; return 0xFFFD; }
        if (((uint8_t)str[1] & 0xC0) != 0x80) { if (bytes_read) *bytes_read = 1; return 0xFFFD; }
        uint32_t cp = ((uint32_t)(b0 & 0x1F) << 6) | ((uint8_t)str[1] & 0x3F);
        if (cp < 0x80) { if (bytes_read) *bytes_read = 1; return 0xFFFD; } /* overlong */
        if (bytes_read) *bytes_read = 2;
        return cp;
    }

    /* 3-byte sequence (1110xxxx 10xxxxxx 10xxxxxx) */
    if ((b0 & 0xF0) == 0xE0) {
        if ((uint8_t)str[1] == 0 || (uint8_t)str[2] == 0) { if (bytes_read) *bytes_read = 1; return 0xFFFD; }
        if (((uint8_t)str[1] & 0xC0) != 0x80 || ((uint8_t)str[2] & 0xC0) != 0x80) {
            if (bytes_read) *bytes_read = 1;
            return 0xFFFD;
        }
        uint32_t cp = ((uint32_t)(b0 & 0x0F) << 12) |
                      ((uint32_t)((uint8_t)str[1] & 0x3F) << 6) |
                      ((uint8_t)str[2] & 0x3F);
        if (cp < 0x800) { if (bytes_read) *bytes_read = 1; return 0xFFFD; } /* overlong */
        /* Surrogates U+D800..U+DFFF are invalid */
        if (cp >= 0xD800 && cp <= 0xDFFF) { if (bytes_read) *bytes_read = 3; return 0xFFFD; }
        if (bytes_read) *bytes_read = 3;
        return cp;
    }

    /* 4-byte sequence (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx) */
    if ((b0 & 0xF8) == 0xF0) {
        if ((uint8_t)str[1] == 0 || (uint8_t)str[2] == 0 || (uint8_t)str[3] == 0) {
            if (bytes_read) *bytes_read = 1;
            return 0xFFFD;
        }
        if (((uint8_t)str[1] & 0xC0) != 0x80 ||
            ((uint8_t)str[2] & 0xC0) != 0x80 ||
            ((uint8_t)str[3] & 0xC0) != 0x80) {
            if (bytes_read) *bytes_read = 1;
            return 0xFFFD;
        }
        uint32_t cp = ((uint32_t)(b0 & 0x07) << 18) |
                      ((uint32_t)((uint8_t)str[1] & 0x3F) << 12) |
                      ((uint32_t)((uint8_t)str[2] & 0x3F) << 6) |
                      ((uint8_t)str[3] & 0x3F);
        if (cp < 0x10000 || cp > 0x10FFFF) { if (bytes_read) *bytes_read = 1; return 0xFFFD; }
        if (bytes_read) *bytes_read = 4;
        return cp;
    }

    /* Invalid lead byte */
    if (bytes_read) *bytes_read = 1;
    return 0xFFFD;
}

/* ---- UTF-8 encode ---- */
/* Encode a Unicode codepoint to UTF-8.
 * Returns the number of bytes written (1-4), or 0 on error. */
int utf8_encode(uint32_t codepoint, char *out) {
    if (!out) return 0;

    if (codepoint < 0x80) {
        out[0] = (char)codepoint;
        return 1;
    }
    if (codepoint < 0x800) {
        out[0] = (char)(0xC0 | (codepoint >> 6));
        out[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    }
    if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
        /* Surrogates are invalid */
        return 0;
    }
    if (codepoint < 0x10000) {
        out[0] = (char)(0xE0 | (codepoint >> 12));
        out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    }
    if (codepoint <= 0x10FFFF) {
        out[0] = (char)(0xF0 | (codepoint >> 18));
        out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[3] = (char)(0x80 | (codepoint & 0x3F));
        return 4;
    }
    return 0;
}

/* ---- Character classification ---- */

int unicode_is_cjk(uint32_t cp) {
    /* CJK Unified Ideographs */
    if (cp >= 0x4E00 && cp <= 0x9FFF) return 1;
    /* CJK Extension A */
    if (cp >= 0x3400 && cp <= 0x4DBF) return 1;
    /* CJK Extension B */
    if (cp >= 0x20000 && cp <= 0x2A6DF) return 1;
    /* CJK Compatibility Ideographs */
    if (cp >= 0xF900 && cp <= 0xFAFF) return 1;
    /* CJK Symbols and Punctuation */
    if (cp >= 0x3000 && cp <= 0x303F) return 1;
    /* Hiragana */
    if (cp >= 0x3040 && cp <= 0x309F) return 1;
    /* Katakana */
    if (cp >= 0x30A0 && cp <= 0x30FF) return 1;
    /* Hangul Syllables */
    if (cp >= 0xAC00 && cp <= 0xD7AF) return 1;
    /* CJK Radicals Supplement */
    if (cp >= 0x2E80 && cp <= 0x2EFF) return 1;
    /* Kangxi Radicals */
    if (cp >= 0x2F00 && cp <= 0x2FDF) return 1;
    /* Fullwidth Forms */
    if (cp >= 0xFF01 && cp <= 0xFF60) return 1;
    /* CJK Strokes */
    if (cp >= 0x31C0 && cp <= 0x31EF) return 1;
    return 0;
}

int unicode_is_cyrillic(uint32_t cp) {
    /* Cyrillic */
    if (cp >= 0x0400 && cp <= 0x04FF) return 1;
    /* Cyrillic Supplement */
    if (cp >= 0x0500 && cp <= 0x052F) return 1;
    /* Cyrillic Extended-A */
    if (cp >= 0x2DE0 && cp <= 0x2DFF) return 1;
    /* Cyrillic Extended-B */
    if (cp >= 0xA640 && cp <= 0xA69F) return 1;
    return 0;
}

int unicode_is_latin(uint32_t cp) {
    /* Basic Latin */
    if (cp >= 0x0041 && cp <= 0x005A) return 1; /* A-Z */
    if (cp >= 0x0061 && cp <= 0x007A) return 1; /* a-z */
    /* Latin-1 Supplement */
    if (cp >= 0x00C0 && cp <= 0x00D6) return 1;
    if (cp >= 0x00D8 && cp <= 0x00F6) return 1;
    if (cp >= 0x00F8 && cp <= 0x00FF) return 1;
    /* Latin Extended-A */
    if (cp >= 0x0100 && cp <= 0x017F) return 1;
    /* Latin Extended-B */
    if (cp >= 0x0180 && cp <= 0x024F) return 1;
    return 0;
}

int unicode_is_digit(uint32_t cp) {
    /* ASCII digits */
    if (cp >= 0x0030 && cp <= 0x0039) return 1;
    /* Fullwidth digits */
    if (cp >= 0xFF10 && cp <= 0xFF19) return 1;
    return 0;
}

int unicode_is_printable(uint32_t cp) {
    if (cp < 0x20) return 0;
    if (cp < 0x7F) return 1;
    if (cp == 0x7F) return 0;
    /* Latin-1 Supplement printable range */
    if (cp >= 0xA0 && cp <= 0xFF) return 1;
    /* Most Unicode above U+0100 is printable (simplified check) */
    if (cp >= 0x0100 && cp <= 0x10FFFF) return 1;
    return 0;
}

int unicode_is_whitespace(uint32_t cp) {
    if (cp == 0x09 || cp == 0x0A || cp == 0x0B || cp == 0x0C || cp == 0x0D ||
        cp == 0x20 || cp == 0x85 || cp == 0xA0) return 1;
    /* Unicode whitespace */
    if (cp == 0x1680) return 1;
    if (cp >= 0x2000 && cp <= 0x200A) return 1;
    if (cp == 0x2028 || cp == 0x2029 || cp == 0x202F || cp == 0x205F || cp == 0x3000) return 1;
    return 0;
}

/* ---- String width ---- */

int unicode_char_width(uint32_t cp) {
    /* CJK characters are double-width */
    if (unicode_is_cjk(cp)) return 2;
    /* Fullwidth forms are double-width */
    if (cp >= 0xFF01 && cp <= 0xFF60) return 2;
    /* Non-printable / control characters have zero width */
    if (!unicode_is_printable(cp)) return 0;
    /* Everything else is single-width */
    return 1;
}

int utf8_string_width(const char *str) {
    if (!str) return 0;
    int width = 0;
    uint32_t i = 0;
    while (str[i]) {
        int bytes_read = 0;
        uint32_t cp = utf8_decode(str + i, &bytes_read);
        if (bytes_read == 0) break;
        width += unicode_char_width(cp);
        i += bytes_read;
    }
    return width;
}

/* ---- Case conversion ---- */

uint32_t unicode_tolower(uint32_t cp) {
    /* ASCII */
    if (cp >= 'A' && cp <= 'Z') return cp + 32;
    /* Latin-1 Supplement */
    if (cp >= 0x00C0 && cp <= 0x00DE && cp != 0x00D7) return cp + 32;
    /* Cyrillic uppercase */
    if (cp >= 0x0410 && cp <= 0x042F) return cp + 32;
    return cp;
}

uint32_t unicode_toupper(uint32_t cp) {
    /* ASCII */
    if (cp >= 'a' && cp <= 'z') return cp - 32;
    /* Latin-1 Supplement */
    if (cp >= 0x00E0 && cp <= 0x00FE && cp != 0x00F7) return cp - 32;
    /* Cyrillic lowercase */
    if (cp >= 0x0430 && cp <= 0x044F) return cp - 32;
    return cp;
}
