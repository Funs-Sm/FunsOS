#ifndef TOUCHPAD_H
#define TOUCHPAD_H

#include "stdint.h"

/* I2C HID touchpad / clickpad driver.
 *
 * Implements the Microsoft HID-over-I2C protocol used by Synaptics,
 * ALPS, ELAN and other laptop touchpads. Provides relative movement
 * deltas, button events, and basic multi-finger tap detection.
 */

#define TOUCHPAD_BUFFER_SIZE 64

/* Forward declaration - the real definition lives in kernel/i2c.h. */
struct i2c_adapter;
typedef struct i2c_adapter i2c_adapter_t;

typedef struct {
    int16_t dx;            /* X-axis delta */
    int16_t dy;            /* Y-axis delta */
    uint8_t buttons;       /* bit0 = left, bit1 = right, bit2 = middle */
    uint8_t fingers;       /* number of fingers detected (1..N) */
    int16_t abs_x;         /* last absolute X (if supported) */
    int16_t abs_y;         /* last absolute Y (if supported) */
} touchpad_event_t;

int touchpad_init(i2c_adapter_t *adapter, uint16_t i2c_addr);
int touchpad_get_event(touchpad_event_t *event);
int touchpad_has_data(void);
void touchpad_handler(void);

#endif