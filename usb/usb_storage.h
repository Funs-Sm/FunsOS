#ifndef USB_STORAGE_H
#define USB_STORAGE_H

#include "stdint.h"
#include "usb_core.h"

#define USB_STORAGE_MAX_LUN 8

typedef struct {
    uint8_t dev_addr;
    uint8_t interface;
    uint8_t ep_in;
    uint8_t ep_out;
    uint32_t block_size;
    uint64_t capacity;
    uint8_t lun;
    uint64_t block_count;   /* Number of blocks */
    int disk_registered;    /* Whether registered with disk_manager */
} usb_storage_dev_t;

void usb_storage_init(void);
int usb_storage_probe(usb_device_t *dev);
int usb_storage_read(usb_storage_dev_t *dev, uint64_t lba, uint32_t count, void *buf);
int usb_storage_write(usb_storage_dev_t *dev, uint64_t lba, uint32_t count, const void *buf);
int usb_storage_get_capacity(usb_storage_dev_t *dev);
void usb_storage_remove(usb_device_t *dev);

/* New SCSI command functions */
int usb_storage_read_capacity(uint8_t dev_addr, uint32_t *block_count, uint32_t *block_size);
int usb_storage_read_blocks(uint8_t dev_addr, uint64_t lba, uint32_t count, void *buf);
int usb_storage_write_blocks(uint8_t dev_addr, uint64_t lba, uint32_t count, const void *buf);
int usb_storage_test_unit_ready(uint8_t dev_addr);
int usb_storage_request_sense(uint8_t dev_addr, void *buf);

/* Get storage device by address */
usb_storage_dev_t *usb_storage_get_by_addr(uint8_t dev_addr);

#endif
