#ifndef FUNSOS_LIBC_H
#define FUNSOS_LIBC_H

/*
 * funsos_libc.h - FUNSOS SDK 轻量级运行时库头文件
 * 提供标准 C 库函数的兼容声明，供用户态应用程序使用。
 * 不依赖任何标准库，完全自包含。
 */

#include "stdint.h"
#include "stddef.h"

/* ===== 内存操作 ===== */
void       *funsos_memset(void *s, int c, uint32_t n);
void       *funsos_memcpy(void *dst, const void *src, uint32_t n);
void       *funsos_memmove(void *dst, const void *src, uint32_t n);
int         funsos_memcmp(const void *a, const void *b, uint32_t n);

/* ===== 动态内存分配 ===== */
void       *funsos_alloc(uint32_t size);
void        funsos_free(void *ptr);

/* ===== 字符串操作 ===== */
uint32_t    funsos_strlen(const char *s);
char       *funsos_strcpy(char *dst, const char *src);
char       *funsos_strncpy(char *dst, const char *src, uint32_t n);
char       *funsos_strcat(char *dst, const char *src);
char       *funsos_strncat(char *dst, const char *src, uint32_t n);
int         funsos_strcmp(const char *a, const char *b);
int         funsos_strncmp(const char *a, const char *b, uint32_t n);
const char *funsos_strstr(const char *haystack, const char *needle);
char       *funsos_strchr(const char *s, int c);
char       *funsos_strrchr(const char *s, int c);
long        funsos_strtol(const char *s, char **endptr, int base);
double      funsos_strtod(const char *s, char **endptr);
char       *funsos_strdup(const char *s);

/* ===== 字符分类 ===== */
int  funsos_isdigit(int c);
int  funsos_isalpha(int c);
int  funsos_isalnum(int c);
int  funsos_isspace(int c);
int  funsos_isupper(int c);
int  funsos_islower(int c);
int  funsos_toupper(int c);
int  funsos_tolower(int c);

/* ===== 数学函数 (软件浮点近似) ===== */
double  funsos_fabs(double x);
double  funsos_floor(double x);
double  funsos_ceil(double x);
double  funsos_sqrt(double x);          /* Newton-Raphson 迭代法 */
double  funsos_sin(double x);           /* Taylor 级数展开 (12项) */
double  funsos_cos(double x);           /* Taylor 级数展开 (12项) */
double  funsos_atan2(double y, double x);
double  funsos_pow(double base, double exp);
double  funsos_log(double x);
float   funsos_sinf(float x);
float   funsos_cosf(float x);
float   funsos_sqrtf(float x);
float   funsos_fabsf(float x);

/* ===== 格式化输出辅助 ===== */
int     funsos_itoa(int value, char *buf, int base);
int     funsos_dtoa(double value, char *buf, int precision);
char   *funsos_format_size(uint64_t bytes, char *buf);

/* ===== 实用工具 ===== */
uint32_t funsos_align_up(uint32_t val, uint32_t align);
uint32_t funsos_min_u(uint32_t a, uint32_t b);
uint32_t funsos_max_u(uint32_t a, uint32_t b);
int      funsos_min_i(int a, int b);
int      funsos_max_i(int a, int b);
void     funsos_swap(void *a, void *b, uint32_t size);
uint32_t funsos_hash_djb2(const char *str);
uint32_t funsos_hash_fnv1a(const char *str);

#endif /* FUNSOS_LIBC_H */
