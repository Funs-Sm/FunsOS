#ifndef KLOG_H
#define KLOG_H

#include "stdint.h"
#include "stdarg.h"
#include "sync.h"

/* Log levels (matching Linux syslog priorities) */
#define KLOG_EMERG    0   /* System is unusable */
#define KLOG_ALERT    1   /* Action must be taken immediately */
#define KLOG_CRIT     2   /* Critical conditions */
#define KLOG_ERR      3   /* Error conditions */
#define KLOG_WARNING  4   /* Warning conditions */
#define KLOG_NOTICE   5   /* Normal but significant */
#define KLOG_INFO     6   /* Informational */
#define KLOG_DEBUG    7   /* Debug messages */

#define KLOG_BUF_SIZE  (64 * 1024)  /* 64KB ring buffer */
#define KLOG_MAX_LINE  512

typedef struct {
    char buf[KLOG_BUF_SIZE];
    uint32_t write_pos;
    uint32_t read_pos;
    uint32_t line_count;
    uint32_t total_lost;    /* Lines lost due to overflow */
    uint32_t current_level; /* Minimum level to log */
    uint32_t timestamp_enabled;
    spinlock_t lock;
} klog_t;

void klog_init(void);
void klog_write(uint32_t level, const char *fmt, ...);
void klog_write_va(uint32_t level, const char *fmt, va_list args);
uint32_t klog_read(char *out, uint32_t max_len);
uint32_t klog_read_from(uint32_t start_line, char *out, uint32_t max_len);
uint32_t klog_get_line_count(void);
void klog_set_level(uint32_t level);
uint32_t klog_get_level(void);
void klog_clear(void);
void klog_dump_to_serial(void);

/* Convenience macros */
#define klog_emerg(fmt, ...)  klog_write(KLOG_EMERG, fmt, ##__VA_ARGS__)
#define klog_alert(fmt, ...)  klog_write(KLOG_ALERT, fmt, ##__VA_ARGS__)
#define klog_crit(fmt, ...)   klog_write(KLOG_CRIT, fmt, ##__VA_ARGS__)
#define klog_err(fmt, ...)    klog_write(KLOG_ERR, fmt, ##__VA_ARGS__)
#define klog_warn(fmt, ...)   klog_write(KLOG_WARNING, fmt, ##__VA_ARGS__)
#define klog_notice(fmt, ...) klog_write(KLOG_NOTICE, fmt, ##__VA_ARGS__)
#define klog_info(fmt, ...)   klog_write(KLOG_INFO, fmt, ##__VA_ARGS__)
#define klog_debug(fmt, ...)  klog_write(KLOG_DEBUG, fmt, ##__VA_ARGS__)

#endif
