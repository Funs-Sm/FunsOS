#ifndef GAMEPAD_H
#define GAMEPAD_H

#include "stdint.h"

/* Game port (ISA) joystick / gamepad driver.
 *
 * The original IBM PC game port is a 15-pin D-sub on the ISA bus that
 * supports up to 2 joysticks with 2 axes and 2 buttons each, plus 4
 * single-bit digital inputs. This driver reads the analog axes via a
 * simple RC-time-discharge measurement and exposes button events as a
 * character device.
 */

#define GAMEPAD_BUFFER_SIZE 64
#define GAMEPAD_MAX_AXIS    32767

/* Button bits */
#define GAMEPAD_BTN_1A   0x01
#define GAMEPAD_BTN_2A   0x02
#define GAMEPAD_BTN_1B   0x04
#define GAMEPAD_BTN_2B   0x08

typedef struct {
    int16_t axis1_x;    /* Joystick A, X axis (-32767..32767) */
    int16_t axis1_y;    /* Joystick A, Y axis */
    int16_t axis2_x;    /* Joystick B, X axis */
    int16_t axis2_y;    /* Joystick B, Y axis */
    uint8_t buttons;    /* button states, GAMEPAD_BTN_* */
} gamepad_state_t;

int gamepad_init(void);
int gamepad_get_state(gamepad_state_t *state);
int gamepad_wait_event(int timeout_ms);

#endif