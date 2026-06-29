#ifndef SYSLOG_H
#define SYSLOG_H

#include "stdint.h"
#include "klog.h"

/* Syslog facilities */
#define SYSLOG_FAC_KERN     (0 << 3)  /* Kernel messages */
#define SYSLOG_FAC_USER     (1 << 3)  /* User-level messages */
#define SYSLOG_FAC_MAIL     (2 << 3)  /* Mail system */
#define SYSLOG_FAC_DAEMON   (3 << 3)  /* System daemons */
#define SYSLOG_FAC_AUTH     (4 << 3)  /* Security/authorization */
#define SYSLOG_FAC_SYSLOG   (5 << 3)  /* Syslog internal */
#define SYSLOG_FAC_LPR      (6 << 3)  /* Line printer */
#define SYSLOG_FAC_NEWS     (7 << 3)  /* Network news */
#define SYSLOG_FAC_UUCP     (8 << 3)  /* UUCP */
#define SYSLOG_FAC_CRON     (9 << 3)  /* Clock daemon */
#define SYSLOG_FAC_AUTHPRIV (10 << 3) /* Private auth */
#define SYSLOG_FAC_FTP      (11 << 3) /* FTP daemon */
#define SYSLOG_FAC_LOCAL0   (16 << 3) /* Local use 0-7 */
#define SYSLOG_FAC_LOCAL7   (23 << 3)

/* Log levels (matching standard syslog priorities) */
#define LOG_EMERG    0   /* System is unusable */
#define LOG_ALERT    1   /* Action must be taken immediately */
#define LOG_CRIT     2   /* Critical conditions */
#define LOG_ERR      3   /* Error conditions */
#define LOG_WARNING  4   /* Warning conditions */
#define LOG_NOTICE   5   /* Normal but significant */
#define LOG_INFO     6   /* Informational */
#define LOG_DEBUG    7   /* Debug messages */

/* Syslog output targets */
#define SYSLOG_TARGET_SERIAL  0x01
#define SYSLOG_TARGET_KLOG    0x02
#define SYSLOG_TARGET_FILE    0x04
#define SYSLOG_TARGET_NETWORK 0x08

#define SYSLOG_MAX_RULES 16
#define SYSLOG_BUF_SIZE  (16 * 1024)  /* 16KB ring buffer */
#define SYSLOG_MAX_LINE  512

typedef struct syslog_rule {
    uint32_t facility;       /* Facility mask (bitfield) */
    uint32_t min_level;      /* Minimum priority level */
    uint32_t targets;        /* Output target bitmask */
    char filepath[128];      /* File path if TARGET_FILE */
    char remote_host[64];    /* Remote host if TARGET_NETWORK */
    uint16_t remote_port;    /* Remote port */
} syslog_rule_t;

void syslog_init(void);
void syslog_log(uint32_t facility, uint32_t level, const char *fmt, ...);
void syslog_log_va(uint32_t facility, uint32_t level, const char *fmt, va_list args);
int syslog_add_rule(syslog_rule_t *rule);
int syslog_remove_rule(uint32_t index);
uint32_t syslog_get_rule_count(void);
syslog_rule_t *syslog_get_rule(uint32_t index);
void syslog_flush(void);

/* Per-process log */
void syslog_process_log(uint32_t pid, uint32_t level, const char *msg);

/* dmesg buffer read interface */
uint32_t syslog_dmesg_read(char *out, uint32_t max_len);
uint32_t syslog_dmesg_read_from(uint32_t start_line, char *out, uint32_t max_len);
uint32_t syslog_dmesg_get_line_count(void);
void syslog_dmesg_clear(void);

#endif
