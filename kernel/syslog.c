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

#define SYSLOG_MAX_RULES 16

static syslog_rule_t rules[SYSLOG_MAX_RULES];
static uint32_t rule_count = 0;
static spinlock_t syslog_lock;

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

void syslog_init(void) {
    memset(rules, 0, sizeof(rules));
    rule_count = 0;
    spinlock_init(&syslog_lock);

    /* Rule 1: All facilities, level <= WARNING -> serial + klog */
    syslog_rule_t r1;
    memset(&r1, 0, sizeof(r1));
    r1.facility = 0xFFFFFFFF;  /* All facilities */
    r1.min_level = KLOG_WARNING;
    r1.targets = SYSLOG_TARGET_SERIAL | SYSLOG_TARGET_KLOG;
    syslog_add_rule(&r1);

    /* Rule 2: Facility KERN, level <= INFO -> klog */
    syslog_rule_t r2;
    memset(&r2, 0, sizeof(r2));
    r2.facility = (1u << (SYSLOG_FAC_KERN >> 3));
    r2.min_level = KLOG_INFO;
    r2.targets = SYSLOG_TARGET_KLOG;
    syslog_add_rule(&r2);

    /* Rule 3: Facility KERN, level <= ERR -> file /var/log/kernel.log */
    syslog_rule_t r3;
    memset(&r3, 0, sizeof(r3));
    r3.facility = (1u << (SYSLOG_FAC_KERN >> 3));
    r3.min_level = KLOG_ERR;
    r3.targets = SYSLOG_TARGET_FILE;
    strncpy(r3.filepath, "/var/log/kernel.log", sizeof(r3.filepath) - 1);
    syslog_add_rule(&r3);
}

void syslog_log_va(uint32_t facility, uint32_t level, const char *fmt, va_list args) {
    if (level > KLOG_DEBUG)
        return;

    /* Format the message with facility.level prefix */
    char msg_buf[1024];
    const char *fac_name = syslog_facility_name(facility);
    const char *lvl_name = syslog_level_name(level);

    int prefix_len = snprintf(msg_buf, sizeof(msg_buf), "%s.%s: ", fac_name, lvl_name);
    if (prefix_len < 0) prefix_len = 0;
    if ((uint32_t)prefix_len >= sizeof(msg_buf)) prefix_len = sizeof(msg_buf) - 1;

    vsnprintf(msg_buf + prefix_len, sizeof(msg_buf) - prefix_len, fmt, args);

    uint32_t flags = spinlock_irq_save(&syslog_lock);

    /* Check each rule */
    uint32_t fac_idx = facility >> 3;
    for (uint32_t i = 0; i < rule_count; i++) {
        syslog_rule_t *r = &rules[i];

        /* Check if facility matches (bitfield) */
        if (fac_idx < 32 && !(r->facility & (1u << fac_idx)))
            continue;

        /* Check if level meets threshold */
        if (level > r->min_level)
            continue;

        /* Output to each target */
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
            /* Write to VFS file (open, append, close) */
            file_t *fp = NULL;
            if (vfs_open(r->filepath, FILE_MODE_WRITE, &fp) == 0 && fp) {
                /* Seek to end for append */
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
            /* Send UDP syslog packet (RFC 5426) to remote host */
            /* Format: <priority>message */
            /* Priority = facility * 8 + level */
            uint32_t priority = (facility >> 3) * 8 + level;
            char udp_buf[1024];
            int udp_len = snprintf(udp_buf, sizeof(udp_buf), "<%u>%s",
                                   priority, msg_buf);
            if (udp_len > 0) {
                /* Parse remote host IP (simple: assume dotted decimal) */
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
    /* Currently no buffering - all output is immediate */
    klog_dump_to_serial();
}

void syslog_process_log(uint32_t pid, uint32_t level, const char *msg) {
    syslog_log(SYSLOG_FAC_USER, level, "[pid=%u] %s", pid, msg);
}
