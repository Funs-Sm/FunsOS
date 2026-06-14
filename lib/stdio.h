#ifndef STDIO_H
#define STDIO_H

#include "stdarg.h"
#include "stddef.h"
#include "stdint.h"

#define EOF (-1)
#define BUFSIZ 1024
#define FILENAME_MAX 256
#define FOPEN_MAX 16
#define L_tmpnam 256
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* FILE structure for VFS-backed file I/O */
typedef struct _FILE {
    void *vfs_file;       /* VFS file_t pointer */
    uint32_t flags;       /* open flags */
    int fd;               /* file descriptor index */
    int eof;              /* EOF flag */
    int error;            /* error flag */
    uint32_t pos;         /* current position */
} FILE;

/* Standard streams (stubs - use VFS underneath) */
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int sprintf(char *buf, const char *fmt, ...);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int vsprintf(char *buf, const char *fmt, va_list ap);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int printf(const char *fmt, ...);
int puts(const char *s);
int putchar(int c);
int getchar(void);

/* File-based I/O */
int fputs(const char *s, FILE *stream);
char *fgets(char *s, int size, FILE *stream);

/* File operations using VFS */
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *ptr, size_t size, size_t count, FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);

/* sscanf - basic implementation */
int sscanf(const char *str, const char *fmt, ...);

/* 格式化输入 */
int scanf(const char *fmt, ...);

/* 字符 I/O */
int fgetc(FILE *stream);
int fputc(int c, FILE *stream);
int ungetc(int c, FILE *stream);

/* 错误处理 */
void perror(const char *s);

/* 临时文件 */
FILE *tmpfile(void);
char *tmpnam(char *s);

/* 文件定位 */
int fflush(FILE *stream);
void rewind(FILE *stream);
typedef uint32_t fpos_t;
int fgetpos(FILE *stream, fpos_t *pos);
int fsetpos(FILE *stream, const fpos_t *pos);

/* 宽字符 I/O (基础) */
#ifndef __WCHAR_T_DEFINED
#define __WCHAR_T_DEFINED
typedef uint32_t wchar_t;
#endif
#ifndef __WINT_T_DEFINED
#define __WINT_T_DEFINED
typedef uint32_t wint_t;
#endif
int fwprintf(FILE *stream, const wchar_t *fmt, ...);
int swprintf(wchar_t *str, size_t n, const wchar_t *fmt, ...);

#endif
