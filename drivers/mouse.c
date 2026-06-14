#include "mouse.h"
#include "irq.h"
#include "idt.h"
#include "sync.h"
#include "kheap.h"
#include "io.h"

static mouse_event_t mouse_buffer[MOUSE_BUFFER_SIZE];
static uint32_t mouse_buf_head = 0;
static uint32_t mouse_buf_tail = 0;

static uint8_t mouse_cycle = 0;
static uint8_t mouse_bytes[3];

static void mouse_wait(int type) {
    uint32_t timeout = 100000;
    if (type == 0) {
        while (timeout--) {
            if ((inb(0x64) & 0x02) == 0) return;
        }
    } else {
        while (timeout--) {
            if ((inb(0x64) & 0x01) != 0) return;
        }
    }
}

static void mouse_write(uint8_t val) {
    mouse_wait(0);
    outb(0x64, 0xD4);
    mouse_wait(0);
    outb(0x60, val);
    uint32_t timeout = 100000;
    while (timeout--) {
        if (inb(0x64) & 0x01) break;
    }
    inb(0x60);
}

static uint8_t mouse_read(void) {
    mouse_wait(1);
    return inb(0x60);
}

void mouse_init(void) {
    mouse_buf_head = 0;
    mouse_buf_tail = 0;
    mouse_cycle = 0;

    outb(0x64, 0xA8);

    outb(0x64, 0x20);
    uint8_t status = mouse_read();
    status |= 0x02;
    status &= ~0x20;
    outb(0x64, 0x60);
    mouse_wait(0);
    outb(0x60, status);

    mouse_write(0xF3);
    mouse_write(200);
    mouse_write(0xF3);
    mouse_write(100);
    mouse_write(0xF3);
    mouse_write(80);

    mouse_write(0xE8);
    mouse_write(3);

    mouse_write(0xE6);

    mouse_write(0xF4);

    irq_register_handler(12, mouse_handler);
    pic_unmask(12);
}

void mouse_handler(regs_t *regs) {
    (void)regs;
    uint8_t byte = inb(0x60);

    if (mouse_cycle == 0 && !(byte & 0x08)) {
        return;
    }

    mouse_bytes[mouse_cycle] = byte;
    mouse_cycle++;

    if (mouse_cycle >= 3) {
        mouse_cycle = 0;

        mouse_event_t event;
        event.buttons = mouse_bytes[0] & 0x07;
        event.flags = 0;

        int16_t raw_dx = mouse_bytes[1];
        int16_t raw_dy = mouse_bytes[2];
        if (mouse_bytes[0] & 0x10) raw_dx |= 0xFF00;
        if (mouse_bytes[0] & 0x20) raw_dy |= 0xFF00;
        event.dx = (int8_t)raw_dx;
        event.dy = (int8_t)raw_dy;

        uint32_t next_tail = (mouse_buf_tail + 1) % MOUSE_BUFFER_SIZE;
        if (next_tail != mouse_buf_head) {
            mouse_buffer[mouse_buf_tail] = event;
            mouse_buf_tail = next_tail;
        }
    }
}

int mouse_get_event(mouse_event_t *event) {
    if (mouse_buf_head == mouse_buf_tail) {
        return 0;
    }
    *event = mouse_buffer[mouse_buf_head];
    mouse_buf_head = (mouse_buf_head + 1) % MOUSE_BUFFER_SIZE;
    return 1;
}

int mouse_has_data(void) {
    return mouse_buf_head != mouse_buf_tail ? 1 : 0;
}
