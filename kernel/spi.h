#ifndef SPI_H
#define SPI_H

#include "stdint.h"

#define SPI_NAME_LEN 32
#define SPI_MAX_CONTROLLERS 4
#define SPI_MAX_DEVICES 16

#define SPI_CPOL      0x01
#define SPI_CPHA      0x02
#define SPI_MODE_0    0
#define SPI_MODE_1    SPI_CPHA
#define SPI_MODE_2    SPI_CPOL
#define SPI_MODE_3    (SPI_CPOL | SPI_CPHA)
#define SPI_CS_HIGH   0x04
#define SPI_LSB_FIRST 0x08
#define SPI_3WIRE     0x10
#define SPI_LOOP      0x20
#define SPI_NO_CS     0x40
#define SPI_READY     0x80

#define SPI_TRANSFER_READ  0x01
#define SPI_TRANSFER_WRITE 0x02

struct spi_transfer {
    const void *tx_buf;
    void *rx_buf;
    uint32_t len;
    uint32_t speed_hz;
    uint16_t delay_usecs;
    uint8_t bits_per_word;
    uint8_t cs_change;
};

struct spi_message;
struct spi_device;
struct spi_controller;

typedef int (*spi_transfer_one_t)(struct spi_controller *ctlr,
                                  struct spi_device *spi,
                                  struct spi_transfer *xfer);
typedef int (*spi_setup_t)(struct spi_device *spi);
typedef int (*spi_prepare_message_t)(struct spi_controller *ctlr,
                                     struct spi_message *msg);
typedef int (*spi_unprepare_message_t)(struct spi_controller *ctlr,
                                       struct spi_message *msg);

struct spi_device {
    char name[SPI_NAME_LEN];
    uint32_t chip_select;
    uint32_t max_speed_hz;
    uint32_t mode;
    uint8_t bits_per_word;
    struct spi_controller *controller;
    uint8_t registered;
    uint64_t xfer_count;
    uint64_t bytes_transferred;
};

typedef struct spi_device spi_device_t;

struct spi_message {
    struct spi_transfer *transfers;
    uint32_t n_transfers;
    spi_device_t *spi;
    uint32_t actual_length;
    uint8_t status;
    void (*complete)(void *context);
    void *context;
};

typedef struct spi_message spi_message_t;

struct spi_controller {
    char name[SPI_NAME_LEN];
    uint32_t bus_num;
    uint32_t num_chipselect;
    uint32_t min_speed_hz;
    uint32_t max_speed_hz;
    spi_transfer_one_t transfer_one;
    spi_setup_t setup;
    spi_prepare_message_t prepare_message;
    spi_unprepare_message_t unprepare_message;
    void *data;
    spi_device_t devices[SPI_MAX_DEVICES];
    uint32_t n_devices;
    uint64_t xfer_count;
    uint64_t bytes_total;
    uint64_t error_count;
    struct spi_controller *next;
};

typedef struct spi_controller spi_controller_t;

int spi_init(void);
int spi_register_controller(spi_controller_t *ctlr);
int spi_add_device(spi_controller_t *ctlr, spi_device_t *spi);
int spi_setup(spi_device_t *spi);
int spi_sync(spi_device_t *spi, spi_message_t *msg);
void spi_print_stats(void);

#endif
