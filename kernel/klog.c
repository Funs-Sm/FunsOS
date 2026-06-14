#include "klog.h"
#include "stdarg.h"
#include "string.h"
#include "stdio.h"
#include "timer.h"
#include "serial.h"
#include "spinlock.h"

static klog_t klog;

void klog_init(void) {
    memset(&klog, 0, sizeof(klog_t));
    klog.current_level = KLOG_INFO;
    klog.timestamp_enabled = 1;
    spinlock_init(&klog.lock);
}

static void klog_putc(char c) {
    klog.buf[klog.write_pos % KLOG_BUF_SIZE] = c;
    klog.write_pos++;

    /* If write_pos has wrapped around and caught up to read_pos, advance read */
    if (klog.write_pos - klog.read_pos > KLOG_BUF_SIZE) {
        /* Skip over the oldest line by advancing read_pos past the next newline */
        while (klog.read_pos < klog.write_pos) {
            char ch = klog.buf[klog.read_pos % KLOG_BUF_SIZE];
            klog.read_pos++;
            if (ch == '\n') {
                klog.total_lost++;
                klog.line_count--;
                break;
            }
        }
    }
}

static void klog_puts(const char *s) {
    while (*s) {
        klog_putc(*s);
        s++;
    }
}

static void klog_put_dec(uint32_t val) {
    char buf[12];
    int i = 0;
    if (val == 0) {
        klog_putc('0');
        return;
    }
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (--i >= 0) {
        klog_putc(buf[i]);
    }
}

void klog_write_va(uint32_t level, const char *fmt, va_list args) {
    if (level > klog.current_level)
        return;

    uint32_t flags = spinlock_irq_save(&klog.lock);

    /* Format timestamp: "[  123.456] " */
    if (klog.timestamp_enabled) {
        uint32_t ticks = timer_get_ticks();
        uint32_t total_ms = ticks * 10; /* 100 Hz timer => 10ms per tick */
        uint32_t sec = total_ms / 1000;
        uint32_t frac = total_ms % 1000;

        klog_putc('[');
        /* Pad seconds to at least 3 digits */
        if (sec < 100) klog_putc(' ');
        if (sec < 10) klog_putc(' ');
        klog_put_dec(sec);
        klog_putc('.');
        /* Pad fractional part to 3 digits */
        if (frac < 100) klog_putc('0');
        if (frac < 10) klog_putc('0');
        klog_put_dec(frac);
        klog_putc(']');
        klog_putc(' ');
    }

    /* Format the user message into a temp buffer */
    char line_buf[KLOG_MAX_LINE];
    vsnprintf(line_buf, KLOG_MAX_LINE, fmt, args);

    /* Write the formatted line into the ring buffer */
    klog_puts(line_buf);

    /* Ensure the line ends with newline */
    uint32_t len = strlen(line_buf);
    if (len == 0 || line_buf[len - 1] != '\n') {
        klog_putc('\n');
    }

    klog.line_count++;

    /* Also output to serial port (COM1) */
    serial_print(COM1, line_buf);
    if (len == 0 || line_buf[len - 1] != '\n') {
        serial_putchar(COM1, '\n');
    }

    spinlock_irq_restore(&klog.lock, flags);
}

void klog_write(uint32_t level, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    klog_write_va(level, fmt, args);
    va_end(args);
}

uint32_t klog_read(char *out, uint32_t max_len) {
    uint32_t flags = spinlock_irq_save(&klog.lock);

    uint32_t available = klog.write_pos - klog.read_pos;
    uint32_t to_read = available < max_len ? available : max_len;

    for (uint32_t i = 0; i < to_read; i++) {
        out[i] = klog.buf[(klog.read_pos + i) % KLOG_BUF_SIZE];
    }

    spinlock_irq_restore(&klog.lock, flags);

    if (to_read < max_len) {
        out[to_read] = '\0';
    }

    return to_read;
}

uint32_t klog_read_from(uint32_t start_line, char *out, uint32_t max_len) {
    uint32_t flags = spinlock_irq_save(&klog.lock);

    /* Scan from read_pos to find the start_line-th line */
    uint32_t pos = klog.read_pos;
    uint32_t line = 0;

    while (pos < klog.write_pos && line < start_line) {
        if (klog.buf[pos % KLOG_BUF_SIZE] == '\n') {
            line++;
        }
        pos++;
    }

    if (line < start_line) {
        /* Not enough lines */
        spinlock_irq_restore(&klog.lock, flags);
        out[0] = '\0';
        return 0;
    }

    /* Read from pos to write_pos */
    uint32_t remaining = klog.write_pos - pos;
    uint32_t to_read = remaining < max_len ? remaining : max_len;

    for (uint32_t i = 0; i < to_read; i++) {
        out[i] = klog.buf[(pos + i) % KLOG_BUF_SIZE];
    }

    spinlock_irq_restore(&klog.lock, flags);

    if (to_read < max_len) {
        out[to_read] = '\0';
    }

    return to_read;
}

uint32_t klog_get_line_count(void) {
    uint32_t flags = spinlock_irq_save(&klog.lock);
    uint32_t count = klog.line_count;
    spinlock_irq_restore(&klog.lock, flags);
    return count;
}

void klog_set_level(uint32_t level) {
    if (level > KLOG_DEBUG)
        level = KLOG_DEBUG;
    klog.current_level = level;
}

uint32_t klog_get_level(void) {
    return klog.current_level;
}

void klog_clear(void) {
    uint32_t flags = spinlock_irq_save(&klog.lock);
    klog.write_pos = 0;
    klog.read_pos = 0;
    klog.line_count = 0;
    klog.total_lost = 0;
    spinlock_irq_restore(&klog.lock, flags);
}

void klog_dump_to_serial(void) {
    uint32_t flags = spinlock_irq_save(&klog.lock);

    serial_print(COM1, "--- klog dump start ---\n");
    uint32_t pos = klog.read_pos;
    while (pos < klog.write_pos) {
        char c = klog.buf[pos % KLOG_BUF_SIZE];
        serial_putchar(COM1, c);
        pos++;
    }
    serial_print(COM1, "--- klog dump end ---\n");

    if (klog.total_lost > 0) {
        char num_buf[16];
        snprintf(num_buf, sizeof(num_buf), "%u", klog.total_lost);
        serial_print(COM1, "Lost lines: ");
        serial_print(COM1, num_buf);
        serial_print(COM1, "\n");
    }

    spinlock_irq_restore(&klog.lock, flags);
}
