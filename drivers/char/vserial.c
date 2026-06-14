#include "vserial.h"
#include "serial.h"
#include "irq.h"
#include "idt.h"
#include "sync.h"
#include "io.h"
#include "string.h"

static char vserial_rx_buf[VSERIAL_BUF_SIZE];
static volatile uint32_t vserial_rx_head = 0;
static volatile uint32_t vserial_rx_tail = 0;

static char vserial_tx_buf[VSERIAL_BUF_SIZE];
static volatile uint32_t vserial_tx_head = 0;
static volatile uint32_t vserial_tx_tail = 0;

static spinlock_t vserial_lock;

static int vserial_port_ready(void) {
    return inb(VSERIAL_PORT + 5) & 0x01;
}

static int vserial_tx_empty(void) {
    return inb(VSERIAL_PORT + 5) & 0x20;
}

void vserial_init(void) {
    vserial_rx_head = 0;
    vserial_rx_tail = 0;
    vserial_tx_head = 0;
    vserial_tx_tail = 0;

    spinlock_init(&vserial_lock);

    /* Initialize COM3 hardware */
    serial_init(VSERIAL_PORT);

    /* Enable receive data interrupt on COM3 (IER bit 0) */
    outb(VSERIAL_PORT + 1, 0x01);

    /* Register IRQ4 handler (shared with COM1) */
    irq_register_handler(4, vserial_handler);
    pic_unmask(4);
}

void vserial_handler(regs_t *regs) {
    (void)regs;

    /* Check if interrupt came from COM3 */
    uint8_t iir = inb(VSERIAL_PORT + 2);
    if (iir & 0x01) {
        /* No interrupt pending for this port */
        return;
    }

    /* Read available data from COM3 */
    while (vserial_port_ready()) {
        char c = (char)inb(VSERIAL_PORT);

        uint32_t next = (vserial_rx_tail + 1) % VSERIAL_BUF_SIZE;
        if (next != vserial_rx_head) {
            vserial_rx_buf[vserial_rx_tail] = c;
            vserial_rx_tail = next;
        }
    }

    /* Try to flush TX buffer */
    while (vserial_tx_head != vserial_tx_tail && vserial_tx_empty()) {
        outb(VSERIAL_PORT, (uint8_t)vserial_tx_buf[vserial_tx_head]);
        vserial_tx_head = (vserial_tx_head + 1) % VSERIAL_BUF_SIZE;
    }
}

int vserial_write(const char *buf, uint32_t len) {
    uint32_t written = 0;

    spinlock_lock(&vserial_lock);

    for (uint32_t i = 0; i < len; i++) {
        /* If TX buffer is empty and port is ready, send directly */
        if (vserial_tx_head == vserial_tx_tail && vserial_tx_empty()) {
            outb(VSERIAL_PORT, (uint8_t)buf[i]);
        } else {
            /* Buffer the byte */
            uint32_t next = (vserial_tx_tail + 1) % VSERIAL_BUF_SIZE;
            if (next == vserial_tx_head) {
                /* TX buffer full - try to drain some */
                while (vserial_tx_head != vserial_tx_tail && vserial_tx_empty()) {
                    outb(VSERIAL_PORT, (uint8_t)vserial_tx_buf[vserial_tx_head]);
                    vserial_tx_head = (vserial_tx_head + 1) % VSERIAL_BUF_SIZE;
                }
                next = (vserial_tx_tail + 1) % VSERIAL_BUF_SIZE;
                if (next == vserial_tx_head) {
                    break; /* Still full, drop remaining */
                }
            }
            vserial_tx_buf[vserial_tx_tail] = buf[i];
            vserial_tx_tail = next;
        }
        written++;
    }

    spinlock_unlock(&vserial_lock);
    return (int)written;
}

int vserial_read(char *buf, uint32_t maxlen) {
    uint32_t count = 0;

    spinlock_lock(&vserial_lock);

    while (count < maxlen && vserial_rx_head != vserial_rx_tail) {
        buf[count] = vserial_rx_buf[vserial_rx_head];
        vserial_rx_head = (vserial_rx_head + 1) % VSERIAL_BUF_SIZE;
        count++;
    }

    spinlock_unlock(&vserial_lock);
    return (int)count;
}

int vserial_available(void) {
    int avail;
    spinlock_lock(&vserial_lock);
    if (vserial_rx_tail >= vserial_rx_head) {
        avail = (int)(vserial_rx_tail - vserial_rx_head);
    } else {
        avail = (int)(VSERIAL_BUF_SIZE - vserial_rx_head + vserial_rx_tail);
    }
    spinlock_unlock(&vserial_lock);
    return avail;
}
