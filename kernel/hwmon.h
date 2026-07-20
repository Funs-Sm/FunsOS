#ifndef HWMON_H
#define HWMON_H

#include "stdint.h"

#define HWMON_NAME_LEN 20
#define HWMON_MAX_DEVICES 8
#define HWMON_MAX_TEMP 8
#define HWMON_MAX_IN 8
#define HWMON_MAX_CURR 4
#define HWMON_MAX_FAN 4
#define HWMON_MAX_PWM 4
#define HWMON_MAX_POWER 4

#define HWMON_SENSOR_TEMP 0
#define HWMON_SENSOR_IN   1
#define HWMON_SENSOR_CURR 2
#define HWMON_SENSOR_FAN  3
#define HWMON_SENSOR_PWM  4
#define HWMON_SENSOR_POWER 5

struct hwmon_temp {
    char label[HWMON_NAME_LEN];
    int32_t value;
    int32_t min;
    int32_t max;
    int32_t crit;
    int32_t alarm;
};

struct hwmon_in {
    char label[HWMON_NAME_LEN];
    int32_t value;
    int32_t min;
    int32_t max;
    int32_t alarm;
};

struct hwmon_curr {
    char label[HWMON_NAME_LEN];
    int32_t value;
    int32_t min;
    int32_t max;
    int32_t alarm;
};

struct hwmon_fan {
    char label[HWMON_NAME_LEN];
    uint32_t speed;
    uint32_t min;
    uint32_t max;
    uint32_t alarm;
};

struct hwmon_pwm {
    char label[HWMON_NAME_LEN];
    uint32_t duty_cycle;
    uint32_t period;
    uint8_t enabled;
};

struct hwmon_power {
    char label[HWMON_NAME_LEN];
    uint32_t value;
    uint32_t cap;
    uint32_t crit;
};

struct hwmon_device {
    char name[HWMON_NAME_LEN];
    uint8_t registered;
    struct hwmon_temp temps[HWMON_MAX_TEMP];
    uint32_t n_temp;
    struct hwmon_in ins[HWMON_MAX_IN];
    uint32_t n_in;
    struct hwmon_curr currs[HWMON_MAX_CURR];
    uint32_t n_curr;
    struct hwmon_fan fans[HWMON_MAX_FAN];
    uint32_t n_fan;
    struct hwmon_pwm pwms[HWMON_MAX_PWM];
    uint32_t n_pwm;
    struct hwmon_power powers[HWMON_MAX_POWER];
    uint32_t n_power;
    struct hwmon_device *next;
};

int hwmon_init(void);
struct hwmon_device *hwmon_device_register(const char *name);
void hwmon_device_unregister(struct hwmon_device *dev);
int hwmon_sensor_read(struct hwmon_device *dev, uint32_t type, uint32_t index, int32_t *val);
int hwmon_sensor_write(struct hwmon_device *dev, uint32_t type, uint32_t index, int32_t val);
void hwmon_tick(void);
void hwmon_print_stats(void);

#endif
