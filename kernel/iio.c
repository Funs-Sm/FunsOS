#include "iio.h"
#include "klog.h"
#include "string.h"

struct iio_global {
    uint8_t initialized;
    struct iio_dev devices[IIO_MAX_DEVS];
    uint32_t n_devices;
    struct iio_dev *dev_list;
    uint64_t total_reads;
    uint64_t total_writes;
    uint32_t next_id;
};

static struct iio_global iio_data;

const char *iio_chan_type_name(uint32_t type) {
    switch (type) {
    case IIO_CHAN_VOLTAGE: return "voltage";
    case IIO_CHAN_CURRENT: return "current";
    case IIO_CHAN_TEMP: return "temp";
    case IIO_CHAN_ACCEL_X: return "accel_x";
    case IIO_CHAN_ACCEL_Y: return "accel_y";
    case IIO_CHAN_ACCEL_Z: return "accel_z";
    case IIO_CHAN_GYRO_X: return "gyro_x";
    case IIO_CHAN_GYRO_Y: return "gyro_y";
    case IIO_CHAN_GYRO_Z: return "gyro_z";
    case IIO_CHAN_LIGHT: return "light";
    case IIO_CHAN_PRESSURE: return "pressure";
    case IIO_CHAN_PROXIMITY: return "proximity";
    default: return "unknown";
    }
}

static int dummy_read_raw(struct iio_dev *dev, struct iio_chan_spec *chan, int *val) {
    (void)dev;
    static uint32_t tick = 0;
    tick++;
    switch (chan->type) {
    case IIO_CHAN_VOLTAGE:
        *val = 3300 + ((tick * 7 + chan->channel * 3) % 200) - 100;
        break;
    case IIO_CHAN_CURRENT:
        *val = 100 + ((tick * 5 + chan->channel) % 50);
        break;
    case IIO_CHAN_TEMP:
        *val = 25000 + ((tick * 3 + chan->channel * 11) % 5000) - 2500;
        break;
    case IIO_CHAN_ACCEL_X:
    case IIO_CHAN_ACCEL_Y:
    case IIO_CHAN_ACCEL_Z:
        *val = (chan->type == IIO_CHAN_ACCEL_Z) ? 1000 : 0;
        *val += ((tick * 13 + chan->channel * 17) % 100) - 50;
        break;
    case IIO_CHAN_GYRO_X:
    case IIO_CHAN_GYRO_Y:
    case IIO_CHAN_GYRO_Z:
        *val = ((tick * 19 + chan->channel * 23) % 200) - 100;
        break;
    case IIO_CHAN_LIGHT:
        *val = 500 + ((tick * 29) % 800);
        break;
    case IIO_CHAN_PRESSURE:
        *val = 101325 + ((tick * 31) % 2000) - 1000;
        break;
    case IIO_CHAN_PROXIMITY:
        *val = (tick % 10) < 3 ? 50 : 255;
        break;
    default:
        *val = 0;
        return -1;
    }
    return 0;
}

static int dummy_write_raw(struct iio_dev *dev, struct iio_chan_spec *chan, int val) {
    (void)dev;
    chan->raw_val = val;
    return 0;
}

static struct iio_info dummy_info = {
    dummy_read_raw,
    dummy_write_raw
};

static void iio_init_dummy_device(struct iio_dev *dev) {
    memset(dev, 0, sizeof(*dev));
    strncpy(dev->name, "dummy_sensor", IIO_NAME_LEN);
    dev->modes = IIO_MODE_IND | IIO_MODE_RING_BUFFER;
    dev->current_mode = IIO_MODE_IND;
    dev->info = &dummy_info;
    dev->n_channels = 12;

    dev->channels[0].type = IIO_CHAN_VOLTAGE; dev->channels[0].channel = 0;
    strncpy(dev->channels[0].name, "vcc", IIO_NAME_LEN);
    dev->channels[0].scale = 1; dev->channels[0].offset = 0;

    dev->channels[1].type = IIO_CHAN_VOLTAGE; dev->channels[1].channel = 1;
    strncpy(dev->channels[1].name, "vdd", IIO_NAME_LEN);
    dev->channels[1].scale = 1; dev->channels[1].offset = 0;

    dev->channels[2].type = IIO_CHAN_TEMP; dev->channels[2].channel = 0;
    strncpy(dev->channels[2].name, "die_temp", IIO_NAME_LEN);
    dev->channels[2].scale = 1000; dev->channels[2].offset = 0;

    dev->channels[3].type = IIO_CHAN_ACCEL_X; dev->channels[3].channel = 0;
    strncpy(dev->channels[3].name, "accel_x", IIO_NAME_LEN);
    dev->channels[3].scale = 1; dev->channels[3].offset = 0;

    dev->channels[4].type = IIO_CHAN_ACCEL_Y; dev->channels[4].channel = 1;
    strncpy(dev->channels[4].name, "accel_y", IIO_NAME_LEN);
    dev->channels[4].scale = 1; dev->channels[4].offset = 0;

    dev->channels[5].type = IIO_CHAN_ACCEL_Z; dev->channels[5].channel = 2;
    strncpy(dev->channels[5].name, "accel_z", IIO_NAME_LEN);
    dev->channels[5].scale = 1; dev->channels[5].offset = 0;

    dev->channels[6].type = IIO_CHAN_GYRO_X; dev->channels[6].channel = 0;
    strncpy(dev->channels[6].name, "gyro_x", IIO_NAME_LEN);
    dev->channels[6].scale = 1; dev->channels[6].offset = 0;

    dev->channels[7].type = IIO_CHAN_GYRO_Y; dev->channels[7].channel = 1;
    strncpy(dev->channels[7].name, "gyro_y", IIO_NAME_LEN);
    dev->channels[7].scale = 1; dev->channels[7].offset = 0;

    dev->channels[8].type = IIO_CHAN_GYRO_Z; dev->channels[8].channel = 2;
    strncpy(dev->channels[8].name, "gyro_z", IIO_NAME_LEN);
    dev->channels[8].scale = 1; dev->channels[8].offset = 0;

    dev->channels[9].type = IIO_CHAN_LIGHT; dev->channels[9].channel = 0;
    strncpy(dev->channels[9].name, "light", IIO_NAME_LEN);
    dev->channels[9].scale = 1; dev->channels[9].offset = 0;

    dev->channels[10].type = IIO_CHAN_PRESSURE; dev->channels[10].channel = 0;
    strncpy(dev->channels[10].name, "pressure", IIO_NAME_LEN);
    dev->channels[10].scale = 1000; dev->channels[10].offset = 0;

    dev->channels[11].type = IIO_CHAN_PROXIMITY; dev->channels[11].channel = 0;
    strncpy(dev->channels[11].name, "proximity", IIO_NAME_LEN);
    dev->channels[11].scale = 1; dev->channels[11].offset = 0;

    dev->registered = 1;
}

int iio_init(void) {
    if (iio_data.initialized) return 0;
    memset(&iio_data, 0, sizeof(iio_data));
    iio_data.next_id = 0;

    iio_init_dummy_device(&iio_data.devices[0]);
    iio_data.devices[0].id = iio_data.next_id++;
    iio_data.n_devices = 1;
    iio_data.dev_list = &iio_data.devices[0];
    iio_data.devices[0].next = NULL;

    iio_data.initialized = 1;
    klog_info("IIO: Industrial I/O subsystem initialized (%u device(s))", iio_data.n_devices);
    return 0;
}

struct iio_dev *iio_device_alloc(uint32_t sizeof_priv) {
    (void)sizeof_priv;
    if (!iio_data.initialized || iio_data.n_devices >= IIO_MAX_DEVS) return NULL;
    struct iio_dev *dev = &iio_data.devices[iio_data.n_devices];
    memset(dev, 0, sizeof(*dev));
    dev->id = iio_data.next_id++;
    return dev;
}

int iio_device_register(struct iio_dev *dev) {
    if (!dev || !iio_data.initialized) return -1;
    dev->registered = 1;
    dev->next = iio_data.dev_list;
    iio_data.dev_list = dev;
    if (dev >= iio_data.devices && dev < iio_data.devices + IIO_MAX_DEVS) {
        iio_data.n_devices++;
    }
    return 0;
}

void iio_device_unregister(struct iio_dev *dev) {
    if (!dev || !iio_data.initialized) return;
    struct iio_dev **prev = &iio_data.dev_list;
    while (*prev) {
        if (*prev == dev) {
            *prev = dev->next;
            dev->registered = 0;
            if (dev >= iio_data.devices && dev < iio_data.devices + IIO_MAX_DEVS) {
                iio_data.n_devices--;
            }
            return;
        }
        prev = &(*prev)->next;
    }
}

int iio_read_channel(struct iio_dev *dev, uint32_t channum, int32_t *val) {
    if (!dev || !val || channum >= dev->n_channels) return -1;
    struct iio_chan_spec *chan = &dev->channels[channum];
    int raw;
    int ret = -1;
    if (dev->info && dev->info->read_raw) {
        ret = dev->info->read_raw(dev, chan, &raw);
    }
    if (ret == 0) {
        chan->raw_val = raw;
        *val = raw;
        dev->total_reads++;
        iio_data.total_reads++;
    }
    return ret;
}

int iio_write_channel(struct iio_dev *dev, uint32_t channum, int32_t val) {
    if (!dev || channum >= dev->n_channels) return -1;
    struct iio_chan_spec *chan = &dev->channels[channum];
    int ret = -1;
    if (dev->info && dev->info->write_raw) {
        ret = dev->info->write_raw(dev, chan, (int)val);
    }
    if (ret == 0) {
        dev->total_writes++;
        iio_data.total_writes++;
    }
    return ret;
}

void iio_tick(void) {
    static uint32_t sim_tick = 0;
    sim_tick++;
    struct iio_dev *d = iio_data.dev_list;
    while (d) {
        for (uint32_t i = 0; i < d->n_channels; i++) {
            if (d->info && d->info->read_raw) {
                int val;
                d->info->read_raw(d, &d->channels[i], &val);
                d->channels[i].raw_val = val;
            }
        }
        d = d->next;
    }
}

void iio_print_stats(void) {
    klog_info("=== IIO Industrial I/O Statistics ===");
    klog_info("Initialized: %s", iio_data.initialized ? "yes" : "no");
    klog_info("Devices: %u", iio_data.n_devices);
    klog_info("Total reads: %llu, writes: %llu",
              (unsigned long long)iio_data.total_reads,
              (unsigned long long)iio_data.total_writes);
    klog_info("");

    struct iio_dev *d = iio_data.dev_list;
    uint32_t dev_idx = 0;
    while (d && dev_idx < IIO_MAX_DEVS) {
        klog_info("Device %u: '%s' (id=%u)", dev_idx, d->name, d->id);
        klog_info("  modes: 0x%X, current mode: %s", d->modes,
                  d->current_mode == IIO_MODE_IND ? "individual" :
                  d->current_mode == IIO_MODE_RING_BUFFER ? "ring_buffer" : "trigger");
        klog_info("  channels: %u", d->n_channels);
        klog_info("  reads: %llu, writes: %llu, overruns: %llu",
                  (unsigned long long)d->total_reads,
                  (unsigned long long)d->total_writes,
                  (unsigned long long)d->buffer_overruns);
        klog_info("");

        klog_info("  Channel list:");
        for (uint32_t i = 0; i < d->n_channels; i++) {
            struct iio_chan_spec *ch = &d->channels[i];
            int val = 0;
            if (d->info && d->info->read_raw) {
                d->info->read_raw(d, ch, &val);
            }
            klog_info("    [%u] %s (%s ch%u): raw=%d, scale=%u, offset=%d",
                      i, ch->name, iio_chan_type_name(ch->type), ch->channel,
                      val, ch->scale, ch->offset);
        }
        klog_info("");
        d = d->next;
        dev_idx++;
    }
}
