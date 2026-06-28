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
static uint8_t mouse_bytes[4];
static int mouse_wheel_enabled = 1;

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

    mouse_wait(0);
    outb(0x64, 0xD4);
    mouse_wait(0);
    outb(0x60, 0xF6);
    mouse_read();

    mouse_wait(0);
    outb(0x64, 0xD4);
    mouse_wait(0);
    outb(0x60, 0xF3);
    mouse_read();
    mouse_wait(0);
    outb(0x64, 0xD4);
    mouse_wait(0);
    outb(0x60, 200);
    mouse_read();

    mouse_wait(0);
    outb(0x64, 0xD4);
    mouse_wait(0);
    outb(0x60, 0xF3);
    mouse_read();
    mouse_wait(0);
    outb(0x64, 0xD4);
    mouse_wait(0);
    outb(0x60, 100);
    mouse_read();

    mouse_wait(0);
    outb(0x64, 0xD4);
    mouse_wait(0);
    outb(0x60, 0xF3);
    mouse_read();
    mouse_wait(0);
    outb(0x64, 0xD4);
    mouse_wait(0);
    outb(0x60, 80);
    mouse_read();

    mouse_wait(0);
    outb(0x64, 0xD4);
    mouse_wait(0);
    outb(0x60, 0xF2);
    mouse_read();
    uint8_t mouse_id = mouse_read();
    if (mouse_id == 3) {
        mouse_wheel_enabled = 1;
    } else {
        mouse_wheel_enabled = 0;
    }

    mouse_wait(0);
    outb(0x64, 0xD4);
    mouse_wait(0);
    outb(0x60, 0xE8);
    mouse_read();
    mouse_wait(0);
    outb(0x64, 0xD4);
    mouse_wait(0);
    outb(0x60, 3);
    mouse_read();

    mouse_wait(0);
    outb(0x64, 0xD4);
    mouse_wait(0);
    outb(0x60, 0xE6);
    mouse_read();

    mouse_wait(0);
    outb(0x64, 0xD4);
    mouse_wait(0);
    outb(0x60, 0xF4);
    mouse_read();

    irq_register_handler(12, mouse_handler);
    pic_unmask(12);
}

void mouse_handler(regs_t *regs) {
    (void)regs;
    uint8_t status = inb(0x64);
    if (!(status & 0x20)) {
        return;
    }

    uint8_t byte = inb(0x60);

    if (mouse_cycle == 0) {
        if (!(byte & 0x08)) {
            return;
        }
        mouse_bytes[0] = byte;
        mouse_cycle = 1;
        return;
    }

    if (mouse_cycle == 1) {
        mouse_bytes[1] = byte;
        mouse_cycle = 2;
        return;
    }

    if (mouse_cycle == 2) {
        mouse_bytes[2] = byte;
        if (mouse_wheel_enabled) {
            mouse_cycle = 3;
            return;
        }
        mouse_cycle = 0;
    } else if (mouse_cycle == 3) {
        mouse_bytes[3] = byte & 0x0F;
        mouse_cycle = 0;
    } else {
        mouse_cycle = 0;
        return;
    }

    if (mouse_cycle != 0) return;

    mouse_event_t event;
    event.buttons = mouse_bytes[0] & 0x07;
    event.flags = 0;
    event.wheel = 0;

    if (mouse_wheel_enabled) {
        int8_t wheel_delta = (int8_t)mouse_bytes[3];
        if (wheel_delta > 0 && wheel_delta <= 8) {
            event.wheel = -wheel_delta;
        } else if (wheel_delta >= -8 && wheel_delta < 0) {
            event.wheel = -wheel_delta;
        }
    }

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
