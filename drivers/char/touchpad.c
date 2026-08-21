/* touchpad.c - I2C HID touchpad driver.
 *
 * Implements the HID-over-I2C protocol used by Microsoft Precision Touchpad
 * compliant devices. Most of the protocol is opaque to the kernel - the
 * touchpad reports raw HID reports via its input reports (HID GET_REPORT).
 *
 * This driver handles the common cases: relative movement, clickpad button
 * reports, and a single-finger tap as a left-click. Multi-finger gestures
 * are passed through to applications via the fingers field.
 */

#include "touchpad.h"
#include "i2c.h"
#include "string.h"

#define HID_REPORT_DESCRIPTOR  0x01
#define HID_INPUT_REPORT       0x01

/* Standard Precision Touchpad report IDs */
#define REPORT_ID_TOUCH     0x01
#define REPORT_ID_INPUT     0x04

/* Internal touchpad state. The driver binds to an adapter at init time. */
static i2c_adapter_t *g_adapter;
static uint16_t       g_addr;
static touchpad_event_t g_buffer[TOUCHPAD_BUFFER_SIZE];
static volatile uint32_t g_head;
static volatile uint32_t g_tail;
static int g_initialized;

/* Push an event to the queue. */
static void tp_push(const touchpad_event_t *event) {
    uint32_t next = (g_tail + 1) % TOUCHPAD_BUFFER_SIZE;
    if (next == g_head) return;  /* drop on overflow */
    g_buffer[g_tail] = *event;
    g_tail = next;
}

/* Read a HID report from the device. */
static int tp_read_report(uint8_t *buf, uint32_t len) {
    uint8_t cmd = HID_INPUT_REPORT;
    struct i2c_msg msgs[2] = {
        { .addr = g_addr, .flags = 0,        .len = 1, .buf = &cmd },
        { .addr = g_addr, .flags = I2C_M_RD, .len = len, .buf = buf },
    };
    return i2c_transfer(g_adapter, msgs, 2);
}

/* Decode a Synaptics-style or generic 6-byte relative report.
 * Common layout: [buttons, dx_lo, dx_hi, dy_lo, dy_hi, misc] */
static void tp_decode_report(const uint8_t *report, uint32_t len,
                              touchpad_event_t *event)
{
    memset(event, 0, sizeof(*event));
    if (len < 5) return;

    event->buttons = report[0] & 0x07;
    int16_t dx = (int16_t)(((uint16_t)report[3] << 8) | report[2]);
    int16_t dy = (int16_t)(((uint16_t)report[5] << 8) | report[4]);
    if (dx > 2047) dx -= 4096;
    if (dy > 2047) dy -= 4096;
    event->dx = dx;
    event->dy = dy;
    event->fingers = 1;

    if (len >= 7) {
        event->fingers = (report[6] & 0x0F) ?: 1;
    }
}

int touchpad_init(i2c_adapter_t *adapter, uint16_t i2c_addr) {
    if (g_initialized) return 0;
    if (!adapter) return -1;
    g_adapter = adapter;
    g_addr = i2c_addr;
    g_head = g_tail = 0;

    /* Power-on: write RESET (0x01 0x00 0x00 0x00). */
    uint8_t rst[] = {0x01, 0x00, 0x00, 0x00};
    struct i2c_msg reset_msg = {
        .addr = g_addr,
        .flags = 0,
        .len = sizeof(rst),
        .buf = rst,
    };
    i2c_transfer(g_adapter, &reset_msg, 1);

    /* Read HID descriptor (4 bytes minimum). */
    uint8_t cmd = 0x00;
    uint8_t desc[4] = {0};
    struct i2c_msg msgs[2] = {
        { .addr = g_addr, .flags = 0,        .len = 1, .buf = &cmd },
        { .addr = g_addr, .flags = I2C_M_RD, .len = 4, .buf = desc },
    };
    if (i2c_transfer(g_adapter, msgs, 2) != 0) return -1;

    if (desc[0] == 0 || desc[1] == 0) return -1;

    g_initialized = 1;
    return 0;
}

void touchpad_handler(void) {
    if (!g_initialized) return;
    uint8_t report[16];
    int r = tp_read_report(report, sizeof(report));
    if (r != 0) return;

    touchpad_event_t ev;
    tp_decode_report(report, sizeof(report), &ev);
    if (ev.dx != 0 || ev.dy != 0 || ev.buttons != 0) {
        tp_push(&ev);
    }
}

int touchpad_get_event(touchpad_event_t *event) {
    if (g_head == g_tail) return 0;
    *event = g_buffer[g_head];
    g_head = (g_head + 1) % TOUCHPAD_BUFFER_SIZE;
    return 1;
}

int touchpad_has_data(void) {
    return g_head != g_tail ? 1 : 0;
}