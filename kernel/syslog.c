#include "syslog.h"
#include "stdarg.h"
#include "string.h"
#include "stdio.h"
#include "serial.h"
#include "vfs.h"
#include "sync.h"
#include "spinlock.h"
#include "udp.h"
#include "net.h"
#include "timer.h"

#define SYSLOG_MAX_RULES 16

static syslog_rule_t rules[SYSLOG_MAX_RULES];
static uint32_t rule_count = 0;
static spinlock_t syslog_lock;

/* Ring buffer for syslog (dmesg) */
static char syslog_buf[SYSLOG_BUF_SIZE];
static uint32_t syslog_write_pos = 0;
static uint32_t syslog_read_pos = 0;
static uint32_t syslog_line_count = 0;
static uint32_t syslog_total_lost = 0;

/* Facility name table */
static const char *facility_names[] = {
    "kern", "user", "mail", "daemon",
    "auth", "syslog", "lpr", "news",
    "uucp", "cron", "authpriv", "ftp",
    "ntp", "security", "console", "solaris",
    "local0", "local1", "local2", "local3",
    "local4", "local5", "local6", "local7"
};

#define FACILITY_NAME_COUNT (sizeof(facility_names) / sizeof(facility_names[0]))

/* Level name table */
static const char *level_names[] = {
    "emerg", "alert", "crit", "err",
    "warning", "notice", "info", "debug"
};

static const char *syslog_facility_name(uint32_t facility) {
    uint32_t idx = facility >> 3;
    if (idx < FACILITY_NAME_COUNT)
        return facility_names[idx];
    return "unknown";
}

static const char *syslog_level_name(uint32_t level) {
    if (level <= 7)
        return level_names[level];
    return "unknown";
}

static void syslog_buf_putc(char c) {
    syslog_buf[syslog_write_pos % SYSLOG_BUF_SIZE] = c;
    syslog_write_pos++;

    if (syslog_write_pos - syslog_read_pos > SYSLOG_BUF_SIZE) {
        while (syslog_read_pos < syslog_write_pos) {
            char ch = syslog_buf[syslog_read_pos % SYSLOG_BUF_SIZE];
            syslog_read_pos++;
            if (ch == '\n') {
                syslog_total_lost++;
                syslog_line_count--;
                break;
            }
        }
    }
}

static void syslog_buf_puts(const char *s) {
    while (*s) {
        syslog_buf_putc(*s);
        s++;
    }
}

void syslog_init(void) {
    memset(rules, 0, sizeof(rules));
    rule_count = 0;
    spinlock_init(&syslog_lock);

    memset(syslog_buf, 0, sizeof(syslog_buf));
    syslog_write_pos = 0;
    syslog_read_pos = 0;
    syslog_line_count = 0;
    syslog_total_lost = 0;

    /* Rule 1: All facilities, level <= WARNING -> serial + klog */
    syslog_rule_t r1;
    memset(&r1, 0, sizeof(r1));
    r1.facility = 0xFFFFFFFF;  /* All facilities */
    r1.min_level = LOG_WARNING;
    r1.targets = SYSLOG_TARGET_SERIAL | SYSLOG_TARGET_KLOG;
    syslog_add_rule(&r1);

    /* Rule 2: Facility KERN, level <= INFO -> klog */
    syslog_rule_t r2;
    memset(&r2, 0, sizeof(r2));
    r2.facility = (1u << (SYSLOG_FAC_KERN >> 3));
    r2.min_level = LOG_INFO;
    r2.targets = SYSLOG_TARGET_KLOG;
    syslog_add_rule(&r2);

    /* Rule 3: Facility KERN, level <= ERR -> file /var/log/kernel.log */
    syslog_rule_t r3;
    memset(&r3, 0, sizeof(r3));
    r3.facility = (1u << (SYSLOG_FAC_KERN >> 3));
    r3.min_level = LOG_ERR;
    r3.targets = SYSLOG_TARGET_FILE;
    strncpy(r3.filepath, "/var/log/kernel.log", sizeof(r3.filepath) - 1);
    syslog_add_rule(&r3);
}

void syslog_log_va(uint32_t facility, uint32_t level, const char *fmt, va_list args) {
    if (level > LOG_DEBUG)
        return;

    const char *fac_name = syslog_facility_name(facility);
    const char *lvl_name = syslog_level_name(level);

    /* Format timestamp: "[  123.456] " */
    char timestamp_buf[32];
    uint32_t ticks = timer_get_ticks();
    uint32_t total_ms = ticks * 10;
    uint32_t sec = total_ms / 1000;
    uint32_t frac = total_ms % 1000;

    snprintf(timestamp_buf, sizeof(timestamp_buf),
             "[%5u.%03u] ", sec, frac);

    /* Format the full message: "[时间] [设施:级别] 消息" */
    char msg_buf[SYSLOG_MAX_LINE];
    int prefix_len = 0;

    prefix_len += snprintf(msg_buf + prefix_len, sizeof(msg_buf) - prefix_len,
                           "%s", timestamp_buf);
    prefix_len += snprintf(msg_buf + prefix_len, sizeof(msg_buf) - prefix_len,
                           "[%s:%s] ", fac_name, lvl_name);

    if (prefix_len < 0) prefix_len = 0;
    if ((uint32_t)prefix_len >= sizeof(msg_buf)) prefix_len = sizeof(msg_buf) - 1;

    vsnprintf(msg_buf + prefix_len, sizeof(msg_buf) - prefix_len, fmt, args);

    uint32_t flags = spinlock_irq_save(&syslog_lock);

    /* Write to syslog ring buffer (dmesg) */
    syslog_buf_puts(msg_buf);
    uint32_t msg_len = strlen(msg_buf);
    if (msg_len == 0 || msg_buf[msg_len - 1] != '\n') {
        syslog_buf_putc('\n');
    }
    syslog_line_count++;

    /* Check each rule */
    uint32_t fac_idx = facility >> 3;
    for (uint32_t i = 0; i < rule_count; i++) {
        syslog_rule_t *r = &rules[i];

        if (fac_idx < 32 && !(r->facility & (1u << fac_idx)))
            continue;

        if (level > r->min_level)
            continue;

        if (r->targets & SYSLOG_TARGET_SERIAL) {
            serial_print(COM1, msg_buf);
            uint32_t mlen = strlen(msg_buf);
            if (mlen == 0 || msg_buf[mlen - 1] != '\n')
                serial_putchar(COM1, '\n');
        }

        if (r->targets & SYSLOG_TARGET_KLOG) {
            klog_write(level, "%s", msg_buf + prefix_len);
        }

        if (r->targets & SYSLOG_TARGET_FILE) {
            file_t *fp = NULL;
            if (vfs_open(r->filepath, FILE_MODE_WRITE, &fp) == 0 && fp) {
                vfs_seek(fp, 0, SEEK_END);
                uint32_t mlen = strlen(msg_buf);
                vfs_write(fp, msg_buf, mlen);
                if (mlen == 0 || msg_buf[mlen - 1] != '\n') {
                    vfs_write(fp, "\n", 1);
                }
                vfs_close(fp);
            }
        }

        if (r->targets & SYSLOG_TARGET_NETWORK) {
            uint32_t priority = (facility >> 3) * 8 + level;
            char udp_buf[1024];
            int udp_len = snprintf(udp_buf, sizeof(udp_buf), "<%u>%s",
                                   priority, msg_buf);
            if (udp_len > 0) {
                ipv4_addr_t dst_ip;
                dst_ip.addr = 0;
                const char *host = r->remote_host;
                uint32_t octet = 0;
                int shift = 24;
                for (int j = 0; host[j] && shift >= 0; j++) {
                    if (host[j] == '.') {
                        dst_ip.addr |= (octet << shift);
                        octet = 0;
                        shift -= 8;
                    } else if (host[j] >= '0' && host[j] <= '9') {
                        octet = octet * 10 + (host[j] - '0');
                    }
                }
                if (shift >= 0) {
                    dst_ip.addr |= (octet << shift);
                }

                if (dst_ip.addr != 0 && r->remote_port != 0) {
                    net_interface_t *iface = net_get_default_interface();
                    if (iface) {
                        udp_send(iface, dst_ip, r->remote_port, 514,
                                 udp_buf, (uint32_t)udp_len);
                    }
                }
            }
        }
    }

    spinlock_irq_restore(&syslog_lock, flags);
}

void syslog_log(uint32_t facility, uint32_t level, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    syslog_log_va(facility, level, fmt, args);
    va_end(args);
}

int syslog_add_rule(syslog_rule_t *rule) {
    if (rule_count >= SYSLOG_MAX_RULES)
        return -1;

    uint32_t flags = spinlock_irq_save(&syslog_lock);
    memcpy(&rules[rule_count], rule, sizeof(syslog_rule_t));
    rule_count++;
    spinlock_irq_restore(&syslog_lock, flags);
    return 0;
}

int syslog_remove_rule(uint32_t index) {
    if (index >= rule_count)
        return -1;

    uint32_t flags = spinlock_irq_save(&syslog_lock);
    for (uint32_t i = index; i < rule_count - 1; i++) {
        memcpy(&rules[i], &rules[i + 1], sizeof(syslog_rule_t));
    }
    rule_count--;
    memset(&rules[rule_count], 0, sizeof(syslog_rule_t));
    spinlock_irq_restore(&syslog_lock, flags);
    return 0;
}

uint32_t syslog_get_rule_count(void) {
    return rule_count;
}

syslog_rule_t *syslog_get_rule(uint32_t index) {
    if (index >= rule_count)
        return NULL;
    return &rules[index];
}

void syslog_flush(void) {
    uint32_t flags = spinlock_irq_save(&syslog_lock);

    file_t *fp = NULL;
    if (vfs_open("/var/log/messages", FILE_MODE_WRITE, &fp) == 0 && fp) {
        vfs_seek(fp, 0, SEEK_END);

        uint32_t pos = syslog_read_pos;
        while (pos < syslog_write_pos) {
            char c = syslog_buf[pos % SYSLOG_BUF_SIZE];
            vfs_write(fp, &c, 1);
            pos++;
        }

        vfs_close(fp);
    }

    spinlock_irq_restore(&syslog_lock, flags);

    klog_dump_to_serial();
}

void syslog_process_log(uint32_t pid, uint32_t level, const char *msg) {
    syslog_log(SYSLOG_FAC_USER, level, "[pid=%u] %s", pid, msg);
}

uint32_t syslog_dmesg_read(char *out, uint32_t max_len) {
    uint32_t flags = spinlock_irq_save(&syslog_lock);

    uint32_t available = syslog_write_pos - syslog_read_pos;
    uint32_t to_read = available < max_len ? available : max_len;

    for (uint32_t i = 0; i < to_read; i++) {
        out[i] = syslog_buf[(syslog_read_pos + i) % SYSLOG_BUF_SIZE];
    }

    spinlock_irq_restore(&syslog_lock, flags);

    if (to_read < max_len) {
        out[to_read] = '\0';
    }

    return to_read;
}

uint32_t syslog_dmesg_read_from(uint32_t start_line, char *out, uint32_t max_len) {
    uint32_t flags = spinlock_irq_save(&syslog_lock);

    uint32_t pos = syslog_read_pos;
    uint32_t line = 0;

    while (pos < syslog_write_pos && line < start_line) {
        if (syslog_buf[pos % SYSLOG_BUF_SIZE] == '\n') {
            line++;
        }
        pos++;
    }

    if (line < start_line) {
        spinlock_irq_restore(&syslog_lock, flags);
        out[0] = '\0';
        return 0;
    }

    uint32_t remaining = syslog_write_pos - pos;
    uint32_t to_read = remaining < max_len ? remaining : max_len;

    for (uint32_t i = 0; i < to_read; i++) {
        out[i] = syslog_buf[(pos + i) % SYSLOG_BUF_SIZE];
    }

    spinlock_irq_restore(&syslog_lock, flags);

    if (to_read < max_len) {
        out[to_read] = '\0';
    }

    return to_read;
}

uint32_t syslog_dmesg_get_line_count(void) {
    uint32_t flags = spinlock_irq_save(&syslog_lock);
    uint32_t count = syslog_line_count;
    spinlock_irq_restore(&syslog_lock, flags);
    return count;
}

void syslog_dmesg_clear(void) {
    uint32_t flags = spinlock_irq_save(&syslog_lock);
    syslog_write_pos = 0;
    syslog_read_pos = 0;
    syslog_line_count = 0;
    syslog_total_lost = 0;
    spinlock_irq_restore(&syslog_lock, flags);
}
