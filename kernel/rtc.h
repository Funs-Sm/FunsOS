#ifndef RTC_H
#define RTC_H

#include "stdint.h"

#define RTC_CMOS_PORT 0x70
#define RTC_DATA_PORT 0x71

#define RTC_NAME_LEN 32
#define RTC_MAX_DEVS 4

typedef struct rtc_time {
    uint8_t sec;
    uint8_t min;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
    uint8_t second;
    uint8_t minute;
    uint8_t weekday;
} rtc_time_t;

struct rtc_alarm {
    uint8_t sec;
    uint8_t min;
    uint8_t hour;
    uint8_t day;
};

struct rtc_device {
    char name[RTC_NAME_LEN];
    struct rtc_time current_time;
    struct rtc_alarm alarm;
    uint8_t alarm_irq_en;
    uint8_t update_irq_en;
    uint64_t tick_count;
    uint64_t read_count;
    uint64_t set_count;
    uint64_t alarm_count;
    struct rtc_device *next;
};

typedef struct rtc_alarm rtc_alarm_t;
typedef struct rtc_device rtc_device_t;

int rtc_init(void);
int rtc_register_device(rtc_device_t *dev);
void rtc_read_time(rtc_time_t *time);
int rtc_set_time(const rtc_time_t *time);
uint32_t rtc_get_timestamp(void);
void rtc_read_alarm(rtc_alarm_t *alarm);
int rtc_set_alarm(const rtc_alarm_t *alarm);
void rtc_tick(void);
void rtc_print_stats(void);

#endif
