#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "stdint.h"
#include "kernel_types.h"

#define KEYBOARD_BUFFER_SIZE 256

#define KEY_PRESSED  0x01
#define KEY_SHIFT    0x02
#define KEY_CTRL     0x04
#define KEY_ALT      0x08
#define KEY_CAPS     0x10
#define KEY_EXTENDED 0x20

typedef struct {
    uint8_t scancode;
    char ascii;
    uint8_t flags;
} keyboard_event_t;

void keyboard_init(void);
void keyboard_handler(regs_t *regs);
int keyboard_get_event(keyboard_event_t *event);
int keyboard_has_data(void);
uint8_t keyboard_get_scancode(void);
int keyboard_read_line(char *buf, uint32_t size);

/* Poll keyboard hardware directly (bypasses interrupt buffer).
 * Returns 1 if a key event was processed, 0 if no data available. */
int keyboard_poll(void);

/* Block until a key event is available (uses semaphore from IRQ handler) */
void keyboard_wait(void);

#endif
