/* slip.c - SLIP (Serial Line IP) protocol driver
 *
 * Encapsulates IP packets over serial lines using the SLIP protocol.
 * SLIP uses special characters to frame packets:
 *   END  (0xC0) - marks end of packet
 *   ESC  (0xDB) - escape character
 *   ESC_END (0xDC) - escaped END byte
 *   ESC_ESC (0xDD) - escaped ESC byte
 */

#include "slip.h"
#include "string.h"
#include "kheap.h"
#include "serial.h"

#define SLIP_END      0xC0
#define SLIP_ESC      0xDB
#define SLIP_ESC_END  0xDC
#define SLIP_ESC_ESC  0xDD

#define SLIP_BUF_SIZE 2048
#define COM1_BASE     0x3F8

static uint8_t  slip_rx_buf[SLIP_BUF_SIZE];
static uint32_t slip_rx_len;
static int      slip_rx_escaped;
static int      slip_initialized;

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    asm volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* Read a byte from COM1 (polling) */
static uint8_t slip_serial_read(void) {
    while (!(inb(COM1_BASE + 5) & 0x01)) {
        /* wait for data ready */
    }
    return inb(COM1_BASE);
}

/* Write a byte to COM1 (polling) */
static void slip_serial_write(uint8_t b) {
    while (!(inb(COM1_BASE + 5) & 0x20)) {
        /* wait for transmit empty */
    }
    outb(COM1_BASE, b);
}

void slip_init(void) {
    slip_rx_len = 0;
    slip_rx_escaped = 0;
    slip_initialized = 1;

    /* Initialize COM1: 115200 baud, 8N1 */
    outb(COM1_BASE + 1, 0x00);  /* Disable interrupts */
    outb(COM1_BASE + 3, 0x80);  /* Enable DLAB */
    outb(COM1_BASE + 0, 0x01);  /* Set divisor lo (115200 baud) */
    outb(COM1_BASE + 1, 0x00);  /* Set divisor hi */
    outb(COM1_BASE + 3, 0x03);  /* 8 bits, no parity, one stop bit */
    outb(COM1_BASE + 2, 0xC7);  /* Enable FIFO, clear, 14-byte threshold */
    outb(COM1_BASE + 4, 0x0B);  /* IRQs enabled, RTS/DSR set */
}

int slip_send(const uint8_t *buf, uint32_t len) {
    if (!buf || len == 0 || !slip_initialized) return -1;

    /* Send END to flush any partial packets */
    slip_serial_write(SLIP_END);

    for (uint32_t i = 0; i < len; i++) {
        switch (buf[i]) {
        case SLIP_END:
            slip_serial_write(SLIP_ESC);
            slip_serial_write(SLIP_ESC_END);
            break;
        case SLIP_ESC:
            slip_serial_write(SLIP_ESC);
            slip_serial_write(SLIP_ESC_ESC);
            break;
        default:
            slip_serial_write(buf[i]);
            break;
        }
    }

    slip_serial_write(SLIP_END);
    return (int)len;
}

int slip_recv(uint8_t *buf, uint32_t maxlen) {
    if (!buf || maxlen == 0 || !slip_initialized) return -1;

    /* Process available serial bytes */
    while (inb(COM1_BASE + 5) & 0x01) {
        uint8_t c = slip_serial_read();

        if (c == SLIP_END) {
            /* End of packet */
            if (slip_rx_len > 0) {
                uint32_t len = slip_rx_len;
                if (len > maxlen) len = maxlen;
                memcpy(buf, slip_rx_buf, len);
                slip_rx_len = 0;
                slip_rx_escaped = 0;
                return (int)len;
            }
            slip_rx_escaped = 0;
            continue;
        }

        if (c == SLIP_ESC) {
            slip_rx_escaped = 1;
            continue;
        }

        if (slip_rx_escaped) {
            if (c == SLIP_ESC_END)       c = SLIP_END;
            else if (c == SLIP_ESC_ESC)  c = SLIP_ESC;
            slip_rx_escaped = 0;
        }

        if (slip_rx_len < SLIP_BUF_SIZE) {
            slip_rx_buf[slip_rx_len++] = c;
        }
    }

    return 0;  /* no complete packet yet */
}
