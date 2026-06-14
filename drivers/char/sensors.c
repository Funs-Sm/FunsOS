#include "sensors.h"
#include "kheap.h"
#include "string.h"
#include "stdio.h"

static sensor_dev_t sensor_devices[SENSOR_MAX_DEVICES];
static uint32_t sensor_count = 0;
static uint32_t poll_tick = 0;

void sensors_init(void) {
    sensor_count = 0;
    memset(sensor_devices, 0, sizeof(sensor_devices));
    poll_tick = 0;
}

int32_t sensors_register(sensor_dev_t *dev) {
    if (!dev || sensor_count >= SENSOR_MAX_DEVICES) return -1;

    memcpy(&sensor_devices[sensor_count], dev, sizeof(sensor_dev_t));
    sensor_devices[sensor_count].id = sensor_count;
    sensor_devices[sensor_count].initialized = 1;
    sensor_devices[sensor_count].enabled = 1;

    /* Do an initial reading */
    sensors_read(sensor_count);

    sensor_count++;
    return (int32_t)(sensor_count - 1);
}

int32_t sensors_unregister(uint32_t id) {
    if (id >= sensor_count) return -1;
    sensor_devices[id].initialized = 0;
    sensor_devices[id].enabled = 0;
    return 0;
}

sensor_dev_t *sensors_get_device(uint32_t id) {
    if (id >= sensor_count) return 0;
    return &sensor_devices[id];
}

sensor_dev_t *sensors_find_by_name(const char *name) {
    for (uint32_t i = 0; i < sensor_count; i++) {
        if (sensor_devices[i].initialized &&
            strcmp(sensor_devices[i].name, name) == 0) {
            return &sensor_devices[i];
        }
    }
    return 0;
}

sensor_dev_t *sensors_find_by_type(uint32_t type, uint32_t index) {
    uint32_t found = 0;
    for (uint32_t i = 0; i < sensor_count; i++) {
        if (sensor_devices[i].initialized && sensor_devices[i].type == type) {
            if (found == index) return &sensor_devices[i];
            found++;
        }
    }
    return 0;
}

int32_t sensors_get_count(void) {
    return (int32_t)sensor_count;
}

int32_t sensors_read(uint32_t id) {
    if (id >= sensor_count) return -1;

    sensor_dev_t *dev = &sensor_devices[id];
    if (!dev->initialized || !dev->enabled) return -1;
    if (!dev->read_raw) return -1;

    int32_t raw = 0;
    if (dev->read_raw(id, &raw) != 0) {
        dev->reading.valid = 0;
        dev->reading.alarm_flags |= SENSOR_ALARM_FAULT;
        return -1;
    }

    /* Apply scale and offset */
    int32_t value = raw * dev->scale_factor + dev->offset;

    dev->reading.value = value;
    dev->reading.timestamp = poll_tick;
    dev->reading.valid = 1;
    dev->reading.alarm_flags = SENSOR_ALARM_NONE;

    /* Update min/max */
    if (value < dev->reading.min_value || !dev->reading.min_value) {
        dev->reading.min_value = value;
    }
    if (value > dev->reading.max_value) {
        dev->reading.max_value = value;
    }

    /* Check alarm thresholds */
    if (dev->reading.low_limit != 0 && value < dev->reading.low_limit) {
        dev->reading.alarm_flags |= SENSOR_ALARM_LOW;
    }
    if (dev->reading.high_limit != 0 && value > dev->reading.high_limit) {
        dev->reading.alarm_flags |= SENSOR_ALARM_HIGH;
    }
    if (dev->reading.crit_limit != 0 && value > dev->reading.crit_limit) {
        dev->reading.alarm_flags |= SENSOR_ALARM_CRIT;
    }

    return 0;
}

int32_t sensors_get_value(uint32_t id) {
    if (id >= sensor_count) return 0;
    return sensor_devices[id].reading.value;
}

int32_t sensors_get_min(uint32_t id) {
    if (id >= sensor_count) return 0;
    return sensor_devices[id].reading.min_value;
}

int32_t sensors_get_max(uint32_t id) {
    if (id >= sensor_count) return 0;
    return sensor_devices[id].reading.max_value;
}

int32_t sensors_get_formatted_value(uint32_t id) {
    if (id >= sensor_count) return 0;

    sensor_dev_t *dev = &sensor_devices[id];
    int32_t val = dev->reading.value;
    /* For temperatures in millidegrees, return degrees */
    if (dev->type == SENSOR_TYPE_TEMP && dev->scale_factor >= 1000) {
        val = val / 1000;
    }
    return val;
}

uint8_t sensors_get_alarms(uint32_t id) {
    if (id >= sensor_count) return 0;
    return sensor_devices[id].reading.alarm_flags;
}

void sensors_poll_all(void) {
    poll_tick++;
    for (uint32_t i = 0; i < sensor_count; i++) {
        if (sensor_devices[i].initialized && sensor_devices[i].enabled) {
            sensors_read(i);
        }
    }
}

/* ---- Temperature sensor functions ---- */

int32_t sensors_temp_register(const char *name, uint32_t subtype,
                               int32_t (*read_fn)(uint32_t, int32_t *)) {
    sensor_dev_t dev;
    memset(&dev, 0, sizeof(sensor_dev_t));

    if (name) {
        uint32_t i;
        for (i = 0; i < SENSOR_NAME_LEN - 1 && name[i]; i++) dev.name[i] = name[i];
        dev.name[i] = '\0';
    }
    dev.type = SENSOR_TYPE_TEMP;
    dev.subtype = subtype;
    dev.scale_factor = 1;
    dev.offset = 0;
    dev.read_raw = read_fn;

    /* Set default unit */
    strcpy(dev.unit, "mC");

    /* Set default temperature limits */
    dev.reading.low_limit = 0;
    dev.reading.high_limit = 80000;   /* 80 C */
    dev.reading.crit_limit = 95000;   /* 95 C */

    return sensors_register(&dev);
}

int32_t sensors_temp_get_cpu(void) {
    sensor_dev_t *dev = sensors_find_by_type(SENSOR_TYPE_TEMP, 0);
    if (!dev) dev = sensors_find_by_type(SENSOR_TYPE_TEMP, 0);
    if (!dev) return 0;
    return sensors_get_value(dev->id);
}

int32_t sensors_temp_get_gpu(void) {
    for (uint32_t i = 0; i < sensor_count; i++) {
        if (sensor_devices[i].initialized &&
            sensor_devices[i].subtype == SENSOR_TEMP_GPU) {
            return sensors_get_value(i);
        }
    }
    return 0;
}

int32_t sensors_temp_get_ambient(void) {
    for (uint32_t i = 0; i < sensor_count; i++) {
        if (sensor_devices[i].initialized &&
            sensor_devices[i].subtype == SENSOR_TEMP_AMBIENT) {
            return sensors_get_value(i);
        }
    }
    return 0;
}

int32_t sensors_temp_set_limits(uint32_t id, int32_t low, int32_t high, int32_t crit) {
    if (id >= sensor_count) return -1;
    if (sensor_devices[id].type != SENSOR_TYPE_TEMP) return -1;

    sensor_devices[id].reading.low_limit = low;
    sensor_devices[id].reading.high_limit = high;
    sensor_devices[id].reading.crit_limit = crit;
    return 0;
}

/* ---- Fan sensor functions ---- */

int32_t sensors_fan_register(const char *name, uint32_t subtype,
                              uint32_t max_rpm, int32_t (*read_fn)(uint32_t, int32_t *)) {
    fan_dev_t *fan = (fan_dev_t *)kmalloc(sizeof(fan_dev_t));
    if (!fan) return -1;
    memset(fan, 0, sizeof(fan_dev_t));

    sensor_dev_t *dev = &fan->sensor;
    if (name) {
        uint32_t i;
        for (i = 0; i < SENSOR_NAME_LEN - 1 && name[i]; i++) dev->name[i] = name[i];
        dev->name[i] = '\0';
    }
    dev->type = SENSOR_TYPE_FAN;
    dev->subtype = subtype;
    dev->scale_factor = 1;
    dev->offset = 0;
    dev->read_raw = read_fn;
    dev->driver_data = fan;
    strcpy(dev->unit, "RPM");

    fan->max_rpm = max_rpm;
    fan->min_rpm = 200;
    fan->control_mode = FAN_MODE_AUTO;
    fan->pwm_duty = 128;
    fan->pulses_per_rev = 2;

    int32_t id = sensors_register(dev);
    if (id < 0) {
        kfree(fan);
        return -1;
    }

    return id;
}

int32_t sensors_fan_get_rpm(uint32_t id) {
    if (id >= sensor_count) return 0;
    if (sensor_devices[id].type != SENSOR_TYPE_FAN) return 0;
    return sensors_get_value(id);
}

int32_t sensors_fan_set_speed(uint32_t id, uint32_t target_rpm) {
    if (id >= sensor_count) return -1;
    if (sensor_devices[id].type != SENSOR_TYPE_FAN) return -1;

    fan_dev_t *fan = (fan_dev_t *)sensor_devices[id].driver_data;
    if (!fan) return -1;

    if (target_rpm > fan->max_rpm) target_rpm = fan->max_rpm;
    if (target_rpm < fan->min_rpm && target_rpm > 0) target_rpm = fan->min_rpm;

    fan->target_rpm = target_rpm;

    /* Convert to PWM duty if needed */
    if (target_rpm == 0) {
        fan->pwm_duty = 0;
    } else {
        uint32_t duty = (target_rpm * 255) / fan->max_rpm;
        if (duty < 30) duty = 30;
        if (duty > 255) duty = 255;
        fan->pwm_duty = (uint8_t)duty;
    }

    /* Call driver-specific write if available */
    if (sensor_devices[id].write_control) {
        sensor_devices[id].write_control(id, fan->pwm_duty);
    }

    return 0;
}

int32_t sensors_fan_set_mode(uint32_t id, uint8_t mode) {
    if (id >= sensor_count) return -1;
    if (sensor_devices[id].type != SENSOR_TYPE_FAN) return -1;

    fan_dev_t *fan = (fan_dev_t *)sensor_devices[id].driver_data;
    if (!fan) return -1;

    fan->control_mode = mode;
    return 0;
}

int32_t sensors_fan_set_pwm(uint32_t id, uint8_t duty) {
    if (id >= sensor_count) return -1;
    if (sensor_devices[id].type != SENSOR_TYPE_FAN) return -1;

    fan_dev_t *fan = (fan_dev_t *)sensor_devices[id].driver_data;
    if (!fan) return -1;

    fan->pwm_duty = duty;
    fan->control_mode = FAN_MODE_MANUAL;

    if (sensor_devices[id].write_control) {
        sensor_devices[id].write_control(id, duty);
    }

    return 0;
}

uint32_t sensors_fan_get_rpm_by_type(uint32_t subtype) {
    for (uint32_t i = 0; i < sensor_count; i++) {
        if (sensor_devices[i].initialized &&
            sensor_devices[i].type == SENSOR_TYPE_FAN &&
            sensor_devices[i].subtype == subtype) {
            return (uint32_t)sensors_get_value(i);
        }
    }
    return 0;
}

/* ---- Voltage sensor functions ---- */

int32_t sensors_volt_register(const char *name, uint32_t subtype,
                               int32_t nominal_mv, int32_t (*read_fn)(uint32_t, int32_t *)) {
    voltage_dev_t *vdev = (voltage_dev_t *)kmalloc(sizeof(voltage_dev_t));
    if (!vdev) return -1;
    memset(vdev, 0, sizeof(voltage_dev_t));

    sensor_dev_t *dev = &vdev->sensor;
    if (name) {
        uint32_t i;
        for (i = 0; i < SENSOR_NAME_LEN - 1 && name[i]; i++) dev->name[i] = name[i];
        dev->name[i] = '\0';
    }
    dev->type = SENSOR_TYPE_VOLTAGE;
    dev->subtype = subtype;
    dev->scale_factor = 1;
    dev->offset = 0;
    dev->read_raw = read_fn;
    dev->driver_data = vdev;
    strcpy(dev->unit, "mV");

    vdev->nominal_voltage = nominal_mv;
    vdev->tolerance_pct = 5;
    vdev->monitored = 1;

    /* Set tolerance-based limits */
    int32_t tol = nominal_mv * vdev->tolerance_pct / 100;
    dev->reading.low_limit = nominal_mv - tol;
    dev->reading.high_limit = nominal_mv + tol;

    int32_t id = sensors_register(dev);
    if (id < 0) {
        kfree(vdev);
        return -1;
    }

    return id;
}

int32_t sensors_volt_get_value(uint32_t id) {
    if (id >= sensor_count) return 0;
    if (sensor_devices[id].type != SENSOR_TYPE_VOLTAGE) return 0;
    return sensors_get_value(id);
}

int32_t sensors_volt_check_tolerance(uint32_t id) {
    if (id >= sensor_count) return -1;
    if (sensor_devices[id].type != SENSOR_TYPE_VOLTAGE) return -1;

    voltage_dev_t *vdev = (voltage_dev_t *)sensor_devices[id].driver_data;
    if (!vdev) return -1;

    int32_t val = sensors_get_value(id);
    int32_t tol = vdev->nominal_voltage * vdev->tolerance_pct / 100;
    if (val < vdev->nominal_voltage - tol) return -1;
    if (val > vdev->nominal_voltage + tol) return -1;
    return 0;
}

/* ---- Sysfs interface ---- */

void sensors_sysfs_init(void) {
    /* Initialize sysfs entries for sensor devices.
     * This would create entries under /sys/class/hwmon/ for each sensor.
     * For example:
     *   /sys/class/hwmon/hwmon0/temp1_input
     *   /sys/class/hwmon/hwmon0/fan1_input
     *   /sys/class/hwmon/hwmon0/in0_input
     */
    for (uint32_t i = 0; i < sensor_count; i++) {
        if (!sensor_devices[i].initialized) continue;

        /* Sysfs registration would go here */
        /* Each sensor gets its own attribute node */
    }
}