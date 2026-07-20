#ifndef IIO_H
#define IIO_H

#include "stdint.h"

#define IIO_NAME_LEN 24
#define IIO_MAX_DEVS 8
#define IIO_MAX_CHANNELS 32

#define IIO_CHAN_VOLTAGE 0
#define IIO_CHAN_CURRENT 1
#define IIO_CHAN_TEMP 2
#define IIO_CHAN_ACCEL_X 3
#define IIO_CHAN_ACCEL_Y 4
#define IIO_CHAN_ACCEL_Z 5
#define IIO_CHAN_GYRO_X 6
#define IIO_CHAN_GYRO_Y 7
#define IIO_CHAN_GYRO_Z 8
#define IIO_CHAN_LIGHT 9
#define IIO_CHAN_PRESSURE 10
#define IIO_CHAN_PROXIMITY 11

#define IIO_MODE_IND 0
#define IIO_MODE_RING_BUFFER 1
#define IIO_MODE_TRIGGER 2

struct iio_chan_spec {
    uint32_t type;
    uint32_t channel;
    char name[IIO_NAME_LEN];
    int32_t raw_val;
    int32_t offset;
    uint32_t scale;
    uint32_t scale_type;
    uint8_t indexed;
    uint8_t output;
};

struct iio_info;
struct iio_dev;

typedef int (*iio_read_raw_t)(struct iio_dev *dev, struct iio_chan_spec *chan, int *val);
typedef int (*iio_write_raw_t)(struct iio_dev *dev, struct iio_chan_spec *chan, int val);

struct iio_info {
    iio_read_raw_t read_raw;
    iio_write_raw_t write_raw;
};

struct iio_dev {
    char name[IIO_NAME_LEN];
    uint32_t id;
    uint8_t registered;
    uint32_t modes;
    uint32_t current_mode;
    struct iio_chan_spec channels[IIO_MAX_CHANNELS];
    uint32_t n_channels;
    struct iio_info *info;
    void *priv;
    uint64_t total_reads;
    uint64_t total_writes;
    uint64_t buffer_overruns;
    struct iio_dev *next;
};

int iio_init(void);
struct iio_dev *iio_device_alloc(uint32_t sizeof_priv);
int iio_device_register(struct iio_dev *dev);
void iio_device_unregister(struct iio_dev *dev);
int iio_read_channel(struct iio_dev *dev, uint32_t channum, int32_t *val);
int iio_write_channel(struct iio_dev *dev, uint32_t channum, int32_t val);
const char *iio_chan_type_name(uint32_t type);
void iio_tick(void);
void iio_print_stats(void);

#endif
