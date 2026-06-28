#ifndef MOUSE_H
#define MOUSE_H

#include "stdint.h"
#include "kernel_types.h"

#define MOUSE_BUFFER_SIZE 256

#define MOUSE_LEFT   0x01
#define MOUSE_RIGHT  0x02
#define MOUSE_MIDDLE 0x04

typedef struct {
    int8_t dx;
    int8_t dy;
    int8_t wheel;
    uint8_t buttons;
    uint8_t flags;
} mouse_event_t;

void mouse_init(void);
void mouse_handler(regs_t *regs);
int mouse_get_event(mouse_event_t *event);
int mouse_has_data(void);

#endif
