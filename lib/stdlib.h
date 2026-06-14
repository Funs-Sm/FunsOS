#ifndef STDLIB_H
#define STDLIB_H

#include "stddef.h"
#include "stdint.h"

#ifndef NULL
#define NULL 0
#endif
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 32767

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t num, size_t size);
void *realloc(void *ptr, size_t size);

int atoi(const char *str);
long atol(const char *str);
char *itoa(int value, char *str, int base);
char *utoa(unsigned int value, char *str, int base);

long strtol(const char *str, char **endptr, int base);
unsigned long strtoul(const char *str, char **endptr, int base);
float strtof(const char *str, char **endptr);

int rand(void);
void srand(unsigned int seed);

void abort(void);
void exit(int status);

int abs(int n);
long labs(long n);

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *));

char *getenv(const char *name);
int system(const char *command);

/* 环境变量 */
extern char **environ;
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);

/* 数值转换增强 */
double strtod(const char *str, char **endptr);
long double strtold(const char *str, char **endptr);

/* 多字节/宽字符转换 */
#ifndef __WCHAR_T_DEFINED
#define __WCHAR_T_DEFINED
typedef uint32_t wchar_t;
#endif
#ifndef __WINT_T_DEFINED
#define __WINT_T_DEFINED
typedef uint32_t wint_t;
#endif
int mbtowc(wchar_t *pwc, const char *s, size_t n);
int wctomb(char *s, wchar_t wchar);
size_t mbstowcs(wchar_t *pwcs, const char *s, size_t n);
size_t wcstombs(char *s, const wchar_t *pwcs, size_t n);

#endif
