/* gamepad.c - ISA game port joystick / gamepad driver.
 *
 * Reads the analog axis position by writing 1 to each axis bit and timing
 * how long it takes for the corresponding pin (read at 0x201 bit4..bit7)
 * to fall to 0. The RC decay time is proportional to the joystick's
 * potentiometer resistance and thus to the axis position.
 *
 * Buttons are read directly as bits 4..7 of port 0x201 (active high).
 */

#include "gamepad.h"
#include "io.h"

#define GAMEPORT_PORT 0x201

#define GAMEPORT_AXIS_MASK   0x0F
#define GAMEPORT_BTN_MASK    0xF0

/* Calibration: a fully-charged RC takes ~24us to discharge through 10k.
 * We use a fixed upper bound and scale the count into int16 range. */
#define GAMEPORT_MAX_COUNT   10000

static int g_initialized;

static int read_axis(uint8_t bit_mask) {
    /* Discharge all axes by writing 0 to the mask. */
    outb(GAMEPORT_PORT, 0x00);
    /* Briefly wait for capacitors to discharge. */
    for (volatile int i = 0; i < 200; i++);

    /* Start charging the selected axis by writing 1 to it. */
    outb(GAMEPORT_PORT, bit_mask);

    /* Time how long it takes for the matching status bit to fall to 0.
     * The corresponding bit position depends on which axis mask bit we
     * selected. The mapping is: bit 0 -> bit 4 (X1), bit 1 -> bit 5 (Y1),
     * bit 2 -> bit 6 (X2), bit 3 -> bit 7 (Y2). */
    uint8_t status_bit = (uint8_t)(bit_mask << 4);
    int count = 0;
    while ((inb(GAMEPORT_PORT) & status_bit) != 0 && count < GAMEPORT_MAX_COUNT) {
        count++;
    }
    /* Discharge again so we leave the port in a known state. */
    outb(GAMEPORT_PORT, 0x00);
    return count;
}

static uint8_t read_buttons(void) {
    /* Buttons are inverted relative to the axis bits - they read 0 when
     * pressed, 1 when released. Map them to logical "1 = pressed". */
    uint8_t raw = inb(GAMEPORT_PORT) & GAMEPORT_BTN_MASK;
    /* Map bits 4..7 to 0..3. */
    return (uint8_t)((raw >> 4) ^ 0x0F) & 0x0F;
}

int gamepad_init(void) {
    if (g_initialized) return 0;
    /* Verify the port exists by writing a known pattern and reading back.
     * Many emulators do not implement the game port, so we just trust it. */
    outb(GAMEPORT_PORT, 0xFF);
    (void)inb(GAMEPORT_PORT);
    outb(GAMEPORT_PORT, 0x00);
    g_initialized = 1;
    return 0;
}

int gamepad_get_state(gamepad_state_t *state) {
    if (!g_initialized) return -1;
    if (!state) return -1;

    int x1 = read_axis(0x01);
    int y1 = read_axis(0x02);
    int x2 = read_axis(0x04);
    int y2 = read_axis(0x08);

    /* Convert counts to signed 16-bit centered values. */
    state->axis1_x = (int16_t)(((int32_t)(x1 - GAMEPORT_MAX_COUNT / 2)
                              * GAMEPAD_MAX_AXIS) / (GAMEPORT_MAX_COUNT / 2));
    state->axis1_y = (int16_t)(((int32_t)(y1 - GAMEPORT_MAX_COUNT / 2)
                              * GAMEPAD_MAX_AXIS) / (GAMEPORT_MAX_COUNT / 2));
    state->axis2_x = (int16_t)(((int32_t)(x2 - GAMEPORT_MAX_COUNT / 2)
                              * GAMEPAD_MAX_AXIS) / (GAMEPORT_MAX_COUNT / 2));
    state->axis2_y = (int16_t)(((int32_t)(y2 - GAMEPORT_MAX_COUNT / 2)
                              * GAMEPAD_MAX_AXIS) / (GAMEPORT_MAX_COUNT / 2));

    state->buttons = read_buttons();
    return 0;
}

int gamepad_wait_event(int timeout_ms) {
    /* Game port is purely polled; we just wait the requested time. */
    if (!g_initialized) return -1;
    int loops = timeout_ms * 1000;
    while (loops--) {
        if (gamepad_get_state((gamepad_state_t *)0) == 0) {
            /* No-op read; just consume a polling cycle. */
        }
    }
    return 0;
}