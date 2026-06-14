#ifndef TOUCHPAD_H
#define TOUCHPAD_H

#include "stdint.h"
#include "kernel_types.h"

#define TOUCHPAD_BUFFER_SIZE 256

/* Touchpad event types */
#define TOUCHPAD_EVENT_MOVE   0x01
#define TOUCHPAD_EVENT_TAP    0x02
#define TOUCHPAD_EVENT_SCROLL 0x03

/* Touchpad tap zones */
#define TOUCHPAD_TAP_NONE     0x00
#define TOUCHPAD_TAP_LEFT     0x01
#define TOUCHPAD_TAP_RIGHT    0x02
#define TOUCHPAD_TAP_CENTER   0x04

typedef struct {
    uint8_t  type;      /* TOUCHPAD_EVENT_MOVE / TAP / SCROLL */
    int32_t  x;         /* Absolute X position */
    int32_t  y;         /* Absolute Y position */
    int16_t  dx;        /* Relative X movement */
    int16_t  dy;        /* Relative Y movement */
    uint8_t  fingers;   /* Number of fingers detected (1-3) */
    uint8_t  tap_zone;  /* TOUCHPAD_TAP_LEFT / RIGHT / CENTER */
    int16_t  scroll_dy; /* Scroll delta (for two-finger scroll) */
} touchpad_event_t;

void touchpad_init(void);
void touchpad_handler(regs_t *regs);
int  touchpad_has_data(void);
int  touchpad_get_event(touchpad_event_t *event);

#endif
