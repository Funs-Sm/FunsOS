#include "hwmon.h"
#include "klog.h"
#include "string.h"

struct hwmon_global {
    uint8_t initialized;
    struct hwmon_device devices[HWMON_MAX_DEVICES];
    uint32_t n_devices;
    struct hwmon_device *device_list;
    uint64_t total_reads;
    uint64_t total_writes;
    uint64_t alarms;
};

static struct hwmon_global hwmon_data;

static void hwmon_init_dummy_device(struct hwmon_device *dev) {
    memset(dev, 0, sizeof(*dev));
    strncpy(dev->name, "coretemp", HWMON_NAME_LEN);
    dev->registered = 1;

    dev->n_temp = 4;
    strncpy(dev->temps[0].label, "CPU Core", HWMON_NAME_LEN);
    dev->temps[0].value = 45000;
    dev->temps[0].min = 20000;
    dev->temps[0].max = 85000;
    dev->temps[0].crit = 100000;

    strncpy(dev->temps[1].label, "CPU Package", HWMON_NAME_LEN);
    dev->temps[1].value = 52000;
    dev->temps[1].min = 20000;
    dev->temps[1].max = 95000;
    dev->temps[1].crit = 110000;

    strncpy(dev->temps[2].label, "Motherboard", HWMON_NAME_LEN);
    dev->temps[2].value = 38000;
    dev->temps[2].min = 20000;
    dev->temps[2].max = 70000;
    dev->temps[2].crit = 80000;

    strncpy(dev->temps[3].label, "GPU", HWMON_NAME_LEN);
    dev->temps[3].value = 55000;
    dev->temps[3].min = 20000;
    dev->temps[3].max = 90000;
    dev->temps[3].crit = 105000;

    dev->n_in = 3;
    strncpy(dev->ins[0].label, "Vcore", HWMON_NAME_LEN);
    dev->ins[0].value = 1050;
    dev->ins[0].min = 800;
    dev->ins[0].max = 1300;

    strncpy(dev->ins[1].label, "+12V", HWMON_NAME_LEN);
    dev->ins[1].value = 12000;
    dev->ins[1].min = 11400;
    dev->ins[1].max = 12600;

    strncpy(dev->ins[2].label, "+5V", HWMON_NAME_LEN);
    dev->ins[2].value = 5000;
    dev->ins[2].min = 4750;
    dev->ins[2].max = 5250;

    dev->n_curr = 2;
    strncpy(dev->currs[0].label, "CPU Current", HWMON_NAME_LEN);
    dev->currs[0].value = 25000;
    dev->currs[0].min = 5000;
    dev->currs[0].max = 80000;

    strncpy(dev->currs[1].label, "GPU Current", HWMON_NAME_LEN);
    dev->currs[1].value = 15000;
    dev->currs[1].min = 1000;
    dev->currs[1].max = 60000;

    dev->n_fan = 2;
    strncpy(dev->fans[0].label, "CPU Fan", HWMON_NAME_LEN);
    dev->fans[0].speed = 1800;
    dev->fans[0].min = 500;
    dev->fans[0].max = 3000;

    strncpy(dev->fans[1].label, "Case Fan", HWMON_NAME_LEN);
    dev->fans[1].speed = 1200;
    dev->fans[1].min = 300;
    dev->fans[1].max = 2000;

    dev->n_pwm = 2;
    strncpy(dev->pwms[0].label, "CPU Fan PWM", HWMON_NAME_LEN);
    dev->pwms[0].duty_cycle = 128;
    dev->pwms[0].period = 255;
    dev->pwms[0].enabled = 1;

    strncpy(dev->pwms[1].label, "Case Fan PWM", HWMON_NAME_LEN);
    dev->pwms[1].duty_cycle = 100;
    dev->pwms[1].period = 255;
    dev->pwms[1].enabled = 1;

    dev->n_power = 2;
    strncpy(dev->powers[0].label, "CPU Power", HWMON_NAME_LEN);
    dev->powers[0].value = 65000;
    dev->powers[0].cap = 125000;
    dev->powers[0].crit = 150000;

    strncpy(dev->powers[1].label, "Package Power", HWMON_NAME_LEN);
    dev->powers[1].value = 95000;
    dev->powers[1].cap = 150000;
    dev->powers[1].crit = 200000;
}

int hwmon_init(void) {
    if (hwmon_data.initialized) return 0;
    memset(&hwmon_data, 0, sizeof(hwmon_data));

    hwmon_init_dummy_device(&hwmon_data.devices[0]);
    hwmon_data.n_devices = 1;
    hwmon_data.device_list = &hwmon_data.devices[0];
    hwmon_data.devices[0].next = NULL;

    hwmon_data.initialized = 1;
    klog_info("Hwmon: hardware monitoring initialized (%u device(s))", hwmon_data.n_devices);
    return 0;
}

struct hwmon_device *hwmon_device_register(const char *name) {
    if (!hwmon_data.initialized || !name || hwmon_data.n_devices >= HWMON_MAX_DEVICES) return NULL;
    struct hwmon_device *dev = &hwmon_data.devices[hwmon_data.n_devices];
    memset(dev, 0, sizeof(*dev));
    strncpy(dev->name, name, HWMON_NAME_LEN - 1);
    dev->registered = 1;
    dev->next = hwmon_data.device_list;
    hwmon_data.device_list = dev;
    hwmon_data.n_devices++;
    return dev;
}

void hwmon_device_unregister(struct hwmon_device *dev) {
    if (!dev || !hwmon_data.initialized) return;
    struct hwmon_device **prev = &hwmon_data.device_list;
    while (*prev) {
        if (*prev == dev) {
            *prev = dev->next;
            hwmon_data.n_devices--;
            memset(dev, 0, sizeof(*dev));
            return;
        }
        prev = &(*prev)->next;
    }
}

int hwmon_sensor_read(struct hwmon_device *dev, uint32_t type, uint32_t index, int32_t *val) {
    if (!dev || !val) return -1;
    hwmon_data.total_reads++;

    switch (type) {
    case HWMON_SENSOR_TEMP:
        if (index >= dev->n_temp) return -1;
        *val = dev->temps[index].value;
        return 0;
    case HWMON_SENSOR_IN:
        if (index >= dev->n_in) return -1;
        *val = dev->ins[index].value;
        return 0;
    case HWMON_SENSOR_CURR:
        if (index >= dev->n_curr) return -1;
        *val = dev->currs[index].value;
        return 0;
    case HWMON_SENSOR_FAN:
        if (index >= dev->n_fan) return -1;
        *val = (int32_t)dev->fans[index].speed;
        return 0;
    case HWMON_SENSOR_PWM:
        if (index >= dev->n_pwm) return -1;
        *val = (int32_t)dev->pwms[index].duty_cycle;
        return 0;
    case HWMON_SENSOR_POWER:
        if (index >= dev->n_power) return -1;
        *val = (int32_t)dev->powers[index].value;
        return 0;
    }
    return -1;
}

int hwmon_sensor_write(struct hwmon_device *dev, uint32_t type, uint32_t index, int32_t val) {
    if (!dev) return -1;
    hwmon_data.total_writes++;

    switch (type) {
    case HWMON_SENSOR_PWM:
        if (index >= dev->n_pwm) return -1;
        if (val < 0) val = 0;
        if ((uint32_t)val > dev->pwms[index].period) val = (int32_t)dev->pwms[index].period;
        dev->pwms[index].duty_cycle = (uint32_t)val;
        uint32_t min_fan = dev->fans[index < dev->n_fan ? index : 0].min;
        uint32_t max_fan = dev->fans[index < dev->n_fan ? index : 0].max;
        if (index < dev->n_fan) {
            uint32_t pct = dev->pwms[index].duty_cycle * 100 / dev->pwms[index].period;
            dev->fans[index].speed = min_fan + (max_fan - min_fan) * pct / 100;
        }
        return 0;
    case HWMON_SENSOR_TEMP:
    case HWMON_SENSOR_IN:
    case HWMON_SENSOR_CURR:
    case HWMON_SENSOR_FAN:
    case HWMON_SENSOR_POWER:
        return -1;
    }
    return -1;
}

void hwmon_tick(void) {
    static uint32_t sim_tick = 0;
    sim_tick++;

    struct hwmon_device *d = hwmon_data.device_list;
    while (d) {
        for (uint32_t i = 0; i < d->n_temp; i++) {
            int32_t delta = (int32_t)((sim_tick * 3 + i * 7) % 5000) - 2500;
            d->temps[i].value = 45000 + delta;
            if (d->temps[i].value > d->temps[i].max) {
                d->temps[i].alarm = 1;
                hwmon_data.alarms++;
            } else {
                d->temps[i].alarm = 0;
            }
        }
        for (uint32_t i = 0; i < d->n_fan; i++) {
            if (d->pwms[i].enabled && d->n_pwm > i) {
                uint32_t pct = d->pwms[i].duty_cycle * 100 / d->pwms[i].period;
                d->fans[i].speed = d->fans[i].min + (d->fans[i].max - d->fans[i].min) * pct / 100;
                d->fans[i].speed += (sim_tick + i * 11) % 50 - 25;
            }
        }
        d = d->next;
    }
}

void hwmon_print_stats(void) {
    klog_info("=== Hwmon Hardware Monitor Statistics ===");
    klog_info("Initialized: %s", hwmon_data.initialized ? "yes" : "no");
    klog_info("Devices: %u", hwmon_data.n_devices);
    klog_info("Total reads: %llu, writes: %llu",
              (unsigned long long)hwmon_data.total_reads,
              (unsigned long long)hwmon_data.total_writes);
    klog_info("Alarms triggered: %llu", (unsigned long long)hwmon_data.alarms);
    klog_info("");

    struct hwmon_device *d = hwmon_data.device_list;
    uint32_t dev_idx = 0;
    while (d && dev_idx < HWMON_MAX_DEVICES) {
        klog_info("Device %u: %s", dev_idx, d->name);

        if (d->n_temp > 0) {
            klog_info("  Temperatures:");
            for (uint32_t i = 0; i < d->n_temp; i++) {
                klog_info("    %s: %d.%03u C %s",
                          d->temps[i].label,
                          d->temps[i].value / 1000,
                          (uint32_t)(d->temps[i].value % 1000),
                          d->temps[i].alarm ? " [ALARM]" : "");
            }
        }

        if (d->n_in > 0) {
            klog_info("  Voltages:");
            for (uint32_t i = 0; i < d->n_in; i++) {
                klog_info("    %s: %d.%03u V",
                          d->ins[i].label,
                          d->ins[i].value / 1000,
                          (uint32_t)(d->ins[i].value % 1000));
            }
        }

        if (d->n_curr > 0) {
            klog_info("  Currents:");
            for (uint32_t i = 0; i < d->n_curr; i++) {
                klog_info("    %s: %d.%03u A",
                          d->currs[i].label,
                          d->currs[i].value / 1000,
                          (uint32_t)(d->currs[i].value % 1000));
            }
        }

        if (d->n_fan > 0) {
            klog_info("  Fans:");
            for (uint32_t i = 0; i < d->n_fan; i++) {
                klog_info("    %s: %u RPM", d->fans[i].label, d->fans[i].speed);
            }
        }

        if (d->n_pwm > 0) {
            klog_info("  PWM:");
            for (uint32_t i = 0; i < d->n_pwm; i++) {
                klog_info("    %s: %u/%u %s",
                          d->pwms[i].label,
                          d->pwms[i].duty_cycle,
                          d->pwms[i].period,
                          d->pwms[i].enabled ? "enabled" : "disabled");
            }
        }

        if (d->n_power > 0) {
            klog_info("  Power:");
            for (uint32_t i = 0; i < d->n_power; i++) {
                klog_info("    %s: %u.%03u W",
                          d->powers[i].label,
                          d->powers[i].value / 1000,
                          d->powers[i].value % 1000);
            }
        }
        klog_info("");
        d = d->next;
        dev_idx++;
    }
}
