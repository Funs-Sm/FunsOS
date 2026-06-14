#include "rtc.h"
#include "sync.h"
#include "io.h"

static spinlock_t rtc_lock;

static uint8_t rtc_read_register(uint8_t reg) {
    outb(RTC_CMOS_PORT, reg | 0x80);
    return inb(RTC_DATA_PORT);
}

static void rtc_write_register(uint8_t reg, uint8_t val) {
    outb(RTC_CMOS_PORT, reg | 0x80);
    outb(RTC_DATA_PORT, val);
}

static uint8_t bcd_to_binary(uint8_t bcd) {
    return (bcd & 0x0F) + ((bcd >> 4) * 10);
}

static int rtc_is_updating(void) {
    outb(RTC_CMOS_PORT, 0x0A);
    return (inb(RTC_DATA_PORT) & 0x80);
}

void rtc_init(void) {
    spinlock_init(&rtc_lock);
    spinlock_lock(&rtc_lock);
    uint8_t prev = rtc_read_register(0x0B);
    rtc_write_register(0x0B, prev | 0x40);
    rtc_write_register(0x0A, 0x26);
    spinlock_unlock(&rtc_lock);
}

void rtc_read_time(rtc_time_t *time) {
    spinlock_lock(&rtc_lock);

    while (rtc_is_updating()) {}

    uint8_t second = rtc_read_register(0x00);
    uint8_t minute = rtc_read_register(0x02);
    uint8_t hour = rtc_read_register(0x04);
    uint8_t day = rtc_read_register(0x07);
    uint8_t month = rtc_read_register(0x08);
    uint8_t year = rtc_read_register(0x09);
    uint8_t weekday = rtc_read_register(0x06);

    uint8_t reg_b = rtc_read_register(0x0B);

    if (!(reg_b & 0x04)) {
        second = bcd_to_binary(second);
        minute = bcd_to_binary(minute);
        hour = bcd_to_binary(hour);
        day = bcd_to_binary(day);
        month = bcd_to_binary(month);
        year = bcd_to_binary(year);
        weekday = bcd_to_binary(weekday);
    }

    if (!(reg_b & 0x02) && (hour & 0x80)) {
        hour = ((hour & 0x7F) + 12) % 24;
    }

    time->second = second;
    time->minute = minute;
    time->hour = hour;
    time->day = day;
    time->month = month;
    time->year = year + 2000;
    time->weekday = weekday;

    spinlock_unlock(&rtc_lock);
}

uint32_t rtc_get_timestamp(void) {
    rtc_time_t t;
    rtc_read_time(&t);

    static const uint8_t days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    uint32_t days = 0;

    for (uint16_t y = 1970; y < t.year; y++) {
        days += 365;
        if ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) {
            days++;
        }
    }

    for (int m = 1; m < t.month; m++) {
        days += days_in_month[m - 1];
        if (m == 2 && ((t.year % 4 == 0 && t.year % 100 != 0) || (t.year % 400 == 0))) {
            days++;
        }
    }

    days += t.day - 1;

    return days * 86400 + t.hour * 3600 + t.minute * 60 + t.second;
}

void rtc_set_time(const rtc_time_t *time) {
    (void)time;
    return;
}
