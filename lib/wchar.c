#include "wchar.h"
#include "stddef.h"

/* ---- 宽字符分类 ---- */

int iswalpha(wint_t c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= 0xC0 && c <= 0xFF && c != 0xD7 && c != 0xF7);
}

int iswdigit(wint_t c) {
    return c >= '0' && c <= '9';
}

int iswalnum(wint_t c) {
    return iswalpha(c) || iswdigit(c);
}

int iswspace(wint_t c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
           c == '\f' || c == '\v';
}

int iswupper(wint_t c) {
    return (c >= 'A' && c <= 'Z') ||
           (c >= 0xC0 && c <= 0xDE && c != 0xD7);
}

int iswlower(wint_t c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 0xDF && c <= 0xFF && c != 0xF7);
}

int iswprint(wint_t c) {
    return c >= 0x20 && c < 0x7F;
}

int iswpunct(wint_t c) {
    return iswprint(c) && !iswalnum(c) && !iswspace(c);
}

int iswcntrl(wint_t c) {
    return c < 0x20 || c == 0x7F;
}

/* ---- 宽字符转换 ---- */

wint_t towupper(wint_t c) {
    if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
    return c;
}

wint_t towlower(wint_t c) {
    if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    return c;
}

/* ---- 宽字符串操作 ---- */

size_t wcslen(const wchar_t *s) {
    size_t len = 0;
    while (*s++) len++;
    return len;
}

wchar_t *wcscpy(wchar_t *dest, const wchar_t *src) {
    wchar_t *ret = dest;
    while ((*dest++ = *src++))
        ;
    return ret;
}

wchar_t *wcsncpy(wchar_t *dest, const wchar_t *src, size_t n) {
    wchar_t *ret = dest;
    while (n && (*dest++ = *src++)) {
        n--;
    }
    while (n--) {
        *dest++ = L'\0';
    }
    return ret;
}

int wcscmp(const wchar_t *s1, const wchar_t *s2) {
    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return (int)(*s1 - *s2);
}

int wcsncmp(const wchar_t *s1, const wchar_t *s2, size_t n) {
    while (n && *s1 && *s1 == *s2) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return (int)(*s1 - *s2);
}

wchar_t *wcschr(const wchar_t *s, wchar_t c) {
    while (*s) {
        if (*s == c) return (wchar_t *)s;
        s++;
    }
    if (c == L'\0') return (wchar_t *)s;
    return NULL;
}

wchar_t *wcsrchr(const wchar_t *s, wchar_t c) {
    const wchar_t *last = NULL;
    while (*s) {
        if (*s == c) last = s;
        s++;
    }
    if (c == L'\0') return (wchar_t *)s;
    return (wchar_t *)last;
}

wchar_t *wcsstr(const wchar_t *haystack, const wchar_t *needle) {
    if (*needle == L'\0') return (wchar_t *)haystack;
    while (*haystack) {
        const wchar_t *h = haystack;
        const wchar_t *n = needle;
        while (*h && *n && *h == *n) {
            h++;
            n++;
        }
        if (*n == L'\0') return (wchar_t *)haystack;
        haystack++;
    }
    return NULL;
}

wchar_t *wcscat(wchar_t *dest, const wchar_t *src) {
    wchar_t *ret = dest;
    while (*dest) dest++;
    while ((*dest++ = *src++))
        ;
    return ret;
}

wchar_t *wcsncat(wchar_t *dest, const wchar_t *src, size_t n) {
    wchar_t *ret = dest;
    while (*dest) dest++;
    while (n-- && *src) {
        *dest++ = *src++;
    }
    *dest = L'\0';
    return ret;
}

/* ---- 宽字符内存操作 ---- */

void *wmemcpy(void *dest, const void *src, size_t n) {
    wchar_t *d = (wchar_t *)dest;
    const wchar_t *s = (const wchar_t *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

void *wmemmove(void *dest, const void *src, size_t n) {
    wchar_t *d = (wchar_t *)dest;
    const wchar_t *s = (const wchar_t *)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

void *wmemset(void *s, wchar_t c, size_t n) {
    wchar_t *p = (wchar_t *)s;
    while (n--) {
        *p++ = c;
    }
    return s;
}

int wmemcmp(const void *s1, const void *s2, size_t n) {
    const wchar_t *a = (const wchar_t *)s1;
    const wchar_t *b = (const wchar_t *)s2;
    while (n--) {
        if (*a != *b) return (int)(*a - *b);
        a++;
        b++;
    }
    return 0;
}

/* ---- 多字节/宽字符转换 ---- */

int mbrtowc(wchar_t *pwc, const char *s, size_t n, void *ps) {
    (void)ps;
    if (!s || n == 0) return 0;
    if (*s == 0) return 0;

    /* 简化实现 - 处理 UTF-8 */
    unsigned char c = (unsigned char)*s;
    if (c < 0x80) {
        if (pwc) *pwc = (wchar_t)c;
        return 1;
    }
    if ((c & 0xE0) == 0xC0) {
        if (n < 2) return -2;
        if (pwc) *pwc = (wchar_t)(((c & 0x1F) << 6) | (s[1] & 0x3F));
        return 2;
    }
    if ((c & 0xF0) == 0xE0) {
        if (n < 3) return -2;
        if (pwc) *pwc = (wchar_t)(((c & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F));
        return 3;
    }
    if ((c & 0xF8) == 0xF0) {
        if (n < 4) return -2;
        if (pwc) *pwc = (wchar_t)(((c & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F));
        return 4;
    }
    return -1;
}

int wcrtomb(char *s, wchar_t wc, void *ps) {
    (void)ps;
    if (!s) return 0;
    if (wc < 0x80) {
        s[0] = (char)wc;
        return 1;
    }
    if (wc < 0x800) {
        s[0] = (char)(0xC0 | ((wc >> 6) & 0x1F));
        s[1] = (char)(0x80 | (wc & 0x3F));
        return 2;
    }
    if (wc < 0x10000) {
        s[0] = (char)(0xE0 | ((wc >> 12) & 0x0F));
        s[1] = (char)(0x80 | ((wc >> 6) & 0x3F));
        s[2] = (char)(0x80 | (wc & 0x3F));
        return 3;
    }
    s[0] = (char)(0xF0 | ((wc >> 18) & 0x07));
    s[1] = (char)(0x80 | ((wc >> 12) & 0x3F));
    s[2] = (char)(0x80 | ((wc >> 6) & 0x3F));
    s[3] = (char)(0x80 | (wc & 0x3F));
    return 4;
}

size_t mbrlen(const char *s, size_t n, void *ps) {
    return (size_t)mbrtowc(NULL, s, n, ps);
}
