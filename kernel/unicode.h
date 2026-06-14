#ifndef UNICODE_H
#define UNICODE_H

#include "stdint.h"

/* UTF-8 decode/encode */
uint32_t utf8_decode(const char *str, int *bytes_read);
int utf8_encode(uint32_t codepoint, char *out);

/* Character classification */
int unicode_is_cjk(uint32_t cp);     /* Chinese/Japanese/Korean */
int unicode_is_cyrillic(uint32_t cp); /* Cyrillic (Russian etc.) */
int unicode_is_latin(uint32_t cp);    /* Latin alphabet */
int unicode_is_digit(uint32_t cp);    /* Numeric digits */
int unicode_is_printable(uint32_t cp);/* Any printable character */
int unicode_is_whitespace(uint32_t cp);

/* String width (for terminal display - CJK chars are 2 columns wide) */
int unicode_char_width(uint32_t cp);
int utf8_string_width(const char *str);

/* Case conversion for Unicode */
uint32_t unicode_tolower(uint32_t cp);
uint32_t unicode_toupper(uint32_t cp);

#endif
