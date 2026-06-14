#ifndef WCHAR_H
#define WCHAR_H

#include "stdint.h"
#include "stddef.h"

typedef uint32_t wchar_t;
typedef uint32_t wint_t;
typedef uint32_t wctype_t;

#define WEOF ((wint_t)-1)

/* 宽字符分类 */
int iswalpha(wint_t c);
int iswdigit(wint_t c);
int iswalnum(wint_t c);
int iswspace(wint_t c);
int iswupper(wint_t c);
int iswlower(wint_t c);
int iswprint(wint_t c);
int iswpunct(wint_t c);
int iswcntrl(wint_t c);

/* 宽字符转换 */
wint_t towupper(wint_t c);
wint_t towlower(wint_t c);

/* 宽字符串操作 */
size_t wcslen(const wchar_t *s);
wchar_t *wcscpy(wchar_t *dest, const wchar_t *src);
wchar_t *wcsncpy(wchar_t *dest, const wchar_t *src, size_t n);
int wcscmp(const wchar_t *s1, const wchar_t *s2);
int wcsncmp(const wchar_t *s1, const wchar_t *s2, size_t n);
wchar_t *wcschr(const wchar_t *s, wchar_t c);
wchar_t *wcsrchr(const wchar_t *s, wchar_t c);
wchar_t *wcsstr(const wchar_t *haystack, const wchar_t *needle);
wchar_t *wcscat(wchar_t *dest, const wchar_t *src);
wchar_t *wcsncat(wchar_t *dest, const wchar_t *src, size_t n);

/* 宽字符内存操作 */
void *wmemcpy(void *dest, const void *src, size_t n);
void *wmemmove(void *dest, const void *src, size_t n);
void *wmemset(void *s, wchar_t c, size_t n);
int wmemcmp(const void *s1, const void *s2, size_t n);

/* 多字节/宽字符转换 */
int mbrtowc(wchar_t *pwc, const char *s, size_t n, void *ps);
int wcrtomb(char *s, wchar_t wc, void *ps);
size_t mbrlen(const char *s, size_t n, void *ps);

#endif
