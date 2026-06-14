#include "touchpad.h"
#include "irq.h"
#include "idt.h"
#include "sync.h"
#include "kheap.h"
#include "io.h"
#include "string.h"

static touchpad_event_t touchpad_buffer[TOUCHPAD_BUFFER_SIZE];
static uint32_t touchpad_buf_head = 0;
static uint32_t touchpad_buf_tail = 0;

static uint8_t touchpad_cycle = 0;
static uint8_t touchpad_bytes[6]; /* PS/2 touchpad can send up to 6 bytes */
static uint8_t touchpad_packet_len = 3;

static int32_t touchpad_x = 0;
static int32_t touchpad_y = 0;
static uint8_t touchpad_fingers = 0;
static uint8_t touchpad_gesture_mode = 0; /* 0=none, 1=scroll */

static void touchpad_wait(int type) {
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

static void touchpad_write(uint8_t val) {
    touchpad_wait(0);
    outb(0x64, 0xD4);
    touchpad_wait(0);
    outb(0x60, val);
    uint32_t timeout = 100000;
    while (timeout--) {
        if (inb(0x64) & 0x01) break;
    }
    inb(0x60);
}

static uint8_t touchpad_read(void) {
    touchpad_wait(1);
    return inb(0x60);
}

static void touchpad_set_sample_rate(uint8_t rate) {
    touchpad_write(0xF3);
    touchpad_write(rate);
}

static int touchpad_detect(void) {
    /* Synaptics touchpad detection sequence:
     * Send set sample rate 200, 100, 200 then read device ID */
    touchpad_set_sample_rate(200);
    touchpad_set_sample_rate(100);
    touchpad_set_sample_rate(80);

    touchpad_write(0xF2); /* Get device ID */
    uint8_t id = touchpad_read();

    /* Touchpad IDs: 0x00 = standard, some touchpads return 0x03 or 0x04 */
    if (id == 0x00 || id == 0x03 || id == 0x04) {
        return 1;
    }
    return 0;
}

void touchpad_init(void) {
    touchpad_buf_head = 0;
    touchpad_buf_tail = 0;
    touchpad_cycle = 0;
    touchpad_x = 0;
    touchpad_y = 0;
    touchpad_fingers = 0;
    touchpad_gesture_mode = 0;
    touchpad_packet_len = 3;

    /* Enable PS/2 auxiliary port */
    outb(0x64, 0xA8);

    /* Read and modify controller config byte */
    outb(0x64, 0x20);
    uint8_t status = touchpad_read();
    status |= 0x02;   /* Enable IRQ12 */
    status &= ~0x20;  /* Disable mouse clock gating */
    outb(0x64, 0x60);
    touchpad_wait(0);
    outb(0x60, status);

    /* Try to detect touchpad via Synaptics magic sequence */
    if (touchpad_detect()) {
        /* Enable extended packet mode for gesture support.
         * Set resolution and sample rate for touchpad. */
        touchpad_write(0xE8); /* Set resolution */
        touchpad_write(0x03); /* 8 counts/mm */

        touchpad_set_sample_rate(80);

        /* Enable touchpad-specific mode: W mode for finger detection.
         * This is a Synaptics extension - send the magic sequence again
         * to enter extended mode. */
        touchpad_set_sample_rate(200);
        touchpad_set_sample_rate(100);
        touchpad_set_sample_rate(80);

        touchpad_write(0xE8); /* Set resolution to request W-mode */
        touchpad_write(0x00);

        touchpad_packet_len = 6; /* Extended packets with finger info */
    } else {
        /* Fallback: standard PS/2 mouse protocol */
        touchpad_write(0xE8);
        touchpad_write(0x03);
        touchpad_set_sample_rate(100);
        touchpad_packet_len = 3;
    }

    /* Enable data reporting */
    touchpad_write(0xF4);

    /* Register IRQ12 handler (shared with mouse) */
    irq_register_handler(12, touchpad_handler);
    pic_unmask(12);
}

void touchpad_handler(regs_t *regs) {
    (void)regs;
    uint8_t byte = inb(0x60);

    /* For standard 3-byte packets, byte 0 must have bit 3 set */
    if (touchpad_cycle == 0 && touchpad_packet_len == 3 && !(byte & 0x08)) {
        return;
    }

    touchpad_bytes[touchpad_cycle] = byte;
    touchpad_cycle++;

    if (touchpad_cycle >= touchpad_packet_len) {
        touchpad_cycle = 0;

        touchpad_event_t event;
        memset(&event, 0, sizeof(touchpad_event_t));

        if (touchpad_packet_len == 3) {
            /* Standard PS/2 relative mode */
            uint8_t flags = touchpad_bytes[0];

            int16_t raw_dx = touchpad_bytes[1];
            int16_t raw_dy = touchpad_bytes[2];
            if (flags & 0x10) raw_dx |= 0xFF00;
            if (flags & 0x20) raw_dy |= 0xFF00;

            event.dx = (int16_t)(int8_t)raw_dx;
            event.dy = (int16_t)(int8_t)raw_dy;
            event.x = touchpad_x + event.dx;
            event.y = touchpad_y + event.dy;
            touchpad_x = event.x;
            touchpad_y = event.y;
            event.fingers = 1;
            event.type = TOUCHPAD_EVENT_MOVE;

            /* Detect tap: left button press without movement */
            if ((flags & 0x01) && event.dx == 0 && event.dy == 0) {
                event.type = TOUCHPAD_EVENT_TAP;
                event.tap_zone = TOUCHPAD_TAP_LEFT;
            }

            /* Detect right button as two-finger tap */
            if (flags & 0x02) {
                event.type = TOUCHPAD_EVENT_TAP;
                event.tap_zone = TOUCHPAD_TAP_RIGHT;
                event.fingers = 2;
            }
        } else {
            /* Extended 6-byte packet (Synaptics W-mode style) */
            uint8_t w = ((touchpad_bytes[0] & 0x30) >> 4) |
                        ((touchpad_bytes[3] & 0x0F) << 2);

            int16_t raw_dx = touchpad_bytes[1];
            int16_t raw_dy = touchpad_bytes[2];
            uint8_t z = touchpad_bytes[3] >> 4;

            if (touchpad_bytes[0] & 0x10) raw_dx |= 0xFF00;
            if (touchpad_bytes[0] & 0x20) raw_dy |= 0xFF00;

            event.dx = (int16_t)(int8_t)raw_dx;
            event.dy = (int16_t)(int8_t)raw_dy;
            event.x = touchpad_x + event.dx;
            event.y = touchpad_y + event.dy;
            touchpad_x = event.x;
            touchpad_y = event.y;

            /* Determine finger count from W field */
            if (w >= 4 && w <= 6) {
                event.fingers = 1;
            } else if (w == 0 || w == 1) {
                event.fingers = 2;
            } else if (w == 2) {
                event.fingers = 3;
            } else {
                event.fingers = 0;
            }

            /* Detect gestures based on finger count */
            if (event.fingers == 0) {
                /* No fingers - ignore */
                return;
            } else if (event.fingers == 1) {
                if (z > 0 && z < 10 && event.dx == 0 && event.dy == 0) {
                    /* Light touch with no movement = tap */
                    event.type = TOUCHPAD_EVENT_TAP;
                    event.tap_zone = TOUCHPAD_TAP_LEFT;
                } else {
                    event.type = TOUCHPAD_EVENT_MOVE;
                }
            } else if (event.fingers >= 2) {
                /* Two or more fingers: scroll mode */
                event.type = TOUCHPAD_EVENT_SCROLL;
                event.scroll_dy = event.dy;
            }
        }

        /* Enqueue event */
        uint32_t next_tail = (touchpad_buf_tail + 1) % TOUCHPAD_BUFFER_SIZE;
        if (next_tail != touchpad_buf_head) {
            touchpad_buffer[touchpad_buf_tail] = event;
            touchpad_buf_tail = next_tail;
        }
    }
}

int touchpad_has_data(void) {
    return touchpad_buf_head != touchpad_buf_tail ? 1 : 0;
}

int touchpad_get_event(touchpad_event_t *event) {
    if (touchpad_buf_head == touchpad_buf_tail) {
        return 0;
    }
    *event = touchpad_buffer[touchpad_buf_head];
    touchpad_buf_head = (touchpad_buf_head + 1) % TOUCHPAD_BUFFER_SIZE;
    return 1;
}
