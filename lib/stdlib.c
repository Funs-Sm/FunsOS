#include "stdlib.h"
#include "string.h"
#include "stdint.h"
#include "stdio.h"

static uint8_t heap[1048576];
static int heap_initialized = 0;

typedef struct heap_block {
    size_t size;
    int is_free;
    struct heap_block *next;
} heap_block_t;

static heap_block_t *heap_head = NULL;

static void init_heap(void) {
    if (heap_initialized) return;
    heap_head = (heap_block_t *)heap;
    heap_head->size = sizeof(heap) - sizeof(heap_block_t);
    heap_head->is_free = 1;
    heap_head->next = NULL;
    heap_initialized = 1;
}

void *malloc(size_t size) {
    if (size == 0) return NULL;
    init_heap();
    size = (size + 3) & ~3;
    heap_block_t *curr = heap_head;
    while (curr) {
        if (curr->is_free && curr->size >= size) {
            if (curr->size > size + sizeof(heap_block_t)) {
                heap_block_t *new_block = (heap_block_t *)((uint8_t *)curr + sizeof(heap_block_t) + size);
                new_block->size = curr->size - size - sizeof(heap_block_t);
                new_block->is_free = 1;
                new_block->next = curr->next;
                curr->next = new_block;
                curr->size = size;
            }
            curr->is_free = 0;
            return (void *)((uint8_t *)curr + sizeof(heap_block_t));
        }
        curr = curr->next;
    }
    return NULL;
}

void free(void *ptr) {
    if (ptr == NULL) return;
    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));
    block->is_free = 1;
    heap_block_t *curr = heap_head;
    while (curr && curr->next) {
        if (curr->is_free && curr->next->is_free) {
            curr->size += sizeof(heap_block_t) + curr->next->size;
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }
}

void *calloc(size_t num, size_t size) {
    size_t total = num * size;
    void *ptr = malloc(total);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

void *realloc(void *ptr, size_t size) {
    if (ptr == NULL) return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    heap_block_t *block = (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));
    size_t old_size = block->size;
    void *new_ptr = malloc(size);
    if (new_ptr) {
        size_t copy_size = old_size < size ? old_size : size;
        memcpy(new_ptr, ptr, copy_size);
        free(ptr);
    }
    return new_ptr;
}

int atoi(const char *str) {
    int result = 0;
    int sign = 1;
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') {
        str++;
    }
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    return sign * result;
}

long atol(const char *str) {
    long result = 0;
    int sign = 1;
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') {
        str++;
    }
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    return sign * result;
}

char *itoa(int value, char *str, int base) {
    if (base < 2 || base > 36) {
        *str = '\0';
        return str;
    }
    char *p = str;
    int negative = 0;
    unsigned int uv;
    if (value < 0 && base == 10) {
        negative = 1;
        uv = (unsigned int)(-(value + 1)) + 1;
    } else {
        uv = (unsigned int)value;
    }
    char buf[33];
    int i = 0;
    if (uv == 0) {
        buf[i++] = '0';
    } else {
        while (uv > 0) {
            int digit = uv % base;
            buf[i++] = digit < 10 ? '0' + digit : 'a' + digit - 10;
            uv /= base;
        }
    }
    if (negative) {
        *p++ = '-';
    }
    while (i--) {
        *p++ = buf[i];
    }
    *p = '\0';
    return str;
}

char *utoa(unsigned int value, char *str, int base) {
    if (base < 2 || base > 36) {
        *str = '\0';
        return str;
    }
    char *p = str;
    char buf[33];
    int i = 0;
    if (value == 0) {
        buf[i++] = '0';
    } else {
        while (value > 0) {
            int digit = value % base;
            buf[i++] = digit < 10 ? '0' + digit : 'a' + digit - 10;
            value /= base;
        }
    }
    while (i--) {
        *p++ = buf[i];
    }
    *p = '\0';
    return str;
}

static unsigned int rand_seed = 1;

int rand(void) {
    rand_seed = rand_seed * 1103515245 + 12345;
    return (int)((rand_seed >> 16) & 0x7FFF);
}

void srand(unsigned int seed) {
    rand_seed = seed;
}

void abort(void) {
    while (1) {
        __asm__ volatile("int $0x03");
    }
}

void exit(int status) {
    __asm__ volatile(
        "movl $1, %%eax\n"
        "movl %0, %%ebx\n"
        "int $0x80\n"
        :
        : "r"(status)
        : "eax", "ebx"
    );
    while (1);
}

int abs(int n) {
    return n < 0 ? -n : n;
}

static void swap_bytes(void *a, void *b, size_t size) {
    uint8_t *pa = (uint8_t *)a;
    uint8_t *pb = (uint8_t *)b;
    while (size--) {
        uint8_t tmp = *pa;
        *pa++ = *pb;
        *pb++ = tmp;
    }
}

void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *)) {
    if (nmemb <= 1) return;
    uint8_t *arr = (uint8_t *)base;
    uint8_t *pivot = arr + (nmemb - 1) * size;
    size_t i = 0;
    for (size_t j = 0; j < nmemb - 1; j++) {
        if (compar(arr + j * size, pivot) <= 0) {
            swap_bytes(arr + i * size, arr + j * size, size);
            i++;
        }
    }
    swap_bytes(arr + i * size, pivot, size);
    qsort(arr, i, size, compar);
    qsort(arr + (i + 1) * size, nmemb - i - 1, size, compar);
}

long strtol(const char *str, char **endptr, int base) {
    long result = 0;
    int sign = 1;

    /* Skip whitespace */
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') str++;

    /* Handle sign */
    if (*str == '-') { sign = -1; str++; }
    else if (*str == '+') { str++; }

    /* Auto-detect base */
    if (base == 0) {
        if (*str == '0') {
            if (str[1] == 'x' || str[1] == 'X') {
                base = 16;
                str += 2;
            } else {
                base = 8;
                str++;
            }
        } else {
            base = 10;
        }
    } else if (base == 16) {
        if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
            str += 2;
        }
    }

    while (*str) {
        int digit;
        if (*str >= '0' && *str <= '9') digit = *str - '0';
        else if (*str >= 'a' && *str <= 'z') digit = *str - 'a' + 10;
        else if (*str >= 'A' && *str <= 'Z') digit = *str - 'A' + 10;
        else break;

        if (digit >= base) break;
        result = result * base + digit;
        str++;
    }

    if (endptr) *endptr = (char *)str;
    return sign * result;
}

unsigned long strtoul(const char *str, char **endptr, int base) {
    unsigned long result = 0;

    /* Skip whitespace */
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') str++;

    /* Skip optional + */
    if (*str == '+') str++;

    /* Auto-detect base */
    if (base == 0) {
        if (*str == '0') {
            if (str[1] == 'x' || str[1] == 'X') {
                base = 16;
                str += 2;
            } else {
                base = 8;
                str++;
            }
        } else {
            base = 10;
        }
    } else if (base == 16) {
        if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
            str += 2;
        }
    }

    while (*str) {
        int digit;
        if (*str >= '0' && *str <= '9') digit = *str - '0';
        else if (*str >= 'a' && *str <= 'z') digit = *str - 'a' + 10;
        else if (*str >= 'A' && *str <= 'Z') digit = *str - 'A' + 10;
        else break;

        if (digit >= base) break;
        result = result * (unsigned long)base + (unsigned long)digit;
        str++;
    }

    if (endptr) *endptr = (char *)str;
    return result;
}

float strtof(const char *str, char **endptr) {
    float result = 0.0f;
    float sign = 1.0f;
    float decimal = 0.1f;
    int has_digits = 0;

    /* Skip whitespace */
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') str++;

    /* Handle sign */
    if (*str == '-') { sign = -1.0f; str++; }
    else if (*str == '+') { str++; }

    /* Integer part */
    while (*str >= '0' && *str <= '9') {
        result = result * 10.0f + (float)(*str - '0');
        str++;
        has_digits = 1;
    }

    /* Decimal part */
    if (*str == '.') {
        str++;
        while (*str >= '0' && *str <= '9') {
            result += (float)(*str - '0') * decimal;
            decimal *= 0.1f;
            str++;
            has_digits = 1;
        }
    }

    /* Exponent part */
    if (has_digits && (*str == 'e' || *str == 'E')) {
        str++;
        int exp_sign = 1;
        int exp_val = 0;
        if (*str == '-') { exp_sign = -1; str++; }
        else if (*str == '+') { str++; }
        while (*str >= '0' && *str <= '9') {
            exp_val = exp_val * 10 + (*str - '0');
            str++;
        }
        float exp_mult = 1.0f;
        for (int i = 0; i < exp_val; i++) exp_mult *= 10.0f;
        if (exp_sign > 0) result *= exp_mult;
        else result /= exp_mult;
    }

    if (endptr) *endptr = (char *)str;
    return sign * result;
}

long labs(long n) {
    return n < 0 ? -n : n;
}

void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void *, const void *)) {
    if (!key || !base || nmemb == 0 || size == 0 || !compar) return NULL;

    const uint8_t *arr = (const uint8_t *)base;
    size_t lo = 0, hi = nmemb;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const void *elem = arr + mid * size;
        int cmp = compar(key, elem);
        if (cmp == 0) return (void *)elem;
        if (cmp < 0) hi = mid;
        else lo = mid + 1;
    }
    return NULL;
}

/* getenv and system are implemented as stubs that use the shell's
   environment variable system when linked into the kernel */

char *getenv(const char *name) {
    (void)name;
    /* In kernel context, this would need to call the shell's env_get.
       For now, return NULL as a safe default. */
    return NULL;
}

int system(const char *command) {
    (void)command;
    /* In kernel context, this would call shell_execute.
       For now, return -1 as a safe default. */
    return -1;
}

/* ---- 环境变量 ---- */

/* 简单的环境变量存储 */
#define MAX_ENV_VARS 64
#define MAX_ENV_LEN 256

static char env_storage[MAX_ENV_VARS][MAX_ENV_LEN];
static char *env_ptrs[MAX_ENV_VARS + 1];
static int env_count = 0;
static int env_initialized = 0;

char **environ = env_ptrs;

static void init_env(void) {
    if (env_initialized) return;
    env_ptrs[0] = NULL;
    env_initialized = 1;
}

int setenv(const char *name, const char *value, int overwrite) {
    if (!name || !value) return -1;

    init_env();

    /* 查找是否已存在 */
    size_t nlen = strlen(name);
    for (int i = 0; i < env_count; i++) {
        if (strncmp(env_ptrs[i], name, nlen) == 0 && env_ptrs[i][nlen] == '=') {
            if (!overwrite) return 0;
            /* 覆盖现有值 */
            snprintf(env_storage[i], MAX_ENV_LEN, "%s=%s", name, value);
            env_ptrs[i] = env_storage[i];
            return 0;
        }
    }

    /* 添加新变量 */
    if (env_count >= MAX_ENV_VARS) return -1;
    snprintf(env_storage[env_count], MAX_ENV_LEN, "%s=%s", name, value);
    env_ptrs[env_count] = env_storage[env_count];
    env_count++;
    env_ptrs[env_count] = NULL;
    return 0;
}

int unsetenv(const char *name) {
    if (!name) return -1;

    init_env();

    size_t nlen = strlen(name);
    for (int i = 0; i < env_count; i++) {
        if (strncmp(env_ptrs[i], name, nlen) == 0 && env_ptrs[i][nlen] == '=') {
            /* 移除 - 用最后一个覆盖当前位置 */
            env_ptrs[i] = env_ptrs[env_count - 1];
            env_count--;
            env_ptrs[env_count] = NULL;
            return 0;
        }
    }
    return 0;
}

/* ---- 数值转换增强 ---- */

double strtod(const char *str, char **endptr) {
    double result = 0.0;
    double sign = 1.0;
    double decimal = 0.1;
    int has_digits = 0;

    /* 跳过空白 */
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') str++;

    /* 处理符号 */
    if (*str == '-') { sign = -1.0; str++; }
    else if (*str == '+') { str++; }

    /* 整数部分 */
    while (*str >= '0' && *str <= '9') {
        result = result * 10.0 + (double)(*str - '0');
        str++;
        has_digits = 1;
    }

    /* 小数部分 */
    if (*str == '.') {
        str++;
        while (*str >= '0' && *str <= '9') {
            result += (double)(*str - '0') * decimal;
            decimal *= 0.1;
            str++;
            has_digits = 1;
        }
    }

    /* 指数部分 */
    if (has_digits && (*str == 'e' || *str == 'E')) {
        str++;
        int exp_sign = 1;
        int exp_val = 0;
        if (*str == '-') { exp_sign = -1; str++; }
        else if (*str == '+') { str++; }
        while (*str >= '0' && *str <= '9') {
            exp_val = exp_val * 10 + (*str - '0');
            str++;
        }
        double exp_mult = 1.0;
        for (int i = 0; i < exp_val; i++) exp_mult *= 10.0;
        if (exp_sign > 0) result *= exp_mult;
        else result /= exp_mult;
    }

    if (endptr) *endptr = (char *)str;
    return sign * result;
}

long double strtold(const char *str, char **endptr) {
    /* 简化实现 - 使用 double 精度 */
    return (long double)strtod(str, endptr);
}

/* ---- 多字节/宽字符转换 ---- */

int mbtowc(wchar_t *pwc, const char *s, size_t n) {
    if (!s || n == 0) return 0;
    if (*s == 0) return 0;

    /* 简化实现 - 假设 ASCII/单字节编码 */
    if (pwc) *pwc = (wchar_t)(unsigned char)*s;
    return 1;
}

int wctomb(char *s, wchar_t wchar) {
    if (!s) return 0;
    /* 简化实现 - 仅处理基本 ASCII */
    if (wchar < 0x80) {
        *s = (char)wchar;
        return 1;
    }
    /* 多字节 UTF-8 编码 */
    if (wchar < 0x800) {
        s[0] = (char)(0xC0 | ((wchar >> 6) & 0x1F));
        s[1] = (char)(0x80 | (wchar & 0x3F));
        return 2;
    }
    if (wchar < 0x10000) {
        s[0] = (char)(0xE0 | ((wchar >> 12) & 0x0F));
        s[1] = (char)(0x80 | ((wchar >> 6) & 0x3F));
        s[2] = (char)(0x80 | (wchar & 0x3F));
        return 3;
    }
    s[0] = (char)(0xF0 | ((wchar >> 18) & 0x07));
    s[1] = (char)(0x80 | ((wchar >> 12) & 0x3F));
    s[2] = (char)(0x80 | ((wchar >> 6) & 0x3F));
    s[3] = (char)(0x80 | (wchar & 0x3F));
    return 4;
}

size_t mbstowcs(wchar_t *pwcs, const char *s, size_t n) {
    if (!s) return 0;
    size_t count = 0;
    while (*s && count < n) {
        pwcs[count++] = (wchar_t)(unsigned char)*s;
        s++;
    }
    if (count < n) pwcs[count] = 0;
    return count;
}

size_t wcstombs(char *s, const wchar_t *pwcs, size_t n) {
    if (!pwcs || !s) return 0;
    size_t count = 0;
    while (*pwcs && count < n) {
        if (*pwcs < 0x80) {
            s[count++] = (char)*pwcs;
        } else {
            /* 简化 - 用 '?' 替代非 ASCII */
            s[count++] = '?';
        }
        pwcs++;
    }
    if (count < n) s[count] = '\0';
    return count;
}
