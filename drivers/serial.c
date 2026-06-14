#include "serial.h"
#include "sync.h"
#include "stdarg.h"
#include "io.h"

static int serial_received(uint16_t port) {
    return inb(port + 5) & 0x01;
}

static int serial_transmit_empty(uint16_t port) {
    return inb(port + 5) & 0x20;
}

void serial_init(uint16_t port) {
    outb(port + 1, 0x00);
    outb(port + 3, 0x80);
    outb(port + 0, 0x01);
    outb(port + 1, 0x00);
    outb(port + 3, 0x03);
    outb(port + 2, 0xC7);
    outb(port + 4, 0x0B);
}

char serial_read(uint16_t port) {
    while (!serial_received(port));
    return inb(port);
}

void serial_putchar(uint16_t port, char c) {
    while (!serial_transmit_empty(port));
    outb(port, (uint8_t)c);
}

void serial_print(uint16_t port, const char *str) {
    while (*str) {
        if (*str == '\n') {
            serial_putchar(port, '\r');
        }
        serial_putchar(port, *str);
        str++;
    }
}

int serial_available(uint16_t port) {
    return serial_received(port);
}

static void serial_print_dec(uint16_t port, int val) {
    char buf[12];
    int i = 0;

    if (val < 0) {
        serial_putchar(port, '-');
        val = -val;
    }

    if (val == 0) {
        serial_putchar(port, '0');
        return;
    }

    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }

    while (i > 0) {
        serial_putchar(port, buf[--i]);
    }
}

static void serial_print_hex(uint16_t port, uint32_t val) {
    char hex[] = "0123456789abcdef";

    serial_putchar(port, '0');
    serial_putchar(port, 'x');

    int started = 0;
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (val >> i) & 0x0F;
        if (nibble || started || i == 0) {
            serial_putchar(port, hex[nibble]);
            started = 1;
        }
    }
}

void serial_printf(uint16_t port, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case 'd': {
                    int val = va_arg(args, int);
                    serial_print_dec(port, val);
                    break;
                }
                case 'x': {
                    uint32_t val = va_arg(args, uint32_t);
                    serial_print_hex(port, val);
                    break;
                }
                case 's': {
                    const char *s = va_arg(args, const char *);
                    if (s) {
                        serial_print(port, s);
                    } else {
                        serial_print(port, "(null)");
                    }
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(args, int);
                    serial_putchar(port, c);
                    break;
                }
                case '%': {
                    serial_putchar(port, '%');
                    break;
                }
                default: {
                    serial_putchar(port, '%');
                    serial_putchar(port, *fmt);
                    break;
                }
            }
        } else {
            if (*fmt == '\n') {
                serial_putchar(port, '\r');
            }
            serial_putchar(port, *fmt);
        }
        fmt++;
    }

    va_end(args);
}
