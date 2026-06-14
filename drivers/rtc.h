#ifndef RTC_H
#define RTC_H

#include "stdint.h"

#define RTC_CMOS_PORT 0x70
#define RTC_DATA_PORT 0x71

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
    uint8_t weekday;
} rtc_time_t;

void rtc_init(void);
void rtc_read_time(rtc_time_t *time);
uint32_t rtc_get_timestamp(void);
void rtc_set_time(const rtc_time_t *time);

#endif
