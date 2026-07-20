#ifndef I2C_H
#define I2C_H

#include "stdint.h"

#define I2C_NAME_LEN 32
#define I2C_MAX_ADAPTERS 4
#define I2C_MAX_CLIENTS 16
#define I2C_MAX_MSGS 8

#define I2C_M_RD 0x0001

struct i2c_msg {
    uint16_t addr;
    uint16_t flags;
    uint16_t len;
    uint8_t *buf;
};

union i2c_smbus_data {
    uint8_t byte;
    uint16_t word;
    uint8_t block[34];
};

struct i2c_adapter;
struct i2c_client;

typedef int (*i2c_master_xfer_t)(struct i2c_adapter *adap, struct i2c_msg *msgs, int num);
typedef int (*i2c_smbus_xfer_t)(struct i2c_adapter *adap, uint16_t addr,
                                uint8_t read_write, uint8_t command,
                                uint8_t protocol, uint8_t size,
                                union i2c_smbus_data *data);

struct i2c_algorithm {
    i2c_master_xfer_t master_xfer;
    i2c_smbus_xfer_t smbus_xfer;
};

struct i2c_client {
    char name[I2C_NAME_LEN];
    uint16_t addr;
    struct i2c_adapter *adapter;
    uint8_t registered;
    uint64_t xfer_count;
};

typedef struct i2c_client i2c_client_t;

struct i2c_adapter {
    char name[I2C_NAME_LEN];
    uint32_t nr;
    const struct i2c_algorithm *algo;
    void *data;
    i2c_client_t clients[I2C_MAX_CLIENTS];
    uint32_t n_clients;
    uint64_t xfer_count;
    uint64_t smbus_count;
    uint64_t error_count;
    struct i2c_adapter *next;
};

typedef struct i2c_adapter i2c_adapter_t;

int i2c_init(void);
int i2c_add_adapter(i2c_adapter_t *adap);
int i2c_add_client(i2c_adapter_t *adap, i2c_client_t *client);
int i2c_transfer(i2c_adapter_t *adap, struct i2c_msg *msgs, int num);
int i2c_smbus_read_byte(i2c_client_t *client, uint8_t command, uint8_t *value);
int i2c_smbus_write_byte(i2c_client_t *client, uint8_t command, uint8_t value);
int i2c_smbus_read_byte_data(i2c_client_t *client, uint8_t command, uint8_t *value);
int i2c_smbus_write_byte_data(i2c_client_t *client, uint8_t command, uint8_t value);
void i2c_print_stats(void);

#endif
