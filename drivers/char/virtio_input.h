#ifndef VIRTIO_INPUT_H
#define VIRTIO_INPUT_H

#include "stdint.h"
#include "kernel_types.h"

/* VirtIO input device driver - keyboard + mouse/tablet events from the host.
 *
 * Implements virtio-input (virtio spec 5.9). The host sends event batches over
 * an event virtqueue, each containing one or more virtio_input_event structs.
 * The driver maps these into:
 *   - keyboard_event_t for KEY events (matches the existing keyboard.h API)
 *   - mouse_event_t    for REL/ABS BTN events
 * so the rest of the kernel does not need to know about virtio.
 */

#define VIRTIO_INPUT_VENDOR_ID         0x1AF4
#define VIRTIO_INPUT_DEVICE_ID_LEGACY  0x1100
#define VIRTIO_INPUT_DEVICE_ID_MODERN  0x1042

/* virtio_input_event type field (low byte) */
#define VIRTIO_INPUT_EV_SYN  0x00
#define VIRTIO_INPUT_EV_KEY  0x01
#define VIRTIO_INPUT_EV_REL  0x02
#define VIRTIO_INPUT_EV_ABS  0x03
#define VIRTIO_INPUT_EV_MSC  0x04

/* REL subcodes */
#define VIRTIO_INPUT_REL_X      0x00
#define VIRTIO_INPUT_REL_Y      0x01
#define VIRTIO_INPUT_REL_WHEEL  0x08

/* KEY subcodes (subset, matches Linux evdev KEY_* for keys we use) */
#define VIRTIO_INPUT_KEY_ESC        1
#define VIRTIO_INPUT_KEY_1          2
#define VIRTIO_INPUT_KEY_2          3
#define VIRTIO_INPUT_KEY_BACKSPACE  14
#define VIRTIO_INPUT_KEY_TAB        15
#define VIRTIO_INPUT_KEY_Q          16
#define VIRTIO_INPUT_KEY_ENTER      28
#define VIRTIO_INPUT_KEY_LEFTSHIFT  42
#define VIRTIO_INPUT_KEY_LEFTCTRL   29
#define VIRTIO_INPUT_KEY_LEFTALT    56
#define VIRTIO_INPUT_KEY_SPACE      57
#define VIRTIO_INPUT_KEY_F1         59
#define VIRTIO_INPUT_KEY_UP         103
#define VIRTIO_INPUT_KEY_DOWN       108
#define VIRTIO_INPUT_KEY_LEFT       105
#define VIRTIO_INPUT_KEY_RIGHT      106

/* BTN subcodes for mouse */
#define VIRTIO_INPUT_BTN_LEFT   0x110
#define VIRTIO_INPUT_BTN_RIGHT  0x111
#define VIRTIO_INPUT_BTN_MIDDLE 0x112

/* Event as delivered by host (8 bytes). */
typedef struct __attribute__((packed)) {
    uint16_t type;
    uint16_t code;
    uint32_t value;
} virtio_input_event_t;

/* Event ring buffer exposed to the rest of the kernel. */
typedef struct {
    uint16_t type;
    uint16_t code;
    uint32_t value;
} input_event_t;

#define VIRTIO_INPUT_RING_SIZE  128

int virtio_input_init(void);
int virtio_input_get_event(input_event_t *ev);
int virtio_input_has_event(void);

#endif