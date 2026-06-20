/* funsos_libc.c - FUNSOS SDK 轻量级运行时库
 * 提供标准 C 库函数的兼容实现，供用户态应用程序使用。
 * 不依赖任何标准库，完全自包含。
 */

#include "funsos.h"
#include "stddef.h"

/* ===== 内存操作 ===== */

void *funsos_memset(void *s, int c, uint32_t n)
{
    uint8_t *p = (uint8_t *)s;
    uint8_t v = (uint8_t)c;
    uint32_t i;

    for (i = 0; i < n; i++)
        p[i] = v;

    return s;
}

void *funsos_memcpy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    uint32_t i;

    for (i = 0; i < n; i++)
        d[i] = s[i];

    return dst;
}

void *funsos_memmove(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    uint32_t i;

    if (d < s) {
        for (i = 0; i < n; i++)
            d[i] = s[i];
    } else if (d > s) {
        for (i = n; i > 0; i--)
            d[i - 1] = s[i - 1];
    }

    return dst;
}

int funsos_memcmp(const void *a, const void *b, uint32_t n)
{
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    uint32_t i;

    for (i = 0; i < n; i++) {
        if (pa[i] != pb[i])
            return (int)pa[i] - (int)pb[i];
    }
    return 0;
}

/* ===== 字符串操作 ===== */

uint32_t funsos_strlen(const char *s)
{
    uint32_t len = 0;
    while (s[len])
        len++;
    return len;
}

char *funsos_strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++) != '\0')
        ;
    return dst;
}

char *funsos_strncpy(char *dst, const char *src, uint32_t n)
{
    uint32_t i;
    char *d = dst;

    for (i = 0; i < n && src[i] != '\0'; i++)
        d[i] = src[i];
    for (; i < n; i++)
        d[i] = '\0';

    return dst;
}

char *funsos_strcat(char *dst, const char *src)
{
    char *d = dst;
    while (*d)
        d++;
    while ((*d++ = *src++) != '\0')
        ;
    return dst;
}

char *funsos_strncat(char *dst, const char *src, uint32_t n)
{
    char *d = dst;
    uint32_t i = 0;

    while (*d)
        d++;
    while (i < n && src[i] != '\0') {
        *d++ = src[i++];
    }
    *d = '\0';

    return dst;
}

int funsos_strcmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int funsos_strncmp(const char *a, const char *b, uint32_t n)
{
    uint32_t i;

    for (i = 0; i < n; i++) {
        if (a[i] != b[i])
            return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == '\0')
            return 0;
    }
    return 0;
}

const char *funsos_strstr(const char *haystack, const char *needle)
{
    uint32_t hlen, nlen, i;

    hlen = funsos_strlen(haystack);
    nlen = funsos_strlen(needle);

    if (nlen == 0)
        return haystack;
    if (nlen > hlen)
        return NULL;

    for (i = 0; i <= hlen - nlen; i++) {
        if (funsos_memcmp(&haystack[i], needle, nlen) == 0)
            return &haystack[i];
    }

    return NULL;
}

char *funsos_strchr(const char *s, int c)
{
    while (*s) {
        if ((unsigned char)*s == (unsigned char)c)
            return (char *)s;
        s++;
    }
    if (c == '\0')
        return (char *)s;
    return NULL;
}

char *funsos_strrchr(const char *s, int c)
{
    const char *last = NULL;

    while (*s) {
        if ((unsigned char)*s == (unsigned char)c)
            last = s;
        s++;
    }
    if (c == '\0')
        return (char *)s;
    return (char *)last;
}

long funsos_strtol(const char *s, char **endptr, int base)
{
    long result = 0;
    int negative = 0;
    int digit;

    /* 跳过空白 */
    while (funsos_isspace(*s))
        s++;

    /* 处理符号 */
    if (*s == '-') {
        negative = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    /* 自动检测进制 */
    if (base == 0) {
        if (*s == '0') {
            if (*(s + 1) == 'x' || *(s + 1) == 'X') {
                base = 16;
                s += 2;
            } else {
                base = 8;
                s++;
            }
        } else {
            base = 10;
        }
    } else if (base == 16) {
        if (*s == '0' && (*(s + 1) == 'x' || *(s + 1) == 'X'))
            s += 2;
    }

    /* 转换数字 */
    while (*s) {
        if (*s >= '0' && *s <= '9')
            digit = *s - '0';
        else if (*s >= 'a' && *s <= 'f')
            digit = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'F')
            digit = *s - 'A' + 10;
        else
            break;

        if (digit >= base)
            break;

        result = result * base + digit;
        s++;
    }

    if (endptr)
        *endptr = (char *)s;

    return negative ? -result : result;
}

double funsos_strtod(const char *s, char **endptr)
{
    double result = 0.0;
    double frac = 0.0;
    double div = 10.0;
    int negative = 0;
    int exp_negative = 0;
    long exponent = 0;
    uint32_t i;

    /* 跳过空白 */
    while (funsos_isspace(*s))
        s++;

    /* 处理符号 */
    if (*s == '-') { negative = 1; s++; }
    else if (*s == '+') { s++; }

    /* 整数部分 */
    while (*s >= '0' && *s <= '9') {
        result = result * 10.0 + (*s - '0');
        s++;
    }

    /* 小数部分 */
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') {
            frac += (*s - '0') / div;
            div *= 10.0;
            s++;
        }
        result += frac;
    }

    /* 指数部分 */
    if (*s == 'e' || *s == 'E') {
        s++;
        if (*s == '-') { exp_negative = 1; s++; }
        else if (*s == '+') { s++; }
        while (*s >= '0' && *s <= '9') {
            exponent = exponent * 10 + (*s - '0');
            s++;
        }
        if (exp_negative) {
            for (i = 0; i < (uint32_t)exponent; i++)
                result /= 10.0;
        } else {
            for (i = 0; i < (uint32_t)exponent; i++)
                result *= 10.0;
        }
    }

    if (endptr)
        *endptr = (char *)s;

    return negative ? -result : result;
}

char *funsos_strdup(const char *s)
{
    uint32_t len;
    char *dup;

    if (s == NULL)
        return NULL;

    len = funsos_strlen(s);
    dup = (char *)funsos_alloc(len + 1);
    if (dup == NULL)
        return NULL;

    funsos_strcpy(dup, s);
    return dup;
}

/* ===== 字符分类 ===== */

int funsos_isdigit(int c)
{
    return (c >= '0' && c <= '9');
}

int funsos_isalpha(int c)
{
    return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
}

int funsos_isalnum(int c)
{
    return funsos_isalpha(c) || funsos_isdigit(c);
}

int funsos_isspace(int c)
{
    return (c == ' ' || c == '\t' || c == '\n' ||
            c == '\r' || c == '\v' || c == '\f');
}

int funsos_isupper(int c)
{
    return (c >= 'A' && c <= 'Z');
}

int funsos_islower(int c)
{
    return (c >= 'a' && c <= 'z');
}

int funsos_toupper(int c)
{
    if (c >= 'a' && c <= 'z')
        return c - ('a' - 'A');
    return c;
}

int funsos_tolower(int c)
{
    if (c >= 'A' && c <= 'Z')
        return c + ('a' - 'A');
    return c;
}

/* ===== 数学函数 (软件浮点近似) ===== */

double funsos_fabs(double x)
{
    if (x < 0.0)
        return -x;
    return x;
}

double funsos_floor(double x)
{
    long i = (long)x;
    if (x < 0.0 && x != (double)i)
        i--;
    return (double)i;
}

double funsos_ceil(double x)
{
    long i = (long)x;
    if (x > 0.0 && x != (double)i)
        i++;
    return (double)i;
}

/* Newton-Raphson 迭代法求平方根 */
double funsos_sqrt(double x)
{
    double guess;
    double prev;
    int i;

    if (x < 0.0)
        return 0.0;  /* 未定义: 返回 0 */
    if (x == 0.0)
        return 0.0;
    if (x == 1.0)
        return 1.0;

    /* 初始猜测值 */
    guess = x / 2.0;

    /* Newton-Raphson 迭代: guess = (guess + x/guess) / 2 */
    for (i = 0; i < 64; i++) {
        prev = guess;
        guess = (guess + x / guess) / 2.0;
        if (funsos_fabs(guess - prev) < 1e-15 * funsos_fabs(guess))
            break;
    }

    return guess;
}

/*
 * sin(x) Taylor 级数展开:
 *   sin(x) = x - x^3/3! + x^5/5! - x^7/7! + ... (前 12 项)
 * 使用角度归一化到 [-pi, pi]
 */
static const double PI_VAL = 3.14159265358979323846;
static const double PI_2  = 6.28318530717958647692;  /* 2*PI */

double funsos_sin(double x)
{
    double term, sum;
    int sign;
    int k;

    /* 归一化到 [-PI, PI] */
    while (x > PI_VAL)
        x -= PI_2;
    while (x < -PI_VAL)
        x += PI_2;

    /* Taylor 级数: sin(x) = Σ (-1)^k * x^(2k+1) / (2k+1)! */
    sum = 0.0;
    sign = 1;
    term = x;

    for (k = 0; k < 12; k++) {
        sum += (double)sign * term;

        /* 计算下一项: term *= x^2 / ((2k+2)*(2k+3)) */
        term *= x * x / (double)((2 * k + 2) * (2 * k + 3));
        sign = -sign;
    }

    return sum;
}

/*
 * cos(x) Taylor 级数展开:
 *   cos(x) = 1 - x^2/2! + x^4/4! - x^6/6! + ... (前 12 项)
 */
double funsos_cos(double x)
{
    double term, sum;
    int sign;
    int k;

    /* 归一化到 [-PI, PI] */
    while (x > PI_VAL)
        x -= PI_2;
    while (x < -PI_VAL)
        x += PI_2;

    /* Taylor 级数: cos(x) = Σ (-1)^k * x^(2k) / (2k)! */
    sum = 0.0;
    sign = 1;
    term = 1.0;

    for (k = 0; k < 12; k++) {
        sum += (double)sign * term;

        /* 计算下一项: term *= x^2 / ((2k+1)*(2k+2)) */
        term *= x * x / (double)((2 * k + 1) * (2 * k + 2));
        sign = -sign;
    }

    return sum;
}

double funsos_atan2(double y, double x)
{
    double angle;
    double abs_y;
    double r;

    if (x == 0.0) {
        if (y > 0.0) return PI_VAL / 2.0;
        if (y < 0.0) return -PI_VAL / 2.0;
        return 0.0;
    }

    abs_y = funsos_fabs(y);
    r = (x - abs_y) / (x + abs_y);
    angle = PI_VAL / 4.0 - r * (0.2447 + 0.0663 * r * r);

    if (y > 0.0)
        return angle;
    if (y < 0.0)
        return -angle;
    if (x < 0.0)
        return PI_VAL;
    return 0.0;
}

double funsos_pow(double base, double exp)
{
    double result;
    long integer_part;
    double frac_part;
    long i;
    int neg;

    if (base == 0.0 && exp > 0.0)
        return 0.0;
    if (exp == 0.0)
        return 1.0;
    if (base == 1.0)
        return 1.0;

    /* 负指数: pow(a,b) = 1/pow(a,-b) */
    neg = 0;
    if (exp < 0.0) {
        exp = -exp;
        neg = 1;
    }

    /* 分离整数和小数部分 */
    integer_part = (long)exp;
    frac_part = exp - (double)integer_part;

    /* 计算 base^integer_part */
    result = 1.0;
    if (integer_part > 0) {
        for (i = 0; i < integer_part; i++)
            result *= base;
    }

    /* 计算 base^frac_part via exp(frac*log(base)) */
    if (frac_part > 0.0 && base > 0.0) {
        double log_val = funsos_log(base);
        double e_val = 1.0;
        double term = 1.0;
        double f = log_val * frac_part;
        int k;

        /* e^x 的 Taylor 展开: e^x = 1 + x + x^2/2! + ... */
        for (k = 1; k < 20; k++) {
            term *= f / (double)k;
            e_val += term;
        }
        result *= e_val;
    }

    return neg ? (1.0 / result) : result;
}

double funsos_log(double x)
{
    double y;
    double z;
    double result;
    double zn;
    int k;

    if (x <= 0.0)
        return 0.0;  /* 未定义 */

    /* 使用 ln(x) = 2 * atanh((x-1)/(x+1)) 的级数展开 */
    /* 先将 x 归一化到 [sqrt(2)/2, sqrt(2)] 区间以提高收敛速度 */
    int exponent = 0;
    while (x > 1.41421356) { x /= 2.0; exponent++; }
    while (x < 0.70710678) { x *= 2.0; exponent--; }

    y = (x - 1.0) / (x + 1.0);
    z = y * y;

    result = 0.0;
    zn = y;

    /* atanh(y) = y + y^3/3 + y^5/5 + ... */
    for (k = 1; k < 50; k += 2) {
        result += zn / (double)k;
        zn *= z;
    }

    result *= 2.0;
    result += (double)(exponent) * 0.6931471805599453;  /* ln(2) */

    return result;
}

/* float 版本的数学函数 */

float funsos_sinf(float x)
{
    return (float)funsos_sin((double)x);
}

float funsos_cosf(float x)
{
    return (float)funsos_cos((double)x);
}

float funsos_sqrtf(float x)
{
    return (float)funsos_sqrt((double)x);
}

float funsos_fabsf(float x)
{
    if (x < 0.0f)
        return -x;
    return x;
}

/* ===== 格式化输出辅助 ===== */

int funsos_itoa(int value, char *buf, int base)
{
    static const char digits[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    char tmp[33];
    int i = 0;
    int neg = 0;
    int len;
    unsigned int uval;

    if (base < 2 || base > 36)
        return -1;

    if (value < 0 && base == 10) {
        neg = 1;
        uval = (unsigned int)(-(value + 1)) + 1;
    } else {
        uval = (unsigned int)value;
    }

    if (uval == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }

    while (uval > 0) {
        tmp[i++] = digits[uval % (unsigned int)base];
        uval /= (unsigned int)base;
    }

    len = i;
    if (neg)
        buf[0] = '-';

    while (--i >= 0) {
        buf[neg + (len - 1 - i)] = tmp[i];
    }
    buf[neg + len] = '\0';

    return neg + len;
}

int funsos_dtoa(double value, char *buf, int precision)
{
    char tmp[64];
    int i = 0;
    int pos = 0;
    long ipart;
    double fpart;
    int neg = 0;
    int j;
    unsigned long uipart;

    if (precision < 0) precision = 6;
    if (precision > 20) precision = 20;

    if (value < 0.0) {
        neg = 1;
        value = -value;
    }

    /* 整数部分 */
    ipart = (long)value;
    fpart = value - (double)ipart;

    /* 四舍五入 */
    {
        double round_val = 0.5;
        int r;
        for (r = 0; r < precision; r++)
            round_val /= 10.0;
        fpart += round_val;
        if (fpart >= 1.0) {
            ipart++;
            fpart -= 1.0;
        }
    }

    uipart = (unsigned long)ipart;

    /* 处理整数部分 */
    if (uipart == 0) {
        tmp[pos++] = '0';
    } else {
        while (uipart > 0) {
            tmp[pos++] = '0' + (uipart % 10);
            uipart /= 10;
        }
    }

    if (neg)
        buf[i++] = '-';

    for (j = pos - 1; j >= 0; j--)
        buf[i++] = tmp[j];

    /* 小数部分 */
    if (precision > 0) {
        buf[i++] = '.';
        for (j = 0; j < precision; j++) {
            fpart *= 10.0;
            buf[i++] = '0' + (int)fpart;
            fpart -= (int)fpart;
        }
    }

    buf[i] = '\0';
    return i;
}

char *funsos_format_size(uint64_t bytes, char *buf)
{
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_idx = 0;
    double val = (double)bytes;
    int int_part;

    while (val >= 1024.0 && unit_idx < 4) {
        val /= 1024.0;
        unit_idx++;
    }

    int_part = (int)val;

    /* 格式化为 "XXX.XX Unit" */
    if (unit_idx == 0) {
        funsos_itoa(int_part, buf, 10);
    } else {
        funsos_itoa(int_part, buf, 10);
        {
            int pos = funsos_strlen(buf);
            buf[pos++] = '.';
            val -= (double)int_part;
            val *= 10.0;
            buf[pos++] = '0' + (int)val;
            val -= (int)val;
            val *= 10.0;
            buf[pos++] = '0' + (int)val;
            buf[pos] = '\0';
        }
    }

    {
        int pos = funsos_strlen(buf);
        buf[pos++] = ' ';
        {
            const char *u = units[unit_idx];
            while (*u)
                buf[pos++] = *u++;
            buf[pos] = '\0';
        }
    }

    return buf;
}

/* ===== 实用工具 ===== */

uint32_t funsos_align_up(uint32_t val, uint32_t align)
{
    if (align == 0)
        return val;
    return (val + align - 1) & ~(align - 1);
}

uint32_t funsos_min_u(uint32_t a, uint32_t b)
{
    return (a < b) ? a : b;
}

uint32_t funsos_max_u(uint32_t a, uint32_t b)
{
    return (a > b) ? a : b;
}

int funsos_min_i(int a, int b)
{
    return (a < b) ? a : b;
}

int funsos_max_i(int a, int b)
{
    return (a > b) ? a : b;
}

void funsos_swap(void *a, void *b, uint32_t size)
{
    uint8_t *pa = (uint8_t *)a;
    uint8_t *pb = (uint8_t *)b;
    uint8_t tmp;
    uint32_t i;

    for (i = 0; i < size; i++) {
        tmp = pa[i];
        pa[i] = pb[i];
        pb[i] = tmp;
    }
}

/* DJB2 哈希算法 */
uint32_t funsos_hash_djb2(const char *str)
{
    uint32_t hash = 5381;
    int c;

    while ((c = (unsigned char)*str++))
        hash = hash * 33 + c;

    return hash;
}

/* FNV-1a 哈希算法 */
uint32_t funsos_hash_fnv1a(const char *str)
{
    uint32_t hash = 2166136261u;  /* FNV offset basis */
    int c;

    while ((c = (unsigned char)*str++)) {
        hash ^= (uint32_t)c;
        hash *= 16777619u;         /* FNV prime */
    }

    return hash;
}

/* ===== 动态内存分配 ===== */

/* 简单的堆内存分配器 - 内部使用链表管理空闲块 */
typedef struct funsos_mem_block {
    uint32_t size;
    struct funsos_mem_block *next;
    uint8_t  used;
} funsos_mem_block_t;

#define FUNSOS_HEAP_SIZE (64 * 1024) /* 64KB SDK堆 */
static uint8_t funsos_heap[FUNSOS_HEAP_SIZE];
static int funsos_heap_inited = 0;

static void funsos_heap_init(void) {
    if (funsos_heap_inited) return;
    funsos_mem_block_t *first = (funsos_mem_block_t *)funsos_heap;
    first->size = FUNSOS_HEAP_SIZE - sizeof(funsos_mem_block_t);
    first->next = NULL;
    first->used = 0;
    funsos_heap_inited = 1;
}

void *funsos_alloc(uint32_t size) {
    if (!funsos_heap_inited) funsos_heap_init();
    if (size == 0) return NULL;

    /* 对齐到8字节 */
    size = (size + 7u) & ~7u;

    funsos_mem_block_t *block = (funsos_mem_block_t *)funsos_heap;
    while ((uint8_t *)block < funsos_heap + FUNSOS_HEAP_SIZE) {
        if (!block->used && block->size >= size) {
            /* 找到合适块，分割（如果剩余空间足够大） */
            if (block->size >= size + sizeof(funsos_mem_block_t) + 16) {
                funsos_mem_block_t *new_block =
                    (funsos_mem_block_t *)((uint8_t *)block + sizeof(funsos_mem_block_t) + size);
                new_block->size = block->size - size - sizeof(funsos_mem_block_t);
                new_block->next = block->next;
                new_block->used = 0;
                block->next = new_block;
                block->size = size;
            }
            block->used = 1;
            return (void *)((uint8_t *)block + sizeof(funsos_mem_block_t));
        }
        block = block->next;
    }
    return NULL; /* 堆满 */
}

void funsos_free(void *ptr) {
    if (!ptr) return;

    funsos_mem_block_t *block =
        (funsos_mem_block_t *)((uint8_t *)ptr - sizeof(funsos_mem_block_t));

    /* 安全检查：确保指针在堆范围内 */
    if ((uint8_t *)block < funsos_heap ||
        (uint8_t *)block >= funsos_heap + FUNSOS_HEAP_SIZE)
        return;

    block->used = 0;

    /* 合并后续空闲块 */
    while (block->next && !block->next->used) {
        block->size += sizeof(funsos_mem_block_t) + block->next->size;
        block->next = block->next->next;
    }
}
