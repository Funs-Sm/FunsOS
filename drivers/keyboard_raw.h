#ifndef KEYBOARD_RAW_H
#define KEYBOARD_RAW_H

#include "stdint.h"
#include "kernel_types.h"

/* AT/PS2 keyboard driver in raw scancode set 2 mode.
 *
 * The default keyboard.c driver handles set 1 scancodes (XT translation off).
 * This driver handles set 2 (the native AT protocol) and exposes raw scan
 * codes to applications that need full control over key bindings.
 */

#define KB2_BUFFER_SIZE  256
#define KB2_MAX_RELEASE_FOLLOWUPS 8

typedef struct {
    uint8_t scancode;   /* raw set 2 scancode (release flag in bit 7) */
    uint8_t pressed;    /* 1 = key press, 0 = key release */
} kb2_event_t;

void kb2_init(void);
void kb2_handler(regs_t *regs);   /* Call from the keyboard ISR */
int  kb2_get_event(kb2_event_t *event);
int  kb2_has_data(void);

/* Helper: convert a set 2 scancode to a set 1 scancode (for compatibility). */
uint8_t kb2_to_set1(uint8_t sc2);

#endif