#ifndef STRING_H
#define STRING_H

#include "stddef.h"

void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int memcmp(const void *s1, const void *s2, size_t n);
void *memchr(const void *s, int c, size_t n);

size_t strlen(const char *s);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
char *strcat(char *dst, const char *src);
char *strncat(char *dst, const char *src, size_t n);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strstr(const char *haystack, const char *needle);
char *strtok(char *str, const char *delim);
char *strtok_r(char *str, const char *delim, char **saveptr);
char *strdup(const char *s);
char *strndup(const char *s, size_t n);

int strcasecmp(const char *s1, const char *s2);
int strncasecmp(const char *s1, const char *s2, size_t n);
char *strerror(int errnum);
size_t strspn(const char *s, const char *accept);
size_t strcspn(const char *s, const char *reject);

int toupper(int c);
int tolower(int c);

/* 内存操作增强 */
void *memccpy(void *dest, const void *src, int c, size_t n);
int memcmp_const_time(const void *s1, const void *s2, size_t n);

/* 错误码字符串 */
char *strerror_r(int errnum, char *buf, size_t buflen);

/* 字符串分析增强 */
size_t strnlen(const char *s, size_t maxlen);
char *strpbrk(const char *s, const char *accept);
char *strsignal(int sig);

#endif
