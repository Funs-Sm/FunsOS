#include "string.h"
#include "stdint.h"
#include "stdlib.h"
#include "stdio.h"

void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

void *memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) {
        *p++ = (uint8_t)c;
    }
    return s;
}

void *memmove(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dst;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *a = (const uint8_t *)s1;
    const uint8_t *b = (const uint8_t *)s2;
    while (n--) {
        if (*a != *b) {
            return *a - *b;
        }
        a++;
        b++;
    }
    return 0;
}

size_t strlen(const char *s) {
    size_t len = 0;
    while (*s++) {
        len++;
    }
    return len;
}

char *strcpy(char *dst, const char *src) {
    char *ret = dst;
    while ((*dst++ = *src++))
        ;
    return ret;
}

char *strncpy(char *dst, const char *src, size_t n) {
    char *ret = dst;
    while (n && (*dst++ = *src++)) {
        n--;
    }
    while (n--) {
        *dst++ = '\0';
    }
    return ret;
}

char *strcat(char *dst, const char *src) {
    char *ret = dst;
    while (*dst) {
        dst++;
    }
    while ((*dst++ = *src++))
        ;
    return ret;
}

char *strncat(char *dst, const char *src, size_t n) {
    char *ret = dst;
    while (*dst) {
        dst++;
    }
    while (n-- && *src) {
        *dst++ = *src++;
    }
    *dst = '\0';
    return ret;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && *s1 == *s2) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) {
        return 0;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) {
            return (char *)s;
        }
        s++;
    }
    if (c == '\0') {
        return (char *)s;
    }
    return NULL;
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) {
            last = s;
        }
        s++;
    }
    if (c == '\0') {
        return (char *)s;
    }
    return (char *)last;
}

char *strstr(const char *haystack, const char *needle) {
    if (*needle == '\0') {
        return (char *)haystack;
    }
    while (*haystack) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n) {
            h++;
            n++;
        }
        if (*n == '\0') {
            return (char *)haystack;
        }
        haystack++;
    }
    return NULL;
}

char *strtok(char *str, const char *delim) {
    static char *last = NULL;
    if (str == NULL) {
        str = last;
    }
    if (str == NULL) {
        return NULL;
    }
    while (*str) {
        const char *d = delim;
        int is_delim = 0;
        while (*d) {
            if (*str == *d) {
                is_delim = 1;
                break;
            }
            d++;
        }
        if (!is_delim) {
            break;
        }
        str++;
    }
    if (*str == '\0') {
        last = NULL;
        return NULL;
    }
    char *token = str;
    while (*str) {
        const char *d = delim;
        int is_delim = 0;
        while (*d) {
            if (*str == *d) {
                is_delim = 1;
                break;
            }
            d++;
        }
        if (is_delim) {
            *str = '\0';
            last = str + 1;
            return token;
        }
        str++;
    }
    last = NULL;
    return token;
}

char *strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *dup = (char *)malloc(len);
    if (dup) {
        memcpy(dup, s, len);
    }
    return dup;
}

char *strndup(const char *s, size_t n) {
    size_t len = strlen(s);
    if (len > n) {
        len = n;
    }
    char *dup = (char *)malloc(len + 1);
    if (dup) {
        memcpy(dup, s, len);
        dup[len] = '\0';
    }
    return dup;
}

void *memchr(const void *s, int c, size_t n) {
    const uint8_t *p = (const uint8_t *)s;
    while (n--) {
        if (*p == (uint8_t)c) {
            return (void *)p;
        }
        p++;
    }
    return NULL;
}

int strcasecmp(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        int c1 = (*s1 >= 'A' && *s1 <= 'Z') ? *s1 + 32 : *s1;
        int c2 = (*s2 >= 'A' && *s2 <= 'Z') ? *s2 + 32 : *s2;
        if (c1 != c2) return c1 - c2;
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int strncasecmp(const char *s1, const char *s2, size_t n) {
    while (n && *s1 && *s2) {
        int c1 = (*s1 >= 'A' && *s1 <= 'Z') ? *s1 + 32 : *s1;
        int c2 = (*s2 >= 'A' && *s2 <= 'Z') ? *s2 + 32 : *s2;
        if (c1 != c2) return c1 - c2;
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

char *strtok_r(char *str, const char *delim, char **saveptr) {
    if (str == NULL) {
        str = *saveptr;
    }
    if (str == NULL) {
        return NULL;
    }
    /* Skip leading delimiters */
    while (*str) {
        const char *d = delim;
        int is_delim = 0;
        while (*d) {
            if (*str == *d) {
                is_delim = 1;
                break;
            }
            d++;
        }
        if (!is_delim) break;
        str++;
    }
    if (*str == '\0') {
        *saveptr = NULL;
        return NULL;
    }
    char *token = str;
    while (*str) {
        const char *d = delim;
        int is_delim = 0;
        while (*d) {
            if (*str == *d) {
                is_delim = 1;
                break;
            }
            d++;
        }
        if (is_delim) {
            *str = '\0';
            *saveptr = str + 1;
            return token;
        }
        str++;
    }
    *saveptr = NULL;
    return token;
}

char *strerror(int errnum) {
    static const char *error_strings[] = {
        [0]   = "Success",
        [1]   = "Operation not permitted",
        [2]   = "No such file or directory",
        [3]   = "No such process",
        [4]   = "Interrupted system call",
        [5]   = "I/O error",
        [6]   = "No such device or address",
        [7]   = "Argument list too long",
        [8]   = "Exec format error",
        [9]   = "Bad file number",
        [10]  = "No child processes",
        [11]  = "Try again",
        [12]  = "Out of memory",
        [13]  = "Permission denied",
        [14]  = "Bad address",
        [15]  = "Block device required",
        [16]  = "Device or resource busy",
        [17]  = "File exists",
        [18]  = "Cross-device link",
        [19]  = "No such device",
        [20]  = "Not a directory",
        [21]  = "Is a directory",
        [22]  = "Invalid argument",
        [23]  = "File table overflow",
        [24]  = "Too many open files",
        [25]  = "Not a typewriter",
        [26]  = "Text file busy",
        [27]  = "File too large",
        [28]  = "No space left on device",
        [29]  = "Illegal seek",
        [30]  = "Read-only file system",
        [31]  = "Too many links",
        [32]  = "Broken pipe",
        [33]  = "Math argument out of domain",
        [34]  = "Math result not representable",
        [35]  = "Resource deadlock would occur",
        [36]  = "File name too long",
        [37]  = "No record locks available",
        [38]  = "Function not implemented",
        [39]  = "Directory not empty",
        [40]  = "Too many symbolic links",
        [42]  = "No message of desired type",
        [43]  = "Identifier removed",
        [74]  = "Not a data message",
        [75]  = "Value too large",
        [84]  = "Illegal byte sequence",
        [88]  = "Socket operation on non-socket",
        [89]  = "Destination address required",
        [90]  = "Message too long",
        [98]  = "Address already in use",
        [99]  = "Cannot assign requested address",
        [100] = "Network is down",
        [101] = "Network is unreachable",
        [103] = "Software caused connection abort",
        [104] = "Connection reset by peer",
        [105] = "No buffer space available",
        [107] = "Transport endpoint not connected",
        [110] = "Connection timed out",
        [111] = "Connection refused",
        [112] = "Host is down",
        [113] = "No route to host",
        [122] = "Quota exceeded",
        [123] = "No medium found",
        [124] = "Wrong medium type",
        [125] = "Operation canceled",
        [200] = "Kernel execution error",
        [201] = "Kernel panic",
        [202] = "Out of kernel memory",
        [203] = "Invalid process",
        [204] = "Invalid thread",
        [205] = "Invalid memory region",
        [206] = "Invalid filesystem",
        [210] = "Mount error",
        [211] = "Unmount error",
        [212] = "Invalid syscall",
        [228] = "Security policy violation",
        [272] = "Page fault",
        [273] = "Segmentation fault",
        [275] = "Illegal instruction",
        [276] = "Division by zero",
        [279] = "Device not found",
        [280] = "Device busy",
        [282] = "Device not supported",
        [300] = "Interpreter error",
        [301] = "Parse error",
        [302] = "Tokenizer error",
        [303] = "Undefined identifier",
        [304] = "Symbol redefinition",
        [305] = "Type mismatch",
        [306] = "Unexpected token",
        [307] = "Missing return",
        [308] = "Break outside loop",
        [309] = "Continue outside loop",
        [310] = "Invalid function call",
        [311] = "Wrong argument count",
        [313] = "Registry corrupt",
        [314] = "Registry type mismatch",
        [315] = "Registry key locked",
        [316] = "Script error",
        [317] = "Not a valid ELF executable",
        [320] = "SDK version mismatch",
        [321] = "Package corrupt",
        [322] = "Package signature invalid",
        [323] = "Package dependency missing",
        [324] = "Package conflict",
        [325] = "GUI subsystem error",
        [326] = "Window error",
        [336] = "End of file reached",
        [337] = "Malformed path",
        [338] = "Resource locked",
        [339] = "Operation timed out",
        [340] = "Operation aborted",
        [341] = "Subsystem not initialized",
        [342] = "Already initialized",
        [343] = "Unsupported operation",
        [344] = "Bad magic number",
        [345] = "Version mismatch",
        [346] = "Internal error",
        [348] = "Buffer overrun",
        [349] = "Buffer underrun",
    };

    if (errnum >= 0 && errnum < 512) {
        if (errnum < (int)(sizeof(error_strings)/sizeof(error_strings[0])) && error_strings[errnum]) {
            return (char *)error_strings[errnum];
        }
    }
    if (errnum >= 35 && errnum <= 133) {
        static char numbuf[32];
        snprintf(numbuf, sizeof(numbuf), "Error %d", errnum);
        return numbuf;
    }
    if (errnum >= 200 && errnum <= 299) {
        static char numbuf2[32];
        snprintf(numbuf2, sizeof(numbuf2), "Kernel error %d", errnum);
        return numbuf2;
    }
    if (errnum >= 300 && errnum <= 399) {
        static char numbuf3[32];
        snprintf(numbuf3, sizeof(numbuf3), "App/SDK error %d", errnum);
        return numbuf3;
    }
    return (char *)"Unknown error";
}

size_t strspn(const char *s, const char *accept) {
    size_t count = 0;
    while (*s) {
        const char *a = accept;
        int found = 0;
        while (*a) {
            if (*s == *a) {
                found = 1;
                break;
            }
            a++;
        }
        if (!found) break;
        count++;
        s++;
    }
    return count;
}

size_t strcspn(const char *s, const char *reject) {
    size_t count = 0;
    while (*s) {
        const char *r = reject;
        int found = 0;
        while (*r) {
            if (*s == *r) {
                found = 1;
                break;
            }
            r++;
        }
        if (found) break;
        count++;
        s++;
    }
    return count;
}

/* ---- 新增函数实现 ---- */

void *memccpy(void *dest, const void *src, int c, size_t n) {
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) {
        *d = *s;
        if (*s == (uint8_t)c) {
            return (void *)(d + 1);
        }
        d++;
        s++;
    }
    return NULL;
}

int memcmp_const_time(const void *s1, const void *s2, size_t n) {
    const uint8_t *a = (const uint8_t *)s1;
    const uint8_t *b = (const uint8_t *)s2;
    int result = 0;
    /* 常量时间比较 - 无论是否发现差异都遍历所有字节 */
    while (n--) {
        result |= (*a ^ *b);
        a++;
        b++;
    }
    return result;
}

char *strerror_r(int errnum, char *buf, size_t buflen) {
    char *msg = strerror(errnum);
    size_t len = strlen(msg);
    if (len >= buflen) len = buflen - 1;
    memcpy(buf, msg, len);
    buf[len] = '\0';
    return buf;
}

size_t strnlen(const char *s, size_t maxlen) {
    size_t len = 0;
    while (len < maxlen && *s) {
        len++;
        s++;
    }
    return len;
}

char *strpbrk(const char *s, const char *accept) {
    while (*s) {
        const char *a = accept;
        while (*a) {
            if (*s == *a) {
                return (char *)s;
            }
            a++;
        }
        s++;
    }
    return NULL;
}

char *strsignal(int sig) {
    static const char *signal_strings[] = {
        "Unknown signal",      /* 0 */
        "Hangup",              /* SIGHUP */
        "Interrupt",           /* SIGINT */
        "Quit",                /* SIGQUIT */
        "Illegal instruction", /* SIGILL */
        "Trace/breakpoint trap", /* SIGTRAP */
        "Aborted",             /* SIGABRT */
        "Bus error",           /* SIGBUS */
        "Floating point exception", /* SIGFPE */
        "Killed",              /* SIGKILL */
        "User defined signal 1", /* SIGUSR1 */
        "Segmentation fault",  /* SIGSEGV */
        "User defined signal 2", /* SIGUSR2 */
        "Broken pipe",         /* SIGPIPE */
        "Alarm clock",         /* SIGALRM */
        "Terminated",          /* SIGTERM */
        "Stack fault",         /* SIGSTKFLT */
        "Child exited",        /* SIGCHLD */
        "Continued",           /* SIGCONT */
        "Stopped",             /* SIGSTOP */
        "Stopped (tty input)", /* SIGTSTP */
        "Stopped (tty input)", /* SIGTTIN */
        "Stopped (tty output)", /* SIGTTOU */
        "Urgent I/O condition", /* SIGURG */
        "CPU time limit exceeded", /* SIGXCPU */
        "File size limit exceeded", /* SIGXFSZ */
        "Virtual timer expired", /* SIGVTALRM */
        "Profiling timer expired", /* SIGPROF */
        "Window changed",      /* SIGWINCH */
        "I/O possible",        /* SIGIO */
        "Power failure",       /* SIGPWR */
        "Bad system call"      /* SIGSYS */
    };

    if (sig >= 0 && sig < 32) {
        return (char *)signal_strings[sig];
    }
    return (char *)"Unknown signal";
}
