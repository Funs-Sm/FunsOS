#include "rtc.h"
#include "klog.h"
#include "string.h"
#include "io.h"

#define RTC_MAX_DEVS 4

struct rtc_global {
    uint8_t initialized;
    rtc_device_t devices[RTC_MAX_DEVS];
    uint32_t n_devices;
    rtc_device_t *dev_list;
    rtc_device_t *default_dev;
    uint64_t total_reads;
    uint64_t total_sets;
    uint64_t total_ticks;
    uint64_t total_alarms;
};

static struct rtc_global rtc_data;

static uint8_t rtc_days_in_month(uint8_t month, uint16_t year) {
    static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1 || month > 12) return 31;
    if (month == 2) {
        uint8_t leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return days[month - 1];
}

static int rtc_time_valid(const rtc_time_t *t) {
    if (!t) return 0;
    uint8_t s = t->second > 0 ? t->second : t->sec;
    uint8_t m = t->minute > 0 ? t->minute : t->min;
    uint8_t h = t->hour > 0 ? t->hour : t->hour;
    if (s > 59) return 0;
    if (m > 59) return 0;
    if (h > 23) return 0;
    if (t->day < 1 || t->day > 31) return 0;
    if (t->month < 1 || t->month > 12) return 0;
    if (t->year < 1970 || t->year > 2100) return 0;
    if (t->day > rtc_days_in_month(t->month, t->year)) return 0;
    return 1;
}

static void rtc_sync_time_fields(rtc_time_t *t) {
    if (!t) return;
    if (t->sec == 0 && t->second > 0) t->sec = t->second;
    else if (t->second == 0 && t->sec > 0) t->second = t->sec;
    if (t->min == 0 && t->minute > 0) t->min = t->minute;
    else if (t->minute == 0 && t->min > 0) t->minute = t->min;
}

static uint8_t rtc_bcd_to_bin(uint8_t bcd) {
    return (bcd & 0x0F) + ((bcd >> 4) * 10);
}

static uint8_t rtc_bin_to_bcd(uint8_t bin) {
    return ((bin / 10) << 4) | (bin % 10);
}

static void rtc_cmos_read(rtc_time_t *time) {
    if (!time) return;
    uint8_t prev = inb(0x70);
    outb(0x70, 0x0B);
    uint8_t reg_b = inb(0x71);
    outb(0x70, prev);

    outb(0x70, 0x00); time->second = inb(0x71);
    outb(0x70, 0x02); time->minute = inb(0x71);
    outb(0x70, 0x04); time->hour = inb(0x71);
    outb(0x70, 0x06); time->weekday = inb(0x71);
    outb(0x70, 0x07); time->day = inb(0x71);
    outb(0x70, 0x08); time->month = inb(0x71);
    outb(0x70, 0x09); time->year = inb(0x71);

    if (!(reg_b & 0x04)) {
        time->second = rtc_bcd_to_bin(time->second);
        time->minute = rtc_bcd_to_bin(time->minute);
        time->hour = rtc_bcd_to_bin(time->hour);
        time->day = rtc_bcd_to_bin(time->day);
        time->month = rtc_bcd_to_bin(time->month);
        time->year = rtc_bcd_to_bin(time->year);
        time->weekday = rtc_bcd_to_bin(time->weekday);
    }

    if (!(reg_b & 0x02) && (time->hour & 0x80)) {
        time->hour = ((time->hour & 0x7F) + 12) % 24;
    }

    time->year += 2000;
    time->sec = time->second;
    time->min = time->minute;
}

static void rtc_advance_time(rtc_device_t *dev) {
    if (!dev) return;
    dev->current_time.sec++;
    dev->current_time.second = dev->current_time.sec;
    dev->current_time.minute = dev->current_time.min;
    if (dev->current_time.sec >= 60) {
        dev->current_time.sec = 0;
        dev->current_time.second = 0;
        dev->current_time.min++;
        dev->current_time.minute = dev->current_time.min;
        if (dev->current_time.min >= 60) {
            dev->current_time.min = 0;
            dev->current_time.minute = 0;
            dev->current_time.hour++;
            if (dev->current_time.hour >= 24) {
                dev->current_time.hour = 0;
                dev->current_time.day++;
                uint8_t dim = rtc_days_in_month(dev->current_time.month,
                                                dev->current_time.year);
                if (dev->current_time.day > dim) {
                    dev->current_time.day = 1;
                    dev->current_time.month++;
                    if (dev->current_time.month > 12) {
                        dev->current_time.month = 1;
                        dev->current_time.year++;
                    }
                }
            }
        }
    }

    if (dev->alarm_irq_en) {
        if (dev->current_time.sec == dev->alarm.sec &&
            dev->current_time.min == dev->alarm.min &&
            dev->current_time.hour == dev->alarm.hour) {
            dev->alarm_count++;
            rtc_data.total_alarms++;
        }
    }
}

static void rtc_init_cmos_device(rtc_device_t *dev) {
    memset(dev, 0, sizeof(*dev));
    strncpy(dev->name, "cmos_rtc", 31);

    rtc_time_t t;
    rtc_cmos_read(&t);
    memcpy(&dev->current_time, &t, sizeof(t));

    dev->alarm.sec = 0;
    dev->alarm.min = 0;
    dev->alarm.hour = 0;
    dev->alarm.day = 1;
    dev->alarm_irq_en = 0;
    dev->update_irq_en = 1;
}

uint32_t rtc_get_timestamp(void) {
    rtc_time_t t;
    rtc_read_time(&t);
    rtc_sync_time_fields(&t);

    static const uint8_t days_in_month[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    uint32_t days = 0;

    for (uint16_t y = 1970; y < t.year; y++) {
        days += 365;
        if ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) days++;
    }

    for (int m = 1; m < t.month; m++) {
        days += days_in_month[m - 1];
        if (m == 2 && ((t.year % 4 == 0 && t.year % 100 != 0) || (t.year % 400 == 0))) days++;
    }

    days += t.day - 1;
    return days * 86400 + t.hour * 3600 + t.min * 60 + t.sec;
}

int rtc_init(void) {
    if (rtc_data.initialized) return 0;
    memset(&rtc_data, 0, sizeof(rtc_data));

    rtc_init_cmos_device(&rtc_data.devices[0]);
    rtc_data.devices[0].next = NULL;
    rtc_data.dev_list = &rtc_data.devices[0];
    rtc_data.default_dev = &rtc_data.devices[0];
    rtc_data.n_devices = 1;

    outb(0x70, 0x8B);
    uint8_t prev = inb(0x71);
    outb(0x70, 0x8B);
    outb(0x71, prev | 0x40);
    outb(0x70, 0x8A);
    outb(0x71, 0x26);

    rtc_data.initialized = 1;
    klog_info("RTC: Real-time clock subsystem initialized (%u devices)",
              rtc_data.n_devices);
    return 0;
}

int rtc_register_device(rtc_device_t *dev) {
    if (!rtc_data.initialized || !dev || rtc_data.n_devices >= RTC_MAX_DEVS) return -1;
    memcpy(&rtc_data.devices[rtc_data.n_devices], dev, sizeof(*dev));
    rtc_device_t *new_dev = &rtc_data.devices[rtc_data.n_devices];
    new_dev->next = rtc_data.dev_list;
    rtc_data.dev_list = new_dev;
    rtc_data.n_devices++;
    klog_info("RTC: registered device '%s'", new_dev->name);
    return 0;
}

void rtc_read_time(rtc_time_t *time) {
    if (!time) return;
    if (!rtc_data.initialized || !rtc_data.default_dev) {
        memset(time, 0, sizeof(*time));
        return;
    }
    rtc_time_t cmos_t;
    rtc_cmos_read(&cmos_t);
    memcpy(&rtc_data.default_dev->current_time, &cmos_t, sizeof(cmos_t));
    memcpy(time, &rtc_data.default_dev->current_time, sizeof(*time));
    rtc_sync_time_fields(time);
    rtc_data.default_dev->read_count++;
    rtc_data.default_dev->tick_count++;
    rtc_data.total_reads++;
}

int rtc_set_time(const rtc_time_t *time) {
    if (!rtc_data.initialized || !rtc_data.default_dev || !time) return -1;
    rtc_time_t t = *time;
    rtc_sync_time_fields(&t);
    if (!rtc_time_valid(&t)) return -1;
    memcpy(&rtc_data.default_dev->current_time, &t, sizeof(t));
    rtc_data.default_dev->set_count++;
    rtc_data.total_sets++;
    klog_info("RTC: time set to %04u-%02u-%02u %02u:%02u:%02u",
              t.year, t.month, t.day, t.hour, t.min, t.sec);
    return 0;
}

void rtc_read_alarm(rtc_alarm_t *alarm) {
    if (!rtc_data.initialized || !rtc_data.default_dev || !alarm) return;
    memcpy(alarm, &rtc_data.default_dev->alarm, sizeof(*alarm));
}

int rtc_set_alarm(const rtc_alarm_t *alarm) {
    if (!rtc_data.initialized || !rtc_data.default_dev || !alarm) return -1;
    memcpy(&rtc_data.default_dev->alarm, alarm, sizeof(*alarm));
    rtc_data.default_dev->alarm_irq_en = 1;
    klog_info("RTC: alarm set to %02u:%02u:%02u",
              alarm->hour, alarm->min, alarm->sec);
    return 0;
}

void rtc_tick(void) {
    if (!rtc_data.initialized) return;
    rtc_device_t *dev = rtc_data.dev_list;
    while (dev) {
        if (dev != rtc_data.default_dev) {
            rtc_advance_time(dev);
        }
        dev->tick_count++;
        dev = dev->next;
    }
    rtc_data.total_ticks++;
}

void rtc_print_stats(void) {
    for (int i = 0; i < 5; i++) rtc_tick();

    klog_info("=== RTC Subsystem Statistics ===");
    klog_info("Initialized: %s", rtc_data.initialized ? "yes" : "no");
    klog_info("Devices: %u", rtc_data.n_devices);
    klog_info("Total ticks: %llu", (unsigned long long)rtc_data.total_ticks);
    klog_info("Total reads: %llu", (unsigned long long)rtc_data.total_reads);
    klog_info("Total sets: %llu", (unsigned long long)rtc_data.total_sets);
    klog_info("Total alarms triggered: %llu", (unsigned long long)rtc_data.total_alarms);
    klog_info("");

    klog_info("RTC devices:");
    rtc_device_t *dev = rtc_data.dev_list;
    uint32_t idx = 0;
    while (dev && idx < RTC_MAX_DEVS) {
        klog_info("  [%u] %s", idx, dev->name);
        klog_info("    current time: %04u-%02u-%02u %02u:%02u:%02u",
                  dev->current_time.year, dev->current_time.month,
                  dev->current_time.day, dev->current_time.hour,
                  dev->current_time.min, dev->current_time.sec);
        klog_info("    alarm time: %02u:%02u:%02u (irq: %s)",
                  dev->alarm.hour, dev->alarm.min, dev->alarm.sec,
                  dev->alarm_irq_en ? "enabled" : "disabled");
        klog_info("    update irq: %s", dev->update_irq_en ? "enabled" : "disabled");
        klog_info("    ticks: %llu, reads: %llu, sets: %llu, alarms: %llu",
                  (unsigned long long)dev->tick_count,
                  (unsigned long long)dev->read_count,
                  (unsigned long long)dev->set_count,
                  (unsigned long long)dev->alarm_count);
        klog_info("");
        dev = dev->next;
        idx++;
    }
}
