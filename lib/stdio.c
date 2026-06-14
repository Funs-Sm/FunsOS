#include "stdio.h"
#include "string.h"
#include "stdint.h"
#include "vfs.h"

void _putbuf(const char *buf, size_t len) {
    (void)buf;
    (void)len;
}

static int output_char(char *buf, size_t size, size_t *pos, char c) {
    if (*pos < size - 1) {
        buf[*pos] = c;
    }
    (*pos)++;
    return 1;
}

static int output_string(char *buf, size_t size, size_t *pos, const char *s, int width, int left_align, char pad) {
    int count = 0;
    int slen = (int)strlen(s);
    int pad_len = width - slen;
    if (pad_len < 0) pad_len = 0;
    if (!left_align) {
        while (pad_len-- > 0) {
            output_char(buf, size, pos, pad);
            count++;
        }
    }
    while (*s) {
        output_char(buf, size, pos, *s++);
        count++;
    }
    if (left_align) {
        while (pad_len-- > 0) {
            output_char(buf, size, pos, ' ');
            count++;
        }
    }
    return count;
}

static int output_int(char *buf, size_t size, size_t *pos, long value, int base, int uppercase, int width, int left_align, char pad, int show_sign) {
    char tmp[33];
    int i = 0;
    int negative = 0;
    unsigned long uv;

    if (value < 0) {
        negative = 1;
        uv = (unsigned long)(-(value + 1)) + 1;
    } else {
        uv = (unsigned long)value;
    }

    if (uv == 0) {
        tmp[i++] = '0';
    } else {
        while (uv > 0) {
            int digit = uv % base;
            tmp[i++] = digit < 10 ? '0' + digit : (uppercase ? 'A' : 'a') + digit - 10;
            uv /= base;
        }
    }

    char prefix = 0;
    if (negative) {
        prefix = '-';
    } else if (show_sign) {
        prefix = '+';
    }

    int digits_len = i;
    int total_len = digits_len + (prefix ? 1 : 0);
    int pad_len = width - total_len;
    if (pad_len < 0) pad_len = 0;

    int count = 0;

    if (left_align) {
        if (prefix) {
            output_char(buf, size, pos, prefix);
            count++;
        }
        while (i--) {
            output_char(buf, size, pos, tmp[i]);
            count++;
        }
        while (pad_len-- > 0) {
            output_char(buf, size, pos, ' ');
            count++;
        }
    } else {
        if (pad == '0') {
            if (prefix) {
                output_char(buf, size, pos, prefix);
                count++;
            }
            while (pad_len-- > 0) {
                output_char(buf, size, pos, '0');
                count++;
            }
        } else {
            while (pad_len-- > 0) {
                output_char(buf, size, pos, ' ');
                count++;
            }
            if (prefix) {
                output_char(buf, size, pos, prefix);
                count++;
            }
        }
        while (i--) {
            output_char(buf, size, pos, tmp[i]);
            count++;
        }
    }
    return count;
}

static int output_uint(char *buf, size_t size, size_t *pos, unsigned long value, int base, int uppercase, int width, int left_align, char pad) {
    return output_int(buf, size, pos, (long)value, base, uppercase, width, left_align, pad, 0);
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    size_t pos = 0;
    int count = 0;

    if (buf && size == 0) {
        size = (size_t)-1;
    }

    while (*fmt) {
        if (*fmt != '%') {
            output_char(buf, size, &pos, *fmt++);
            count++;
            continue;
        }
        fmt++;

        int left_align = 0;
        int show_sign = 0;
        char pad = ' ';
        int width = 0;
        int long_flag = 0;

        while (1) {
            if (*fmt == '-') {
                left_align = 1;
                fmt++;
            } else if (*fmt == '+') {
                show_sign = 1;
                fmt++;
            } else if (*fmt == '0') {
                pad = '0';
                fmt++;
            } else {
                break;
            }
        }

        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }

        if (*fmt == 'l') {
            long_flag = 1;
            fmt++;
        }

        switch (*fmt) {
        case 'd': {
            long val;
            if (long_flag) {
                val = va_arg(ap, long);
            } else {
                val = va_arg(ap, int);
            }
            count += output_int(buf, size, &pos, val, 10, 0, width, left_align, pad, show_sign);
            fmt++;
            break;
        }
        case 'u': {
            unsigned long val;
            if (long_flag) {
                val = va_arg(ap, unsigned long);
            } else {
                val = va_arg(ap, unsigned int);
            }
            count += output_uint(buf, size, &pos, val, 10, 0, width, left_align, pad);
            fmt++;
            break;
        }
        case 'x': {
            unsigned long val;
            if (long_flag) {
                val = va_arg(ap, unsigned long);
            } else {
                val = va_arg(ap, unsigned int);
            }
            count += output_uint(buf, size, &pos, val, 16, 0, width, left_align, pad);
            fmt++;
            break;
        }
        case 'X': {
            unsigned long val;
            if (long_flag) {
                val = va_arg(ap, unsigned long);
            } else {
                val = va_arg(ap, unsigned int);
            }
            count += output_uint(buf, size, &pos, val, 16, 1, width, left_align, pad);
            fmt++;
            break;
        }
        case 'o': {
            unsigned long val;
            if (long_flag) {
                val = va_arg(ap, unsigned long);
            } else {
                val = va_arg(ap, unsigned int);
            }
            count += output_uint(buf, size, &pos, val, 8, 0, width, left_align, pad);
            fmt++;
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (s == NULL) s = "(null)";
            count += output_string(buf, size, &pos, s, width, left_align, ' ');
            fmt++;
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            output_char(buf, size, &pos, c);
            count++;
            fmt++;
            break;
        }
        case 'p': {
            unsigned long val = (unsigned long)(uintptr_t)va_arg(ap, void *);
            output_char(buf, size, &pos, '0');
            output_char(buf, size, &pos, 'x');
            count += 2;
            count += output_uint(buf, size, &pos, val, 16, 0, width, left_align, pad);
            fmt++;
            break;
        }
        case '%': {
            output_char(buf, size, &pos, '%');
            count++;
            fmt++;
            break;
        }
        default: {
            output_char(buf, size, &pos, '%');
            count++;
            output_char(buf, size, &pos, *fmt);
            count++;
            fmt++;
            break;
        }
        }
    }

    if (buf && size > 0) {
        if (pos < size) {
            buf[pos] = '\0';
        } else {
            buf[size - 1] = '\0';
        }
    }

    return count;
}

int vsprintf(char *buf, const char *fmt, va_list ap) {
    return vsnprintf(buf, (size_t)-1, fmt, ap);
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return ret;
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(buf, (size_t)-1, fmt, ap);
    va_end(ap);
    return ret;
}

int printf(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (ret > 0) {
        _putbuf(buf, (size_t)ret);
    }
    return ret;
}

int puts(const char *s) {
    int len = (int)strlen(s);
    _putbuf(s, (size_t)len);
    _putbuf("\n", 1);
    return len + 1;
}

int putchar(int c) {
    char ch = (char)c;
    _putbuf(&ch, 1);
    return c;
}

int getchar(void) {
    /* Stub - in kernel mode, read from keyboard */
    return EOF;
}

int fputs(const char *s, FILE *stream) {
    (void)stream;
    if (!s) return EOF;
    int len = (int)strlen(s);
    _putbuf(s, (size_t)len);
    return len;
}

char *fgets(char *s, int size, FILE *stream) {
    (void)stream;
    if (!s || size <= 0) return 0;
    /* Stub - in kernel mode, can't easily read line */
    s[0] = '\0';
    return 0;
}

/* ---- FILE-based I/O using VFS ---- */

/* VFS API is available through vfs.h include above */

/* Simple FILE slot allocator */
static FILE file_slots[FOPEN_MAX];
static int file_slots_used[FOPEN_MAX];
static int file_slots_initialized = 0;

static void init_file_slots(void) {
    if (file_slots_initialized) return;
    for (int i = 0; i < FOPEN_MAX; i++) {
        file_slots_used[i] = 0;
    }
    file_slots_initialized = 1;
}

static FILE *alloc_file_slot(void) {
    init_file_slots();
    for (int i = 0; i < FOPEN_MAX; i++) {
        if (!file_slots_used[i]) {
            file_slots_used[i] = 1;
            return &file_slots[i];
        }
    }
    return 0;
}

static void free_file_slot(FILE *f) {
    if (!f) return;
    int idx = (int)(f - file_slots);
    if (idx >= 0 && idx < FOPEN_MAX) {
        file_slots_used[idx] = 0;
    }
}

FILE *fopen(const char *path, const char *mode) {
    if (!path || !mode) return 0;

    uint32_t flags = 0;
    if (mode[0] == 'r') {
        flags = 0x01; /* READ */
    } else if (mode[0] == 'w') {
        flags = 0x02; /* WRITE */
    } else if (mode[0] == 'a') {
        flags = 0x02 | 0x04; /* WRITE | APPEND */
    }

    FILE *f = alloc_file_slot();
    if (!f) return 0;

    file_t *vfs_file = 0;
    if (vfs_open(path, flags, &vfs_file) != 0 || !vfs_file) {
        free_file_slot(f);
        return 0;
    }

    f->vfs_file = vfs_file;
    f->flags = flags;
    f->fd = -1;
    f->eof = 0;
    f->error = 0;
    f->pos = 0;

    return f;
}

int fclose(FILE *stream) {
    if (!stream) return EOF;
    if (stream->vfs_file) {
        vfs_close((file_t *)stream->vfs_file);
    }
    free_file_slot(stream);
    return 0;
}

size_t fread(void *ptr, size_t size, size_t count, FILE *stream) {
    if (!ptr || !stream || !stream->vfs_file || size == 0 || count == 0) return 0;
    uint32_t total = (uint32_t)(size * count);
    int n = vfs_read((file_t *)stream->vfs_file, ptr, total);
    if (n < 0) {
        stream->error = 1;
        return 0;
    }
    if ((uint32_t)n < total) {
        stream->eof = 1;
    }
    stream->pos += (uint32_t)n;
    return (size_t)(n / (int)size);
}

size_t fwrite(const void *ptr, size_t size, size_t count, FILE *stream) {
    if (!ptr || !stream || !stream->vfs_file || size == 0 || count == 0) return 0;
    uint32_t total = (uint32_t)(size * count);
    int n = vfs_write((file_t *)stream->vfs_file, ptr, total);
    if (n < 0) {
        stream->error = 1;
        return 0;
    }
    stream->pos += (uint32_t)n;
    return (size_t)(n / (int)size);
}

int fseek(FILE *stream, long offset, int whence) {
    if (!stream || !stream->vfs_file) return -1;
    int ret = vfs_seek((file_t *)stream->vfs_file, (int32_t)offset, whence);
    if (ret == 0) {
        stream->eof = 0;
    }
    return ret;
}

long ftell(FILE *stream) {
    if (!stream || !stream->vfs_file) return -1;
    return (long)stream->pos;
}

int feof(FILE *stream) {
    if (!stream) return 0;
    return stream->eof;
}

int ferror(FILE *stream) {
    if (!stream) return 0;
    return stream->error;
}

void clearerr(FILE *stream) {
    if (!stream) return;
    stream->eof = 0;
    stream->error = 0;
}

/* ---- 新增函数实现 ---- */

int scanf(const char *fmt, ...) {
    /* 简化实现 - 从标准输入读取一行再解析 */
    /* 在内核/用户态环境中，stdin 通常不可用，返回 0 */
    (void)fmt;
    return 0;
}

int fgetc(FILE *stream) {
    if (!stream || !stream->vfs_file) return EOF;
    unsigned char c;
    size_t n = fread(&c, 1, 1, stream);
    if (n == 0) return EOF;
    return (int)c;
}

int fputc(int c, FILE *stream) {
    if (!stream || !stream->vfs_file) return EOF;
    unsigned char ch = (unsigned char)c;
    size_t n = fwrite(&ch, 1, 1, stream);
    if (n == 0) return EOF;
    return c;
}

int ungetc(int c, FILE *stream) {
    if (!stream || c == EOF) return EOF;
    /* 简化实现 - 回退文件位置 */
    if (stream->pos > 0) {
        stream->pos--;
        stream->eof = 0;
    }
    return c;
}

void perror(const char *s) {
    if (s && *s) {
        _putbuf(s, strlen(s));
        _putbuf(": ", 2);
    }
    /* 简化 - 输出通用错误信息 */
    _putbuf("error\n", 6);
}

FILE *tmpfile(void) {
    /* 简化实现 - 在 ramfs 上创建临时文件 */
    static int tmp_counter = 0;
    char tmpname[32];
    tmpname[0] = '/';
    tmpname[1] = 't';
    tmpname[2] = 'm';
    tmpname[3] = 'p';
    tmpname[4] = '/';
    char *p = &tmpname[5];
    int num = ++tmp_counter;
    char buf[12];
    int i = 0;
    if (num == 0) buf[i++] = '0';
    else {
        int n = num;
        while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    }
    while (i--) *p++ = buf[i];
    *p = '\0';
    return fopen(tmpname, "w+");
}

char *tmpnam(char *s) {
    static char tmpbuf[L_tmpnam];
    static int tmp_counter = 0;
    char *buf = s ? s : tmpbuf;
    int num = ++tmp_counter;
    char *p = buf;
    *p++ = '/'; *p++ = 't'; *p++ = 'm'; *p++ = 'p'; *p++ = '/';
    char digits[12];
    int i = 0;
    if (num == 0) digits[i++] = '0';
    else {
        int n = num;
        while (n > 0) { digits[i++] = '0' + (n % 10); n /= 10; }
    }
    while (i--) *p++ = digits[i];
    *p = '\0';
    return buf;
}

int fflush(FILE *stream) {
    (void)stream;
    /* 简化实现 - 无缓冲区刷新操作 */
    return 0;
}

void rewind(FILE *stream) {
    if (!stream) return;
    fseek(stream, 0, SEEK_SET);
    stream->error = 0;
    stream->eof = 0;
}

int fgetpos(FILE *stream, fpos_t *pos) {
    if (!stream || !pos) return -1;
    *pos = (fpos_t)stream->pos;
    return 0;
}

int fsetpos(FILE *stream, const fpos_t *pos) {
    if (!stream || !pos) return -1;
    return fseek(stream, (long)*pos, SEEK_SET);
}

int fwprintf(FILE *stream, const wchar_t *fmt, ...) {
    /* 简化实现 - 将宽字符格式串转为窄字符输出 */
    (void)stream;
    (void)fmt;
    return 0;
}

int swprintf(wchar_t *str, size_t n, const wchar_t *fmt, ...) {
    /* 简化实现 */
    (void)str; (void)n; (void)fmt;
    return 0;
}

/* ---- sscanf - basic implementation ---- */

int sscanf(const char *str, const char *fmt, ...) {
    if (!str || !fmt) return 0;

    va_list ap;
    va_start(ap, fmt);

    int count = 0;
    const char *p = str;
    int skip_ws = 1;

    while (*fmt) {
        if (*fmt == ' ') {
            skip_ws = 1;
            fmt++;
            continue;
        }

        if (*fmt != '%') {
            if (skip_ws) {
                while (*p == ' ' || *p == '\t' || *p == '\n') p++;
                skip_ws = 0;
            }
            if (*p != *fmt) break;
            p++;
            fmt++;
            continue;
        }

        fmt++; /* skip '%' */

        /* Skip width for now */
        while (*fmt >= '0' && *fmt <= '9') fmt++;

        if (skip_ws && *fmt != 'c') {
            while (*p == ' ' || *p == '\t' || *p == '\n') p++;
            skip_ws = 0;
        }

        switch (*fmt) {
        case 'd': {
            int *out = va_arg(ap, int *);
            int sign = 1;
            int val = 0;
            int digits = 0;
            if (*p == '-') { sign = -1; p++; }
            else if (*p == '+') { p++; }
            while (*p >= '0' && *p <= '9') {
                val = val * 10 + (*p - '0');
                p++;
                digits++;
            }
            if (digits > 0) {
                *out = sign * val;
                count++;
            }
            fmt++;
            break;
        }
        case 'x': {
            unsigned int *out = va_arg(ap, unsigned int *);
            unsigned int val = 0;
            int digits = 0;
            if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
            while ((*p >= '0' && *p <= '9') ||
                   (*p >= 'a' && *p <= 'f') ||
                   (*p >= 'A' && *p <= 'F')) {
                int digit;
                if (*p >= '0' && *p <= '9') digit = *p - '0';
                else if (*p >= 'a' && *p <= 'f') digit = *p - 'a' + 10;
                else digit = *p - 'A' + 10;
                val = val * 16 + (unsigned int)digit;
                p++;
                digits++;
            }
            if (digits > 0) {
                *out = val;
                count++;
            }
            fmt++;
            break;
        }
        case 's': {
            char *out = va_arg(ap, char *);
            int len = 0;
            while (*p && *p != ' ' && *p != '\t' && *p != '\n') {
                *out++ = *p++;
                len++;
            }
            *out = '\0';
            if (len > 0) count++;
            fmt++;
            break;
        }
        case 'c': {
            char *out = va_arg(ap, char *);
            if (*p) {
                *out = *p++;
                count++;
            }
            fmt++;
            break;
        }
        case '%': {
            if (*p == '%') p++;
            fmt++;
            break;
        }
        default:
            fmt++;
            break;
        }
    }

    va_end(ap);
    return count;
}
