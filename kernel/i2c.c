#include "i2c.h"
#include "klog.h"
#include "string.h"

#define I2C_SMBUS_READ  1
#define I2C_SMBUS_WRITE 0
#define I2C_SMBUS_BYTE  1

struct i2c_global {
    uint8_t initialized;
    i2c_adapter_t adapters[I2C_MAX_ADAPTERS];
    uint32_t n_adapters;
    i2c_adapter_t *adapter_list;
    uint64_t total_xfers;
    uint64_t total_smbus;
    uint64_t total_errors;
};

static struct i2c_global i2c_data;

static int virt_i2c_master_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num) {
    if (!adap || !msgs || num <= 0) return -1;
    for (int i = 0; i < num; i++) {
        (void)msgs[i];
    }
    return num;
}

static int virt_i2c_smbus_xfer(struct i2c_adapter *adap, uint16_t addr, uint8_t read_write,
                                uint8_t command, uint8_t protocol, uint8_t size,
                                union i2c_smbus_data *data) {
    (void)protocol; (void)size;
    if (!adap || !data) return -1;
    if (read_write == I2C_SMBUS_READ) {
        data->byte = (uint8_t)((addr + command) & 0xFF);
    }
    return 0;
}

static const struct i2c_algorithm virt_i2c_algo = {
    .master_xfer = virt_i2c_master_xfer,
    .smbus_xfer = virt_i2c_smbus_xfer,
};

static i2c_adapter_t *i2c_find_adapter(uint32_t nr) {
    i2c_adapter_t *adap = i2c_data.adapter_list;
    while (adap) {
        if (adap->nr == nr) return adap;
        adap = adap->next;
    }
    return NULL;
}

static i2c_client_t *i2c_find_client(i2c_adapter_t *adap, uint16_t addr) {
    if (!adap) return NULL;
    for (uint32_t i = 0; i < adap->n_clients; i++) {
        if (adap->clients[i].registered && adap->clients[i].addr == addr) {
            return &adap->clients[i];
        }
    }
    return NULL;
}

static void i2c_init_virtual_bus(i2c_adapter_t *adap, uint32_t nr) {
    memset(adap, 0, sizeof(*adap));
    strncpy(adap->name, "virt_i2c", I2C_NAME_LEN - 1);
    adap->nr = nr;
    adap->algo = &virt_i2c_algo;
    adap->n_clients = 0;

    static const struct {
        const char *name;
        uint16_t addr;
    } virt_clients[] = {
        { "eeprom", 0x50 },
        { "rtc_i2c", 0x68 },
        { "sensor_temp", 0x48 },
        { "pmic", 0x30 },
    };

    for (uint32_t i = 0; i < sizeof(virt_clients)/sizeof(virt_clients[0]); i++) {
        i2c_client_t *client = &adap->clients[i];
        strncpy(client->name, virt_clients[i].name, I2C_NAME_LEN - 1);
        client->addr = virt_clients[i].addr;
        client->adapter = adap;
        client->registered = 1;
        client->xfer_count = 0;
        adap->n_clients++;
    }
}

int i2c_init(void) {
    if (i2c_data.initialized) return 0;
    memset(&i2c_data, 0, sizeof(i2c_data));

    i2c_init_virtual_bus(&i2c_data.adapters[0], 0);
    i2c_data.adapters[0].next = NULL;
    i2c_data.adapter_list = &i2c_data.adapters[0];
    i2c_data.n_adapters = 1;

    i2c_data.initialized = 1;
    klog_info("I2C: I2C bus subsystem initialized (%u adapters, %u clients)",
              i2c_data.n_adapters, i2c_data.adapters[0].n_clients);
    return 0;
}

int i2c_add_adapter(i2c_adapter_t *adap) {
    if (!i2c_data.initialized || !adap || i2c_data.n_adapters >= I2C_MAX_ADAPTERS) return -1;
    memcpy(&i2c_data.adapters[i2c_data.n_adapters], adap, sizeof(*adap));
    i2c_adapter_t *new_adap = &i2c_data.adapters[i2c_data.n_adapters];
    new_adap->nr = i2c_data.n_adapters;
    new_adap->next = i2c_data.adapter_list;
    i2c_data.adapter_list = new_adap;
    i2c_data.n_adapters++;
    klog_info("I2C: added adapter '%s' (nr=%u)", new_adap->name, new_adap->nr);
    return 0;
}

int i2c_add_client(i2c_adapter_t *adap, i2c_client_t *client) {
    if (!i2c_data.initialized || !adap || !client || adap->n_clients >= I2C_MAX_CLIENTS) return -1;
    memcpy(&adap->clients[adap->n_clients], client, sizeof(*client));
    i2c_client_t *new_client = &adap->clients[adap->n_clients];
    new_client->adapter = adap;
    new_client->registered = 1;
    adap->n_clients++;
    klog_info("I2C: added client '%s' at 0x%02X on %s",
              new_client->name, new_client->addr, adap->name);
    return 0;
}

int i2c_transfer(i2c_adapter_t *adap, struct i2c_msg *msgs, int num) {
    if (!i2c_data.initialized || !adap || !adap->algo || !adap->algo->master_xfer) return -1;
    int ret = adap->algo->master_xfer(adap, msgs, num);
    if (ret >= 0) {
        adap->xfer_count++;
        i2c_data.total_xfers++;
        for (int i = 0; i < num; i++) {
            i2c_client_t *cl = i2c_find_client(adap, msgs[i].addr);
            if (cl) cl->xfer_count++;
        }
    } else {
        adap->error_count++;
        i2c_data.total_errors++;
    }
    return ret;
}

int i2c_smbus_read_byte(i2c_client_t *client, uint8_t command, uint8_t *value) {
    if (!client || !client->adapter || !value) return -1;
    i2c_adapter_t *adap = client->adapter;
    if (!adap->algo || !adap->algo->smbus_xfer) return -1;
    union i2c_smbus_data data;
    int ret = adap->algo->smbus_xfer(adap, client->addr, I2C_SMBUS_READ,
                                      command, I2C_SMBUS_BYTE, 1, &data);
    if (ret == 0) {
        *value = data.byte;
        adap->smbus_count++;
        i2c_data.total_smbus++;
        client->xfer_count++;
    } else {
        adap->error_count++;
        i2c_data.total_errors++;
    }
    return ret;
}

int i2c_smbus_write_byte(i2c_client_t *client, uint8_t command, uint8_t value) {
    if (!client || !client->adapter) return -1;
    i2c_adapter_t *adap = client->adapter;
    if (!adap->algo || !adap->algo->smbus_xfer) return -1;
    union i2c_smbus_data data;
    data.byte = value;
    int ret = adap->algo->smbus_xfer(adap, client->addr, I2C_SMBUS_WRITE,
                                      command, I2C_SMBUS_BYTE, 1, &data);
    if (ret == 0) {
        adap->smbus_count++;
        i2c_data.total_smbus++;
        client->xfer_count++;
    } else {
        adap->error_count++;
        i2c_data.total_errors++;
    }
    return ret;
}

int i2c_smbus_read_byte_data(i2c_client_t *client, uint8_t command, uint8_t *value) {
    return i2c_smbus_read_byte(client, command, value);
}

int i2c_smbus_write_byte_data(i2c_client_t *client, uint8_t command, uint8_t value) {
    return i2c_smbus_write_byte(client, command, value);
}

void i2c_print_stats(void) {
    uint8_t test_val = 0;
    i2c_client_t *cl = i2c_find_client(i2c_data.adapter_list, 0x50);
    if (cl) i2c_smbus_read_byte(cl, 0, &test_val);

    klog_info("=== I2C Bus Subsystem Statistics ===");
    klog_info("Initialized: %s", i2c_data.initialized ? "yes" : "no");
    klog_info("I2C adapters: %u", i2c_data.n_adapters);
    klog_info("Total transfers: %llu", (unsigned long long)i2c_data.total_xfers);
    klog_info("Total SMBus ops: %llu", (unsigned long long)i2c_data.total_smbus);
    klog_info("Total errors: %llu", (unsigned long long)i2c_data.total_errors);
    klog_info("");

    klog_info("I2C adapters:");
    i2c_adapter_t *adap = i2c_data.adapter_list;
    uint32_t aidx = 0;
    while (adap && aidx < I2C_MAX_ADAPTERS) {
        klog_info("  [%u] %s (bus %u)", aidx, adap->name, adap->nr);
        klog_info("    algo: %s", adap->algo ? "registered" : "none");
        klog_info("    ops: xfer=%llu smbus=%llu err=%llu",
                  (unsigned long long)adap->xfer_count,
                  (unsigned long long)adap->smbus_count,
                  (unsigned long long)adap->error_count);
        klog_info("    clients (%u):", adap->n_clients);
        for (uint32_t i = 0; i < adap->n_clients; i++) {
            i2c_client_t *c = &adap->clients[i];
            if (c->registered) {
                klog_info("      0x%02X '%s': %llu transfers",
                          c->addr, c->name, (unsigned long long)c->xfer_count);
            }
        }
        klog_info("");
        adap = adap->next;
        aidx++;
    }
}
