#ifndef DRIVER_MANAGER_H
#define DRIVER_MANAGER_H

#include "stdint.h"

/* Driver types */
#define DRIVER_TYPE_UNKNOWN    0
#define DRIVER_TYPE_BLOCK      1
#define DRIVER_TYPE_CHAR       2
#define DRIVER_TYPE_NET        3
#define DRIVER_TYPE_AUDIO      4
#define DRIVER_TYPE_GPU        5
#define DRIVER_TYPE_USB_HCD    6
#define DRIVER_TYPE_INPUT      7

/* Driver states */
#define DRIVER_STATE_UNUSED    0
#define DRIVER_STATE_LOADING   1
#define DRIVER_STATE_RUNNING   2
#define DRIVER_STATE_ERROR     3
#define DRIVER_STATE_STOPPED   4

typedef struct driver_info {
    char name[32];
    char description[64];
    uint32_t type;
    uint32_t state;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t pci_class;
    uint8_t pci_subclass;
    uint8_t pci_prog_if;
    uint8_t pci_bus;
    uint8_t pci_dev;
    uint8_t pci_func;
    void *driver_data;
    int (*init)(struct driver_info *drv);
    void (*shutdown)(struct driver_info *drv);
    int (*ioctl)(struct driver_info *drv, uint32_t cmd, void *arg);
} driver_info_t;

void driver_manager_init(void);
int driver_register(driver_info_t *drv);
int driver_unregister(const char *name);
driver_info_t *driver_find_by_name(const char *name);
driver_info_t *driver_find_by_pci(uint16_t vendor, uint16_t device);
driver_info_t *driver_find_by_class(uint8_t cls, uint8_t subclass);
uint32_t driver_get_count(void);
driver_info_t *driver_get_by_index(uint32_t index);
void driver_list_all(void);

#endif
