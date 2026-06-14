#ifndef USB_H
#define USB_H

#include "stdint.h"

#define USB_MAX_DEVICES 128
#define USB_MAX_ENDPOINTS 32

typedef struct {
    uint8_t addr;
    uint8_t speed;
    uint8_t config;
    uint16_t vendor_id;
    uint16_t device_id;
} usb_device_t;

void usb_init(void);
int32_t usb_control_msg(usb_device_t *dev, uint8_t request, uint16_t value, uint16_t index, void *buf, uint16_t len);
int32_t usb_bulk_transfer(usb_device_t *dev, uint8_t endpoint, void *buf, uint32_t len);

#endif
