#ifndef BATTERY_H
#define BATTERY_H

#include "stdint.h"

typedef struct {
    uint8_t present;
    uint8_t charging;
    uint8_t discharging;
    uint8_t critical;
    uint8_t percent;
    uint16_t voltage;    /* mV */
    uint16_t current;    /* mA */
    uint16_t capacity;   /* mAh */
    uint16_t design_cap; /* mAh */
    char type[16];       /* "Li-ion", "NiMH", etc. */
    char status[16];     /* "Charging", "Discharging", "Full" */
} battery_info_t;

void battery_init(void);
int battery_get_info(battery_info_t *info);
uint8_t battery_get_percent(void);
int battery_is_charging(void);

#endif
