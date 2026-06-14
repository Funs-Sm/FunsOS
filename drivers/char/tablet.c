#include "tablet.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"

static tablet_dev_t tablet_devices[TABLET_MAX_DEVICES];
static uint32_t tablet_count = 0;
static uint32_t global_tick = 0;

void tablet_init(void) {
    tablet_count = 0;
    memset(tablet_devices, 0, sizeof(tablet_devices));
}

int32_t tablet_register_device(tablet_dev_t *dev) {
    if (!dev || tablet_count >= TABLET_MAX_DEVICES) return -1;

    memcpy(&tablet_devices[tablet_count], dev, sizeof(tablet_dev_t));
    tablet_devices[tablet_count].id = tablet_count;
    tablet_devices[tablet_count].initialized = 1;
    tablet_devices[tablet_count].connected = 1;

    /* Set default ranges if not specified */
    if (tablet_devices[tablet_count].range.x_max == 0) {
        tablet_devices[tablet_count].range.x_min = 0;
        tablet_devices[tablet_count].range.x_max = 15200;
        tablet_devices[tablet_count].range.y_min = 0;
        tablet_devices[tablet_count].range.y_max = 9500;
        tablet_devices[tablet_count].range.pressure_min = 0;
        tablet_devices[tablet_count].range.pressure_max = TABLET_MAX_PRESSURE;
        tablet_devices[tablet_count].range.tilt_min = -TABLET_MAX_TILT;
        tablet_devices[tablet_count].range.tilt_max = TABLET_MAX_TILT;
    }

    tablet_count++;
    return (int32_t)(tablet_count - 1);
}

int32_t tablet_unregister_device(uint32_t id) {
    if (id >= tablet_count) return -1;
    tablet_devices[id].initialized = 0;
    tablet_devices[id].connected = 0;
    return 0;
}

tablet_dev_t *tablet_get_device(uint32_t id) {
    if (id >= tablet_count) return 0;
    return &tablet_devices[id];
}

int32_t tablet_get_device_count(void) {
    return (int32_t)tablet_count;
}

int32_t tablet_get_pen_x(uint32_t dev_id) {
    if (dev_id >= tablet_count) return 0;
    return tablet_devices[dev_id].pen.x;
}

int32_t tablet_get_pen_y(uint32_t dev_id) {
    if (dev_id >= tablet_count) return 0;
    return tablet_devices[dev_id].pen.y;
}

int32_t tablet_get_pen_pressure(uint32_t dev_id) {
    if (dev_id >= tablet_count) return 0;
    return tablet_devices[dev_id].pen.pressure;
}

int32_t tablet_get_pen_tilt_x(uint32_t dev_id) {
    if (dev_id >= tablet_count) return 0;
    return tablet_devices[dev_id].pen.tilt_x;
}

int32_t tablet_get_pen_tilt_y(uint32_t dev_id) {
    if (dev_id >= tablet_count) return 0;
    return tablet_devices[dev_id].pen.tilt_y;
}

uint8_t tablet_get_pen_buttons(uint32_t dev_id) {
    if (dev_id >= tablet_count) return 0;
    return tablet_devices[dev_id].pen.buttons;
}

uint8_t tablet_is_pen_in_proximity(uint32_t dev_id) {
    if (dev_id >= tablet_count) return 0;
    return tablet_devices[dev_id].pen.in_proximity;
}

uint8_t tablet_is_pen_in_contact(uint32_t dev_id) {
    if (dev_id >= tablet_count) return 0;
    return tablet_devices[dev_id].pen.in_contact;
}

void tablet_set_event_callback(uint32_t dev_id, void (*callback)(uint32_t, tablet_event_t *)) {
    if (dev_id >= tablet_count) return;
    tablet_devices[dev_id].event_callback = callback;
}

static int32_t tablet_normalize(int32_t value, int32_t old_min, int32_t old_max,
                                 int32_t new_min, int32_t new_max) {
    if (old_max == old_min) return new_min;
    int64_t scaled = (int64_t)(value - old_min) * (new_max - new_min) / (old_max - old_min);
    return (int32_t)(scaled + new_min);
}

void tablet_process_hid_report(uint32_t dev_id, const uint8_t *report, uint32_t len) {
    if (dev_id >= tablet_count || !report || len < 3) return;

    tablet_dev_t *dev = &tablet_devices[dev_id];
    if (!dev->initialized || !dev->connected) return;

    /* Save previous state */
    memcpy(&dev->last_pen, &dev->pen, sizeof(tablet_pen_state_t));

    uint8_t report_id = report[0];

    switch (report_id) {
        case TABLET_REPORT_ID: {
            if (len < 8) break;

            int32_t raw_x = ((int32_t)report[2] << 8) | report[1];
            int32_t raw_y = ((int32_t)report[4] << 8) | report[3];
            int32_t raw_pressure = ((int32_t)report[6] << 8) | report[5];
            uint8_t raw_buttons = report[7];

            /* Proximity detection */
            uint8_t in_prox = (raw_pressure > 0 || raw_buttons != 0) ? 1 : 0;
            uint8_t in_contact = (raw_pressure > dev->range.pressure_min) ? 1 : 0;

            /* Convert to normalized coordinates */
            dev->pen.x = raw_x;
            dev->pen.y = raw_y;
            dev->pen.pressure = raw_pressure;
            dev->pen.buttons = raw_buttons;
            dev->pen.in_proximity = in_prox;
            dev->pen.in_contact = in_contact;
            dev->pen.timestamp = global_tick;

            /* Tilt data (optional fields in HID report) */
            if (len >= 10) {
                dev->pen.tilt_x = (int32_t)(int8_t)report[8];
                dev->pen.tilt_y = (int32_t)(int8_t)report[9];
            }

            /* Determine tool type from buttons */
            if (raw_buttons & TABLET_BTN_ERASER) {
                dev->pen.tool_type = TABLET_TOOL_ERASER;
            } else if (raw_buttons & TABLET_BTN_STYLUS) {
                dev->pen.tool_type = TABLET_TOOL_PEN;
            }

            break;
        }
        case TABLET_PKT_PROXIMITY: {
            if (len < 2) break;
            dev->pen.in_proximity = report[1] ? 1 : 0;
            dev->pen.in_contact = 0;
            dev->pen.pressure = 0;
            dev->pen.timestamp = global_tick;
            break;
        }
        case TABLET_PKT_BUTTON: {
            if (len < 2) break;
            dev->pen.buttons = report[1];
            dev->pen.timestamp = global_tick;
            break;
        }
        default:
            break;
    }

    /* Fire event callback if state changed */
    if (dev->event_callback &&
        (dev->pen.x != dev->last_pen.x ||
         dev->pen.y != dev->last_pen.y ||
         dev->pen.pressure != dev->last_pen.pressure ||
         dev->pen.buttons != dev->last_pen.buttons ||
         dev->pen.in_proximity != dev->last_pen.in_proximity ||
         dev->pen.in_contact != dev->last_pen.in_contact ||
         dev->pen.tilt_x != dev->last_pen.tilt_x ||
         dev->pen.tilt_y != dev->last_pen.tilt_y)) {

        tablet_event_t event;
        memset(&event, 0, sizeof(tablet_event_t));
        event.type = report_id;
        event.x = dev->pen.x;
        event.y = dev->pen.y;
        event.pressure = dev->pen.pressure;
        event.tilt_x = dev->pen.tilt_x;
        event.tilt_y = dev->pen.tilt_y;
        event.tool_type = dev->pen.tool_type;
        event.buttons = dev->pen.buttons;
        event.in_proximity = dev->pen.in_proximity;
        event.in_contact = dev->pen.in_contact;
        event.timestamp = dev->pen.timestamp;

        dev->event_callback(dev_id, &event);
    }
}

void tablet_poll(uint32_t dev_id) {
    global_tick++;
    if (dev_id >= tablet_count) return;

    tablet_dev_t *dev = &tablet_devices[dev_id];
    if (!dev->initialized || !dev->connected) return;

    /* USB HID polling would happen here if using USB interface */
    /* For now, this is a placeholder for the periodic polling loop */
}

void tablet_screen_to_tablet(uint32_t dev_id, int32_t screen_x, int32_t screen_y,
                              int32_t *tablet_x, int32_t *tablet_y) {
    if (dev_id >= tablet_count) return;

    tablet_dev_t *dev = &tablet_devices[dev_id];
    if (tablet_x) {
        *tablet_x = tablet_normalize(screen_x, 0, dev->range.resolution_x,
            dev->range.x_min, dev->range.x_max);
    }
    if (tablet_y) {
        *tablet_y = tablet_normalize(screen_y, 0, dev->range.resolution_y,
            dev->range.y_min, dev->range.y_max);
    }
}

void tablet_tablet_to_screen(uint32_t dev_id, int32_t tablet_x, int32_t tablet_y,
                              int32_t *screen_x, int32_t *screen_y) {
    if (dev_id >= tablet_count) return;

    tablet_dev_t *dev = &tablet_devices[dev_id];
    if (screen_x) {
        *screen_x = tablet_normalize(tablet_x, dev->range.x_min, dev->range.x_max,
            0, dev->range.resolution_x);
    }
    if (screen_y) {
        *screen_y = tablet_normalize(tablet_y, dev->range.y_min, dev->range.y_max,
            0, dev->range.resolution_y);
    }
}