#ifndef SENSORS_H
#define SENSORS_H

#include "stdint.h"

/* Sensor types */
#define SENSOR_TYPE_TEMP        0x01
#define SENSOR_TYPE_FAN         0x02
#define SENSOR_TYPE_VOLTAGE     0x03
#define SENSOR_TYPE_CURRENT     0x04
#define SENSOR_TYPE_POWER       0x05
#define SENSOR_TYPE_HUMIDITY    0x06
#define SENSOR_TYPE_AMBIENT     0x07

/* Temperature sensor subtypes */
#define SENSOR_TEMP_CPU         0x10
#define SENSOR_TEMP_GPU         0x11
#define SENSOR_TEMP_AMBIENT     0x12
#define SENSOR_TEMP_MB          0x13
#define SENSOR_TEMP_DISK        0x14

/* Fan subtypes */
#define SENSOR_FAN_CPU          0x20
#define SENSOR_FAN_CHASSIS      0x21
#define SENSOR_FAN_GPU          0x22
#define SENSOR_FAN_PSU          0x23

/* Voltage subtypes */
#define SENSOR_VOLT_CORE        0x30
#define SENSOR_VOLT_DRAM        0x31
#define SENSOR_VOLT_IO          0x32
#define SENSOR_VOLT_3V3         0x33
#define SENSOR_VOLT_5V          0x34
#define SENSOR_VOLT_12V         0x35
#define SENSOR_VOLT_BATTERY     0x36

/* Sensor limits */
#define SENSOR_MAX_DEVICES      32
#define SENSOR_NAME_LEN         64
#define SENSOR_UNIT_LEN         16

/* Sensor alarm flags */
#define SENSOR_ALARM_NONE       0x00
#define SENSOR_ALARM_LOW        0x01
#define SENSOR_ALARM_HIGH       0x02
#define SENSOR_ALARM_CRIT       0x04
#define SENSOR_ALARM_FAULT      0x08

/* Sensor reading */
typedef struct {
    int32_t value;              /* Raw value (scaled by factor) */
    int32_t min_value;          /* Minimum observed */
    int32_t max_value;          /* Maximum observed */
    int32_t low_limit;          /* Low warning threshold */
    int32_t high_limit;         /* High warning threshold */
    int32_t crit_limit;         /* Critical threshold */
    uint32_t timestamp;         /* Last reading timestamp */
    uint8_t alarm_flags;        /* Active alarm flags */
    uint8_t valid;              /* Reading is valid */
} sensor_reading_t;

/* Sensor device */
typedef struct {
    uint32_t id;
    char name[SENSOR_NAME_LEN];
    char unit[SENSOR_UNIT_LEN];
    uint32_t type;
    uint32_t subtype;
    int32_t scale_factor;       /* Multiply reading by this to get actual value */
    int32_t offset;             /* Add this to get actual value */
    sensor_reading_t reading;
    /* Callbacks for hardware-specific access */
    int32_t (*read_raw)(uint32_t id, int32_t *raw);
    int32_t (*write_control)(uint32_t id, int32_t value);
    void    *driver_data;
    uint8_t  initialized;
    uint8_t  enabled;
} sensor_dev_t;

/* Fan control modes */
#define FAN_MODE_OFF        0
#define FAN_MODE_MANUAL     1
#define FAN_MODE_AUTO       2
#define FAN_MODE_SMART      3

/* Fan device extension */
typedef struct {
    sensor_dev_t sensor;
    uint32_t min_rpm;
    uint32_t max_rpm;
    uint32_t target_rpm;
    uint32_t current_rpm;
    uint8_t  control_mode;
    uint8_t  pwm_duty;       /* 0-255 for PWM control */
    uint8_t  pulses_per_rev; /* Tachometer pulses per revolution */
} fan_dev_t;

/* Temperature device extension */
typedef struct {
    sensor_dev_t sensor;
    int32_t  critical_temp;   /* Emergency shutdown temperature */
    int32_t  throttle_temp;   /* CPU/GPU throttling temperature */
    uint8_t  source;          /* Temperature source identifier */
} temp_dev_t;

/* Voltage device extension */
typedef struct {
    sensor_dev_t sensor;
    int32_t  nominal_voltage; /* Nominal voltage in mV */
    int32_t  tolerance_pct;   /* Tolerance percentage */
    uint8_t  monitored;       /* Is this voltage rail monitored */
} voltage_dev_t;

/* Sensor API */
void sensors_init(void);
int32_t sensors_register(sensor_dev_t *dev);
int32_t sensors_unregister(uint32_t id);
sensor_dev_t *sensors_get_device(uint32_t id);
sensor_dev_t *sensors_find_by_name(const char *name);
sensor_dev_t *sensors_find_by_type(uint32_t type, uint32_t index);
int32_t sensors_get_count(void);

/* Reading operations */
int32_t sensors_read(uint32_t id);
int32_t sensors_get_value(uint32_t id);
int32_t sensors_get_min(uint32_t id);
int32_t sensors_get_max(uint32_t id);
int32_t sensors_get_formatted_value(uint32_t id);
uint8_t sensors_get_alarms(uint32_t id);

/* Polling */
void sensors_poll_all(void);

/* Temperature specific */
int32_t sensors_temp_register(const char *name, uint32_t subtype, int32_t (*read_fn)(uint32_t, int32_t *));
int32_t sensors_temp_get_cpu(void);
int32_t sensors_temp_get_gpu(void);
int32_t sensors_temp_get_ambient(void);
int32_t sensors_temp_set_limits(uint32_t id, int32_t low, int32_t high, int32_t crit);

/* Fan specific */
int32_t sensors_fan_register(const char *name, uint32_t subtype, uint32_t max_rpm,
                              int32_t (*read_fn)(uint32_t, int32_t *));
int32_t sensors_fan_get_rpm(uint32_t id);
int32_t sensors_fan_set_speed(uint32_t id, uint32_t target_rpm);
int32_t sensors_fan_set_mode(uint32_t id, uint8_t mode);
int32_t sensors_fan_set_pwm(uint32_t id, uint8_t duty);
uint32_t sensors_fan_get_rpm_by_type(uint32_t subtype);

/* Voltage specific */
int32_t sensors_volt_register(const char *name, uint32_t subtype, int32_t nominal_mv,
                               int32_t (*read_fn)(uint32_t, int32_t *));
int32_t sensors_volt_get_value(uint32_t id);
int32_t sensors_volt_check_tolerance(uint32_t id);

/* Sysfs interface */
void sensors_sysfs_init(void);

#endif